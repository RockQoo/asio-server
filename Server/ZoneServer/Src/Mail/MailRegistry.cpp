#include "pch.h"
#include "Mail/MailRegistry.h"

std::shared_ptr<MailModel::Mutexed> MailRegistry::Add(const Network::SessionId clientSessionId,
                                              const Common::PlayerId playerId,
                                              std::vector<Common::MailInfo> initial)
{
    // Mutexed는 완벽 전달 생성자를 갖고 있어서 Model의 인자를 그대로 넘길 수 있다 --
    // 덕분에 "빈 모델 생성 -> 채우기" 두 단계가 한 단계로 줄었다.
    auto mailBox = std::make_shared<MailModel::Mutexed>(std::move(initial));

    std::unique_lock lock(mutex_);
    mails_[clientSessionId] = Entry{playerId, mailBox};
    return mailBox;
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
    return it != mails_.end() ? it->second.mailBox : nullptr;
}

void MailRegistry::ForEach(const std::function<void(const Network::SessionId, const Common::PlayerId,
                                                const std::shared_ptr<MailModel::Mutexed>&)>& func) const
{
    std::shared_lock lock(mutex_);
    for (const auto& [clientSessionId, entry] : mails_)
    {
        func(clientSessionId, entry.playerId, entry.mailBox);
    }
}
