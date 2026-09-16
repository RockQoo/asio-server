#pragma once

#include "Shared/Common/Src/PacketId.h"

#include "Shared/Core/Src/Packet/BinaryWriter.h"

namespace Common
{
    // 패킷 구조체를 바이트로 만든다.
    //
    // **두 종류가 있다.** 고정 레이아웃 구조체는 그대로 memcpy 하면 되고(`#pragma pack(1)`),
    // 문자열이나 목록이 든 것은 자기 `Serialize()` 로 직접 쓴다. 호출부가 그 구분을 알 필요가
    // 없도록 여기서 concept 으로 가른다 -- 콘텐츠를 추가하다 가변 길이가 되어도 보내는 쪽
    // 코드는 한 글자도 안 바뀐다.
    template <typename TPacket>
    concept SelfSerializing = requires(const TPacket& packet) {
        { packet.Serialize() } -> std::same_as<std::vector<byte>>;
    };

    template <typename TPacket>
    [[nodiscard]] std::vector<byte> ToBytes(const TPacket& packet)
    {
        if constexpr (SelfSerializing<TPacket>)
        {
            return packet.Serialize();
        }
        else
        {
            static_assert(std::is_trivially_copyable_v<TPacket> && std::is_standard_layout_v<TPacket>,
                          "고정 레이아웃이 아니면 Serialize() 를 만들어야 한다");
            const auto* const bytes = reinterpret_cast<const byte*>(&packet);
            return std::vector<byte>(bytes, bytes + sizeof(TPacket));
        }
    }

    // 보내는 방향을 컴파일 타임에 검증한다.
    //
    // 번호 대역이 1000 단위로 잘려 있어서 id 값 하나로 방향이 나온다. 그래서 "이 함수로
    // 보낼 수 있는 패킷인가"를 **빌드가 막아준다** -- 패킷 이름 규칙이 지키면 좋은 것에서
    // 안 지키면 빌드가 안 되는 것이 된다.
    template <typename TPacket, EPacketDirection... TAllowed>
    constexpr void AssertDirection()
    {
        constexpr auto kDirection = DirectionOf(TPacket::kPacketId);
        static_assert(((kDirection == TAllowed) || ...),
                      "이 함수로 보낼 수 없는 방향의 패킷이다 (.claude/rules/packet-naming.md)");
    }
}
