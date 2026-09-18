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
    // 작업의 두 만기가 서로 다른 스레드에서 겹쳐 돌 수 있다.
    constexpr int32_t kTimerLaneCount = 1;
}

WorldApp::WorldApp(WorldConfig config)
    : config_(std::move(config))
    , network_(config_.ioThreadCount)
    , dbPool_(config_.dbConnectionString)
    , signals_(network_.At(0), SIGINT, SIGTERM)
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
//   (소켓)   network_ -- accept 포트 셋 + IOCP 완료 + 프레임 조립. 파이프라인 레인이 아니다
//            (근거: docs/design/network-lane.md)
//   BASIC    Basic / Login / Tool
//   DB       Db
//   TIMER    Timer
//
// **TICK·BROADCAST·LB가 없다.** World에는 흐르는 시간도 좌표·시야도 없어서 박자와 팬아웃이
// 필요 없고, 수신 1차 파싱은 BASIC이 겸한다(그 단계가 좁은 목인 것은 BasicProcessor 주석 참고).
// ─────────────────────────────────────────────────────────────────────────────
void WorldApp::InitProducers()
{
    auto& producerHolder = Pipeline::ProducerHolder::Instance();

    producerHolder.InitProducer(Pipeline::EProducerType::Basic, static_cast<int32_t>(config_.basicThreadCount), config_.laneBackend);
    producerHolder.InitProducer(Pipeline::EProducerType::Db,    static_cast<int32_t>(config_.dbThreadCount), config_.laneBackend);
    producerHolder.InitProducer(Pipeline::EProducerType::Timer, kTimerLaneCount, config_.laneBackend);

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

    // 포트마다 상대가 다르다. 셋 다 같은 io 스레드 벌을 쓰고, 붙은 세션은 Service가 나눠 붙인다.
    network_.AddListener(config_.gatewayPort, gatewayLinkHandler_);
    network_.AddListener(config_.zonePort, zoneLinkHandler_);
    network_.AddListener(config_.toolPort, toolLinkHandler_);
    network_.Start();

    // **레인이 돌기 시작한 뒤에 건다** -- 타이머가 만기를 TIMER 레인에 넣기 때문이다.
    timerProcessor_->Start(network_.Next());

    // 레인 적체와 송신 적체를 나란히 남긴다. **부하에서 "어디가 밀렸나"를 판정할 유일한
    // 수단**이라 켜 둔다 -- 처리량 수치만 보면 느리다는 것까지만 알 수 있다.
    statsTimer_ = std::make_unique<Timer::RepeatingTimer>(network_.Next());
    statsTimer_->Start(config_.statsDumpInterval, [this]
    {
        Pipeline::ProducerHolder::Instance().LogStats();
        network_.LogStats();
    });

    SetupSignalHandling();
    keyBinder_.Start();

    LOG.Info(ELogCategory::General, "WorldServer 대기 시작")
        .KV("GatewayPort", config_.gatewayPort).KV("ZonePort", config_.zonePort)
        .KV("ToolPort", config_.toolPort)
        .KV("IoThreads", config_.ioThreadCount)
        .KV("BasicLanes", config_.basicThreadCount)
        .KV("DbLanes", config_.dbThreadCount)
        .KV("TimerLanes", kTimerLaneCount)
        .KV("LaneBackend", Pipeline::ToString(config_.laneBackend));

    network_.Join();

    // **일을 주는 쪽부터 끊는다.** MessageProducer::Stop()은 남은 일을 소진한 뒤 스레드를
    // 끝내므로, 주는 쪽이 먼저 서야 받는 쪽이 살아 있는 동안 그 일을 마저 처리한다.
    keyBinder_.Stop();
    if (statsTimer_)
    {
        statsTimer_->Stop();
    }
    timerProcessor_->Stop();

    // 레인끼리의 순서는 EProducerType 선언 순서 하나로 정해진다(Basic -> Db -> Timer).
    // Db가 뒤에 있어 밀린 저장을 마저 소진한다.
    Pipeline::ProducerHolder::Instance().Stop();
    LOG.Info(ELogCategory::General, "WorldServer 종료 완료");
}

void WorldApp::Stop()
{
    // 포트를 몇 개 열었든 여기는 한 줄이다 -- 포트가 늘어도 이 함수를 같이 고칠 일이 없다.
    network_.Stop();
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
