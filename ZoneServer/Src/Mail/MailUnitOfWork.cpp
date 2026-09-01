#include "ZoneServer/Src/pch.h"
#include "ZoneServer/Src/Mail/MailUnitOfWork.h"
#include "ZoneServer/Src/World/WorldLink.h"

#include "Core/Src/Network/Session.h"
#include "Core/Src/Packet/BinaryWriter.h"
#include "WorldServer/Src/Packet/ZoneLinkPacketId.h"

namespace Mail
{
    Task::UnitOfWork MakeMailUnitOfWork(Zone::WorldLink& worldLink, const Network::SessionId clientSessionId,
                                        const uint32_t playerId)
    {
        return Task::UnitOfWork(clientSessionId, [&worldLink, playerId](const std::span<const byte> serialized)
        {
            const auto worldSession = worldLink.Get();
            if (!worldSession)
            {
                return;
            }

            Packet::BinaryWriter writer;
            writer.Write(playerId);
            writer.WriteBytes(serialized);
            worldSession->SendPacket(static_cast<uint16_t>(World::ZoneLinkPacketId::UnitOfWorkStream), writer.GetBuffer());
        });
    }
}
