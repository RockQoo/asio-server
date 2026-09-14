#pragma once

#include "Server/WorldServer/Src/Packet/ZoneLinkPackets.h"
#include "Server/WorldServer/Src/World/PlayerManager.h"

#include "Shared/Core/Src/Packet/BinaryWriter.h"

namespace World
{
    // W2ZEnterZone 본문을 만든다. 포맷은 ZoneLinkPackets.h 의 표가 유일한 계약이고,
    // 읽는 쪽은 ZoneServer 의 WorldLinkHandler 다.
    //
    // **쓰는 곳이 둘이라 함수로 뺐다** -- 신규 입장(LoginProcessor)과 핸드오프
    // (ZoneLinkHandler). 둘이 같은 바이트를 만들어야 존 쪽 파서가 하나로 끝난다.
    [[nodiscard]] inline std::vector<byte> BuildEnterZoneBody(
        const PlayerZoneStatePacket& state,
        const std::unordered_map<Protocol::MailId, MailInfo>& mails,
        const std::unordered_map<uint8_t, int64_t>& currencies)
    {
        Packet::BinaryWriter binaryWriter;
        binaryWriter.Write(state);

        // 개수를 uint16으로 두는 이유: Header::MaxBodySize()가 8192라 그 안에 들어갈 수 있는
        // 우편 수가 애초에 수백 단위다. 넘치는 상황은 아래에서 잘라낸다.
        binaryWriter.Write(static_cast<uint16_t>(mails.size()));
        for (const auto& [mailId, info] : mails)
        {
            binaryWriter.Write(info.mailId);
            binaryWriter.WriteString(info.title);
            binaryWriter.WriteString(info.body);
            binaryWriter.Write(info.sendUt);
            binaryWriter.Write(info.endUt);
        }

        binaryWriter.Write(static_cast<uint16_t>(currencies.size()));
        for (const auto& [type, amount] : currencies)
        {
            binaryWriter.Write(type);
            binaryWriter.Write(amount);
        }

        return binaryWriter.MoveBuffer();
    }
}
