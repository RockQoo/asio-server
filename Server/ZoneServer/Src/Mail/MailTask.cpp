#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Mail/MailTask.h"

#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Core/Src/Task/UnitOfWork.h"
#include "Shared/Protocol/Src/TaskKind.h"

#include <utility>

namespace Mail
{
    namespace
    {
        // 추가/삭제 태스크가 같은 포맷(원본 전체)을 쓴다 -- 삭제 태스크에 지워진 내용이 통째로
        // 들어 있어야 롤백(= 되살리기)이 가능하기 때문이다.
        void WriteMail(Packet::BinaryWriter& writer, const MailInfo& info)
        {
            writer.Write(info.mailId);
            writer.WriteString(info.title);
            writer.WriteString(info.body);
            writer.Write(info.sendUt);
            writer.Write(info.endUt);
        }
    }

    AddMailTask::AddMailTask(std::shared_ptr<MailModel::Mutexed> mailBox, MailInfo info)
        : mailBox_(std::move(mailBox))
        , info_(std::move(info))
    {
    }

    uint16_t AddMailTask::Kind() const noexcept
    {
        return Protocol::MakeTaskKind(Protocol::ETaskCategory::Mail, Protocol::EMailTask::Added);
    }

    void AddMailTask::Serialize(Packet::BinaryWriter& writer) const
    {
        WriteMail(writer, info_);
    }

    void AddMailTask::Rollback(Task::UnitOfWork& sink) const
    {
        if (!mailBox_)
        {
            return;
        }

        // 추가를 되돌리는 건 삭제다. 롤백 전용 함수를 따로 만들지 않고 정상 함수를 그대로
        // 쓰되, sink(전송 없는 UnitOfWork)로 기록을 버린다.
        //
        // 반환값을 버리지 않는 이유: 롤백은 조금 전에 성공한 추가를 되돌리는 것뿐이라 실패할
        // 수 없다는 전제인데, 여기 걸렸다면 그 전제가 깨진 것이다. 되돌릴 방법이 없으니
        // 드러내기만 한다(cpp-patterns.md "롤백은 실패할 수 없다는 게 전제다" 참고).
        if (const auto errorCode = mailBox_->Write()->DelMail(info_.mailId, sink, false);
            errorCode != EErrorCode::Success)
        {
            LOG.Error(ELogCategory::Zone, "우편 추가 롤백 실패 -- 메모리와 DB가 어긋난다")
                .KV("MailId", info_.mailId).KV("ErrorCode", static_cast<int32_t>(errorCode));
        }
    }

    DelMailTask::DelMailTask(std::shared_ptr<MailModel::Mutexed> mailBox, MailInfo info)
        : mailBox_(std::move(mailBox))
        , info_(std::move(info))
    {
    }

    uint16_t DelMailTask::Kind() const noexcept
    {
        return Protocol::MakeTaskKind(Protocol::ETaskCategory::Mail, Protocol::EMailTask::Removed);
    }

    void DelMailTask::Serialize(Packet::BinaryWriter& writer) const
    {
        WriteMail(writer, info_);
    }

    void DelMailTask::Rollback(Task::UnitOfWork& sink) const
    {
        if (!mailBox_)
        {
            return;
        }

        // 삭제를 되돌리려면 mailId까지 그대로 복원해야 해서 AddMail(서버가 id를 새로 배정)이
        // 아니라 InsertMail(id 지정)을 쓴다.
        if (const auto errorCode = mailBox_->Write()->InsertMail(info_, sink);
            errorCode != EErrorCode::Success)
        {
            LOG.Error(ELogCategory::Zone, "우편 삭제 롤백 실패 -- 지워진 우편이 되살아나지 못했다")
                .KV("MailId", info_.mailId).KV("ErrorCode", static_cast<int32_t>(errorCode));
        }
    }
}
