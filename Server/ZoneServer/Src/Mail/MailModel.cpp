#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Mail/MailModel.h"
#include "Server/ZoneServer/Src/Mail/MailTask.h"

#include "Shared/Core/Src/Task/UnitOfWork.h"

#include <memory>
#include <utility>

namespace Mail
{
    void MailModel::BindSelf(const std::shared_ptr<Mutexed>& self)
    {
        self_ = self;
    }

    EErrorCode MailModel::AddMail(MailInfo info, Task::UnitOfWork& unitOfWork)
    {
        // id를 먼저 소비하지 않고 후보만 본다 -- 실패로 끝나는 요청이 id를 하나씩 태우면
        // 롤백해도 그 구멍은 되돌아오지 않는다.
        const auto mailId = nextMailId_;
        if (mails_.contains(mailId))
        {
            return EErrorCode::MailAlreadyExists;
        }

        info.mailId = mailId;

        auto task = std::make_unique<AddMailTask>(LockSelf(), info);

        ++nextMailId_;
        mails_[mailId] = std::move(info);

        // 상태를 바꾼 뒤에만 기록한다 -- 태스크 목록이 곧 "실제로 적용된 변경"이어야 역순
        // 롤백이 정확해진다.
        unitOfWork.AddTask(std::move(task));

        return EErrorCode::Success;
    }

    EErrorCode MailModel::InsertMail(MailInfo info, Task::UnitOfWork& unitOfWork)
    {
        const auto mailId = info.mailId;
        if (mails_.contains(mailId))
        {
            return EErrorCode::MailAlreadyExists;
        }

        auto task = std::make_unique<AddMailTask>(LockSelf(), info);

        // nextMailId_를 되살린 id 뒤로 밀어둔다 -- 롤백으로 되살아난 우편의 id를 나중에 AddMail이
        // 다시 배정해버리면 같은 id가 두 번 존재하게 된다.
        if (mailId >= nextMailId_)
        {
            nextMailId_ = mailId + 1;
        }

        mails_[mailId] = std::move(info);
        unitOfWork.AddTask(std::move(task));

        return EErrorCode::Success;
    }

    EErrorCode MailModel::DelMail(const uint32_t mailId, Task::UnitOfWork& unitOfWork, const bool /*isTimeout*/)
    {
        const auto it = mails_.find(mailId);
        if (it == mails_.end())
        {
            return EErrorCode::MailNotFound;
        }

        // 지워진 원본을 통째로 실어둔다 -- 이게 없으면 삭제를 되돌릴 수 없다(제목/본문/기간이
        // 사라진 우편이 부활한다).
        auto task = std::make_unique<DelMailTask>(LockSelf(), it->second);

        mails_.erase(it);
        unitOfWork.AddTask(std::move(task));

        return EErrorCode::Success;
    }

    std::vector<uint32_t> MailModel::TakeExpiredMailIds(const int64_t nowUt) const
    {
        std::vector<uint32_t> expired;
        for (const auto& [mailId, info] : mails_)
        {
            if (info.endUt <= nowUt)
            {
                expired.push_back(mailId);
            }
        }
        return expired;
    }

    std::shared_ptr<MailModel::Mutexed> MailModel::LockSelf() const
    {
        // BindSelf를 빼먹었으면 여기서 빈 핸들이 나가고, 그 태스크는 롤백 때 아무것도 못 한다.
        // 우편함을 만드는 곳이 한 군데(MailRegistry::Add)뿐이라 그 자리만 지키면 된다.
        return self_.lock();
    }
}
