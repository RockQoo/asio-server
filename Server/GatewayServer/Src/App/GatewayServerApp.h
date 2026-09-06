#pragma once

#include "Shared/Core/Src/Network/Connector.h"
#include "Shared/Core/Src/Network/IoContextPool.h"
#include "Shared/Core/Src/Network/Listener.h"
#include "Shared/Core/Src/Network/SessionManager.h"
#include "Server/GatewayServer/Src/Handler/ClientLinkHandler.h"
#include "Server/GatewayServer/Src/Handler/WorldLinkHandler.h"
#include "Server/GatewayServer/Src/World/WorldLink.h"

#include <asio.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Gateway
{
    struct GatewayServerConfig
    {
        uint16_t clientPort{9000};
        std::string worldHost{"127.0.0.1"};
        uint16_t worldPort{9100};
        size_t ioThreadCount{2};
    };

    // 전체를 조립하는 곳: 클라이언트 accept용 Listener 하나 + World로 나가는 Connector 하나.
    // 인증/게임 로직이 전혀 없는 순수 릴레이라 ZoneServerApp/WorldServerApp보다 훨씬 얇다.
    class GatewayServerApp
    {
    public:
        explicit GatewayServerApp(GatewayServerConfig config);

        void Run();
        void Stop();

    private:
        void SetupSignalHandling();

        GatewayServerConfig config_;
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
