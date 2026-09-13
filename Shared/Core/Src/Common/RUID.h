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
    //   bit 21 ~ 14  : 노드(프로세스) 번호 (8비트, 256개)
    //   bit 13 ~ 0   : 같은 밀리초 안의 시퀀스 (14비트, 16,384개/ms = 1,600만/초)
    inline constexpr int32_t kTimestampBits = 41;
    inline constexpr int32_t kNodeBits = 8;
    inline constexpr int32_t kSequenceBits = 14;

    // 기준 시각(epoch): 2026-01-01T00:00:00Z의 유닉스 밀리초.
    // **이 값을 바꾸면 안 된다** -- 기준을 뒤로 옮기면 같은 시각의 밀리초 값이 작아져서, 새로
    // 발급하는 id가 기존 id보다 작아진다(시간순 정렬이 뒤집히고, 클러스터드 인덱스에 중간
    // 삽입이 되살아나고, 최악은 기존 id와 겹친다).
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
    //
    // 대역을 나눈 이유: 예전에는 World가 0, Zone이 담당 최소 zoneId를 그대로 썼는데, World를
    // 여러 대로 늘리면 World 1번과 Zone 1번이 같은 번호가 되어 조용히 겹친다.
    inline constexpr uint32_t kNodeIdReserved = 0;
    inline constexpr uint32_t kNodeIdWorldBegin = 1;
    inline constexpr uint32_t kNodeIdZoneBegin = 100;
    inline constexpr uint32_t kNodeIdSeed = 254;
    inline constexpr uint32_t kNodeIdTool = 255;

    // 프로세스마다 하나. 스레드 여러 개(BASIC 워커 + 유지보수 타이머)가 동시에 부를 수 있어
    // 락 없이 CAS로 처리한다.
    class RUIDGenerator
    {
    public:
        [[nodiscard]] static RUIDGenerator& Instance();

        // 프로세스 기동 시 한 번. nodeId는 프로세스마다 달라야 하고(같으면 id가 겹친다),
        // 8비트를 넘거나 예약값(0)이면 밀리초 칸 오염/중복 발급으로 이어지므로 여기서 막는다.
        void Initialize(const uint32_t nodeId);

        [[nodiscard]] RUID Next();

    private:
        RUIDGenerator() = default;

        // 마지막으로 발급한 (밀리초 << kSequenceBits | 시퀀스). 둘을 한 워드에 담아야 CAS
        // 하나로 원자적으로 갱신할 수 있다(따로 두면 그 사이에 끼어들 틈이 생긴다).
        std::atomic<uint64_t> lastStamp_{0};
        uint32_t nodeId_{0};
    };
}
