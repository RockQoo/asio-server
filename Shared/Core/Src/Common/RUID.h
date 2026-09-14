#pragma once

#include "Shared/Core/Src/Common/BasicTypes.h"

namespace Common
{
    // 요청 하나를 전 서버에서 유일하게 가리키는 식별자. Zone -> World -> DB 로그를 한 줄로
    // 이어 붙이고, 클라이언트가 "내가 보낸 어느 요청의 결과인지"를 짝지을 때 쓴다.
    //
    // **왜 GUID(128비트)가 아니라 int64인가**
    //   - DB에 넣으면 BIGINT이고 SQL Server에는 UNSIGNED BIGINT가 없다 -> 부호 있는 타입이어야
    //     경계마다 캐스팅이 안 생긴다. 63비트만 쓰므로 음수가 나올 수 없다.
    //   - 무작위 GUID를 클러스터드 인덱스 키로 쓰면 삽입이 인덱스 중간에 꽂혀 페이지 분할과
    //     단편화가 난다. 시각이 앞에 오면 append-only 삽입이라 그 문제가 없고, 시간순 정렬도
    //     공짜로 따라온다.
    //   - 8바이트라 와이어와 인덱스 모두 GUID의 절반이다.
    using RUID = int64_t;

    // "아직 발급되지 않았다/유효하지 않다"를 나타내는 값. 0은 "시각 0 + 노드 0 + 시퀀스 0"이라는
    // 유효한 조합이라 센티넬로 쓸 수 없다 -- 부호 있는 타입을 고른 부수 이득이다.
    inline constexpr RUID kInvalidRUID = -1;

    // 비트 배분:
    //   bit 63       : 부호. 쓰지 않는다(항상 0이라 음수가 나올 수 없다)
    //   bit 62 ~ 22  : 기준 시각부터의 밀리초 (41비트, 약 69.7년)
    //   bit 21 ~ 12  : 노드(프로세스) 번호 (10비트, 1,024개)
    //   bit 11 ~ 0   : 같은 밀리초 안의 시퀀스 (12비트, 4,096개/ms = 409만/초)
    //
    // 셋의 합이 63이어야 한다(아래 static_assert). 배분을 바꿀 때의 판단 기준과 실측 버스트
    // 수치는 docs/design/unique-id.md 참고.
    inline constexpr int32_t kTimestampBits = 41;
    inline constexpr int32_t kNodeBits = 10;
    inline constexpr int32_t kSequenceBits = 12;

    static_assert(kTimestampBits + kNodeBits + kSequenceBits == 63,
                  "부호 비트 하나는 비워둬야 음수가 나오지 않는다");

    inline constexpr int64_t kSequenceMask = (int64_t{1} << kSequenceBits) - 1;
    inline constexpr int64_t kNodeMask = (int64_t{1} << kNodeBits) - 1;
    inline constexpr int64_t kTimestampMask = (int64_t{1} << kTimestampBits) - 1;

    // 기준 시각(epoch): 2026-01-01T00:00:00Z의 유닉스 밀리초.
    // **이 값을 바꾸면 안 된다** -- 기준을 뒤로 옮기면 같은 시각의 밀리초 값이 작아져서, 새로
    // 발급하는 id가 기존 id보다 작아진다(시간순 정렬이 뒤집히고, 클러스터드 인덱스에 중간
    // 삽입이 되살아난다).
    // 유닉스 기준(1970)을 그대로 쓰지 않는 이유: 41비트 중 56년치를 이미 써버린 상태로
    // 시작하게 되어 13년밖에 남지 않는다. 여기서 세면 2095년까지 간다.
    inline constexpr int64_t kEpochMs = 1767225600000;

    // 노드 번호 대역. **프로세스마다 달라야 하고, 겹치면 서로 같은 id를 발급한다**(설계
    // 근거와 실측: docs/design/unique-id.md) --
    // 그래서 "알아서 다른 값"이 아니라 대역으로 못박는다.
    //
    //   0          예약. 초기화를 빠뜨린 프로세스를 잡기 위한 값이라 발급에 쓰지 않는다
    //   1   ~ 99   WorldServer   (World 1번 = 1, 2번 = 2, ...)
    //   100 ~ 199  ZoneServer    (100 + 담당 최소 zoneId)
    //   254        시드/도구 예약 -- Sql/seed.sql 이 T-SQL 로 만드는 id 가 여기에 들어간다.
    //              실행 중인 어느 프로세스도 이 번호를 쓰지 않으므로, 시드 id 와 실제 발급
    //              id 는 **시각이 겹쳐도** 절대 충돌하지 않는다
    //   255        운영툴(GmTool) 예약 -- 아직 자체 발급하지 않는다
    //   256 ~ 1023 미사용(10비트라 여유가 있다). 대역을 넓힐 때 여기서 가져다 쓴다
    //
    // 대역을 나눈 이유: 예전에는 World가 0, Zone이 담당 최소 zoneId를 그대로 썼는데, World를
    // 여러 대로 늘리면 World 1번과 Zone 1번이 같은 번호가 되어 조용히 겹친다.
    inline constexpr uint32_t kNodeIdReserved = 0;
    inline constexpr uint32_t kNodeIdWorldBegin = 1;
    inline constexpr uint32_t kNodeIdZoneBegin = 100;
    inline constexpr uint32_t kNodeIdSeed = 254;
    inline constexpr uint32_t kNodeIdTool = 255;

