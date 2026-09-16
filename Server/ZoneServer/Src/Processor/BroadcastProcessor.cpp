#include "pch.h"
#include "Processor/BroadcastProcessor.h"
#include "World/WorldLink.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Common/Src/Packet/RelayEnvelope.h"
#include "Shared/Common/Src/PacketId.h"

namespace Zone
{
    BroadcastProcessor::BroadcastProcessor(Processor::Group<EProcessorId>& broadcastGroup,
                                             WorldLink& worldLink)
        : broadcastGroup_(broadcastGroup)
        , worldLink_(worldLink)
    {
    }

    void BroadcastProcessor::Broadcast(const Common::ZoneId zoneId, std::vector<Network::SessionId> targets,
                                        const PacketId innerPacketId, std::vector<byte> payload)
    {
        broadcastGroup_.Post(EProcessorId::Broadcast, zoneId.Value(),
            [&worldLink = worldLink_, targets = std::move(targets), innerPacketId, payload = std::move(payload)]
            {
                const auto worldSession = worldLink.Get();
                if (!worldSession)
                {
                    return;
                }

                for (const auto clientSessionId : targets)
                {
                    Common::RelayEnvelope header{};
                    header.clientSessionId = clientSessionId;
                    header.innerPacketId = static_cast<uint16_t>(innerPacketId);

                    Packet::BinaryWriter binaryWriter;
                    binaryWriter.Write(header);
                    binaryWriter.WriteBytes(payload);
                    worldSession->SendPacket(PacketId::Z2WRelay, binaryWriter.GetBuffer());
                }
            });
    }
}
