#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Packet
{
    // 바이트 단위로 그대로 복사해도 안전한 타입(POD 성격, 패딩 문제 없음을 가정)인지 검사하는 concept
    template <typename T>
    concept TriviallySerializable = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

    // 패킷 페이로드를 만들 때 쓰는 append-only 바이너리 라이터
    class BinaryWriter
    {
    public:
        BinaryWriter() = default;

        template <TriviallySerializable T>
        void Write(const T& value)
        {
            const auto* const bytes = reinterpret_cast<const byte*>(&value);
            buffer_.insert(buffer_.end(), bytes, bytes + sizeof(T));
        }

        // 길이(uint16) + 문자열 데이터 순서로 저장한다
        void WriteString(const std::string_view text)
        {
            Write(static_cast<uint16_t>(text.size()));
            const auto* const bytes = reinterpret_cast<const byte*>(text.data());
            buffer_.insert(buffer_.end(), bytes, bytes + text.size());
        }

        void WriteBytes(const std::span<const byte> data)
        {
            buffer_.insert(buffer_.end(), data.begin(), data.end());
        }

        [[nodiscard]] const std::vector<byte>& GetBuffer() const noexcept { return buffer_; }
        [[nodiscard]] std::vector<byte> MoveBuffer() noexcept { return std::move(buffer_); }
        [[nodiscard]] size_t Size() const noexcept { return buffer_.size(); }

    private:
        std::vector<byte> buffer_;
    };
}
