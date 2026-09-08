#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/App/ZoneServerApp.h"

#include "Shared/Core/Src/Timer/RepeatingTimer.h"

#include <chrono>
#include <csignal>
#include <utility>

namespace Zone
{
    ZoneServerApp::ZoneServerApp(ZoneServerConfig config)
        : config_(std::move(config))
        , ioPool_(config_.ioThreadCount)
        , zoneWorkers_(config_.zones, config_.poolSizes, worldLink_, mailRegistry_)
        , worldLinkHandler_(zoneWorkers_, worldLink_, config_.zones, config_.lbThreadCount)
        , mailExpiryService_(mailRegistry_, worldLink_)
        , signals_(ioPool_.At(0), SIGINT, SIGTERM)
    {
    }

    ZoneServerApp::~ZoneServerApp() = default;

    void ZoneServerApp::Run()
    {
        zoneWorkers_.Start(ioPool_, config_.tickInterval);
        worldLinkHandler_.Start();

        worldConnector_ = std::make_shared<Network::Connector>(ioPool_.At(0), config_.worldHost, config_.worldPort, worldLinkHandler_);
        worldConnector_->Start();

        // zone 워커 스레드가 아니라 io_context 하나를 그대로 빌려서 도는 별도 스레드다 --
        // 존 로직 스레드와 겹치지 않아야 Mutexed가 방어하는 "진짜 교차 스레드" 시나리오가
        // 성립한다(ZoneInstance.h/Mail::MailExpiryService 주석 참고).
        mailExpiryTimer_ = std::make_unique<Timer::RepeatingTimer>(ioPool_.Next());
        mailExpiryTimer_->Start(config_.mailSweepInterval, [this]
        {
            const auto nowUt = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            mailExpiryService_.SweepOnce(nowUt);
        });

        SetupSignalHandling();

        std::string zoneIdList;
        for (const auto& def : config_.zones)
        {
            if (!zoneIdList.empty())
            {
                zoneIdList += ",";
            }
            zoneIdList += std::to_string(def.zoneId);
        }

        LOG.Info(ELogCategory::General, "ZoneServer 대기 시작")
            .KV("ZoneIds", zoneIdList).KV("LbThreads", config_.lbThreadCount)
            .KV("BasicThreads", config_.poolSizes.basicThreadCount)
            .KV("TickThreads", config_.poolSizes.tickThreadCount)
            .KV("BroadcastThreads", config_.poolSizes.broadcastThreadCount)
            .KV("WorldHost", config_.worldHost).KV("WorldPort", config_.worldPort)
            .KV("IoThreads", config_.ioThreadCount).KV("TickMs", config_.tickInterval.count());

        ioPool_.Run();
        ioPool_.Join();

        if (mailExpiryTimer_)
        {
            mailExpiryTimer_->Stop();
        }
        worldLinkHandler_.Stop();
        zoneWorkers_.Stop();
        LOG.Info(ELogCategory::General, "ZoneServer 종료 완료");
    }

    void ZoneServerApp::Stop()
    {
        if (worldConnector_)
        {
            worldConnector_->Stop();
        }
        ioPool_.Stop();
    }

    void ZoneServerApp::SetupSignalHandling()
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
