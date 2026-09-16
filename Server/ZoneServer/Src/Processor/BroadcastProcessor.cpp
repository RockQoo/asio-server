#include "pch.h"
#include "Processor/BroadcastProcessor.h"
#include "Shared/Core/Src/Network/SessionHolder.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Common/Src/Packet/RelayEnvelope.h"
#include "Shared/Common/Src/PacketId.h"

BroadcastProcessor::BroadcastProcessor(Processor::Group<EZoneProcessorId>& broadcastGroup,
                                         Network::SessionHolder& worldLink)
    : broadcastGroup_(broadcastGroup)
    , worldLink_(worldLink)
{
}

void BroadcastProcessor::Broadcast(const Common::ZoneId zoneId, std::vector<Network::SessionId> targets,
                                    const PacketId innerPacketId, std::vector<byte> payload)
{
    broadcastGroup_.Post(EZoneProcessorId::Broadcast, zoneId.Value(),
        [&worldLink = worldLink_, targets = std::move(targets), innerPacketId, payload = std::move(payload)]
        {
            const auto worldSession = worldLink.Get();
            if (!worldSession)
            {
                return;
            }

            for (const auto clientSessionId : targets)
            {
                worldSession->SendPacket(
                    PacketId::Z2WRelay,
                    Common::WrapRelay(clientSessionId, static_cast<uint16_t>(innerPacketId), payload));
            }
        });
}
