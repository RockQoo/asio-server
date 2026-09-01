#pragma once

#include "Core/Src/Common/Types.h"
#include "ZoneServer/Src/Mail/MailModel.h"

#include <functional>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace Mail
{
    // 이 존에 있는 모든 플레이어의 MailModel::Sync(=Synchronized<MailModel>)를 들고 있는
    // 레지스트리. Network::SessionManager와 동일한 shared_mutex 패턴 -- 컨테이너 자체(플레이어
    // 입장/퇴장에 따른 삽입/삭제)는 항상 존 로직 스레드에서만 바뀌지만, 유지보수 타이머
    // 스레드가 ForEach로 동시에 순회할 수 있어 락이 필요하다(개별 MailModel 내용물 보호는
    // ARef가 따로 한다 -- 이 레지스트리는 "어떤 플레이어가 있는지" 목록만 보호한다).
    class MailRegistry
    {
    public:
        void Add(const Network::SessionId clientSessionId);
        void Remove(const Network::SessionId clientSessionId);

        [[nodiscard]] std::shared_ptr<MailModel::Sync> Find(const Network::SessionId clientSessionId) const;

        void ForEach(const std::function<void(const Network::SessionId,
                                              const std::shared_ptr<MailModel::Sync>&)>& func) const;

    private:
        mutable std::shared_mutex mutex_;
        std::unordered_map<Network::SessionId, std::shared_ptr<MailModel::Sync>> mails_;
    };
}
