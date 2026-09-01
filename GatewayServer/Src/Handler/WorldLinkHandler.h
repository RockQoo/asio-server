#pragma once

#include "Core/Src/Network/IPacketHandler.h"
#include "Core/Src/Packet/PacketDispatcher.h"
#include "WorldServer/Src/Packet/GatewayLinkPacketId.h"

namespace Network
{
    class SessionManager;
}

namespace Gateway
{
    class WorldLink;

    // Gateway -> World 아웃바운드 연결(Network::Connector로 생성)의 IPacketHandler.
    // World가 보낸 ToClient envelope을 벗겨서 clientSessionId에 해당하는 로컬 클라이언트
    // 세션으로 그대로 전달한다. OnSessionOpened는 accept가 아니라 connect 성공 시 호출된다
    // (Core::Network::Connector 주석 참고 -- 인터페이스를 그대로 재사용).
    class WorldLinkHandler final : public Network::IPacketHandler
    {
    public:
        WorldLinkHandler(Network::SessionManager& sessionManager, WorldLink& worldLink);

        void OnSessionOpened(const std::shared_ptr<Network::Session>& session) override;
        void OnPacket(const std::shared_ptr<Network::Session>& session,
                      const Packet::PacketHeader& header,
                      const std::span<const byte> payload) override;
        void OnClosed(const std::shared_ptr<Network::Session>& session, const std::error_code& reason) override;

    private:
        void RegisterHandlers();
        void HandleToClient(const std::shared_ptr<Network::Session>& worldSession, const std::span<const byte> payload);

        Network::SessionManager& sessionManager_;
        WorldLink& worldLink_;
        Packet::PacketDispatcher<World::GatewayLinkPacketId, std::shared_ptr<Network::Session>> dispatcher_;
    };
}
