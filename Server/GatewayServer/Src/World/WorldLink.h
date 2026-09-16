#pragma once

#include "Shared/Core/Src/Network/Session.h"

namespace Gateway
{
    // Gateway -> World로 나가는 현재 연결(있으면 딱 하나)을 들고 있는 스레드 세이프 홀더.
    // 클라이언트 accept 처리(ClientLinkHandler)와 World 연결 처리(WorldLinkHandler)가
    // 서로 다른 I/O 스레드에서 동시에 이 세션을 참조/전송할 수 있어 shared_mutex로 보호한다.
    class WorldLink
    {
    public:
        void Set(const Network::Session::SPtr& session)
        {
            std::unique_lock lock(mutex_);
            session_ = session;
        }

        void Clear()
        {
            std::unique_lock lock(mutex_);
            session_.reset();
        }

        [[nodiscard]] Network::Session::SPtr Get() const
        {
            std::shared_lock lock(mutex_);
            return session_;
        }

    private:
        mutable std::shared_mutex mutex_;
        Network::Session::SPtr session_;
    };
}
