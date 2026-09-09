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

    // Gateway가 통지해온 모든 클라이언트 세션의 "현재 어느 존에 있는지" 라우팅 테이블 +
    // 브로드캐스트 대상 목록.
    //
    // **왜 샤드로 쪼개져 있는가**: BASIC 큐 그룹이 스레드 N개이고 클라이언트 관련 메시지는
    // ownerId=clientSessionId로 배정되므로, 어떤 클라이언트의 항목은 항상 같은 스레드에서만
    // 만져진다. 그런데 맵 하나를 공유하면 서로 다른 클라이언트를 다루는 스레드들이 같은
    // 컨테이너에 삽입/삭제를 하게 되고(rehash!) 그것만으로 깨진다 -- 키가 겹치지 않아도
    // 컨테이너 자체가 안전하지 않기 때문이다. 그래서 스레드 수만큼 맵을 나누고
    // `clientSessionId % shardCount`로 고르면, **각 맵을 그 맵을 담당하는 스레드 하나만
    // 건드리게 되어 락이 사라진다.** 나누는 규칙은 ProcessorGroup의 스레드 선택 규칙과
    // 반드시 같아야 하고(둘 다 `% N`), 그래서 생성자가 그룹의 스레드 수를 그대로 받는다.
    //
    // 그 결과 **전체 순회(ForEach)는 한 스레드에서 할 수 없다.** 샤드마다 그 샤드를 소유한
    // 스레드로 메시지를 하나씩 보내야 하고, ForEachInShard가 그 용도다(WorldServerApp::
    // BroadcastToAll 참고).
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
