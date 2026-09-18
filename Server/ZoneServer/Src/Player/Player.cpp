#include "pch.h"
#include "Player/Player.h"

#include "Player/PlayerTask.h"
#include "Player/PlayerStreamHandler.h"
#include "Task/ZoneUnitOfWork.h"

#include "Server/Common/Src/ErrorCode.h"
#include "Server/Common/Src/TaskKind.h"

Player::Player(Network::SessionHolder& worldLink, const Network::SessionId clientSessionId,
               const Common::PlayerId playerId, const Common::ZoneId zoneId,
               const float x, const float y,
               std::shared_ptr<MailModel::Mutexed> mailBox,
               const std::vector<Common::CurrencyInfo>& currencies)
    : Unit(Common::UnitId{playerId.Value()}, Unit::EType::Player, zoneId, x, y)
    , worldLink_(worldLink)
    , clientSessionId_(clientSessionId)
    , mailBox_(std::move(mailBox))
    , wallet_(currencies)
{
}

void Player::Handle(Zone& zone, const PacketId packetId, const std::span<const byte> payload)
{
    PlayerStreamHandler::Dispatch(PlayerContext{*this, zone}, packetId, payload);
}

void Player::Tick(const UnitTickContext& context)
{
    Unit::Tick(context);

    // 모델마다 "이번 틱에 할 일이 있나"를 먼저 묻는다. 하나도 없으면 UoW 를 열지 않는다 --
    // UoW 는 열 때마다 RUID 를 하나 태우는데, 할 일이 없는 틱이 압도적으로 많다.
    // **모델이 늘면 여기에 한 줄, 아래에 한 줄이다.**
    const bool mailExpired = mailBox_ != nullptr && (*mailBox_)->HasExpired(context.nowUt);
    if (!mailExpired)
    {
        return;
    }

    // 한 틱의 변경 전부가 한 트랜잭션이다. 클라이언트가 요청한 것이 아니라 서버가 스스로
    // 만든 변경이라 요청 패킷이 없는 쪽 생성자를 쓴다.
    ZoneUnitOfWork unitOfWork(this);

    if (mailExpired)
    {
        TickMail(context, unitOfWork);
    }
}

void Player::TickMail(const UnitTickContext& context, ZoneUnitOfWork& unitOfWork)
{
    // 조회(TakeExpiredMailIds)와 그 결과로 이어지는 삭제(RemoveMail) 사이에 BASIC 이
    // 끼어들면 "방금 있다고 한 우편이 없다"가 된다. 그래서 잠금을 두 호출에 걸쳐 유지하려고
    // 매번 새 프록시를 만드는 임시 Write() 대신 named 프록시를 쓴다.
    const auto writeProxy = mailBox_->Write();

    const auto expiredIds = writeProxy->TakeExpiredMailIds(context.nowUt);
    if (expiredIds.empty())
    {
        // HasExpired 를 통과했는데 비어 있다 -- 그 사이 BASIC 이 먼저 지웠다.
        return;
    }

    for (const auto mailId : expiredIds)
    {
        // 방금 TakeExpiredMailIds가 알려준 id라 실패할 일이 없다 -- 그래도 반환값을 버리지
        // 않는 이유는, 실패했다면 조회와 삭제 사이에 누가 끼어들었다는 뜻이고(불변식이
        // 깨졌다는 신호) 조용히 넘기면 원인을 찾을 수 없기 때문이다.
        if (const auto errorCode = writeProxy->RemoveMail(mailId, unitOfWork, true);
            errorCode != EErrorCode::Success)
        {
            unitOfWork.SetError(errorCode);
            return;
        }
    }
}

void Player::RollbackUoW(const ZoneUnitOfWork& unitOfWork) noexcept
{
    try
    {
        // 되돌리는 과정에서 쌓이는 태스크를 받아 버리는 통. 이게 없으면 모델의 정상 함수를
        // 재사용할 수 없다 -- 되돌리려고 부른 RemoveMail이 또 태스크를 남기기 때문이다.
        Task::RollbackUnitOfWork sink;

        const auto& tasks = unitOfWork.GetTasks();
        for (auto it = tasks.rbegin(); it != tasks.rend(); ++it)
        {
            const auto& task = **it;

            // **여기가 역연산을 아는 유일한 자리다.** 되돌리기는 "정상 함수를 반대로 한 번"
            // 부르는 것이고, 무엇을 넣을지는 Paired의 New/Prev가 알려준다.
            switch (static_cast<Common::ETaskType>(task.Kind()))
            {
            case Common::ETaskType::MailAdd:
                {
                    if (mailBox_ == nullptr)
                    {
                        break;
                    }
                    // 추가를 되돌리는 건 삭제다. New의 mailId를 지운다.
                    const auto& info = static_cast<const AddMailTask&>(task).Info().New();
                    if (const auto errorCode = mailBox_->Write()->RemoveMail(info.mailId, sink, false);
                        errorCode != EErrorCode::Success)
                    {
                        LOG.Error(ELogCategory::Zone, "우편 추가 롤백 실패 -- 메모리와 DB가 어긋난다")
                            .KV("MailId", info.mailId).KV("ErrorCode", static_cast<int32_t>(errorCode));
                    }
                }
                break;

            case Common::ETaskType::MailDel:
                {
                    if (mailBox_ == nullptr)
                    {
                        break;
                    }
                    // 삭제를 되돌리려면 mailId까지 그대로 복원해야 해서 AddMail(서버가 id를
                    // 새로 배정)이 아니라 InsertMail(id 지정)을 쓴다. Prev가 지워진 원본이다.
                    const auto& info = static_cast<const RemoveMailTask&>(task).Info().Prev();
                    if (const auto errorCode = mailBox_->Write()->InsertMail(info, sink);
                        errorCode != EErrorCode::Success)
                    {
                        LOG.Error(ELogCategory::Zone, "우편 삭제 롤백 실패 -- 지워진 우편이 되살아나지 못했다")
                            .KV("MailId", info.mailId).KV("ErrorCode", static_cast<int32_t>(errorCode));
                    }
                }
                break;

            case Common::ETaskType::CurrencyUpdate:
                {
                    // Prev를 그대로 되돌린다. 검증할 게 없어서(이전 값은 방금까지 유효했던
                    // 값이다) 실패할 수 없고, 그게 값 복원 방식을 고른 이유다.
                    const auto& currencyTask = static_cast<const CurrencyTask&>(task);
                    const auto prevValue = currencyTask.Value().Prev();
                    if (const auto errorCode = wallet_.SetCurrency(currencyTask.Type(), prevValue, sink);
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
        LOG.Error(ELogCategory::Zone, "롤백 중 예외 -- 메모리와 DB가 어긋난다")
            .KV("UnitId", GetUnitId()).KV("What", ex.what());
    }
    catch (...)
    {
        LOG.Error(ELogCategory::Zone, "롤백 중 알 수 없는 예외")
            .KV("UnitId", GetUnitId());
    }
}
