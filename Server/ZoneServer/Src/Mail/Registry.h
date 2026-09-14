#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Mail/Model.h"

namespace Mail
{
    // 이 존에 있는 모든 플레이어의 Model::Mutexed(=Thread::Mutexed<Model>)를 들고 있는
    // 레지스트리. Network::SessionManager와 동일한 shared_mutex 패턴 -- 컨테이너 자체(플레이어
    // 입장/퇴장에 따른 삽입/삭제)는 항상 존 로직 스레드에서만 바뀌지만, 유지보수 타이머
    // 스레드가 ForEach로 동시에 순회할 수 있어 락이 필요하다(개별 Model 내용물 보호는
    // ARef가 따로 한다 -- 이 레지스트리는 "어떤 플레이어가 있는지" 목록만 보호한다).
    class Registry
    {
    public:
        // 우편함을 만들어 **채워진 상태로** 돌려준다. 만든 뒤 Find로 다시 찾지 않아도 되고,
        // 무엇보다 "빈 우편함이 레지스트리에 먼저 등록되는" 틈이 없다 -- 그 틈에 만료 스윕이
        // 돌면 그 사람의 우편이 없는 것으로 보인다.
        //
        // **playerId를 같이 받아 두는 이유**: 만료 스윕이 UnitOfWork를 열려면 playerId가
        // 필요한데, 그 스윕은 유지보수 타이머 스레드라 플레이어 레인 전용인 PlayerRegistry를
        // 읽을 수 없다. 이 레지스트리는 자기 shared_mutex로 보호되므로 여기 두면 안전하다.
        [[nodiscard]] std::shared_ptr<Model::Mutexed> Add(const Network::SessionId clientSessionId,
                                                          const Protocol::PlayerId playerId,
                                                          std::vector<Info> initial);
        void Remove(const Network::SessionId clientSessionId);

        [[nodiscard]] std::shared_ptr<Model::Mutexed> Find(const Network::SessionId clientSessionId) const;

        void ForEach(const std::function<void(const Network::SessionId, const Protocol::PlayerId,
                                              const std::shared_ptr<Model::Mutexed>&)>& func) const;

    private:
        struct Entry
        {
            Protocol::PlayerId playerId;
            std::shared_ptr<Model::Mutexed> mailBox;
        };

        mutable std::shared_mutex mutex_;
        std::unordered_map<Network::SessionId, Entry> mails_;
    };
}
