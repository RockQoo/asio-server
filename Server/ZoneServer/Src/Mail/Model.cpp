#include "pch.h"
#include "Mail/Model.h"
#include "Mail/MailTask.h"

#include "Shared/Core/Src/Task/UnitOfWork.h"

namespace Mail
{
    Model::Model(std::vector<Info> initial)
    {
        for (auto& info : initial)
        {
            const auto mailId = info.mailId;
            mails_.insert_or_assign(mailId, std::move(info));
        }
    }

    void Model::BindSelf(const std::shared_ptr<Mutexed>& self)
    {
        self_ = self;
    }

    EErrorCode Model::AddMail(Info info, Task::UnitOfWork& unitOfWork)
    {
        // 전역 유일이라 이미 있는 id가 나올 수 없다. 그래도 확인하는 건 **여기서 걸리면
        // 발급기가 고장 났다는 신호**이기 때문이다 -- 조용히 덮어쓰면 남의 우편이 사라진다.
        // 실패한 요청이 id를 하나 태우는 건 신경 쓰지 않는다(RUID는 ms당 4,096개다).
        const Protocol::MailId mailId{Common::Ruid::Create()};
        if (mails_.contains(mailId))
        {
            return EErrorCode::MailAlreadyExists;
        }

        info.mailId = mailId;

        auto task = std::make_unique<AddMailTask>(LockSelf(), info);

        mails_[mailId] = std::move(info);

        // 상태를 바꾼 뒤에만 기록한다 -- 태스크 목록이 곧 "실제로 적용된 변경"이어야 역순
        // 롤백이 정확해진다.
        unitOfWork.AddTask(std::move(task));

        return EErrorCode::Success;
    }

    EErrorCode Model::InsertMail(Info info, Task::UnitOfWork& unitOfWork)
    {
        const auto mailId = info.mailId;
        if (mails_.contains(mailId))
        {
            return EErrorCode::MailAlreadyExists;
        }

        auto task = std::make_unique<AddMailTask>(LockSelf(), info);

        mails_[mailId] = std::move(info);
        unitOfWork.AddTask(std::move(task));

        return EErrorCode::Success;
    }

    EErrorCode Model::DelMail(const Protocol::MailId mailId, Task::UnitOfWork& unitOfWork, const bool /*isTimeout*/)
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

    std::vector<Protocol::MailId> Model::TakeExpiredMailIds(const int64_t nowUt) const
    {
        std::vector<Protocol::MailId> expired;
        for (const auto& [mailId, info] : mails_)
        {
            if (info.endUt <= nowUt)
            {
                expired.push_back(mailId);
            }
        }
        return expired;
    }


    std::shared_ptr<Model::Mutexed> Model::LockSelf() const
    {
        // BindSelf를 빼먹었으면 여기서 빈 핸들이 나가고, 그 태스크는 롤백 때 아무것도 못 한다.
        // 우편함을 만드는 곳이 한 군데(Registry::Add)뿐이라 그 자리만 지키면 된다.
        return self_.lock();
    }
}
