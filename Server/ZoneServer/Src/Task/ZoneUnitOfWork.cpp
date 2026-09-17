#include "pch.h"
#include "Task/ZoneUnitOfWork.h"
#include "Player/PlayerTask.h"
#include "Player/Player.h"
#include "Player/PlayerTask.h"
#include "Shared/Core/Src/Network/SessionHolder.h"
#include "Shared/Common/Src/Packet/RelayEnvelope.h"

#include "Shared/Core/Src/Base/RUID.h"
#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Common/Src/ErrorCode.h"
#include "Shared/Common/Src/TaskKind.h"
#include "Shared/Common/Src/Packet/Wire.h"

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

ZoneUnitOfWork::ZoneUnitOfWork(Network::SessionHolder& worldLink, Player& player, const PacketId requestPacketId)
    : Task::UnitOfWork(player.GetSessionId(), Base::Ruid::Create())
    , worldLink_(worldLink)
    , clientSessionId_(player.GetSessionId())
    , playerId_(player.GetPlayerId())
    , requestPacketId_(static_cast<uint16_t>(requestPacketId))
    , models_{player.GetMailBox(), &player.GetWallet()}
{
}

ZoneUnitOfWork::ZoneUnitOfWork(Network::SessionHolder& worldLink, const Network::SessionId clientSessionId,
                       const Common::PlayerId playerId, Models models)
    : Task::UnitOfWork(clientSessionId, Base::Ruid::Create())
    , worldLink_(worldLink)
    , clientSessionId_(clientSessionId)
    , playerId_(playerId)
    , requestPacketId_(0)
    , models_(std::move(models))
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

            LOG.Debug(ELogCategory::Zone, "ZoneUnitOfWork 실패로 롤백")
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
        LOG.Error(ELogCategory::Zone, "ZoneUnitOfWork 커밋 중 예외")
            .KV("ClientSessionId", clientSessionId_).KV("What", ex.what());
    }
    catch (...)
    {
        LOG.Error(ELogCategory::Zone, "ZoneUnitOfWork 커밋 중 알 수 없는 예외")
            .KV("ClientSessionId", clientSessionId_);
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

void ZoneUnitOfWork::RollbackAll() noexcept
{
    try
    {
        // 되돌리는 과정에서 쌓이는 태스크를 받아 버리는 통. 이게 없으면 모델의 정상 함수를
        // 재사용할 수 없다 -- 되돌리려고 부른 DelMail이 또 태스크를 남기기 때문이다.
        Task::RollbackUnitOfWork sink;

        for (auto it = Tasks().rbegin(); it != Tasks().rend(); ++it)
        {
            const auto& task = **it;

            // **여기가 역연산을 아는 유일한 자리다.** 되돌리기는 "정상 함수를 반대로 한 번"
            // 부르는 것이고, 무엇을 넣을지는 Paired의 New/Prev가 알려준다.
            switch (static_cast<Common::ETaskType>(task.Kind()))
            {
            case Common::ETaskType::MailAdd:
                {
                    if (!models_.mailBox)
                    {
                        break;
                    }
                    // 추가를 되돌리는 건 삭제다. New의 mailId를 지운다.
                    const auto& info = static_cast<const AddMailTask&>(task).Info().New();
                    if (const auto errorCode = models_.mailBox->Write()->RemoveMail(info.mailId, sink, false);
                        errorCode != EErrorCode::Success)
                    {
                        LOG.Error(ELogCategory::Zone, "우편 추가 롤백 실패 -- 메모리와 DB가 어긋난다")
                            .KV("MailId", info.mailId).KV("ErrorCode", static_cast<int32_t>(errorCode));
                    }
                }
                break;

            case Common::ETaskType::MailDel:
                {
                    if (!models_.mailBox)
                    {
                        break;
                    }
                    // 삭제를 되돌리려면 mailId까지 그대로 복원해야 해서 AddMail(서버가 id를
                    // 새로 배정)이 아니라 InsertMail(id 지정)을 쓴다. Prev가 지워진 원본이다.
                    const auto& info = static_cast<const RemoveMailTask&>(task).Info().Prev();
                    if (const auto errorCode = models_.mailBox->Write()->InsertMail(info, sink);
                        errorCode != EErrorCode::Success)
                    {
                        LOG.Error(ELogCategory::Zone, "우편 삭제 롤백 실패 -- 지워진 우편이 되살아나지 못했다")
                            .KV("MailId", info.mailId).KV("ErrorCode", static_cast<int32_t>(errorCode));
                    }
                }
                break;

            case Common::ETaskType::CurrencyUpdate:
                {
                    if (models_.wallet == nullptr)
                    {
                        break;
                    }
                    // Prev를 그대로 되돌린다. 검증할 게 없어서(이전 값은 방금까지 유효했던
                    // 값이다) 실패할 수 없고, 그게 값 복원 방식을 고른 이유다.
                    const auto& currencyTask = static_cast<const CurrencyTask&>(task);
                    const auto prevValue = currencyTask.Value().Prev();
                    if (const auto errorCode = models_.wallet->SetCurrency(currencyTask.Type(), prevValue, sink);
                        errorCode != EErrorCode::Success)
                    {
                        LOG.Error(ELogCategory::Zone, "재화 롤백 실패 -- 메모리와 DB가 어긋난다")
                            .KV("CurrencyType", static_cast<uint32_t>(currencyTask.Type()))
                            .KV("PrevValue", prevValue).KV("ErrorCode", static_cast<int32_t>(errorCode));
                    }
                }
                break;

            case Common::ETaskType::None:
            default:
                LOG.Error(ELogCategory::Zone, "되돌릴 줄 모르는 태스크 -- 메모리와 DB가 어긋난다")
                    .KV("TaskKind", task.Kind());
                break;
            }
        }
    }
    catch (const std::exception& ex)
    {
        // 롤백은 조금 전에 성공한 변경을 되돌리는 것뿐이라 실패할 수 없다는 전제다. 여기
        // 걸렸다면 그 전제가 깨진 것이고(모델이 롤백 경로에 새 검증을 넣었다는 뜻),
        // 복구를 시도해도 더 나빠지기만 하므로 드러내기만 한다.
        LOG.Error(ELogCategory::Zone, "ZoneUnitOfWork 롤백 중 예외 -- 메모리와 DB가 어긋난다")
            .KV("ClientSessionId", clientSessionId_).KV("What", ex.what());
    }
    catch (...)
    {
        LOG.Error(ELogCategory::Zone, "ZoneUnitOfWork 롤백 중 알 수 없는 예외")
            .KV("ClientSessionId", clientSessionId_);
    }

    ClearTasks();
}

void ZoneUnitOfWork::SendToWorld(const std::span<const byte> stream) const
{
    const auto worldSession = worldLink_.Get();
    if (!worldSession)
    {
        return;
    }

    Packet::BinaryWriter binaryWriter;
    binaryWriter.Write(playerId_);
    binaryWriter.Write(GetRequestId());
    binaryWriter.WriteBytes(stream);
    worldSession->SendPacket(PacketId::Z2WUnitOfWorkStream, binaryWriter.GetBuffer());
}

void ZoneUnitOfWork::SendTaskResult(const int32_t errorCode, const std::span<const byte> stream) const
{
    const auto worldSession = worldLink_.Get();
    if (!worldSession)
    {
        return;
    }

    Packet::BinaryWriter innerBinaryWriter;
    innerBinaryWriter.Write(errorCode);
    innerBinaryWriter.Write(requestPacketId_);

    // requestId를 태스크 스트림 밖에 두는 이유: 실패하면 스트림이 비어서 안에 넣으면
    // 클라이언트가 실패한 요청을 짝지을 수 없다. 성공/실패 어느 쪽이든 여기 실린다.
    innerBinaryWriter.Write(GetRequestId());
    innerBinaryWriter.WriteBytes(stream);

    // Zone은 클라이언트와 직접 연결되지 않으므로 World를 거치는 봉투에 담아 보낸다
    // (ZoneProcessor::SendToPlayer와 같은 경로).
    Common::SendRelay(worldSession, PacketId::Z2WRelay, clientSessionId_,
                      PacketId::Z2CTaskResult, innerBinaryWriter.GetBuffer());
}
