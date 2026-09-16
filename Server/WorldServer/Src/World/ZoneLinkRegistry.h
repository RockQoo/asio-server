#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Protocol/Src/Ids.h"
#include "Shared/Core/Src/Thread/Mutexed.h"

namespace World
{
    struct ZoneLinkInfo
    {
        Network::Session::SPtr zoneSession;
        float xMin{};
        float xMax{};
        float yMin{};
        float yMax{};

        [[nodiscard]] constexpr bool Contains(const float x, const float y) const noexcept
        {
            return x >= xMin && x < xMax && y >= yMin && y < yMax;
        }
    };

    // 접속해 있는 Zone 프로세스들의 "zoneId -> 연결/담당 사각형" 테이블.
    // FindZoneContaining이 "이 좌표는 어느 존 담당인가"를 결정하는 유일한 지점이다.
    //
    // **여기만 락이 필요하다** -- 어느 레인에서 처리하든 그 클라이언트가 있는 존의 링크를
    // 찾아야 해서, 주인을 가로질러 읽는 자리이기 때문이다. 어피니티로는 못 막는다.
    //
    //   조회 -> `registry->Find(...)`        (const = shared_lock)
    //   변경 -> `registry.Write()->Add(...)` (비const = unique_lock)
    //
    // World는 존 배치 규칙을 모른다 -- 각 존이 등록할 때 알려준 사각형만 본다.
    //
    // 설계 근거: docs/design/locking-strategy.md
    class ZoneLinkRegistry
    {
    public:
        using Mutexed = Thread::Mutexed<ZoneLinkRegistry>;

        void Add(const Protocol::ZoneId zoneId, const Network::Session::SPtr& zoneSession,
                 const float xMin, const float xMax, const float yMin, const float yMax);
        void Remove(const Protocol::ZoneId zoneId);

        // 연결 하나가 여러 zoneId를 등록했을 수 있으므로(한 Zone 서버 프로세스가 존 여러 개를
        // 호스팅), 그 연결이 끊기면 이 세션이 걸려 있는 zoneId 항목을 전부 지운다.
        void RemoveBySession(const Network::SessionId sessionId);

        [[nodiscard]] std::optional<ZoneLinkInfo> Find(const Protocol::ZoneId zoneId) const;
        [[nodiscard]] std::optional<Protocol::ZoneId> FindZoneContaining(const float x, const float y) const;

        // 신규 접속을 어디에 넣을지 정할 때 쓴다. zoneId가 가장 작은 존을 "첫 존"으로 보고 그
        // 중앙 좌표를 함께 준다 -- 스폰 좌표를 상수로 박아두면 배치가 바뀔 때(존 격자화처럼)
        // 그 좌표가 어느 존에도 속하지 않게 되어 아무도 입장하지 못한다. 실제로 그렇게 깨졌다.
        struct EntryPoint
        {
            Protocol::ZoneId zoneId{};
            float x{};
            float y{};
        };
        [[nodiscard]] std::optional<EntryPoint> FindEntryPoint() const;

    private:
        std::unordered_map<Protocol::ZoneId, ZoneLinkInfo> zones_;
    };
}
