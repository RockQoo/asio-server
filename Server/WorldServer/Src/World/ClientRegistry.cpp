#include "Server/WorldServer/Src/pch.h"
#include "Server/WorldServer/Src/World/ClientRegistry.h"

namespace World
{
    void ClientRegistry::Add(const Network::SessionId clientSessionId, const std::shared_ptr<Network::Session>& gatewaySession)
    {
        clients_[clientSessionId] = ClientInfo{gatewaySession, 0};
    }

    void ClientRegistry::Remove(const Network::SessionId clientSessionId)
    {
        clients_.erase(clientSessionId);
    }

    void ClientRegistry::SetZone(const Network::SessionId clientSessionId, const uint32_t zoneId)
    {
        if (const auto it = clients_.find(clientSessionId); it != clients_.end())
        {
            it->second.zoneId = zoneId;
        }
    }

    std::optional<ClientInfo> ClientRegistry::Find(const Network::SessionId clientSessionId) const
    {
        const auto it = clients_.find(clientSessionId);
        if (it == clients_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    void ClientRegistry::ForEach(const std::function<void(const Network::SessionId, const ClientInfo&)>& func) const
    {
        for (const auto& [clientSessionId, info] : clients_)
        {
            func(clientSessionId, info);
        }
    }

    size_t ClientRegistry::Count() const
    {
        return clients_.size();
    }
}
