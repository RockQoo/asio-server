#include "pch.h"
#include "Task/ZoneUnitOfWork.h"

#include "Player/PlayerTask.h"

#include "Server/Core/Src/Base/RUID.h"
#include "Server/Core/Src/Network/Session.h"
#include "Server/Core/Src/Network/SessionHolder.h"
#include "Server/Core/Src/Packet/BinaryWriter.h"
#include "Server/Common/Src/ErrorCode.h"
#include "Server/Common/Src/Packet/RelayEnvelope.h"
#include "Server/Common/Src/Packet/Wire.h"
#include "Server/Common/Src/Packet/ZonePackets.h"
#include "Server/Common/Src/TaskKind.h"

namespace
{
    // 추가/삭제 태스크가 같은 포맷(원본 전체)을 쓴다 -- 삭제 태스크에 지워진 내용이 통째로
    // 들어 있어야 롤백(= 되살리기)과 클라이언트 적용이 가능하기 때문이다.
    void WriteMail(Packet::BinaryWriter& binaryWriter, const Common::MailInfo& info)
    {
        binaryWriter.Write(info.mailId);
        binaryWriter.WriteString(info.title);
        binaryWriter.WriteString(info.body);
        binaryWriter.Write(info.sendUt);
        binaryWriter.Write(info.endUt);
    }
}

ZoneUnitOfWork::ZoneUnitOfWork(Unit* const ownerUnit)
    : Task::UnitOfWork(static_cast<uint64_t>(ownerUnit->GetUnitId().Value()), Base::Ruid::Create())
    , requestPacketId_(0)
    , ownerUnit_(ownerUnit)
{
}

ZoneUnitOfWork::~ZoneUnitOfWork() noexcept
{
    try
    {
        if (HasError())
        {
            // **되돌리는 방법은 주인만 안다.** 이 클래스는 모델 목록을 들고 있지 않는다.
            ownerUnit_->RollbackUoW(*this);
            ClearTasks();

            // 되돌렸으므로 클라이언트가 적용할 태스크는 없다 -- 에러 코드만 알려준다.
            SendTaskResult(GetError(), {});

            LOG.Debug(ELogCategory::Zone, "ZoneUnitOfWork 실패로 롤백")
                .KV("UnitId", ownerUnit_->GetUnitId()).KV("RequestPacketId", requestPacketId_)
                .KV("ErrorCode", GetError());
            return;
        }

        if (IsEmpty())
        {
            // 상태를 하나도 바꾸지 않은 요청(조회만, 또는 이번 틱에 만료된 우편이 없음) --
            // 보낼 것이 없다. 에러가 아니므로 클라이언트에도 알릴 것이 없다.
            return;
        }

        // 성공 경로에서 딱 한 번만 직렬화하고, 그 바이트를 World와 클라이언트가 공유한다.
        const auto stream = Serialize();
        SendToWorld(stream);
        SendTaskResult(static_cast<int32_t>(EErrorCode::Success), stream);
    }
    catch (const std::exception& ex)
    {
        LOG.Error(ELogCategory::Zone, "ZoneUnitOfWork 커밋 중 예외")
            .KV("UnitId", ownerUnit_->GetUnitId()).KV("What", ex.what());
    }
    catch (...)
    {
        LOG.Error(ELogCategory::Zone, "ZoneUnitOfWork 커밋 중 알 수 없는 예외")
            .KV("UnitId", ownerUnit_->GetUnitId());
    }
}

std::vector<byte> ZoneUnitOfWork::Serialize() const
{
    Packet::BinaryWriter binaryWriter;
    binaryWriter.Write(GetOwnerId());
    binaryWriter.Write(static_cast<uint16_t>(Tasks().size()));

    for (const auto& task : Tasks())
    {
        // 태스크마다 따로 직렬화한 뒤 그 길이를 앞에 붙인다 -- 길이를 미리 알 수 없어서
        // 한 버퍼에 이어 쓸 수 없다(BinaryWriter는 되돌아가 덮어쓰지 않는다).
        Packet::BinaryWriter taskBinaryWriter;

        // **여기가 태스크의 뜻을 아는 유일한 자리다.** 태스크는 값만 들고 있고, 어떤 값을
        // 어떤 순서로 쓰는지는 이 분기가 정한다. 새 태스크를 추가하면 case를 하나 더 쓴다.
        switch (static_cast<Common::ETaskType>(task->Kind()))
        {
        case Common::ETaskType::MailAdd:
            // 추가는 New(들어온 우편)를 내보낸다.
            WriteMail(taskBinaryWriter, static_cast<const AddMailTask&>(*task).Info().New());
            break;

        case Common::ETaskType::MailDel:
            // 삭제는 Prev(지워진 우편)를 내보낸다 -- 받는 쪽이 어느 우편이 사라졌는지
            // 알아야 하고, 클라이언트는 그 mailId를 자기 목록에서 지운다.
            WriteMail(taskBinaryWriter, static_cast<const RemoveMailTask&>(*task).Info().Prev());
            break;

        case Common::ETaskType::CurrencyUpdate:
            {
                const auto& currencyTask = static_cast<const CurrencyTask&>(*task);
                taskBinaryWriter.Write(static_cast<uint8_t>(currencyTask.Type()));
                taskBinaryWriter.Write(currencyTask.Value().New());
                taskBinaryWriter.Write(currencyTask.Value().Prev());
            }
            break;

        case Common::ETaskType::None:
        default:
            LOG.Error(ELogCategory::Zone, "직렬화할 줄 모르는 태스크 -- 빈 본문으로 나간다")
                .KV("TaskKind", task->Kind());
            break;
        }

        const auto& payload = taskBinaryWriter.GetBuffer();
        binaryWriter.Write(task->Kind());
        binaryWriter.Write(static_cast<uint32_t>(payload.size()));
        binaryWriter.WriteBytes(payload);
    }

    const auto& buffer = binaryWriter.GetBuffer();
    return std::vector<byte>(buffer.begin(), buffer.end());
}
void ZoneUnitOfWork::SendToWorld(const std::span<const byte> stream) const
{
    auto* const worldLink = ownerUnit_->GetWorldLink();
    if (worldLink == nullptr)
    {
        return;
    }

    const auto worldSession = worldLink->Get();
    if (!worldSession)
    {
        return;
    }

    Packet::BinaryWriter binaryWriter;
    binaryWriter.Write(ownerUnit_->GetUnitId());
    binaryWriter.Write(GetRequestId());
    binaryWriter.WriteBytes(stream);
    worldSession->SendPacket(PacketId::Z2WUnitOfWorkStream, binaryWriter.GetBuffer());
}

void ZoneUnitOfWork::SendTaskResult(const int32_t errorCode, const std::span<const byte> stream) const
{
    const auto clientSessionId = ownerUnit_->GetClientSessionId();
    auto* const worldLink = ownerUnit_->GetWorldLink();
    if (worldLink == nullptr)
    {
        // 주인이 없는 유닛(몬스터)의 변경은 돌려줄 클라이언트가 없다.
        return;
    }

    const auto worldSession = worldLink->Get();
    if (!worldSession)
    {
        return;
    }

    Common::Z2CTaskResult packet;
    packet.errorCode = errorCode;
    packet.requestPacketId = requestPacketId_;
    packet.requestId = GetRequestId();
    packet.taskStream = stream;

    // Zone은 클라이언트와 직접 연결되지 않으므로 World를 거치는 봉투에 담아 보낸다.
    Common::SendRelay(worldSession, PacketId::Z2WRelay, clientSessionId, packet);
}
