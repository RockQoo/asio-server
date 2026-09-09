#include "Server/WorldServer/Src/pch.h"
#include "Server/WorldServer/Src/World/ClientRegistry.h"

#include "Shared/Core/Src/Common/CoreException.h"

namespace World
{
    ClientRegistry::ClientRegistry(const size_t shardCount)
    {
        if (shardCount == 0)
        {
            throw Common::CoreException(Common::ECoreErrorCode::InvalidArgument,
                                         "ClientRegistry: shardCount는 0보다 커야 한다");
        }

        shards_.reserve(shardCount);
        for (size_t index = 0; index < shardCount; ++index)
        {
            shards_.push_back(std::make_unique<std::unordered_map<Network::SessionId, ClientInfo>>());
        }
    }

    void ClientRegistry::Add(const Network::SessionId clientSessionId, const std::shared_ptr<Network::Session>& gatewaySession)
    {
        (*shards_[ShardIndexOf(clientSessionId)])[clientSessionId] = ClientInfo{gatewaySession, 0};
    }

    void ClientRegistry::Remove(const Network::SessionId clientSessionId)
    {
        shards_[ShardIndexOf(clientSessionId)]->erase(clientSessionId);
    }

    void ClientRegistry::SetZone(const Network::SessionId clientSessionId, const uint32_t zoneId)
    {
        auto& shard = *shards_[ShardIndexOf(clientSessionId)];
        if (const auto it = shard.find(clientSessionId); it != shard.end())
        {
            it->second.zoneId = zoneId;
        }
    }

    std::optional<ClientInfo> ClientRegistry::Find(const Network::SessionId clientSessionId) const
    {
        const auto& shard = *shards_[ShardIndexOf(clientSessionId)];
        const auto it = shard.find(clientSessionId);
        if (it == shard.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    void ClientRegistry::ForEachInShard(const size_t shardIndex,
                                         const std::function<void(const Network::SessionId, const ClientInfo&)>& func) const
    {
        for (const auto& [clientSessionId, info] : *shards_[shardIndex])
        {
            func(clientSessionId, info);
        }
    }

    size_t ClientRegistry::CountInShard(const size_t shardIndex) const
    {
        return shards_[shardIndex]->size();
    }
}
