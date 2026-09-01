#pragma once

#include <memory>
#include <shared_mutex>

namespace Network
{
    class Session;
}

namespace Zone
{
    // Zone -> World로 나가는 현재 연결(딱 하나)을 들고 있는 스레드 세이프 홀더.
    // ZoneWorld(존 로직 스레드), Mail 만료 유지보수 타이머(별도 스레드), WorldLinkHandler
    // (World 연결의 I/O 스레드)가 서로 다른 스레드에서 동시에 이 세션을 참조/전송할 수 있어
    // shared_mutex로 보호한다(GatewayServer/Src/World/WorldLink.h와 동일한 패턴).
    class WorldLink
    {
    public:
        void Set(const std::shared_ptr<Network::Session>& session)
        {
            std::unique_lock lock(mutex_);
            session_ = session;
        }

        void Clear()
        {
            std::unique_lock lock(mutex_);
            session_.reset();
        }

        [[nodiscard]] std::shared_ptr<Network::Session> Get() const
        {
            std::shared_lock lock(mutex_);
            return session_;
        }

    private:
        mutable std::shared_mutex mutex_;
        std::shared_ptr<Network::Session> session_;
    };
}
