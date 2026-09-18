#pragma once

#include "Server/Core/Src/Base/BasicTypes.h"
#include "Server/Core/Src/Network/IPacketHandler.h"

// 운영툴 <-> World 연결의 IPacketHandler.
//
// **예전에는 ToolProcessor가 이 역할을 겸했다** -- 프로세서가 수신구까지 들고 있어서
// "레인에서 도는 것"과 "소켓 스레드에서 도는 것"이 한 클래스에 섞여 있었다. 다른 두 링크와
// 같은 모양으로 갈라둔다.
class T2WHandler final : public Network::IPacketHandler
{
public:
    void OnSessionOpened(const Network::Session::SPtr& session) override;
    void OnPacket(const Network::Session::SPtr& session,
                  const Packet::Header& header,
                  const std::span<const byte> payload) override;
    void OnClosed(const Network::Session::SPtr& session, const std::error_code& reason) override;
};
