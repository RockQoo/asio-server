#pragma once

#include "Server/Core/Src/Console/KeyBinder.h"
#include "Server/Core/Src/Network/Connector.h"
#include "Server/Core/Src/Network/IoContextPool.h"
#include "Server/Core/Src/Network/Listener.h"
#include "Server/Core/Src/Network/SessionManager.h"
#include "Handler/C2GHandler.h"
#include "Handler/W2GHandler.h"
#include "Server/Core/Src/Network/SessionHolder.h"

#include "App/GatewayConfig.h"

#include <asio.hpp>

// 전체를 조립하는 곳: 클라이언트 accept용 Listener 하나 + World로 나가는 Connector 하나.
// 인증/게임 로직이 전혀 없는 순수 릴레이라 GatewayApp/App보다 훨씬 얇다.
class GatewayApp
{
public:
    explicit GatewayApp(GatewayConfig config);

    void Run();
    void Stop();

private:
    void SetupSignalHandling();

    GatewayConfig config_;
    Network::IoContextPool ioPool_;
    Network::SessionManager sessionManager_;
    Network::SessionHolder worldLink_;
    C2GHandler clientHandler_;
    W2GHandler worldLinkHandler_;
    std::shared_ptr<Network::Listener> clientListener_;
    std::shared_ptr<Network::Connector> worldConnector_;

    // F키 테스트 하네스. 콜백은 전용 입력 스레드에서 돈다(서버 레인이 아니다).
    Console::KeyBinder keyBinder_;
    asio::signal_set signals_;
};
