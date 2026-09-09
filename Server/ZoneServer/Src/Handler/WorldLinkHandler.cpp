#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Handler/WorldLinkHandler.h"
#include "Server/ZoneServer/Src/Handler/PlayerProcessor.h"
#include "Server/ZoneServer/Src/World/WorldLink.h"
#include "Server/WorldServer/Src/Packet/OwnerIdPeek.h"
#include "Server/WorldServer/Src/Packet/ZoneLinkPackets.h"
#include "Shared/Protocol/Src/PacketId.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"

#include <cstring>
#include <utility>

namespace Zone
{
    WorldLinkHandler::WorldLinkHandler(PlayerProcessor& playerProcessor,
                                       Processor::ProcessorGroup<EProcessorId>& lbGroup,
                                       Processor::ProcessorGroup<EProcessorId>& playerGroup,
                                       WorldLink& worldLink, std::vector<ZoneDef> zoneDefs)
        : playerProcessor_(playerProcessor)
        , lbGroup_(lbGroup)
        , playerGroup_(playerGroup)
        , worldLink_(worldLink)
        , zoneDefs_(std::move(zoneDefs))
    {
    }

    void WorldLinkHandler::OnSessionOpened(const std::shared_ptr<Network::Session>& session)
    {
        worldLink_.Set(session);

        // 이 프로세스가 담당하는 존마다 한 번씩 등록한다 -- 여러 zoneId가 이 연결 하나를 같이
        // 쓴다(프로세스 하나가 존 여러 개를 동시에 호스팅할 수 있으므로).
        for (const auto& def : zoneDefs_)
        {
            World::ZoneRegisterPacket registerPacket{};
            registerPacket.zoneId = def.zoneId;
            registerPacket.xMin = def.xMin;
            registerPacket.xMax = def.xMax;
            registerPacket.yMin = def.yMin;
            registerPacket.yMax = def.yMax;
            session->SendPacket(PacketId::Z2WZoneRegister,
                                std::as_bytes(std::span(&registerPacket, 1)));

            LOG.Info(ELogCategory::Zone, "World 연결 성공, 존 등록")
                .KV("ZoneId", def.zoneId)
                .KV("XMin", def.xMin).KV("XMax", def.xMax)
                .KV("YMin", def.yMin).KV("YMax", def.yMax);
        }
    }

    std::optional<uint64_t> WorldLinkHandler::OwnerIdOf(const PacketId packetId, const std::span<const byte> payload)
    {
        switch (packetId)
        {
        case PacketId::W2ZEnterZoneRequest:
            // PlayerZoneStatePacket.clientSessionId -- zoneId(uint32) 뒤라 offset 4.
            return World::PeekOwnerId<Network::SessionId>(payload, sizeof(uint32_t));

        case PacketId::W2ZLeaveZoneNotify:
            // LeaveZoneNotifyPacket.clientSessionId (offset 0)
            return World::PeekOwnerId<Network::SessionId>(payload);

        case PacketId::W2ZRelay:
            // ClientEnvelopeHeader.clientSessionId (offset 0)
            return World::PeekOwnerId<Network::SessionId>(payload);

        default:
            return std::nullopt;
        }
    }

    void WorldLinkHandler::OnPacket(const std::shared_ptr<Network::Session>& /*session*/,
                                    const Packet::PacketHeader& header,
                                    const std::span<const byte> payload)
    {
        // 여기는 I/O 스레드(Session의 strand)다. ownerId만 훔쳐보고 바이트를 복사해 LB 레인에
        // 넘긴다 -- payload는 이 함수가 끝나면 I/O 스레드가 재사용할 버퍼를 가리킨다.
        const auto packetId = static_cast<PacketId>(header.id);

        const auto ownerId = OwnerIdOf(packetId, payload);
        if (!ownerId)
        {
            LOG.Warning(ELogCategory::Zone, "ownerId를 읽을 수 없는 패킷, 버림")
                .KV("PacketId", header.id).KV("PayloadSize", payload.size());
            return;
        }

        std::vector<byte> payloadCopy(payload.begin(), payload.end());

        lbGroup_.Post(EProcessorId::Lb, *ownerId,
            [this, packetId, ownerId = *ownerId, payloadCopy = std::move(payloadCopy)]
            {
                DecodeAndDispatch(packetId, ownerId, payloadCopy);
            });
    }

