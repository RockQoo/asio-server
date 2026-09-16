#pragma once

#include "Shared/Core/Src/Network/IPacketHandler.h"
#include "Shared/Core/Src/Network/SessionHolder.h"

namespace Network
{
    class SessionManager;
}


// 실제 게임 클라이언트를 accept하는 핸들러. 인증/게임 로직은 전혀 모른다 -- 접속 통지와
// 원본 패킷을 그대로 World로 릴레이(clientSessionId만 앞에 얹어서)하는 순수 패스스루다.
// 다른 서버 프로세스와의 Link가 아니라 실제 게임 클라이언트를 받는 쪽이지만, IPacketHandler
// 구현체는 전부 "연결 하나를 관리하는 핸들러"라는 공통점이 있어 XxxLinkHandler로 통일한다.
class C2GHandler final : public Network::IPacketHandler
{
public:
    C2GHandler(Network::SessionManager& sessionManager, Network::SessionHolder& worldLink);

    void OnSessionOpened(const Network::Session::SPtr& session) override;
    void OnPacket(const Network::Session::SPtr& session,
                  const Packet::Header& header,
                  const std::span<const byte> payload) override;
    void OnClosed(const Network::Session::SPtr& session, const std::error_code& reason) override;

private:
    Network::SessionManager& sessionManager_;
    Network::SessionHolder& worldLink_;
};
