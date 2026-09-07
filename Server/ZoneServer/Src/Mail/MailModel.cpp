#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Mail/MailModel.h"

#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Core/Src/Task/UnitOfWork.h"
#include "Shared/Protocol/Src/ErrorCode.h"
#include "Shared/Protocol/Src/TaskKind.h"

#include <utility>

namespace Mail
{
    namespace
    {
        // 추가/삭제 태스크가 같은 포맷(원본 전체)을 쓴다 -- 삭제 태스크에 지워진 내용이 통째로
        // 들어 있어야 롤백(= 되살리기)이 가능하기 때문이다(UnitOfWork.h 주석 참고).
        [[nodiscard]] std::vector<byte> SerializeMail(const MailInfo& info)
        {
            Packet::BinaryWriter writer;
            writer.Write(info.mailId);
            writer.WriteString(info.title);
            writer.WriteString(info.body);
            writer.Write(info.sendUt);
            writer.Write(info.endUt);

            const auto buffer = writer.GetBuffer();
            return std::vector<byte>(buffer.begin(), buffer.end());
        }
    }

    void MailModel::AddMail(MailInfo info, Task::UnitOfWork& unitOfWork)
    {
        // id를 먼저 소비하지 않고 후보만 본다 -- 실패로 끝나는 요청이 id를 하나씩 태우면
        // 롤백해도 그 구멍은 되돌아오지 않는다.
        const auto mailId = nextMailId_;
        if (mails_.contains(mailId))
        {
            unitOfWork.SetError(EErrorCode::MailAlreadyExists);
            return;
        }

        info.mailId = mailId;
        unitOfWork.RecordTask(Protocol::MakeTaskKind(Protocol::ETaskCategory::Mail, Protocol::EMailTask::Added),
                              SerializeMail(info));

        ++nextMailId_;
        mails_[mailId] = std::move(info);
    }

    void MailModel::DelMail(const uint32_t mailId, Task::UnitOfWork& unitOfWork, const bool /*isTimeout*/)
    {
        const auto it = mails_.find(mailId);
        if (it == mails_.end())
        {
            unitOfWork.SetError(EErrorCode::MailNotFound);
            return;
        }

        unitOfWork.RecordTask(Protocol::MakeTaskKind(Protocol::ETaskCategory::Mail, Protocol::EMailTask::Removed),
                              SerializeMail(it->second));

        mails_.erase(it);
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

    void MailModel::UndoAdd(const uint32_t mailId)
    {
        mails_.erase(mailId);
        // nextMailId_는 되돌리지 않는다 -- id는 단조 증가하기만 하면 되고, 되돌렸다가는
        // 이미 World로 나간 다른 태스크의 id와 겹칠 수 있다.
    }

    void MailModel::UndoRemove(MailInfo info)
    {
        const auto mailId = info.mailId;
        mails_[mailId] = std::move(info);
    }
}
