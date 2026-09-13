#pragma once

#include "Shared/Core/Src/Common/Types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Network
{
    class Session;
}

namespace World
{
    struct ClientInfo
    {
        std::shared_ptr<Network::Session> gatewaySession;
        uint32_t zoneId{};
    };

    // "이 클라이언트가 지금 어느 존에 있는지" 라우팅 테이블 + 브로드캐스트 대상 목록.
    //
    // **맵 하나를 공유하면 키가 겹치지 않아도 깨진다**(rehash). 그래서 스레드 수만큼 샤딩하고
    // `clientSessionId % shardCount`로 고른다 -- 나누는 규칙이 큐 그룹의 스레드 선택 규칙과
    // 같아야 하므로(둘 다 `% N`) 생성자가 그룹의 스레드 수를 그대로 받는다.
    //
    // **그래서 전체 순회를 한 스레드에서 할 수 없다** -- 샤드마다 그 샤드를 소유한 스레드로
    // 메시지를 보내야 한다(ForEachInShard, WorldServerApp::BroadcastToAll 참고).
    //
    // 설계 근거: docs/design/locking-strategy.md
    class ClientRegistry
    {
    public:
        explicit ClientRegistry(const size_t shardCount);

        // 아래 넷은 전부 "그 clientSessionId를 담당하는 스레드"에서만 호출해야 한다.
        void Add(const Network::SessionId clientSessionId, const std::shared_ptr<Network::Session>& gatewaySession);
        void Remove(const Network::SessionId clientSessionId);
        void SetZone(const Network::SessionId clientSessionId, const uint32_t zoneId);
        [[nodiscard]] std::optional<ClientInfo> Find(const Network::SessionId clientSessionId) const;

        [[nodiscard]] size_t ShardCount() const noexcept { return shards_.size(); }

        // 샤드 하나를 소유한 스레드에서만 호출한다. 샤드 인덱스는 그대로 ownerId로 쓸 수 있다
        // (`index % shardCount == index`이므로 그 샤드를 담당하는 스레드로 정확히 간다).
        void ForEachInShard(const size_t shardIndex,
                            const std::function<void(const Network::SessionId, const ClientInfo&)>& func) const;

        [[nodiscard]] size_t CountInShard(const size_t shardIndex) const;

    private:
        [[nodiscard]] size_t ShardIndexOf(const Network::SessionId clientSessionId) const noexcept
        {
            return static_cast<size_t>(clientSessionId % shards_.size());
        }

        // unique_ptr로 감싸는 이유는 벡터 재할당 방지가 아니라(생성 후 크기가 고정된다)
        // **거짓 공유(false sharing)** 때문이다 -- 맵 본체를 값으로 늘어놓으면 서로 다른
        // 스레드가 쓰는 헤더들이 같은 캐시 라인에 들어갈 수 있다.
        std::vector<std::unique_ptr<std::unordered_map<Network::SessionId, ClientInfo>>> shards_;
    };
}
