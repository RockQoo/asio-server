#pragma once

#include "Shared/Core/Src/Common/CoreErrorCode.h"
#include "Shared/Core/Src/Common/CoreException.h"
#include "Shared/Core/Src/Packet/Header.h"

namespace Packet
{
    // [헤더 + 페이로드] 프레임을 만든다. Session::SendPacket과 ProtocolClient가 같은 로직을 쓰도록
    // 여기 하나로 모아뒀다 -- 프레임 포맷이 바뀌면 이 함수만 고치면 된다.
    //
    // **상한을 여기서 막는다.** bodySize가 uint16이라 그냥 캐스팅하면 65,536을 넘는 순간 값이
    // 랩어라운드해서(예: 65,540 -> 4) 받는 쪽이 본문 4바이트만 읽고 **나머지를 헤더로 해석한다**
    // -- 스트림이 통째로 어긋나고 아무도 눈치채지 못한다. 8,192~65,535 구간도 받는 쪽
    // Packet::Buffer가 예외를 던져 연결을 끊는데, 서버 간 링크는 재연결이 없어서 거기서 끝난다.
    // 둘 다 **보내는 쪽에서 막는 편이 훨씬 싸다**: 여기서 던지면 원인이 보내는 코드에 남는다.
    [[nodiscard]] inline std::vector<byte> BuildFrame(const uint16_t packetId,
                                                             const std::span<const byte> payload)
    {
        if (payload.size() > Header::MaxBodySize())
        {
            throw Common::CoreException(Common::ECoreErrorCode::PacketTooLarge,
                                        "BuildFrame: 본문이 MaxBodySize를 초과한다");
        }

        Header header{};
        header.bodySize = static_cast<uint16_t>(payload.size());
        header.id = packetId;

        std::vector<byte> frame(Header::HeaderSize() + payload.size());
        std::memcpy(frame.data(), &header, Header::HeaderSize());
        if (!payload.empty())
        {
            std::memcpy(frame.data() + Header::HeaderSize(), payload.data(), payload.size());
        }

        return frame;
    }

    // 패킷 id enum을 그대로 받는 오버로드. Session::SendPacket과 같은 이유로 둔다 --
    // 호출부에서 static_cast<uint16_t>를 반복하지 않게 하되, Core는 어떤 enum인지 모른다.
    template <typename TPacketId> requires std::is_enum_v<TPacketId>
    [[nodiscard]] inline std::vector<byte> BuildFrame(const TPacketId packetId,
                                                      const std::span<const byte> payload)
    {
        return BuildFrame(static_cast<uint16_t>(packetId), payload);
    }
}
