#include "pch.h"
#include "Shared/Core/Src/Packet/Buffer.h"

#include "Shared/Core/Src/Common/CoreException.h"

namespace Packet
{
    void Buffer::Append(const std::span<const byte> data)
    {
        if (writePos_ + data.size() > buffer_.size())
        {
            buffer_.resize(std::max(writePos_ + data.size(), buffer_.size() * 2 + data.size()));
        }

        std::memcpy(buffer_.data() + writePos_, data.data(), data.size());
        writePos_ += data.size();
    }

    bool Buffer::TryExtract(Header& outHeader, std::vector<byte>& outPayload)
    {
        const auto available = writePos_ - readPos_;
        if (available < Header::HeaderSize())
        {
            return false;
        }

        Header header{};
        std::memcpy(&header, buffer_.data() + readPos_, Header::HeaderSize());

        if (header.bodySize > Header::MaxBodySize())
        {
            throw Common::CoreException(Common::ECoreErrorCode::PacketTooLarge,
                                         "Buffer: 본문 크기가 MaxBodySize를 초과하여 연결을 종료한다");
        }

        const auto totalSize = Header::HeaderSize() + header.bodySize;
        if (available < totalSize)
        {
            return false;
        }

        outHeader = header;
        outPayload.assign(buffer_.begin() + static_cast<std::ptrdiff_t>(readPos_ + Header::HeaderSize()),
                           buffer_.begin() + static_cast<std::ptrdiff_t>(readPos_ + totalSize));

        readPos_ += totalSize;
        Compact();
        return true;
    }

    void Buffer::Compact()
    {
        if (readPos_ == writePos_)
        {
            readPos_ = 0;
            writePos_ = 0;
            return;
        }

        // 이미 읽은 앞부분이 버퍼 절반을 넘을 때만 당겨온다; 패킷 하나 꺼낼 때마다 매번
        // memmove 하는 비용을 피하기 위함이다 (한 번의 TCP 수신에 여러 패킷이 몰려 있는 경우가 많음).
        if (readPos_ > buffer_.size() / 2)
        {
            const auto remaining = writePos_ - readPos_;
            std::memmove(buffer_.data(), buffer_.data() + readPos_, remaining);
            readPos_ = 0;
            writePos_ = remaining;
        }
    }
}
