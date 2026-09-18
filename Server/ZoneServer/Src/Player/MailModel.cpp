#include "pch.h"
#include "Player/MailModel.h"

#include "Player/PlayerTask.h"

#include "Server/Core/Src/Task/UnitOfWork.h"
#include "Server/Common/Src/ContentLimit.h"

MailModel::MailModel(std::vector<Common::MailInfo> initial)
{
    for (auto& info : initial)
    {
        const auto mailId = info.mailId;
        mails_.insert_or_assign(mailId, std::move(info));
    }

    RecomputeNextExpireUt();
}

EErrorCode MailModel::AddMail(Common::MailInfo info, Task::UnitOfWork& unitOfWork)
{
    // **상한은 AddMail 에만 있다.** InsertMail(롤백/복원 경로)에 같은 검사를 넣으면
    // 상한에 걸린 상태에서 삭제를 되돌릴 때 "되돌려 넣을 자리가 없다"가 되어 롤백이
    // 실패한다 -- 롤백은 실패할 수 없다는 게 전제다(cpp-patterns.md).
    if (mails_.size() >= Common::kMaxMailCount)
    {
        return EErrorCode::MailBoxFull;
    }

    // 전역 유일이라 이미 있는 id가 나올 수 없다. 그래도 확인하는 건 **여기서 걸리면
    // 발급기가 고장 났다는 신호**이기 때문이다 -- 조용히 덮어쓰면 남의 우편이 사라진다.
    // 실패한 요청이 id를 하나 태우는 건 신경 쓰지 않는다(RUID는 ms당 4,096개다).
    const Common::MailId mailId{Base::Ruid::Create()};
    if (mails_.contains(mailId))
    {
        return EErrorCode::MailAlreadyExists;
    }

    info.mailId = mailId;

    mails_[mailId] = info;
    nextExpireUt_ = std::min(nextExpireUt_, info.endUt);

    // **상태를 바꾼 뒤에만 기록한다**(불변 규칙) -- 태스크 목록이 곧 "실제로 적용된 변경"이어야
    // 역순 롤백이 정확해진다. 추가의 Prev 는 "없었다"라서 빈 MailInfo 다.
    unitOfWork.AddTask<AddMailTask>(std::move(info), Common::MailInfo{});

    return EErrorCode::Success;
}

EErrorCode MailModel::InsertMail(Common::MailInfo info, Task::UnitOfWork& unitOfWork)
{
    const auto mailId = info.mailId;
    if (mails_.contains(mailId))
    {
        return EErrorCode::MailAlreadyExists;
    }

    mails_[mailId] = info;
    nextExpireUt_ = std::min(nextExpireUt_, info.endUt);
    unitOfWork.AddTask<AddMailTask>(std::move(info), Common::MailInfo{});

    return EErrorCode::Success;
}

EErrorCode MailModel::RemoveMail(const Common::MailId mailId, Task::UnitOfWork& unitOfWork, const bool /*isTimeout*/)
{
    const auto it = mails_.find(mailId);
    if (it == mails_.end())
    {
        return EErrorCode::MailNotFound;
    }

    // 지워진 원본을 통째로 실어둔다 -- 이게 없으면 삭제를 되돌릴 수 없다(제목/본문/기간이
    // 사라진 우편이 부활한다).
    // 지워진 원본이 Prev 다 -- 이게 없으면 삭제를 되돌릴 수 없다.
    auto removed = it->second;
    mails_.erase(it);

    // 지워진 것이 마침 가장 이른 만기였을 수 있다 -- 그러면 다음 만기를 다시 찾는다.
    if (removed.endUt <= nextExpireUt_)
    {
        RecomputeNextExpireUt();
    }
    unitOfWork.AddTask<RemoveMailTask>(Common::MailInfo{}, std::move(removed));

    return EErrorCode::Success;
}

std::vector<Common::MailId> MailModel::TakeExpiredMailIds(const int64_t nowUt) const
{
    std::vector<Common::MailId> expired;
    for (const auto& [mailId, info] : mails_)
    {
        if (info.endUt <= nowUt)
        {
            expired.push_back(mailId);
        }
    }
    return expired;
}


void MailModel::RecomputeNextExpireUt() noexcept
{
    nextExpireUt_ = kNoExpire;
    for (const auto& [mailId, info] : mails_)
    {
        nextExpireUt_ = std::min(nextExpireUt_, info.endUt);
    }
}
