#include "ZoneServer/Src/pch.h"
#include "ZoneServer/Src/Worker/BroadcastDispatcher.h"
#include "ZoneServer/Src/World/WorldLink.h"

#include "Core/Src/Network/Session.h"
#include "Core/Src/Packet/BinaryWriter.h"
#include "WorldServer/Src/Packet/RelayEnvelope.h"
#include "WorldServer/Src/Packet/ZoneLinkPacketId.h"

namespace Zone
{
    BroadcastDispatcher::BroadcastDispatcher(Thread::AffinityWorkerPool<TaskWorker>& broadcastPool, WorldLink& worldLink)
        : broadcastPool_(broadcastPool)
        , worldLink_(worldLink)
    {
    }

    void BroadcastDispatcher::Broadcast(const uint32_t zoneId, std::vector<Network::SessionId> targets,
                                        const uint16_t innerPacketId, std::vector<byte> payload)
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
                    header.innerPacketId = innerPacketId;

                    Packet::BinaryWriter writer;
                    writer.Write(header);
                    writer.WriteBytes(payload);
                    worldSession->SendPacket(static_cast<uint16_t>(World::ZoneLinkPacketId::ForwardToWorld), writer.GetBuffer());
                }
            });
    }
}