    void WorldLinkHandler::OnClosed(const std::shared_ptr<Network::Session>& /*session*/, const std::error_code& reason)
    {
        worldLink_.Clear();
        LOG.Warning(ELogCategory::Zone, "World 연결 끊김").KV("Message", reason.message());
    }

    void WorldLinkHandler::DecodeAndDispatch(const PacketId packetId, const Network::SessionId ownerId,
                                             const std::vector<byte>& payload)
    {
        // 여기부터는 LB 레인. 패킷 id 파싱과 1차 분기만 하고, 콘텐츠 해석은 플레이어 레인 몫이다.
        switch (packetId)
        {
        case PacketId::W2ZEnterZoneRequest:
            HandleEnterZoneRequest(payload);
            break;
        case PacketId::W2ZLeaveZoneNotify:
            HandleLeaveZoneNotify(payload);
            break;
        case PacketId::W2ZRelay:
            HandleForwardToZone(ownerId, payload);
            break;
        default:
            break;
        }
    }

    void WorldLinkHandler::HandleEnterZoneRequest(const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(World::PlayerZoneStatePacket))
        {
            return;
        }

        World::PlayerZoneStatePacket state{};
        std::memcpy(&state, payload.data(), sizeof(World::PlayerZoneStatePacket));

        playerGroup_.Post(EProcessorId::Player, state.clientSessionId, [this, state]
        {
            playerProcessor_.OnPlayerEnter(state.clientSessionId, state.playerId, state.zoneId,
                                            state.x, state.y);
        });
    }

    void WorldLinkHandler::HandleLeaveZoneNotify(const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(World::LeaveZoneNotifyPacket))
        {
            return;
        }

        World::LeaveZoneNotifyPacket leave{};
        std::memcpy(&leave, payload.data(), sizeof(World::LeaveZoneNotifyPacket));

        playerGroup_.Post(EProcessorId::Player, leave.clientSessionId,
            [this, clientSessionId = leave.clientSessionId]
            {
                playerProcessor_.OnPlayerLeave(clientSessionId);
            });
    }

    void WorldLinkHandler::HandleForwardToZone(const Network::SessionId ownerId, const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(World::ClientEnvelopeHeader))
        {
            return;
        }

        World::ClientEnvelopeHeader header{};
        std::memcpy(&header, payload.data(), sizeof(World::ClientEnvelopeHeader));
        const auto innerPayload = payload.subspan(sizeof(World::ClientEnvelopeHeader));

        const auto innerPacketId = static_cast<PacketId>(header.innerPacketId);
        if (innerPacketId == PacketId::C2ZEcho)
        {
            ReplyEcho(header, innerPayload);
            return;
        }

        // Echo를 제외한 나머지는 내용을 들여다보지 않고 그대로 플레이어 레인에 넘긴다 --
        // 와이어 포맷 파싱은 PlayerProcessor 쪽 몫이다.
        std::vector<byte> innerPayloadCopy(innerPayload.begin(), innerPayload.end());
        playerGroup_.Post(EProcessorId::Player, ownerId,
            [this, ownerId, innerPacketId, innerPayloadCopy = std::move(innerPayloadCopy)]
            {
                playerProcessor_.HandleClientPacket(ownerId, innerPacketId, innerPayloadCopy);
            });
    }

    void WorldLinkHandler::ReplyEcho(const World::ClientEnvelopeHeader& header,
                                     const std::span<const byte> innerPayload) const
    {
        const auto worldSession = worldLink_.Get();
        if (!worldSession)
        {
            return;
        }

        // 받은 envelope을 그대로 쓰되 innerPacketId만 응답 방향으로 바꾼다 -- 요청과 응답이
        // 같은 id를 공유하지 않는 것이 패킷 id 규약이다(본문은 받은 것 그대로).
        World::ClientEnvelopeHeader replyHeader = header;
        replyHeader.innerPacketId = static_cast<uint16_t>(PacketId::Z2CEchoAck);

        Packet::BinaryWriter writer;
        writer.Write(replyHeader);
        writer.WriteBytes(innerPayload);
        worldSession->SendPacket(PacketId::Z2WRelay, writer.GetBuffer());
    }
}
