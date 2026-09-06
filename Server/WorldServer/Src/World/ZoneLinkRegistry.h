#pragma once

#include "Shared/Core/Src/Common/Types.h"

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
    };

    // 접속해 있는 Zone 서버 프로세스들의 "zoneId -> 연결/담당 구간" 테이블. ClientRegistry와
    // 마찬가지로 WorldWorker 스레드 하나만 건드리므로 락이 필요 없다. FindZoneContainingX가
    // 존 핸드오프 시 "이 좌표는 어느 존 담당인가"를 결정하는 유일한 지점이다(2존 고정 인접
    // 테이블이라 선형 탐색으로 충분).
    class ZoneLinkRegistry
    {
    public:
        void Add(const uint32_t zoneId, const std::shared_ptr<Network::Session>& zoneSession,
                 const float xMin, const float xMax);
        void Remove(const uint32_t zoneId);

        // 연결 하나가 여러 zoneId를 등록했을 수 있으므로(한 Zone 서버 프로세스가 존 여러 개를
        // 호스팅), 그 연결이 끊기면 이 세션이 걸려 있는 zoneId 항목을 전부 지운다.
        void RemoveBySession(const Network::SessionId sessionId);

        [[nodiscard]] std::optional<ZoneLinkInfo> Find(const uint32_t zoneId) const;
        [[nodiscard]] std::optional<uint32_t> FindZoneContainingX(const float x) const;

    private:
        std::unordered_map<uint32_t, ZoneLinkInfo> zones_;
    };
}
