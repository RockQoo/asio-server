#pragma once

#include "Shared/Core/Src/Packet/PacketHeader.h"

#include <cstring>
#include <span>
#include <vector>

namespace Packet
{
    // [헤더 + 페이로드] 프레임을 만든다. Session::SendPacket과 TestClient가 같은 로직을 쓰도록
    // 여기 하나로 모아뒀다 -- 프레임 포맷이 바뀌면 이 함수만 고치면 된다.
    [[nodiscard]] inline std::vector<byte> BuildFrame(const uint16_t packetId,
                                                             const std::span<const byte> payload)
    {
        PacketHeader header{};
        header.bodySize = static_cast<uint16_t>(payload.size());
        header.id = packetId;

        std::vector<byte> frame(PacketHeader::HeaderSize() + payload.size());
        std::memcpy(frame.data(), &header, PacketHeader::HeaderSize());
        if (!payload.empty())
        {
            std::memcpy(frame.data() + PacketHeader::HeaderSize(), payload.data(), payload.size());
        }

        return frame;
    }
}
