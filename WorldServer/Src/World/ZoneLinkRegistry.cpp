#include "WorldServer/Src/pch.h"
#include "WorldServer/Src/World/ZoneLinkRegistry.h"

#include "Core/Src/Network/Session.h"

namespace World
{
    void ZoneLinkRegistry::Add(const uint32_t zoneId, const std::shared_ptr<Network::Session>& zoneSession,
                                const float xMin, const float xMax)
    {
        zones_[zoneId] = ZoneLinkInfo{zoneSession, xMin, xMax};
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

    std::optional<uint32_t> ZoneLinkRegistry::FindZoneContainingX(const float x) const
    {
        for (const auto& [zoneId, info] : zones_)
        {
            if (x >= info.xMin && x < info.xMax)
            {
                return zoneId;
            }
        }
        return std::nullopt;
    }
}
