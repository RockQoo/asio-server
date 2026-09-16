#include "pch.h"
#include "Handler/ZoneLinkHandler.h"

#include "Packet/OwnerIdPeek.h"
#include "Packet/ZoneLinkPackets.h"
#include "Processor/MainProcessor.h"

#include "Shared/Core/Src/Base/RUID.h"
#include "Shared/Core/Src/Network/Session.h"

namespace World
{
    ZoneLinkHandler::ZoneLinkHandler(Processor::Group<EProcessorId>& basicGroup, MainProcessor& mainProcessor)
        : basicGroup_(basicGroup)
        , mainProcessor_(mainProcessor)
    {
    }

    std::optional<uint64_t> ZoneLinkHandler::OwnerIdOf(const PacketId packetId, const std::span<const byte> payload)
    {
        switch (packetId)
        {
        case PacketId::Z2WZoneRegister:
            // Z2WZoneRegister.zoneId (offset 0)
            return PeekOwnerId<uint32_t>(payload);

        case PacketId::Z2WRelay:
            // RelayEnvelope.clientSessionId (offset 0)
            return PeekOwnerId<Network::SessionId>(payload);

        case PacketId::Z2WZoneTransfer:
            // Z2WZoneTransfer.clientSessionId -- zoneId(uint32) 뒤라 offset 4다.
            // 구조체가 #pragma pack(1)이라 패딩이 없다는 것에 기대고 있다.
            return PeekOwnerId<Network::SessionId>(payload, sizeof(uint32_t));

        case PacketId::Z2WUnitOfWorkStream:
            // Task::UnitOfWork::Serialize가 스트림 맨 앞에 넣어둔 ownerId(= clientSessionId).
            // 그 앞에 Zone이 붙인 playerId(int64) + requestId(int64)가 있어 offset 16이다.
            return PeekOwnerId<uint64_t>(payload, sizeof(Common::PlayerId) + sizeof(Base::RUID));

        default:
            return std::nullopt;
        }
    }

    void ZoneLinkHandler::OnSessionOpened(const Network::Session::SPtr& session)
    {
        LOG.Info(ELogCategory::Zone, "Zone 연결 수락")
            .KV("SessionId", session->Id()).KV("Remote", session->RemoteAddress());
    }

    void ZoneLinkHandler::OnPacket(const Network::Session::SPtr& session,
                                   const Packet::Header& header,
                                   const std::span<const byte> payload)
    {
        // 여기는 이 연결의 I/O 스레드(Session의 strand)다. 라우팅에 필요한 정수 하나만 읽고
        // 바이트를 복사해 넘긴다.
        const auto packetId = static_cast<PacketId>(header.id);

        const auto ownerId = OwnerIdOf(packetId, payload);
        if (!ownerId)
        {
            LOG.Warning(ELogCategory::Zone, "ownerId를 읽을 수 없는 패킷, 버림")
                .KV("PacketId", header.id).KV("PayloadSize", payload.size());
            return;
        }

        std::vector<byte> payloadCopy(payload.begin(), payload.end());

        basicGroup_.Post(EProcessorId::Main, *ownerId,
            [this, session, packetId, payloadCopy = std::move(payloadCopy)]
            {
                mainProcessor_.DispatchFromZone(packetId, session, payloadCopy);
            });
    }

    void ZoneLinkHandler::OnClosed(const Network::Session::SPtr& session, const std::error_code& /*reason*/)
    {
        // 세션 종료 통지도 I/O 스레드에서 오므로, 여기서 레지스트리를 직접 건드리지 않고
        // BASIC 그룹으로 넘긴다. 주인은 끊긴 세션 자신이다 -- 등록/해제가 같은 스레드에서
        // 순서대로 처리되게 하려는 것이고, 레지스트리 자체는 Mutexed가 따로 지킨다.
        const auto sessionId = session->Id();
        basicGroup_.Post(EProcessorId::Main, sessionId, [this, sessionId]
        {
            mainProcessor_.RemoveZoneLink(sessionId);
        });
    }
}
