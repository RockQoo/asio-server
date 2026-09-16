#include "pch.h"
#include "Handler/WorldLinkHandler.h"
#include "Processor/PlayerProcessor.h"
#include "World/WorldLink.h"
#include "Server/WorldServer/Src/Packet/OwnerIdPeek.h"
#include "Server/WorldServer/Src/Packet/ZoneLinkPackets.h"
#include "Shared/Common/Src/PacketId.h"

#include "Shared/Core/Src/Network/Session.h"

namespace Zone
{
    WorldLinkHandler::WorldLinkHandler(PlayerProcessor& playerProcessor,
                                       Processor::Group<EProcessorId>& playerGroup,
                                       WorldLink& worldLink, std::vector<Def> zoneDefs)
        : playerProcessor_(playerProcessor)
        , playerGroup_(playerGroup)
        , worldLink_(worldLink)
        , zoneDefs_(std::move(zoneDefs))
    {
    }

    void WorldLinkHandler::OnSessionOpened(const Network::Session::SPtr& session)
    {
        worldLink_.Set(session);

        // 이 프로세스가 담당하는 존마다 한 번씩 등록한다 -- 여러 zoneId가 이 연결 하나를 같이
        // 쓴다(프로세스 하나가 존 여러 개를 동시에 호스팅할 수 있으므로).
        for (const auto& def : zoneDefs_)
        {
            World::Z2WZoneRegister registerPacket{};
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
        case PacketId::W2ZEnterZone:
            // W2ZEnterZone.clientSessionId -- zoneId(uint32) 뒤라 offset 4.
            return World::PeekOwnerId<Network::SessionId>(payload, sizeof(uint32_t));

        case PacketId::W2ZLeaveZone:
            // W2ZLeaveZone.clientSessionId (offset 0)
            return World::PeekOwnerId<Network::SessionId>(payload);

        case PacketId::W2ZRelay:
            // RelayEnvelope.clientSessionId (offset 0)
            return World::PeekOwnerId<Network::SessionId>(payload);

        default:
            return std::nullopt;
        }
    }

    void WorldLinkHandler::OnPacket(const Network::Session::SPtr& /*session*/,
                                    const Packet::Header& header,
                                    const std::span<const byte> payload)
    {
        // 여기는 I/O 스레드(Session의 strand)다. ownerId만 훔쳐보고 바이트를 복사해 플레이어
        // 레인에 넘긴다 -- payload는 이 함수가 끝나면 I/O 스레드가 재사용할 버퍼를 가리킨다.
        const auto packetId = static_cast<PacketId>(header.id);

        const auto ownerId = OwnerIdOf(packetId, payload);
        if (!ownerId)
        {
            LOG.Warning(ELogCategory::Zone, "ownerId를 읽을 수 없는 패킷, 버림")
                .KV("PacketId", header.id).KV("PayloadSize", payload.size());
            return;
        }

        std::vector<byte> payloadCopy(payload.begin(), payload.end());

        playerGroup_.Post(EProcessorId::Player, *ownerId,
            [this, clientSessionId = static_cast<Network::SessionId>(*ownerId),
             packetId, payloadCopy = std::move(payloadCopy)]
            {
                playerProcessor_.DispatchFromWorld(packetId, clientSessionId, payloadCopy);
            });
    }

    void WorldLinkHandler::OnClosed(const Network::Session::SPtr& /*session*/, const std::error_code& reason)
    {
        worldLink_.Clear();
        LOG.Warning(ELogCategory::Zone, "World 연결 끊김").KV("Message", reason.message());
    }
}
