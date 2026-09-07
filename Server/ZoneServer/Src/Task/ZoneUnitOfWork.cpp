#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Task/ZoneUnitOfWork.h"
#include "Server/ZoneServer/Src/Mail/MailModel.h"
#include "Server/ZoneServer/Src/Mail/MailRegistry.h"
#include "Server/ZoneServer/Src/World/WorldLink.h"
#include "Server/WorldServer/Src/Packet/RelayEnvelope.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryReader.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Protocol/Src/ErrorCode.h"

#include <string>
#include <utility>

namespace Zone
{
    ZoneUnitOfWork::ZoneUnitOfWork(WorldLink& worldLink, Mail::MailRegistry& mailRegistry,
                                   const Network::SessionId clientSessionId, const uint32_t playerId,
                                   const PacketId requestPacketId)
        : Task::UnitOfWork(clientSessionId)
        , worldLink_(worldLink)
        , mailRegistry_(mailRegistry)
        , clientSessionId_(clientSessionId)
        , playerId_(playerId)
        , requestPacketId_(static_cast<uint16_t>(requestPacketId))
    {
    }

    ZoneUnitOfWork::ZoneUnitOfWork(WorldLink& worldLink, Mail::MailRegistry& mailRegistry,
                                   const Network::SessionId clientSessionId, const uint32_t playerId)
        : Task::UnitOfWork(clientSessionId)
        , worldLink_(worldLink)
        , mailRegistry_(mailRegistry)
        , clientSessionId_(clientSessionId)
        , playerId_(playerId)
        , requestPacketId_(0)
    {
    }

    void ZoneUnitOfWork::OnFlush(const Task::ETaskTarget target, const std::span<const byte> stream)
    {
        if (target == Task::ETaskTarget::Db)
        {
            const auto worldSession = worldLink_.Get();
            if (!worldSession)
            {
                return;
            }

            Packet::BinaryWriter writer;
            writer.Write(playerId_);
            writer.WriteBytes(stream);
            worldSession->SendPacket(PacketId::Z2WUnitOfWorkStream, writer.GetBuffer());
            return;
        }

        SendTaskResult(static_cast<int32_t>(EErrorCode::Success), stream);
    }

    void ZoneUnitOfWork::OnRollback(const uint16_t taskKind, const std::span<const byte> payload)
    {
        switch (Protocol::CategoryOf(taskKind))
        {
        case Protocol::ETaskCategory::Mail:
            RollbackMailTask(static_cast<Protocol::EMailTask>(Protocol::SubTaskOf(taskKind)), payload);
            break;
        default:
            // 역연산을 모르는 태스크를 만나면 메모리와 DB가 어긋난 채로 남는다 -- 새 콘텐츠
            // 태스크를 추가하면서 이 switch를 빼먹었다는 신호라 반드시 눈에 띄어야 한다.
            LOG.Error(ELogCategory::Zone, "역연산이 등록되지 않은 UnitOfWork 태스크")
                .KV("ClientSessionId", clientSessionId_).KV("TaskKind", taskKind);
            break;
        }
    }

    void ZoneUnitOfWork::OnFailed(const int32_t errorCode)
    {
        // 되돌렸으므로 클라이언트가 적용할 태스크는 없다 -- 에러 코드만 알려준다.
        SendTaskResult(errorCode, {});

        LOG.Debug(ELogCategory::Zone, "UnitOfWork 실패로 롤백")
            .KV("ClientSessionId", clientSessionId_).KV("RequestPacketId", requestPacketId_)
            .KV("ErrorCode", errorCode);
    }

    void ZoneUnitOfWork::RollbackMailTask(const Protocol::EMailTask subTask,
                                           const std::span<const byte> payload) const
    {
        const auto mailModel = mailRegistry_.Find(clientSessionId_);
        if (!mailModel)
        {
            return;
        }

        // 태스크 페이로드가 곧 역연산에 필요한 정보다(추가는 mailId만, 삭제는 지워진 원본
        // 전체) -- MailModel이 삭제 태스크에 원본을 통째로 싣는 이유가 이것이다.
        Packet::BinaryReader reader(payload);
        Mail::MailInfo info{};
        if (!reader.Read(info.mailId) || !reader.ReadString(info.title) || !reader.ReadString(info.body)
            || !reader.Read(info.sendUt) || !reader.Read(info.endUt))
        {
            return;
        }

        switch (subTask)
        {
        case Protocol::EMailTask::Added:
            mailModel->Write()->UndoAdd(info.mailId);
            break;
        case Protocol::EMailTask::Removed:
            mailModel->Write()->UndoRemove(std::move(info));
            break;
        default:
            LOG.Error(ELogCategory::Zone, "역연산이 등록되지 않은 Mail 태스크")
                .KV("ClientSessionId", clientSessionId_).KV("SubTask", static_cast<uint16_t>(subTask));
            break;
        }
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
