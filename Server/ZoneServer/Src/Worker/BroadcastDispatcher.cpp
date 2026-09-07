#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Worker/BroadcastDispatcher.h"
#include "Server/ZoneServer/Src/World/WorldLink.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Server/WorldServer/Src/Packet/RelayEnvelope.h"
#include "Shared/Protocol/Src/PacketId.h"

namespace Zone
{
    BroadcastDispatcher::BroadcastDispatcher(Thread::AffinityWorkerPool<TaskWorker>& broadcastPool, WorldLink& worldLink)
        : broadcastPool_(broadcastPool)
        , worldLink_(worldLink)
    {
    }

    void BroadcastDispatcher::Broadcast(const uint32_t zoneId, std::vector<Network::SessionId> targets,
                                        const Protocol::PacketId innerPacketId, std::vector<byte> payload)
    {
        broadcastPool_.GetWorker(zoneId).PostTask(
            [&worldLink = worldLink_, targets = std::move(targets), innerPacketId, payload = std::move(payload)]
            {
                const auto worldSession = worldLink.Get();
                if (!worldSession)
                {
                    return;
                }

                for (const auto clientSessionId : targets)
                {
                    World::ClientEnvelopeHeader header{};
                    header.clientSessionId = clientSessionId;
                    header.innerPacketId = static_cast<uint16_t>(innerPacketId);

                    Packet::BinaryWriter writer;
                    writer.Write(header);
                    writer.WriteBytes(payload);
                    worldSession->SendPacket(Protocol::PacketId::Z2WRelay, writer.GetBuffer());
                }
            });
    }
}
