#include "ZoneServer/Src/pch.h"
#include "ZoneServer/Src/Mail/MailRegistry.h"

namespace Mail
{
    void MailRegistry::Add(const Network::SessionId clientSessionId)
    {
        std::unique_lock lock(mutex_);
        mails_[clientSessionId] = std::make_shared<MailModel::ARef>();
    }

    void MailRegistry::Remove(const Network::SessionId clientSessionId)
    {
        std::unique_lock lock(mutex_);
        mails_.erase(clientSessionId);
    }

    std::shared_ptr<MailModel::ARef> MailRegistry::Find(const Network::SessionId clientSessionId) const
    {
        std::shared_lock lock(mutex_);
        const auto it = mails_.find(clientSessionId);
        return it != mails_.end() ? it->second : nullptr;
    }

    void MailRegistry::ForEach(const std::function<void(const Network::SessionId,
                                                        const std::shared_ptr<MailModel::ARef>&)>& func) const
    {
        std::shared_lock lock(mutex_);
        for (const auto& [clientSessionId, mailModel] : mails_)
        {
            func(clientSessionId, mailModel);
        }
    }
}
