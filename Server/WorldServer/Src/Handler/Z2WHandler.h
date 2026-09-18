#pragma once

#include "Server/Core/Src/Base/BasicTypes.h"
#include "Server/Core/Src/Network/IPacketHandler.h"

// Zone <-> World 연결의 IPacketHandler. Zone 서버 프로세스가 여러 개 연결해 오며, 각 연결이
// 자기가 호스팅하는 zoneId/담당 사각형을 ZoneRegister로 알려온다.
//
// **G2WHandler와 같은 자리다** -- 프레임을 BASIC 레인으로 넘기기만 하고 껍질은 까지 않는다.
// 존 링크는 패킷마다 주인도 위치도 달라서 껍질 까기가 더 복잡한데, 그래서 더더욱 소켓
// 스레드에서 하지 않는다.
class Z2WHandler final : public Network::IPacketHandler
{
public:
    void OnSessionOpened(const Network::Session::SPtr& session) override;
    void OnPacket(const Network::Session::SPtr& session,
                  const Packet::Header& header,
                  const std::span<const byte> payload) override;
    void OnClosed(const Network::Session::SPtr& session, const std::error_code& reason) override;
};
