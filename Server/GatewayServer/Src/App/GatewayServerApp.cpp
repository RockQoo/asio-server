#include "Server/GatewayServer/Src/pch.h"
#include "Server/GatewayServer/Src/App/GatewayServerApp.h"

#include <csignal>
#include <utility>

namespace Gateway
{
    GatewayServerApp::GatewayServerApp(GatewayServerConfig config)
        : config_(std::move(config))
        , ioPool_(config_.ioThreadCount)
        , clientHandler_(sessionManager_, worldLink_)
        , worldLinkHandler_(sessionManager_, worldLink_)
        , signals_(ioPool_.At(0), SIGINT, SIGTERM)
    {
    }

    void GatewayServerApp::Run()
    {
        worldConnector_ = std::make_shared<Network::Connector>(ioPool_.At(0), config_.worldHost, config_.worldPort, worldLinkHandler_);
        worldConnector_->Start();

        clientListener_ = std::make_shared<Network::Listener>(ioPool_.At(0), ioPool_, config_.clientPort, clientHandler_);
        clientListener_->Start();

        SetupSignalHandling();

        LOG.Info(ELogCategory::General, "GatewayServer 대기 시작")
            .KV("ClientPort", config_.clientPort)
            .KV("WorldHost", config_.worldHost).KV("WorldPort", config_.worldPort);

        ioPool_.Run();
        ioPool_.Join();

        LOG.Info(ELogCategory::General, "GatewayServer 종료 완료");
    }

    void GatewayServerApp::Stop()
    {
        if (clientListener_)
        {
            clientListener_->Stop();
        }
        if (worldConnector_)
        {
            worldConnector_->Stop();
        }
        ioPool_.Stop();
    }

    void GatewayServerApp::SetupSignalHandling()
    {
        signals_.async_wait([this](const std::error_code ec, const int signalNumber)
        {
            if (!ec)
            {
                LOG.Info(ELogCategory::General, "시그널 수신, 종료 절차 시작").KV("Signal", signalNumber);
                Stop();
            }
        });
    }
}
