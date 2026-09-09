#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Thread/Mutexed.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

namespace Network
{
    class Session;
}

namespace World
{
    struct ZoneLinkInfo
    {
        std::shared_ptr<Network::Session> zoneSession;
        float xMin{};
        float xMax{};
        float yMin{};
        float yMax{};

        [[nodiscard]] constexpr bool Contains(const float x, const float y) const noexcept
        {
            return x >= xMin && x < xMax && y >= yMin && y < yMax;
        }
    };

    // 접속해 있는 Zone 서버 프로세스들의 "zoneId -> 연결/담당 사각형" 테이블.
    // FindZoneContaining이 존 핸드오프 시 "이 좌표는 어느 존 담당인가"를 결정하는 유일한
    // 지점이다(존 몇 개짜리 테이블이라 선형 탐색으로 충분 -- 존이 많아지면 격자 인덱스가
    // 필요해진다).
    //
    // **왜 여기만 락이 필요한가**: ClientRegistry는 clientSessionId로 샤딩해서 "그 항목을 만지는
    // 스레드가 항상 하나"를 만들 수 있지만, 이 테이블은 그게 안 된다 -- 어느 클라이언트의
    // 메시지를 처리하든(= BASIC의 어느 스레드에서든) 그 클라이언트가 있는 존의 링크를
    // 찾아야 하기 때문이다. 즉 **주인이 다른 데이터를 가로질러 읽는 자리**라, ownerId
    // 어피니티(1층)로는 못 막고 모델 단위 읽기/쓰기 락(2층)이 필요하다.
    //
    // 쓰기는 존 서버가 붙고 끊길 때뿐이라 극히 드물고 읽기는 패킷마다 일어나므로,
    // shared_mutex 기반 Thread::Mutexed가 정확히 맞는 도구다. `Mutexed`를 클래스 안에
    // 별칭으로 두는 것은 Mail::MailModel과 같은 관용구다:
    //   조회 -> `registry->Find(...)`        (const 메서드 = shared_lock)
    //   변경 -> `registry.Write()->Add(...)` (비const 메서드 = unique_lock)
    //
    // World는 존 배치 규칙을 모른다. 각 존이 등록할 때 알려준 사각형만 보고 판단하므로,
    // 배치가 격자에서 CSV 기반 불규칙 배치로 바뀌어도 이 클래스는 그대로다.
    class ZoneLinkRegistry
    {
    public:
        using Mutexed = Thread::Mutexed<ZoneLinkRegistry>;

        void Add(const uint32_t zoneId, const std::shared_ptr<Network::Session>& zoneSession,
                 const float xMin, const float xMax, const float yMin, const float yMax);
        void Remove(const uint32_t zoneId);

        // 연결 하나가 여러 zoneId를 등록했을 수 있으므로(한 Zone 서버 프로세스가 존 여러 개를
        // 호스팅), 그 연결이 끊기면 이 세션이 걸려 있는 zoneId 항목을 전부 지운다.
        void RemoveBySession(const Network::SessionId sessionId);

        [[nodiscard]] std::optional<ZoneLinkInfo> Find(const uint32_t zoneId) const;
        [[nodiscard]] std::optional<uint32_t> FindZoneContaining(const float x, const float y) const;

        // 신규 접속을 어디에 넣을지 정할 때 쓴다. zoneId가 가장 작은 존을 "첫 존"으로 보고 그
        // 중앙 좌표를 함께 준다 -- 스폰 좌표를 상수로 박아두면 배치가 바뀔 때(존 격자화처럼)
        // 그 좌표가 어느 존에도 속하지 않게 되어 아무도 입장하지 못한다. 실제로 그렇게 깨졌다.
        struct EntryPoint
        {
            uint32_t zoneId{};
            float x{};
            float y{};
        };
        [[nodiscard]] std::optional<EntryPoint> FindEntryPoint() const;

    private:
        std::unordered_map<uint32_t, ZoneLinkInfo> zones_;
    };
}
