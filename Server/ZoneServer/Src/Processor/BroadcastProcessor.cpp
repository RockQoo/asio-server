#include "pch.h"
#include "Processor/BroadcastProcessor.h"
#include "Server/Core/Src/Network/SessionHolder.h"

#include "Server/Core/Src/Network/Session.h"
#include "Server/Core/Src/Packet/BinaryWriter.h"
#include "Server/Common/Src/Packet/RelayEnvelope.h"
#include "Server/Common/Src/PacketId.h"
#include "Server/Common/Src/Packet/Wire.h"

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
                Common::SendRelay(worldSession, PacketId::Z2WRelay, clientSessionId,
                                  innerPacketId, payload);
            }
        });
}
