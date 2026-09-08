#include "Server/WorldServer/Src/pch.h"
#include "Server/WorldServer/Src/World/ZoneLinkRegistry.h"

#include "Shared/Core/Src/Network/Session.h"

namespace World
{
    void ZoneLinkRegistry::Add(const uint32_t zoneId, const std::shared_ptr<Network::Session>& zoneSession,
                                const float xMin, const float xMax, const float yMin, const float yMax)
    {
        zones_[zoneId] = ZoneLinkInfo{zoneSession, xMin, xMax, yMin, yMax};
    }

    void ZoneLinkRegistry::Remove(const uint32_t zoneId)
    {
        zones_.erase(zoneId);
    }

    void ZoneLinkRegistry::RemoveBySession(const Network::SessionId sessionId)
    {
        std::erase_if(zones_, [sessionId](const auto& entry)
        {
            return entry.second.zoneSession && entry.second.zoneSession->Id() == sessionId;
        });
    }

    std::optional<ZoneLinkInfo> ZoneLinkRegistry::Find(const uint32_t zoneId) const
    {
        const auto it = zones_.find(zoneId);
        if (it == zones_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<uint32_t> ZoneLinkRegistry::FindZoneContaining(const float x, const float y) const
    {
        for (const auto& [zoneId, info] : zones_)
        {
            if (info.Contains(x, y))
            {
                return zoneId;
            }
        }
        return std::nullopt;
    }

    std::optional<ZoneLinkRegistry::EntryPoint> ZoneLinkRegistry::FindEntryPoint() const
    {
        // unordered_map이라 순회 순서가 정해져 있지 않다 -- "가장 작은 zoneId"를 직접 고른다.
        // 그래야 서버를 다시 띄울 때마다 입장 존이 바뀌지 않는다.
        const ZoneLinkInfo* best = nullptr;
        uint32_t bestZoneId = 0;
        for (const auto& [zoneId, info] : zones_)
        {
            if (best == nullptr || zoneId < bestZoneId)
            {
                best = &info;
                bestZoneId = zoneId;
            }
        }

        if (best == nullptr)
        {
            return std::nullopt;
        }

        // 경계가 아니라 중앙에 스폰한다 -- 경계에 정확히 놓으면 첫 이동에서 바로 핸드오프가
        // 일어나 "입장했는데 곧 다른 존"이 되어 로그가 읽기 어려워진다.
        return EntryPoint{
            bestZoneId,
            (best->xMin + best->xMax) * 0.5f,
            (best->yMin + best->yMax) * 0.5f,
        };
    }
}
