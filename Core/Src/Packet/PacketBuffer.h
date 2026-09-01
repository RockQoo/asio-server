#pragma once

#include "Core/Src/Packet/PacketHeader.h"

#include <cstddef>
#include <span>
#include <vector>

namespace Packet
{
    // 스트림 재조립 버퍼: TCP는 메시지 경계가 아니라 바이트 스트림만 보장하므로, 들어오는
    // 바이트를 여기에 계속 쌓아두고 [헤더 + 본문]이 온전히 도착했을 때만 패킷을 꺼낸다.
    class PacketBuffer
    {
    public:
        void Append(std::span<const byte> data);

        // 완전한 패킷이 있으면 꺼낸다. 헤더가 PacketHeader::MaxBodySize()를 넘는 본문 크기를
        // 주장하면 Common::CoreException(EErrorCode::PacketTooLarge)을 던진다
        // (손상되었거나 악의적인 스트림으로부터 보호).
        [[nodiscard]] bool TryExtract(PacketHeader& outHeader, std::vector<byte>& outPayload);

        [[nodiscard]] size_t BufferedSize() const noexcept { return writePos_ - readPos_; }

    private:
        void Compact();

        std::vector<byte> buffer_;
        size_t readPos_{0};
        size_t writePos_{0};
    };
}
