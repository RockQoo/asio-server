#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Network/IPacketHandler.h"
#include "Shared/Core/Src/Packet/PacketDispatcher.h"
#include "Shared/Protocol/Src/PacketId.h"

#include <cstdint>

namespace World
{
    class ClientRegistry;
    class ZoneLinkRegistry;
    class WorldWorker;

    // Gateway <-> World 연결의 IPacketHandler. Gateway가 accept한 각 클라이언트를 대신 통지받아
    // ClientRegistry에 등록/해제하고, 클라이언트 패킷(FromClient)을 그 클라이언트가 현재 있는
    // 존으로 릴레이한다. OnPacket 자체는 이 연결의 I/O 스레드에서 실행되지만, 바이트만 복사해
    // WorldWorker(World의 단일 처리 스레드)로 넘기고 실제 ClientRegistry/ZoneLinkRegistry 접근은
    // 전부 그 스레드에서 일어난다 -- I/O 스레드가 게임/라우팅 상태를 직접 건드리지 않는다.
    class GatewayLinkHandler final : public Network::IPacketHandler
    {
    public:
        GatewayLinkHandler(ClientRegistry& clientRegistry, ZoneLinkRegistry& zoneLinkRegistry, WorldWorker& worldWorker);

        void OnSessionOpened(const std::shared_ptr<Network::Session>& session) override;
        void OnPacket(const std::shared_ptr<Network::Session>& session,
                      const Packet::PacketHeader& header,
                      const std::span<const byte> payload) override;
        void OnClosed(const std::shared_ptr<Network::Session>& session, const std::error_code& reason) override;

    private:
        void RegisterHandlers();

        void HandleClientConnected(const std::shared_ptr<Network::Session>& gatewaySession, const std::span<const byte> payload);
        void HandleClientDisconnected(const std::shared_ptr<Network::Session>& gatewaySession, const std::span<const byte> payload);
        void HandleFromClient(const std::shared_ptr<Network::Session>& gatewaySession, const std::span<const byte> payload);

        ClientRegistry& clientRegistry_;
        ZoneLinkRegistry& zoneLinkRegistry_;
        WorldWorker& worldWorker_;
        Packet::PacketDispatcher<PacketId, std::shared_ptr<Network::Session>> dispatcher_;
    };
}
