#include "pch.h"
#include "App/GatewayApp.h"


GatewayApp::GatewayApp(GatewayConfig config)
    : config_(std::move(config))
    , network_(config_.ioThreadCount)
    , clientHandler_(sessionManager_, worldLink_)
    , worldLinkHandler_(sessionManager_, worldLink_)
    , signals_(network_.At(0), SIGINT, SIGTERM)
{
}

void GatewayApp::Run()
{
    network_.AddConnector(config_.worldHost, config_.worldPort, worldLinkHandler_);
    network_.AddListener(config_.clientPort, clientHandler_);
    network_.Start();

    SetupSignalHandling();

    keyBinder_.Start();

    LOG.Info(ELogCategory::General, "GatewayServer 대기 시작")
        .KV("ClientPort", config_.clientPort)
        .KV("WorldHost", config_.worldHost).KV("WorldPort", config_.worldPort);

    network_.Join();

    keyBinder_.Stop();

    LOG.Info(ELogCategory::General, "GatewayServer 종료 완료");
}

void GatewayApp::Stop()
{
    network_.Stop();
}

void GatewayApp::SetupSignalHandling()
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
