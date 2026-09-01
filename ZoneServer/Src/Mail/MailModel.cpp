#include "ZoneServer/Src/pch.h"
#include "ZoneServer/Src/Mail/MailModel.h"

#include "Core/Src/Packet/BinaryWriter.h"
#include "Core/Src/Task/UnitOfWork.h"

namespace Mail
{
    uint32_t MailModel::AddMail(MailInfo info, Task::UnitOfWork& unitOfWork)
    {
        info.mailId = nextMailId_++;

        Packet::BinaryWriter writer;
        writer.Write(info.mailId);
        writer.WriteString(info.title);
        writer.WriteString(info.body);
        writer.Write(info.sendUt);
        writer.Write(info.endUt);
        unitOfWork.RecordTask(static_cast<uint16_t>(MailTaskKind::Added), writer.GetBuffer());

        const auto mailId = info.mailId;
        mails_[mailId] = std::move(info);
        return mailId;
    }

    bool MailModel::DelMail(const uint32_t mailId, Task::UnitOfWork& unitOfWork, const bool /*isTimeout*/)
    {
        const auto it = mails_.find(mailId);
        if (it == mails_.end())
        {
            return false;
        }

        Packet::BinaryWriter writer;
        writer.Write(it->second.mailId);
        writer.WriteString(it->second.title);
        writer.WriteString(it->second.body);
        writer.Write(it->second.sendUt);
        writer.Write(it->second.endUt);
        unitOfWork.RecordTask(static_cast<uint16_t>(MailTaskKind::Removed), writer.GetBuffer());

        mails_.erase(it);
        return true;
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
}
