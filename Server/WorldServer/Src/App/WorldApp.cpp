#include "pch.h"
#include "App/WorldApp.h"

#include "Processor/BasicProcessor.h"
#include "Processor/DbProcessor.h"
#include "Processor/LoginProcessor.h"
#include "Processor/ProcessorIds.h"
#include "Processor/TimerProcessor.h"
#include "Processor/ToolProcessor.h"
#include "Server/Core/Src/Network/Session.h"

namespace
{
    // TIMER 레인의 레인 수. 만기 판정만 하는 레인이라 하나로 고정한다 -- 늘리면 같은 주기
    // 작업의 두 만기가 서로 다른 스레드에서 겹쳐 돌 수 있다. 실무 원본도 1로 고정이다.
    constexpr int32_t kTimerLaneCount = 1;
}

WorldApp::WorldApp(WorldConfig config)
    : config_(std::move(config))
    , ioPool_(config_.ioThreadCount)
    , dbPool_(config_.dbConnectionString)
    , signals_(ioPool_.At(0), SIGINT, SIGTERM)
{
}

WorldApp::~WorldApp()
{
    // 레인이 소유한 프로세서들이 이 App의 멤버(playerManager_ 등)를 참조하므로, 레인을
    // 먼저 버려야 한다. Run()이 정상 종료했다면 이미 Stop()까지 끝난 상태다.
    Pipeline::ProducerHolder::Instance().Shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// 이 프로세스의 레인과 그 위에 얹히는 프로세서. **World의 레인 구성은 여기가 전부다.**
//
//   NETWORK  ioPool_ (프로액터) -- IOCP 완료 + 프레임 조립. 파이프라인 레인이 아니다.
//   BASIC    Basic / Login / Tool
//   DB       Db
//   TIMER    Timer
//
// 노션 World와 같은 구성이다(TICK·BROADCAST·LB가 없다 -- 흐르는 시간도, 좌표·시야도 없고,
// 1차 파싱은 BASIC이 겸한다).
// ─────────────────────────────────────────────────────────────────────────────
void WorldApp::InitProducers()
{
    auto& producerHolder = Pipeline::ProducerHolder::Instance();

    producerHolder.InitProducer(Pipeline::EProducerType::Basic, static_cast<int32_t>(config_.basicThreadCount));
    producerHolder.InitProducer(Pipeline::EProducerType::Db,    static_cast<int32_t>(config_.dbThreadCount));
    producerHolder.InitProducer(Pipeline::EProducerType::Timer, kTimerLaneCount);

    // **AddProcessor가 돌려준 id를 보관해야 다른 곳에서 여기로 메시지를 보낼 수 있다.**
    // 프로세서 객체의 소유권은 레인으로 넘어가고, 여기 남는 것은 id뿐이다.
    auto& ids = Ids();

    auto* const basicProducer = producerHolder.GetProducer(Pipeline::EProducerType::Basic);

    // BasicProcessor가 LoginProcessor를 직접 참조하므로 먼저 만들어 참조를 잡아둔 뒤 등록한다.
    // (DB는 참조가 필요 없다 -- DbProcessor::Execute가 PushMsg로 보내는 static 함수다.)
    auto loginOwned = std::make_unique<LoginProcessor>(playerManager_, zoneLinkRegistry_);
    LoginProcessor& login = *loginOwned;
    ids.login = basicProducer->AddProcessor(std::move(loginOwned));

    ids.main = basicProducer->AddProcessor(
        std::make_unique<BasicProcessor>(playerManager_, zoneLinkRegistry_, login));

    ids.tool = basicProducer->AddProcessor(
        std::make_unique<ToolProcessor>(playerManager_, zoneLinkRegistry_, config_.toolSharedSecret));

    auto* const dbProducer = producerHolder.GetProducer(Pipeline::EProducerType::Db);
    ids.db = dbProducer->AddProcessor(std::make_unique<DbProcessor>(dbPool_));

    auto* const timerProducer = producerHolder.GetProducer(Pipeline::EProducerType::Timer);
    auto timerOwned = std::make_unique<TimerProcessor>();
    timerProcessor_ = timerOwned.get();
    ids.timer = timerProducer->AddProcessor(std::move(timerOwned));
}

void WorldApp::Run()
{
    InitProducers();
    Pipeline::ProducerHolder::Instance().Start();

    gatewayListener_ = std::make_shared<Network::Listener>(ioPool_.At(0), ioPool_, config_.gatewayPort, gatewayLinkHandler_);
    gatewayListener_->Start();

    zoneListener_ = std::make_shared<Network::Listener>(ioPool_.At(0), ioPool_, config_.zonePort, zoneLinkHandler_);
    zoneListener_->Start();

    toolListener_ = std::make_shared<Network::Listener>(ioPool_.At(0), ioPool_, config_.toolPort, toolLinkHandler_);
    toolListener_->Start();

    // **레인이 돌기 시작한 뒤에 건다** -- 타이머가 만기를 TIMER 레인에 넣기 때문이다.
    timerProcessor_->Start();

    SetupSignalHandling();
    keyBinder_.Start();

    LOG.Info(ELogCategory::General, "WorldServer 대기 시작")
        .KV("GatewayPort", config_.gatewayPort).KV("ZonePort", config_.zonePort)
        .KV("ToolPort", config_.toolPort)
        .KV("IoThreads", config_.ioThreadCount)
        .KV("BasicLanes", config_.basicThreadCount)
        .KV("DbLanes", config_.dbThreadCount)
        .KV("TimerLanes", kTimerLaneCount);

    ioPool_.Run();
    ioPool_.Join();

    // **일을 주는 쪽부터 끊는다.** MessageProducer::Stop()은 남은 일을 소진한 뒤 스레드를
    // 끝내므로, 주는 쪽이 먼저 서야 받는 쪽이 살아 있는 동안 그 일을 마저 처리한다.
    keyBinder_.Stop();
    timerProcessor_->Stop();

    // 레인끼리의 순서는 EProducerType 선언 순서 하나로 정해진다(Basic -> Db -> Timer).
    // Db가 뒤에 있어 밀린 저장을 마저 소진한다.
    Pipeline::ProducerHolder::Instance().Stop();
    LOG.Info(ELogCategory::General, "WorldServer 종료 완료");
}

void WorldApp::Stop()
{
    if (gatewayListener_)
    {
        gatewayListener_->Stop();
    }
    if (zoneListener_)
    {
        zoneListener_->Stop();
    }
    if (toolListener_)
    {
        toolListener_->Stop();
    }
    ioPool_.Stop();
}

void WorldApp::SetupSignalHandling()
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
