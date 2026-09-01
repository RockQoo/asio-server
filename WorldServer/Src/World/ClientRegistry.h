#pragma once

#include "Core/Src/Common/Types.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>

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
    // 브로드캐스트 대상 목록. WorldWorker 스레드 하나만 이 클래스를 건드린다는 게 불변식이라
    // (Gateway/Zone 연결의 I/O 스레드는 WorldWorker::PostTask로 작업만 넘기고 직접 호출하지
    // 않는다 -- GatewayLinkHandler/ZoneLinkHandler 참고) 락이 필요 없다.
    class ClientRegistry
    {
    public:
        void Add(const Network::SessionId clientSessionId, const std::shared_ptr<Network::Session>& gatewaySession);
        void Remove(const Network::SessionId clientSessionId);
        void SetZone(const Network::SessionId clientSessionId, const uint32_t zoneId);

        [[nodiscard]] std::optional<ClientInfo> Find(const Network::SessionId clientSessionId) const;
        void ForEach(const std::function<void(const Network::SessionId, const ClientInfo&)>& func) const;

        [[nodiscard]] size_t Count() const;

    private:
        std::unordered_map<Network::SessionId, ClientInfo> clients_;
    };
}
