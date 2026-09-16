#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Network/Session.h"

namespace Network
{
    // 현재 연결된 모든 세션을 관리하는 스레드 세이프 레지스트리.
    // 조회(Find/ForEach)는 shared lock을 사용하므로 여러 로직 스레드가 동시에 전송해도
    // 서로 직렬화되어 막히지 않는다.
    class SessionManager
    {
    public:
        void Add(const Session::SPtr& session);
        void Remove(const SessionId id);

        [[nodiscard]] Session::SPtr Find(const SessionId id) const;
        [[nodiscard]] size_t Count() const;

        void ForEach(const std::function<void(const Session::SPtr&)>& func) const;

    private:
        mutable std::shared_mutex mutex_;
        std::unordered_map<SessionId, Session::SPtr> sessions_;
    };
}
