#pragma once

#include "Shared/Common/Src/PacketId.h"

#include "Shared/Common/Src/Packet/RelayEnvelope.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"

// 패킷 구조체 <-> 바이트. **보내는 쪽과 받는 쪽을 같은 모양으로 둔다** -- 한쪽만 읽어도
// 다른 쪽이 어떻게 생겼는지 짐작이 된다.
//
//   ToBytes(packet)              구조체 -> 바이트   (Serialize() 있으면 그걸, 없으면 memcpy)
//   FromBytes(packet, payload)   바이트 -> 구조체   (Parse()     있으면 그걸, 없으면 memcpy)
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

    // 받은 바이트를 패킷 구조체로 읽는다. `ToBytes` 의 정확한 반대편이다.
    //
    // **여기가 검증의 유일한 자리다.** 와이어에는 타입 정보가 없어서 길이 필드를 믿고 읽어야
    // 하는데 그 값이 거짓일 수 있다 -- 잘린 프레임, 조작, 버전 불일치. false 를 돌려주면
    // 호출부(Dispatcher)가 로그만 남기고 버린다.
    //
    // **고정 레이아웃 패킷은 `Parse()` 를 쓸 필요가 없다.** 아래 else 가 크기 검사 + memcpy 를
    // 대신한다. 문자열/목록이 들었거나 꼬리를 span 으로 들고 있는 패킷만 직접 쓴다.
    template <typename TPacket>
    concept SelfParsing = requires(TPacket& packet, const std::span<const byte> payload) {
        { packet.Parse(payload) } -> std::same_as<bool>;
    };

    template <typename TPacket>
    [[nodiscard]] bool FromBytes(TPacket& packet, const std::span<const byte> payload)
    {
        if constexpr (SelfParsing<TPacket>)
        {
            return packet.Parse(payload);
        }
        else
        {
            static_assert(std::is_trivially_copyable_v<TPacket> && std::is_standard_layout_v<TPacket>,
                          "고정 레이아웃이 아니면 Parse() 를 만들어야 한다");

            // 더 길게 온 것은 통과시킨다 -- 뒤에 붙은 건 이 빌드가 모르는 새 필드일 수 있고,
            // 앞쪽 레이아웃이 같으면 읽는 데 지장이 없다(구버전 서버가 신버전을 받는 경우).
            if (payload.size() < sizeof(TPacket))
            {
                return false;
            }

            std::memcpy(&packet, payload.data(), sizeof(TPacket));
            return true;
        }
    }

    // ---- 보내기 ----
    //
    // **패킷 구조체 하나만 받는다.** id 는 `TPacket::kPacketId` 에서 나오므로 호출부가
    // id 와 본문을 따로 넘기다 어긋날 수가 없다.
    //
    // 예전에는 부르는 자리마다 이렇게 적혀 있었다(7곳):
    //   session->SendPacket(PacketId::W2ZLeaveZone, std::as_bytes(std::span(&leave, 1)));
    // `as_bytes` 는 고정 레이아웃에만 맞아서, 그 패킷에 문자열이 하나 붙는 순간 조용히
    // 어긋난다 -- `ToBytes` 는 concept 으로 갈라주므로 그런 일이 없다.
    template <typename TPacket>
    void SendPacket(const Network::Session::SPtr& session, const TPacket& packet)
    {
        session->SendPacket(TPacket::kPacketId, ToBytes(packet));
    }

    // 중계 봉투에 실어 보낸다. 겉봉투 id(홉)와 안쪽 패킷(내용물)이 따로라 둘 다 받는다.
    //
    // 예전에는 이 세 줄이 Zone/World/Tool 여덟 곳에 흩어져 있었다:
    //   session->SendPacket(PacketId::Z2WRelay,
    //       Common::WrapRelay(clientSessionId, static_cast<uint16_t>(inner), ToBytes(packet)));
    template <typename TPacket>
    void SendRelay(const Network::Session::SPtr& session, const PacketId outerPacketId,
                   const uint64_t clientSessionId, const TPacket& packet)
    {
        session->SendPacket(outerPacketId,
                            WrapRelay(clientSessionId, static_cast<uint16_t>(TPacket::kPacketId),
                                      ToBytes(packet)));
    }

    // 안쪽이 이미 바이트인 경우(중계 그대로 넘기기, 브로드캐스트 등). 구조체를 안 거치므로
    // 안쪽 id 를 직접 받는다 -- 위 오버로드와 달리 `kPacketId` 로 유도할 것이 없다.
    inline void SendRelay(const Network::Session::SPtr& session, const PacketId outerPacketId,
                          const uint64_t clientSessionId, const PacketId innerPacketId,
                          const std::span<const byte> innerPayload)
    {
        session->SendPacket(outerPacketId,
                            WrapRelay(clientSessionId, static_cast<uint16_t>(innerPacketId), innerPayload));
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
