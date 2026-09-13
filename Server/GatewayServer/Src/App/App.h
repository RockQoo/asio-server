#pragma once

#include "Shared/Core/Src/Network/Connector.h"
#include "Shared/Core/Src/Network/IoContextPool.h"
#include "Shared/Core/Src/Network/Listener.h"
#include "Shared/Core/Src/Network/SessionManager.h"
#include "Handler/ClientLinkHandler.h"
#include "Handler/WorldLinkHandler.h"
#include "World/WorldLink.h"

#include "App/Config.h"

#include <asio.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Gateway
{
    // 전체를 조립하는 곳: 클라이언트 accept용 Listener 하나 + World로 나가는 Connector 하나.
    // 인증/게임 로직이 전혀 없는 순수 릴레이라 App/App보다 훨씬 얇다.
    class App
    {
    public:
        explicit App(Config config);

        void Run();
        void Stop();

    private:
        void SetupSignalHandling();

        Config config_;
        Network::IoContextPool ioPool_;
        Network::SessionManager sessionManager_;
        WorldLink worldLink_;
        ClientLinkHandler clientHandler_;
        WorldLinkHandler worldLinkHandler_;
        std::shared_ptr<Network::Listener> clientListener_;
        std::shared_ptr<Network::Connector> worldConnector_;
        asio::signal_set signals_;
    };
}
