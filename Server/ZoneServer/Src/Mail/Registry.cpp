#include "pch.h"
#include "Mail/Registry.h"

namespace Mail
{
    std::shared_ptr<Model::Mutexed> Registry::Add(const Network::SessionId clientSessionId,
                                                  const Protocol::PlayerId playerId,
                                                  std::vector<Info> initial)
    {
        // Mutexed는 완벽 전달 생성자를 갖고 있어서 Model의 인자를 그대로 넘길 수 있다 --
        // 덕분에 "빈 모델 생성 -> 채우기" 두 단계가 한 단계로 줄었다.
        auto mailBox = std::make_shared<Model::Mutexed>(std::move(initial));

        // 우편함이 자기를 감싼 Mutexed 핸들을 알아야 변경 태스크를 만들 수 있다 -- 롤백이
        // 그 우편함을 다시 잠그고 되돌려야 하기 때문이다(Model::BindSelf 주석 참고).
        // 이것만은 생성자로 옮길 수 없다: 그 시점엔 감싸는 래퍼가 아직 없다.
        // 우편함을 만드는 곳은 여기 하나뿐이라 이 한 줄만 지키면 된다.
        mailBox->Write()->BindSelf(mailBox);

        std::unique_lock lock(mutex_);
        mails_[clientSessionId] = Entry{playerId, mailBox};
        return mailBox;
    }

    void Registry::Remove(const Network::SessionId clientSessionId)
    {
        std::unique_lock lock(mutex_);
        mails_.erase(clientSessionId);
    }

    std::shared_ptr<Model::Mutexed> Registry::Find(const Network::SessionId clientSessionId) const
    {
        std::shared_lock lock(mutex_);
        const auto it = mails_.find(clientSessionId);
        return it != mails_.end() ? it->second.mailBox : nullptr;
    }

    void Registry::ForEach(const std::function<void(const Network::SessionId, const Protocol::PlayerId,
                                                    const std::shared_ptr<Model::Mutexed>&)>& func) const
    {
        std::shared_lock lock(mutex_);
        for (const auto& [clientSessionId, entry] : mails_)
        {
            func(clientSessionId, entry.playerId, entry.mailBox);
        }
    }
}
