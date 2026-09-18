#pragma once

#include "Server/Core/Src/Network/IPacketHandler.h"
#include "Server/Core/Src/Packet/Dispatcher.h"
#include "Server/Common/Src/PacketId.h"
#include "Server/Common/Src/Packet/RelayEnvelope.h"
#include "Server/Common/Src/Packet/Wire.h"
#include "Server/Core/Src/Network/SessionHolder.h"

namespace Network
{
    class SessionManager;
}


// Gateway -> World 아웃바운드 연결(Network::Connector로 생성)의 IPacketHandler.
// World가 보낸 ToClient envelope을 벗겨서 clientSessionId에 해당하는 로컬 클라이언트
// 세션으로 그대로 전달한다. OnSessionOpened는 accept가 아니라 connect 성공 시 호출된다
// (Core::Network::Connector 주석 참고 -- 인터페이스를 그대로 재사용).
class W2GHandler final : public Network::IPacketHandler
{
public:
    W2GHandler(Network::SessionManager& sessionManager, Network::SessionHolder& worldLink);

    void OnSessionOpened(const Network::Session::SPtr& session) override;
    void OnPacket(const Network::Session::SPtr& session,
                  const Packet::Header& header,
                  const std::span<const byte> payload) override;
    void OnClosed(const Network::Session::SPtr& session, const std::error_code& reason) override;

private:
    void Register();
    void HandleToClient(const Network::Session::SPtr& worldSession, const Common::W2GRelay& packet);

    Network::SessionManager& sessionManager_;
    Network::SessionHolder& worldLink_;
    Packet::Dispatcher<PacketId, Network::Session::SPtr> dispatcher_;
};
