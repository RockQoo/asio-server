#pragma once

#include "Core/Src/Packet/BinaryWriter.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>

namespace Packet
{
    // 소유하지 않는(non-owning) 바이트 span(보통 패킷 페이로드) 위를 커서로 순회하며 읽는 리더
    class BinaryReader
    {
    public:
        explicit BinaryReader(const std::span<const byte> data) noexcept
            : data_(data)
        {
        }

        template <TriviallySerializable T>
        [[nodiscard]] bool Read(T& outValue) noexcept
        {
            if (position_ + sizeof(T) > data_.size())
            {
                return false;
            }

            std::memcpy(&outValue, data_.data() + position_, sizeof(T));
            position_ += sizeof(T);
            return true;
        }

        [[nodiscard]] bool ReadString(std::string& outText)
        {
            uint16_t length{};
            if (!Read(length) || position_ + length > data_.size())
            {
                return false;
            }

            const auto* const begin = reinterpret_cast<const char*>(data_.data() + position_);
            outText.assign(begin, length);
            position_ += length;
            return true;
        }

        [[nodiscard]] std::span<const byte> RemainingBytes() const noexcept
        {
            return data_.subspan(position_);
        }

        // 정확히 length바이트를 잘라서 반환하고 커서를 그만큼 전진시킨다. 길이 프리픽스가
        // 붙은 개별 필드(예: 태스크별 페이로드)를 RemainingBytes()처럼 "나머지 전부"가 아니라
        // 딱 그 필드만큼만 끊어 읽어야 할 때 쓴다.
        [[nodiscard]] std::optional<std::span<const byte>> ReadBytes(const size_t length) noexcept
        {
            if (position_ + length > data_.size())
            {
                return std::nullopt;
            }

            const auto bytes = data_.subspan(position_, length);
            position_ += length;
            return bytes;
        }

        [[nodiscard]] size_t Remaining() const noexcept { return data_.size() - position_; }
        [[nodiscard]] bool AtEnd() const noexcept { return position_ >= data_.size(); }

    private:
        std::span<const byte> data_;
        size_t position_{0};
    };
}
