#pragma once

#include <cstdint>

namespace World
{
    // Gateway <-> World 사이 프로토콜(S2W/W2S). 클라이언트 프로토콜(Server/ZoneServer/Src/Packet/
    // PacketId.h)과는 별개의 id 공간이다 -- 이 패킷들은 게이트웨이/월드끼리만 주고받고,
    // envelope으로 감싼 원본 클라이언트 패킷은 ClientEnvelopeHeader::innerPacketId로 구분한다.
    enum class GatewayLinkPacketId : uint16_t
    {
        ClientConnected = 1,     // S2W: 클라이언트 accept 직후 통지, payload = clientSessionId(8바이트)
        ClientDisconnected = 2,  // S2W: 클라이언트 접속 종료 통지, payload = clientSessionId(8바이트)
        FromClient = 3,          // S2W: ClientEnvelopeHeader + 클라이언트가 보낸 원본 패킷 그대로
        ToClient = 4,            // W2S: ClientEnvelopeHeader + 클라이언트에게 보낼 원본 패킷 그대로
    };
}
