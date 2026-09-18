#pragma once

#include "App/ZoneDef.h"

#include "Server/Core/Src/Network/IPacketHandler.h"
#include "Server/Core/Src/Network/SessionHolder.h"

// World 와의 연결 하나의 IPacketHandler. **연결이 zoneId 하나당 하나라 이것도 존마다 하나다.**
//
// **여기 있는 것은 전부 I/O 스레드(Session 의 strand)에서 돈다.** 하는 일은 하나 --
// 바이트를 복사해 그 존의 LB 레인에 넣는다. 봉투를 까는 것도, 주인을 뽑는 것도 LB 가 한다.
//
// 이 소켓의 수신은 strand 하나로 직렬화되므로, 여기가 무거워지면 **그 존의 수신 전체**가
// 그만큼 좁아진다 -- 바이트 복사 이상을 여기에 두지 말 것.
//
// **owner 가 이 링크의 세션 id 다.** 연결마다 값이 달라서 존이 여럿이면 LB 레인도 그만큼
// 갈린다. 반대로 한 존 안에서는 값이 하나뿐이라 그 존의 LB 병렬도는 1이다.
class W2ZHandler final : public Network::IPacketHandler
{
public:
    W2ZHandler(const ZoneDef& def, Network::SessionHolder& worldLink);

    void OnSessionOpened(const Network::Session::SPtr& session) override;
    void OnPacket(const Network::Session::SPtr& session,
                  const Packet::Header& header,
                  const std::span<const byte> payload) override;
    void OnClosed(const Network::Session::SPtr& session, const std::error_code& reason) override;

private:
    const ZoneDef def_;
    Network::SessionHolder& worldLink_;
};
