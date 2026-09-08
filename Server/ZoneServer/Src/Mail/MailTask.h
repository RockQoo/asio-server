#pragma once

#include "Server/ZoneServer/Src/Mail/MailModel.h"

#include "Shared/Core/Src/Task/ITask.h"

#include <cstdint>
#include <memory>

namespace Mail
{
    // 우편 추가/삭제 변경 기록. 둘 다 MailInfo 원본을 통째로 들고 있다 -- 삭제를 되돌릴 때
    // 제목/본문/기간까지 그대로 복원해야 하고, 클라이언트도 같은 값을 받아 자기 우편함에
    // 적용하기 때문이다.
    //
    // 우편함을 shared_ptr로 붙잡아두는 이유: 태스크가 살아 있는 동안(= 요청 스코프) 그 우편함이
    // 사라지면 되돌릴 대상이 없어진다. 요청 처리 중 플레이어가 퇴장해도 롤백은 안전하게 끝난다.
    class AddMailTask final : public Task::ITask
    {
    public:
        AddMailTask(std::shared_ptr<MailModel::Mutexed> mailBox, MailInfo info);

        [[nodiscard]] uint16_t Kind() const noexcept override;
        void Serialize(Packet::BinaryWriter& writer) const override;
        void Rollback(Task::UnitOfWork& sink) const override;

    private:
        std::shared_ptr<MailModel::Mutexed> mailBox_;
        MailInfo info_;
    };

    class DelMailTask final : public Task::ITask
    {
    public:
        DelMailTask(std::shared_ptr<MailModel::Mutexed> mailBox, MailInfo info);

        [[nodiscard]] uint16_t Kind() const noexcept override;
        void Serialize(Packet::BinaryWriter& writer) const override;
        void Rollback(Task::UnitOfWork& sink) const override;

    private:
        std::shared_ptr<MailModel::Mutexed> mailBox_;
        MailInfo info_;
    };
}
