#pragma once

#include "Shared/Common/Src/MailInfo.h"
#include "Shared/Common/Src/TaskKind.h"

#include "Shared/Core/Src/Task/ITask.h"
#include "Shared/Core/Src/Task/Paired.h"

namespace Mail
{
    // 우편 추가 기록. **데이터만 갖는다** -- 직렬화도 롤백도 ZoneUnitOfWork가 taskKind로
    // 분기해서 한다(ITask 주석 참고).
    //
    // 우편은 통째로 들고 있는다. 삭제를 되돌릴 때 제목/본문/기간까지 그대로 복원해야 하고,
    // 클라이언트도 같은 값을 받아 자기 우편함에 적용하기 때문이다.
    //
    // **추가의 Prev는 빈 MailInfo다** -- "그 우편이 없었다"가 직전 상태다.
    class AddMailTask final : public Task::ITask
    {
    public:
        void Set(Common::MailInfo newInfo, Common::MailInfo prevInfo)
        {
            info_.Set(std::move(newInfo), std::move(prevInfo));
        }

        [[nodiscard]] uint16_t Kind() const noexcept override
        {
            return static_cast<uint16_t>(Common::ETaskType::MailAdd);
        }

        [[nodiscard]] const Task::Paired<Common::MailInfo>& Info() const noexcept { return info_; }

    private:
        Task::Paired<Common::MailInfo> info_;
    };

    // 우편 삭제 기록. **삭제의 New가 빈 MailInfo이고 Prev가 지워진 원본이다** -- 되돌리기는
    // Prev를 그대로 다시 넣는 것이고, mailId까지 같아야 하므로 원본 전체가 필요하다.
    class DelMailTask final : public Task::ITask
    {
    public:
        void Set(Common::MailInfo newInfo, Common::MailInfo prevInfo)
        {
            info_.Set(std::move(newInfo), std::move(prevInfo));
        }

        [[nodiscard]] uint16_t Kind() const noexcept override
        {
            return static_cast<uint16_t>(Common::ETaskType::MailDel);
        }

        [[nodiscard]] const Task::Paired<Common::MailInfo>& Info() const noexcept { return info_; }

    private:
        Task::Paired<Common::MailInfo> info_;
    };
}
