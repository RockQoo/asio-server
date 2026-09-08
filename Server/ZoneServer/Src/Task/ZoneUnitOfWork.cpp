#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Task/ZoneUnitOfWork.h"
#include "Server/ZoneServer/Src/World/WorldLink.h"
#include "Server/WorldServer/Src/Packet/RelayEnvelope.h"

#include "Shared/Core/Src/Common/RequestId.h"
#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Protocol/Src/ErrorCode.h"

#include <exception>

namespace Zone
{
    ZoneUnitOfWork::ZoneUnitOfWork(WorldLink& worldLink, const Network::SessionId clientSessionId,
                                   const uint32_t playerId, const PacketId requestPacketId)
        : Task::UnitOfWork(clientSessionId, Common::RequestIdGenerator::Instance().Next())
        , worldLink_(worldLink)
        , clientSessionId_(clientSessionId)
        , playerId_(playerId)
        , requestPacketId_(static_cast<uint16_t>(requestPacketId))
    {
    }

    ZoneUnitOfWork::ZoneUnitOfWork(WorldLink& worldLink, const Network::SessionId clientSessionId,
                                   const uint32_t playerId)
        : Task::UnitOfWork(clientSessionId, Common::RequestIdGenerator::Instance().Next())
        , worldLink_(worldLink)
        , clientSessionId_(clientSessionId)
        , playerId_(playerId)
        , requestPacketId_(0)
    {
    }

    ZoneUnitOfWork::~ZoneUnitOfWork() noexcept
    {
        try
        {
            if (HasError())
            {
                RollbackAll();

                // 되돌렸으므로 클라이언트가 적용할 태스크는 없다 -- 에러 코드만 알려준다.
                SendTaskResult(GetError(), {});

                LOG.Debug(ELogCategory::Zone, "UnitOfWork 실패로 롤백")
                    .KV("ClientSessionId", clientSessionId_).KV("RequestPacketId", requestPacketId_)
                    .KV("ErrorCode", GetError());
                return;
            }

            if (IsEmpty())
            {
                // 상태를 하나도 바꾸지 않은 요청(조회만, 또는 만료 대상이 없는 스윕) -- 보낼
                // 것이 없다. 에러가 아니므로 클라이언트에도 알릴 것이 없다.
                return;
            }

            // 성공 경로에서 딱 한 번만 직렬화하고, 그 바이트를 World와 클라이언트가 공유한다.
            const auto stream = Serialize();
            SendToWorld(stream);
            SendTaskResult(static_cast<int32_t>(EErrorCode::Success), stream);
        }
        catch (const std::exception& ex)
        {
            LOG.Error(ELogCategory::Zone, "UnitOfWork 커밋 중 예외")
                .KV("ClientSessionId", clientSessionId_).KV("What", ex.what());
        }
        catch (...)
        {
            LOG.Error(ELogCategory::Zone, "UnitOfWork 커밋 중 알 수 없는 예외")
                .KV("ClientSessionId", clientSessionId_);
        }
    }

    void ZoneUnitOfWork::SendToWorld(const std::span<const byte> stream) const
    {
        const auto worldSession = worldLink_.Get();
        if (!worldSession)
        {
            return;
        }

        Packet::BinaryWriter writer;
        writer.Write(playerId_);
        writer.Write(GetRequestId());
        writer.WriteBytes(stream);
        worldSession->SendPacket(PacketId::Z2WUnitOfWorkStream, writer.GetBuffer());
    }

    void ZoneUnitOfWork::SendTaskResult(const int32_t errorCode, const std::span<const byte> stream) const
    {
        const auto worldSession = worldLink_.Get();
        if (!worldSession)
        {
            return;
        }

        Packet::BinaryWriter inner;
        inner.Write(errorCode);
        inner.Write(requestPacketId_);

        // requestId를 태스크 스트림 밖에 두는 이유: 실패하면 스트림이 비어서 안에 넣으면
        // 클라이언트가 실패한 요청을 짝지을 수 없다. 성공/실패 어느 쪽이든 여기 실린다.
        inner.Write(GetRequestId());
        inner.WriteBytes(stream);

        // Zone은 클라이언트와 직접 연결되지 않으므로 World를 거치는 봉투에 담아 보낸다
        // (ZoneInstance::SendToPlayer와 같은 경로).
        World::ClientEnvelopeHeader header{};
        header.clientSessionId = clientSessionId_;
        header.innerPacketId = static_cast<uint16_t>(PacketId::Z2CTaskResult);

        Packet::BinaryWriter writer;
        writer.Write(header);
        writer.WriteBytes(inner.GetBuffer());
        worldSession->SendPacket(PacketId::Z2WRelay, writer.GetBuffer());
    }
}
