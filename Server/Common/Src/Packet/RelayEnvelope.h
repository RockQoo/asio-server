#pragma once

#include "Server/Common/Src/PacketId.h"

#include "Server/Core/Src/Packet/BinaryWriter.h"

namespace Common
{
#pragma pack(push, 1)
    // Gateway<->World, World<->Zone 두 홉 모두에서 재사용하는 "이 패킷이 어느 클라이언트
    // 것인지" 태그. 실제 게임 패킷(Move/Chat/MailAdd 등 Server/ZoneServer/Src/Packet의 원본 패킷)을
    // 그대로 뒤에 이어붙이고, 이 헤더만 보고 라우팅한다 -- 소켓 하나(Gateway<->World,
    // Zone<->World 연결)로 여러 플레이어의 패킷을 다중화하기 위함이다. 중계 서버(Gateway/
    // World)는 innerPacketId/바디의 내용을 해석할 필요가 없다 -- clientSessionId만 보고
    // 어디로 그대로 전달할지만 결정하면 된다(그래서 대부분의 중계 코드는 페이로드를 그대로
    // 재전송한다).
    struct RelayEnvelope
    {
        uint64_t clientSessionId;
        uint16_t innerPacketId;
    };
#pragma pack(pop)

    // 봉투를 벗긴 결과. 봉투 자체와 그 뒤에 이어 붙은 원본 패킷을 같이 돌려준다.
    // **innerPayload 는 인자로 받은 버퍼를 가리킨다** -- 복사가 아니라 subspan 이므로
    // 그 버퍼보다 오래 들고 있으면 안 된다.
    struct RelayView
    {
        RelayEnvelope envelope{};
        std::span<const byte> innerPayload;
    };

    // 봉투를 씌운다. 중계 홉 네 개(G2WRelay/W2GRelay/W2ZRelay/Z2WRelay)가 전부 이 모양이라
    // 여기 한 곳에 둔다 -- 예전에는 같은 네 줄이 Handler 와 Processor 열 곳에 흩어져 있었다.
    [[nodiscard]] inline std::vector<byte> WrapRelay(const uint64_t clientSessionId,
                                                     const uint16_t innerPacketId,
                                                     const std::span<const byte> innerPayload)
    {
        Packet::BinaryWriter binaryWriter;
        binaryWriter.Write(RelayEnvelope{clientSessionId, innerPacketId});
        binaryWriter.WriteBytes(innerPayload);
        return binaryWriter.MoveBuffer();
    }

    // 봉투를 벗긴다. 봉투가 다 안 왔으면 nullopt -- 호출부가 그대로 버리면 된다.
    [[nodiscard]] inline std::optional<RelayView> UnwrapRelay(const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(RelayEnvelope))
        {
            return std::nullopt;
        }

        RelayView view;
        std::memcpy(&view.envelope, payload.data(), sizeof(RelayEnvelope));
        view.innerPayload = payload.subspan(sizeof(RelayEnvelope));
        return view;
    }

    // 중계 패킷 넷의 받는 쪽 구조체. **봉투는 공유하되 패킷 구조체는 id 마다 하나씩** 둔다 --
    // `kPacketId` 는 값이 하나여야 해서 넷이 공유할 수 없고, 그것이 있어야 등록이
    // `Register(this, &X::Handle)` 한 형태로 통일된다.
    //
    // **수명 주의**: `innerPayload`/`raw` 는 복사가 아니라 수신 버퍼를 가리키는 subspan 이다.
    // 핸들러 스코프를 넘겨 들고 있으면 안 된다(패킷 구조체 공통 규약 -- packet-naming.md).
    //
    // `raw` 가 따로 있는 이유: 중계는 **봉투째 그대로** 다음 홉으로 넘기는 경우가 있어서
    // (World 가 Gateway<->Zone 사이에서 그렇다) 원본 바이트가 필요하다. 없으면 다시
    // `WrapRelay` 로 조립해야 해서 중계 경로에 할당이 생긴다.
    template <PacketId TPacketId>
    struct RelayPacket
    {
        static constexpr PacketId kPacketId = TPacketId;

        RelayEnvelope envelope{};
        std::span<const byte> innerPayload;  // 봉투 뒤 -- 안쪽으로 넘길 때
        std::span<const byte> raw;           // 본문 전체 -- 봉투째 재전송할 때

        [[nodiscard]] bool Parse(const std::span<const byte> payload)
        {
            const auto view = UnwrapRelay(payload);
            if (!view)
            {
                return false;
            }

            envelope = view->envelope;
            innerPayload = view->innerPayload;
            raw = payload;
            return true;
        }
    };

    using G2WRelay = RelayPacket<PacketId::G2WRelay>;
    using W2GRelay = RelayPacket<PacketId::W2GRelay>;
    using W2ZRelay = RelayPacket<PacketId::W2ZRelay>;
    using Z2WRelay = RelayPacket<PacketId::Z2WRelay>;
}
