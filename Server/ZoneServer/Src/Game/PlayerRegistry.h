#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Server/ZoneServer/Src/Game/Player.h"

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Zone
{
    // 이 프로세스에 접속해 있는 Player들의 소유자. **플레이어 레인(owner = clientSessionId)
    // 전용**이다.
    //
    // WorldServer의 ClientRegistry와 완전히 같은 이유로 샤딩돼 있다: 어떤 플레이어의 항목은
    // 항상 같은 스레드에서만 만져지지만, **맵 하나를 공유하면 서로 다른 플레이어를 다루는
    // 스레드들이 같은 컨테이너에 삽입/삭제를 하게 되어(rehash) 키가 겹치지 않아도 깨진다.**
    // 그래서 스레드 수만큼 맵을 나누고 `clientSessionId % shardCount`로 고른다 -- 나누는
    // 규칙이 큐 그룹의 스레드 선택 규칙과 같아야 하므로(둘 다 `% N`) 생성자가 그룹의 스레드
    // 수를 그대로 받는다.
    //
    // Player를 shared_ptr로 들고 있는 이유는 **존 레인(ZoneInstance의 로스터)도 같은 객체를
    // 가리키기 때문**이다. 두 레인이 같은 객체를 보되, 모델별로 어느 레인이 만질 수 있는지는
    // Player.h 주석이 규정한다.
    class PlayerRegistry
    {
    public:
        explicit PlayerRegistry(const size_t shardCount);

        // 아래 셋은 전부 "그 clientSessionId를 담당하는 스레드"에서만 호출해야 한다.
        void Add(const std::shared_ptr<Player>& player);
        void Remove(const Network::SessionId clientSessionId);
        [[nodiscard]] std::shared_ptr<Player> Find(const Network::SessionId clientSessionId) const;

        [[nodiscard]] size_t ShardCount() const noexcept { return shards_.size(); }
        [[nodiscard]] size_t CountInShard(const size_t shardIndex) const;

    private:
        [[nodiscard]] size_t ShardIndexOf(const Network::SessionId clientSessionId) const noexcept
        {
            return static_cast<size_t>(clientSessionId % shards_.size());
        }

        // unique_ptr로 감싸는 것은 거짓 공유(false sharing) 방지다 -- 맵 본체를 값으로
        // 늘어놓으면 서로 다른 스레드가 쓰는 헤더들이 같은 캐시 라인에 들어갈 수 있다.
        std::vector<std::unique_ptr<std::unordered_map<Network::SessionId, std::shared_ptr<Player>>>> shards_;
    };
}
