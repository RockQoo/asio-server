#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Game/PlayerRegistry.h"

#include "Shared/Core/Src/Common/CoreException.h"

namespace Zone
{
    PlayerRegistry::PlayerRegistry(const size_t shardCount)
    {
        if (shardCount == 0)
        {
            throw Common::CoreException(Common::ECoreErrorCode::InvalidArgument,
                                         "PlayerRegistry: shardCount는 0보다 커야 한다");
        }

        shards_.reserve(shardCount);
        for (size_t index = 0; index < shardCount; ++index)
        {
            shards_.push_back(
                std::make_unique<std::unordered_map<Network::SessionId, std::shared_ptr<Player>>>());
        }
    }

    void PlayerRegistry::Add(const std::shared_ptr<Player>& player)
    {
        (*shards_[ShardIndexOf(player->GetSessionId())])[player->GetSessionId()] = player;
    }

    void PlayerRegistry::Remove(const Network::SessionId clientSessionId)
    {
        shards_[ShardIndexOf(clientSessionId)]->erase(clientSessionId);
    }

    std::shared_ptr<Player> PlayerRegistry::Find(const Network::SessionId clientSessionId) const
    {
        const auto& shard = *shards_[ShardIndexOf(clientSessionId)];
        const auto it = shard.find(clientSessionId);
        if (it == shard.end())
        {
            return nullptr;
        }
        return it->second;
    }

    size_t PlayerRegistry::CountInShard(const size_t shardIndex) const
    {
        return shards_[shardIndex]->size();
    }
}