    // 생성기 본체. 인스턴스 하나가 노드 하나를 담당한다.
    //
    // 프로세스 전역으로 쓸 때는 아래 `Ruid`를 쓰고, 이 클래스를 직접 만드는 것은 한 프로세스
    // 안에서 여러 노드를 흉내 내야 할 때뿐이다(테스트).
    //
    // 스레드 여러 개(레인 워커 + 유지보수 타이머)가 동시에 `Next()`를 불러도 안전하다 --
    // (밀리초, 시퀀스)를 한 워드에 담아 CAS 하나로 갱신하므로 락이 없다.
    class RuidGenerator
    {
    public:
        // 시계가 뒤로 가면 RUID는 중복 대신 "정체"를 택한다. 조용히 느려지기만 하면 원인을
        // 찾을 수 없으므로, 발생 사실을 세서 밖으로 내보낸다.
        struct Health
        {
            int64_t clockRollbackCount{};      // 시계 역행을 감지한 횟수(CAS 재시도마다 센다)
            int64_t maxRollbackMs{};           // 그중 가장 크게 뒤로 간 폭(ms)
            int64_t sequenceExhaustedCount{};  // 한 밀리초의 시퀀스를 다 써서 대기한 횟수
        };

        // 한 번만 성공한다. 두 번째 호출은 아무것도 바꾸지 않고 false를 돌려준다 --
        // 재초기화를 허용하면 lastStamp_가 0으로 리셋되면서 같은 밀리초의 시퀀스를 0부터
        // 다시 발급한다(= 이미 나간 id와 충돌한다).
        [[nodiscard]] bool Initialize(const uint32_t nodeId) noexcept;

        [[nodiscard]] bool Initialized() const noexcept { return initialized_; }
        [[nodiscard]] uint32_t NodeId() const noexcept { return nodeId_; }

        [[nodiscard]] RUID Next();
        [[nodiscard]] Health GetHealth() const noexcept;

    private:
        void RecordRollback(const int64_t backwardMs) noexcept;

        // 마지막으로 발급한 (밀리초 << kSequenceBits | 시퀀스). 둘을 한 워드에 담아야 CAS
        // 하나로 원자적으로 갱신할 수 있다(따로 두면 그 사이에 끼어들 틈이 생긴다).
        std::atomic<uint64_t> lastStamp_{0};
        uint32_t nodeId_{0};
        bool initialized_{false};

        // 정상 경로에서는 건드리지 않으므로 비용이 없다.
        std::atomic<int64_t> rollbackCount_{0};
        std::atomic<int64_t> maxRollbackMs_{0};
        std::atomic<int64_t> sequenceExhausted_{0};
    };

    static_assert(std::atomic<uint64_t>::is_always_lock_free,
                  "CAS가 lock-free가 아니면 락을 피한 의미가 없다");

    // 프로세스 전역 진입점. 기동 시 `Ruid::Init(nodeId)` 한 번, 이후 어디서든 `Ruid::Create()`.
    //
    // **타입 별칭 `RUID`(전부 대문자, = int64_t)와 이 클래스 `Ruid`(파스칼)는 다른 것이다.**
    //     Common::RUID id = Common::Ruid::Create();
    class Ruid final
    {
    public:
        Ruid() = delete;   // 인스턴스로 만들지 않는다. 필요하면 RuidGenerator를 쓴다.

        using Health = RuidGenerator::Health;

        // 기동 시 한 번. 예약값/범위 초과/두 번째 호출이면 **그 자리에서 중단한다** --
        // 잘못 발급된 id는 DB에 영구히 남고 나중에 고칠 방법이 없다.
        static void Init(const uint32_t nodeId);

        [[nodiscard]] static bool Initialized() noexcept;
        [[nodiscard]] static uint32_t NodeId() noexcept;

        [[nodiscard]] static RUID Create();
        [[nodiscard]] static Health GetHealth() noexcept;

        // 디코딩. 로그에 남은 id 하나를 "언제 어느 프로세스가 만들었나"로 되짚을 때 쓴다.
        [[nodiscard]] static constexpr int64_t TimestampMsOf(const RUID id) noexcept
        {
            return ((id >> (kNodeBits + kSequenceBits)) & kTimestampMask) + kEpochMs;
        }
        [[nodiscard]] static constexpr uint32_t NodeIdOf(const RUID id) noexcept
        {
            return static_cast<uint32_t>((id >> kSequenceBits) & kNodeMask);
        }
        [[nodiscard]] static constexpr int64_t SequenceOf(const RUID id) noexcept
        {
            return id & kSequenceMask;
        }

        // "225090439823233024 | 2026-09-14 03:09:02.832 | node=1 | seq=0"
        [[nodiscard]] static std::string Describe(const RUID id);

    private:
        [[nodiscard]] static RuidGenerator& Generator() noexcept;
    };
}
