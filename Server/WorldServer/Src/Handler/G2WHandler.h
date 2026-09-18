#pragma once

#include "Server/Core/Src/Base/Types.h"
#include "Server/Core/Src/Network/IPacketHandler.h"

// Gateway <-> World 연결의 IPacketHandler.
//
// **여기는 아직 소켓 스레드다.** IOCP 완료와 프레임 조립은 Session이 끝냈고, 이 콜백은
// 그 결과를 **BASIC 레인으로 넘기기만** 한다 -- 껍질은 까지 않는다.
// 껍질 까기는 BasicProcessor::OnRecvStream(owner = 게이트웨이 세션 id)의 일이다.
//
// **owner를 게이트웨이 세션 id로 주는 이유**: 이 단계의 주인은 아직 클라이언트가 아니라
// 소켓이다. 안에 든 clientSessionId는 껍질을 까야 나온다.
class G2WHandler final : public Network::IPacketHandler
{
public:
    void OnSessionOpened(const Network::Session::SPtr& session) override;
    void OnPacket(const Network::Session::SPtr& session,
                  const Packet::Header& header,
                  const std::span<const byte> payload) override;
    void OnClosed(const Network::Session::SPtr& session, const std::error_code& reason) override;
};
