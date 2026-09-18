#include "pch.h"
#include "App/ZoneApp.h"

#include "Player/PlayerStreamHandler.h"
#include "Processor/BroadcastProcessor.h"
#include "Processor/ProcessorIds.h"
#include "Processor/TickProcessor.h"
#include "Processor/TimerProcessor.h"
#include "Processor/ZoneNetworkProcessor.h"
#include "Processor/ZoneProcessor.h"

#include "Server/Core/Src/Timer/RepeatingTimer.h"

namespace
{
    // TIMER 레인의 레인 수. 만기 판정만 하는 레인이라 하나로 고정한다 -- 늘리면 같은 주기
    // 작업의 두 만기가 서로 다른 스레드에서 겹쳐 돌 수 있다.
    constexpr int32_t kTimerLaneCount = 1;
}

ZoneApp::ZoneApp(ZoneConfig config)
    : config_(std::move(config))
    , network_(config_.ioThreadCount)
    , signals_(network_.At(0), SIGINT, SIGTERM)
{
}

ZoneApp::~ZoneApp()
{
    // 레인이 소유한 프로세서들이 이 App 의 멤버(zones_ 의 링크와 세계)를 참조하므로, 레인을
    // 먼저 버려야 한다. Run() 이 정상 종료했다면 이미 Stop() 까지 끝난 상태다.
    Pipeline::ProducerHolder::Instance().Shutdown();
}

void ZoneApp::InitProducers()
{
    auto& producerHolder = Pipeline::ProducerHolder::Instance();

    const auto zoneCount = config_.zones.size();

    producerHolder.InitProducer(Pipeline::EProducerType::Lb,
                                static_cast<int32_t>(config_.lbThreadCount), config_.laneBackend);
    producerHolder.InitProducer(Pipeline::EProducerType::Basic,
                                static_cast<int32_t>(config_.basicThreadCount), config_.laneBackend);

    // **TICK 만 해시를 끈다.** 주인이 1..2N 로 촘촘하고 레인이 2N+1 개라 나머지 연산이
    // 항등이 되어 충돌이 0이다. 해시를 켜면 그 1:1이 깨져 두 존의 틱이 한 스레드로 합쳐진다.
    producerHolder.InitProducer(Pipeline::EProducerType::Tick,
                                static_cast<int32_t>(TickLaneCount(zoneCount)),
                                config_.laneBackend, false);

    producerHolder.InitProducer(Pipeline::EProducerType::Broadcast,
                                static_cast<int32_t>(config_.broadcastThreadCount), config_.laneBackend);
    producerHolder.InitProducer(Pipeline::EProducerType::Timer, kTimerLaneCount, config_.laneBackend);

    auto* const lbProducer = producerHolder.GetProducer(Pipeline::EProducerType::Lb);
    auto* const basicProducer = producerHolder.GetProducer(Pipeline::EProducerType::Basic);
    auto* const tickProducer = producerHolder.GetProducer(Pipeline::EProducerType::Tick);
    auto* const broadcastProducer = producerHolder.GetProducer(Pipeline::EProducerType::Broadcast);
    auto* const timerProducer = producerHolder.GetProducer(Pipeline::EProducerType::Timer);

    auto& ids = Ids();

    std::vector<Common::ZoneId> zoneIds;
    zoneIds.reserve(zoneCount);

    zones_.reserve(zoneCount);
    for (size_t ordinal = 0; ordinal < zoneCount; ++ordinal)
    {
        const auto& def = config_.zones[ordinal];

        ZoneRuntime runtime{};
        runtime.worldLink = std::make_unique<Network::SessionHolder>();
        runtime.zone = std::make_shared<Zone>(def, *runtime.worldLink);
        runtime.handler = std::make_unique<W2ZHandler>(def, *runtime.worldLink);

        // **담당 존마다 처리기가 네 벌이다.** 프로세서 객체의 소유권은 레인으로 넘어가고,
        // 여기 남는 것은 id 뿐이다 -- 보내는 쪽은 그 id 만 보고 객체를 모른다.
        ZoneLaneTarget target{};
        target.lb = lbProducer->AddProcessor(
            std::make_unique<ZoneNetworkProcessor>(def.zoneId, *runtime.worldLink));
        target.basic = basicProducer->AddProcessor(std::make_unique<ZoneProcessor>(runtime.zone));
        target.tick = tickProducer->AddProcessor(std::make_unique<TickProcessor>(runtime.zone));
        target.broadcast = broadcastProducer->AddProcessor(
            std::make_unique<BroadcastProcessor>(def.zoneId, *runtime.worldLink));

        // 주인은 서수에서 파생시킨다 -- 어디에도 저장하지 않으므로 갈릴 자리가 없다.
        target.tickOwner = ZoneTickOwner(ordinal);
        target.otherOwner = ZoneOtherOwner(ordinal);
        target.broadcastOwner = Pipeline::OwnerId{static_cast<int64_t>(def.zoneId.Value())};

        ids.zones[def.zoneId] = target;
        zoneIds.push_back(def.zoneId);

        // **배정이 계산대로 되는지 기동 때 한 번 찍는다.** 레인 수만 로그에 남기면
        // "3개를 만들었다"까지만 알고 "존마다 다른 레인으로 갔는가"는 모른다 -- TICK 은
        // 그게 깨지는 순간 두 존의 틱이 한 스레드로 합쳐지는데 증상이 "좀 느리다"뿐이라
        // 눈으로 못 찾는다.
        const auto tickLanes = TickLaneCount(zoneCount);
        LOG.Info(ELogCategory::General, "레인 배정")
            .KV("Zone", def.zoneId)
            .KV("TickOwner", target.tickOwner.value)
            .KV("TickLane", Pipeline::LaneIndexOf(target.tickOwner, tickLanes, false))
            .KV("OtherOwner", target.otherOwner.value)
            .KV("OtherLane", Pipeline::LaneIndexOf(target.otherOwner, tickLanes, false))
            .KV("BroadcastOwner", target.broadcastOwner.value)
            .KV("BroadcastLane",
                Pipeline::LaneIndexOf(target.broadcastOwner, config_.broadcastThreadCount, true));

        zones_.push_back(std::move(runtime));
    }

    auto timerOwned = std::make_unique<TimerProcessor>(std::move(zoneIds), config_.tickInterval,
                                                      config_.fanoutFlushInterval);
    timerProcessor_ = timerOwned.get();
    ids.timer = timerProducer->AddProcessor(std::move(timerOwned));
}

