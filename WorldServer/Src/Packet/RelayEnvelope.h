#pragma once

#include <cstdint>

namespace World
{
#pragma pack(push, 1)
    // Gateway<->World, World<->Zone 두 홉 모두에서 재사용하는 "이 패킷이 어느 클라이언트
    // 것인지" 태그. 실제 게임 패킷(Move/Chat/MailAdd 등 ZoneServer/Src/Packet의 원본 패킷)을
    // 그대로 뒤에 이어붙이고, 이 헤더만 보고 라우팅한다 -- 소켓 하나(Gateway<->World,
    // Zone<->World 연결)로 여러 플레이어의 패킷을 다중화하기 위함이다. 중계 서버(Gateway/
    // World)는 innerPacketId/바디의 내용을 해석할 필요가 없다 -- clientSessionId만 보고
    // 어디로 그대로 전달할지만 결정하면 된다(그래서 대부분의 중계 코드는 페이로드를 그대로
    // 재전송한다).
    struct ClientEnvelopeHeader
    {
        uint64_t clientSessionId;
        uint16_t innerPacketId;
    };
#pragma pack(pop)
}
