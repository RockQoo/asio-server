#include "pch.h"
#include "App/ZoneApp.h"

#include "Server/Core/Src/Timer/RepeatingTimer.h"


ZoneApp::ZoneApp(ZoneConfig config)
    : config_(std::move(config))
    , network_(config_.ioThreadCount)
    , playerGroup_("Player", config_.poolSizes.playerThreadCount, config_.slowTaskWarnThreshold)
    // 샤드 개수를 플레이어 레인 스레드 수와 맞춘다 -- 둘 다 `% N`으로 나누므로 같아야
    // "그 샤드를 만지는 스레드가 항상 하나"가 성립한다(PlayerRegistry.h 주석 참고).
    , playerRegistry_(playerGroup_.ThreadCount())
    , zoneWorkers_(config_.zones, config_.poolSizes, config_.slowTaskWarnThreshold, worldLink_)
    , playerProcessor_(playerRegistry_, zoneWorkers_, zoneWorkers_.Broadcaster(), worldLink_, mailRegistry_)
    , worldLinkHandler_(playerProcessor_, playerGroup_, worldLink_, config_.zones)
    , mailExpiryService_(mailRegistry_, worldLink_)
    , signals_(network_.At(0), SIGINT, SIGTERM)
{
}

ZoneApp::~ZoneApp() = default;

void ZoneApp::Run()
{
    playerGroup_.Start();
    zoneWorkers_.Start(network_, config_.tickInterval);

    // 존은 accept하지 않는다 -- World로 나가는 링크 하나가 전부다.
    network_.AddConnector(config_.worldHost, config_.worldPort, worldLinkHandler_);
    network_.Start();

    // 어느 레인에도 속하지 않고 io_context 하나를 빌려서 도는 유지보수 타이머다 --
    // 존 레인/플레이어 레인과 겹치지 않아야 Mutexed가 방어하는 "진짜 교차 스레드"
    // 시나리오가 성립한다(Model 참고).
    mailExpiryTimer_ = std::make_unique<Timer::RepeatingTimer>(network_.Next());
    mailExpiryTimer_->Start(config_.mailSweepInterval, [this]
    {
        const auto nowUt = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        mailExpiryService_.SweepOnce(nowUt);
    });

    // 레인별 대기/처리 시간을 주기적으로 남긴다. **부하 테스트에서 "어디가 밀렸나"를
    // 판정할 유일한 수단**이라 켜 둔다 -- 처리량 수치만 보면 느리다는 것까지만 알 수 있다.
    statsTimer_ = std::make_unique<Timer::RepeatingTimer>(network_.Next());
    statsTimer_->Start(config_.statsDumpInterval, [this]
    {
        playerGroup_.LogStats();
        zoneWorkers_.LogStats();
    });

    SetupSignalHandling();

    keyBinder_.Start();

    std::string zoneIdList;
    for (const auto& def : config_.zones)
    {
        if (!zoneIdList.empty())
        {
            zoneIdList += ",";
        }
        zoneIdList += std::to_string(def.zoneId.Value());
    }

    LOG.Info(ELogCategory::General, "ZoneServer 대기 시작")
        .KV("ZoneIds", zoneIdList)
        .KV("IoThreads", config_.ioThreadCount)
        .KV("PlayerThreads", playerGroup_.ThreadCount())
        .KV("ZoneThreads", config_.poolSizes.zoneThreadCount)
        .KV("BroadcastThreads", config_.poolSizes.broadcastThreadCount)
        .KV("WorldHost", config_.worldHost).KV("WorldPort", config_.worldPort)
        .KV("TickMs", config_.tickInterval.count());

    network_.Join();

    if (mailExpiryTimer_)
    {
        mailExpiryTimer_->Stop();
    }

    // **종속 관계의 역순으로 내린다.** 플레이어 레인이 존 레인과 브로드캐스트 레인에
    // 일을 던지므로 그 순서로 세워야 이미 정지한 레인에 새 일이
    // 들어가지 않는다(Group::Stop()은 큐에 남은 것을 소진한 뒤 join한다).
    // **레인을 세우기 전에 입력 스레드를 멈춘다** -- 안 그러면 F키 한 번이 이미 닫히는
    // 중인 레인으로 일을 밀어 넣는다.
    keyBinder_.Stop();

    playerGroup_.Stop();
    zoneWorkers_.Stop();  // 내부에서 타이머 취소 -> 존 레인 -> 브로드캐스트 순

    LOG.Info(ELogCategory::General, "ZoneServer 종료 완료");
}

void ZoneApp::Stop()
{
    network_.Stop();
}

void ZoneApp::SetupSignalHandling()
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
