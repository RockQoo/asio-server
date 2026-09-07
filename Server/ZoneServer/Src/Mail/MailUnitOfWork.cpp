#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Mail/MailUnitOfWork.h"
#include "Server/ZoneServer/Src/World/WorldLink.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Protocol/Src/PacketId.h"

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
            worldSession->SendPacket(PacketId::Z2WUnitOfWorkStream, writer.GetBuffer());
        });
    }
}
