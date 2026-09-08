#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Mail/MailRegistry.h"

namespace Mail
{
    void MailRegistry::Add(const Network::SessionId clientSessionId)
    {
        auto mailBox = std::make_shared<MailModel::Mutexed>();

        // 우편함이 자기를 감싼 Mutexed 핸들을 알아야 변경 태스크를 만들 수 있다 -- 롤백이
        // 그 우편함을 다시 잠그고 되돌려야 하기 때문이다(MailModel::BindSelf 주석 참고).
        // 우편함을 만드는 곳은 여기 하나뿐이라 이 한 줄만 지키면 된다.
        mailBox->Write()->BindSelf(mailBox);

        std::unique_lock lock(mutex_);
        mails_[clientSessionId] = std::move(mailBox);
    }

    void MailRegistry::Remove(const Network::SessionId clientSessionId)
    {
        std::unique_lock lock(mutex_);
        mails_.erase(clientSessionId);
    }

    std::shared_ptr<MailModel::Mutexed> MailRegistry::Find(const Network::SessionId clientSessionId) const
    {
        std::shared_lock lock(mutex_);
        const auto it = mails_.find(clientSessionId);
        return it != mails_.end() ? it->second : nullptr;
    }

    void MailRegistry::ForEach(const std::function<void(const Network::SessionId,
                                                        const std::shared_ptr<MailModel::Mutexed>&)>& func) const
    {
        std::shared_lock lock(mutex_);
        for (const auto& [clientSessionId, mailModel] : mails_)
        {
            func(clientSessionId, mailModel);
        }
    }
}
