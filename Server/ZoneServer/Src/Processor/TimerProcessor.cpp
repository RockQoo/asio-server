#include "pch.h"
#include "Processor/TimerProcessor.h"

#include "Processor/ProcessorIds.h"

#include "Server/Core/Src/Pipeline/ProducerHolder.h"

namespace
{
    [[nodiscard]] int64_t NowUt()
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
}

TimerProcessor::TimerProcessor(std::vector<Common::ZoneId> zoneIds,
                               const std::chrono::milliseconds tickInterval,
                               const std::chrono::milliseconds fanoutFlushInterval)
    : zoneIds_(std::move(zoneIds))
    , tickInterval_(tickInterval)
    , fanoutFlushInterval_(fanoutFlushInterval)
{
}

TimerProcessor::~TimerProcessor()
{
    Stop();
}

void TimerProcessor::RegistHandler()
{
    Regist(EZoneMsg::TimerTick, &TimerProcessor::OnTimerTick);
    Regist(EZoneMsg::TimerFanoutFlush, &TimerProcessor::OnTimerFanoutFlush);
}

void TimerProcessor::Start(asio::io_context& timerContext)
{
    if (Pipeline::GetProducer<Pipeline::EProducerType::Timer>() == nullptr)
    {
        LOG.Warning(ELogCategory::General, "TIMER 레인이 없어 주기 작업을 걸지 않는다");
        return;
    }

    lastTickAt_ = std::chrono::steady_clock::now();

    // 만기 자체는 통계를 남기지 않으므로, 만기 핸들러는 다시 PushMsg 로 이 레인에 넣는다
    // (그래야 다른 메시지와 같은 취급을 받는다).
    tickTimer_ = std::make_unique<Timer::RepeatingTimer>(timerContext);
    tickTimer_->Start(tickInterval_, []
    {
        Pipeline::PushMsg<Pipeline::EProducerType::Timer>(
            EZoneMsg::TimerTick, Ids().timer, Pipeline::OwnerId{kGlobalQueryKey});
    });

    fanoutFlushTimer_ = std::make_unique<Timer::RepeatingTimer>(timerContext);
    fanoutFlushTimer_->Start(fanoutFlushInterval_, []
    {
        Pipeline::PushMsg<Pipeline::EProducerType::Timer>(
            EZoneMsg::TimerFanoutFlush, Ids().timer, Pipeline::OwnerId{kGlobalQueryKey});
    });

    LOG.Info(ELogCategory::General, "주기 작업 타이머 시작")
        .KV("TickMs", tickInterval_.count())
        .KV("FanoutFlushMs", fanoutFlushInterval_.count())
        .KV("Zones", zoneIds_.size());
}

void TimerProcessor::Stop()
{
    // 만드는 역순으로 멈춘다. Stop() 뒤에는 새 만기가 예약되지 않으므로, 이 다음에 레인을
    // 세우면 이미 들어온 만기까지만 소진되고 끝난다.
    if (fanoutFlushTimer_)
    {
        fanoutFlushTimer_->Stop();
        fanoutFlushTimer_.reset();
    }
    if (tickTimer_)
    {
        tickTimer_->Stop();
        tickTimer_.reset();
    }
}

void TimerProcessor::OnTimerTick(const Pipeline::OwnerId& /*owner*/)
{
    ++tickCount_;

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::duration<float>>(now - lastTickAt_);
    lastTickAt_ = now;

    const ZoneTickBody body{elapsed.count(), NowUt()};

    // **담당 존마다 하나씩, 그 존의 틱 주인으로 넣는다.** 존이 여럿이어도 여기서 갈라지고,
    // 받는 쪽은 레인이 서로 달라 서로를 기다리지 않는다.
    for (const auto zoneId : zoneIds_)
    {
        const auto target = Ids().ZoneTarget(zoneId);
        Pipeline::PushMsg<Pipeline::EProducerType::Tick>(
            EZoneMsg::ZoneTick, target.tick, target.tickOwner, body);
    }
}

void TimerProcessor::OnTimerFanoutFlush(const Pipeline::OwnerId& /*owner*/)
{
    for (const auto zoneId : zoneIds_)
    {
        const auto target = Ids().ZoneTarget(zoneId);
        Pipeline::PushMsg<Pipeline::EProducerType::Broadcast>(
            EZoneMsg::FanoutFlush, target.broadcast, target.broadcastOwner);
    }
}