void ZoneApp::Run()
{
    // 패킷 표를 먼저 채운다. 비어 있으면 모든 클라 패킷이 미등록으로 버려진다.
    PlayerStreamHandler::Init();

    InitProducers();
    Pipeline::ProducerHolder::Instance().Start();

    // **담당 존마다 연결을 따로 연다.** 어느 소켓으로 들어왔느냐가 곧 어느 존의 것이냐라,
    // 받는 쪽이 존을 찾는 표를 들 필요가 없다.
    for (auto& runtime : zones_)
    {
        network_.AddConnector(config_.worldHost, config_.worldPort, *runtime.handler);
    }
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
        .KV("LbLanes", config_.lbThreadCount)
        .KV("BasicLanes", config_.basicThreadCount)
        .KV("TickLanes", TickLaneCount(config_.zones.size()))
        .KV("BroadcastLanes", config_.broadcastThreadCount)
        .KV("TimerLanes", kTimerLaneCount)
        .KV("WorldHost", config_.worldHost).KV("WorldPort", config_.worldPort)
        .KV("TickMs", config_.tickInterval.count())
        .KV("LaneBackend", Pipeline::ToString(config_.laneBackend));

    network_.Join();

    // **일을 주는 쪽부터 끊는다.** 타이머가 살아 있으면 이미 닫히는 레인에 만기가 들어간다.
    keyBinder_.Stop();

    if (statsTimer_)
    {
        statsTimer_->Stop();
    }
    if (timerProcessor_ != nullptr)
    {
        timerProcessor_->Stop();
    }

    Pipeline::ProducerHolder::Instance().Stop();

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
