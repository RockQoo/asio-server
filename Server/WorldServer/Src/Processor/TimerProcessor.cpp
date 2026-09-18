#include "pch.h"
#include "Processor/TimerProcessor.h"

#include "Processor/ProcessorIds.h"
#include "Server/Core/Src/Pipeline/ProducerHolder.h"

TimerProcessor::~TimerProcessor()
{
    Stop();
}

void TimerProcessor::RegistHandler()
{
    Regist(EWorldMsg::TimerShortTick, &TimerProcessor::OnShortTick);
    Regist(EWorldMsg::TimerLongTick,  &TimerProcessor::OnLongTick);
}

void TimerProcessor::Start()
{
    auto* const timerProducer = Pipeline::GetProducer<Pipeline::EProducerType::Timer>();
    if (timerProducer == nullptr || !timerProducer->IsRunning())
    {
        LOG.Warning(ELogCategory::General, "TIMER 레인이 없어 주기 작업을 걸지 않는다");
        return;
    }

    // 두 타이머 모두 TIMER 레인의 executor에서 만기된다 -- 만기 감시가 이 레인의 일이다.
    // 만기 자체는 통계를 남기지 않으므로, 만기 핸들러는 다시 PushMsg로 이 레인에 넣는다
    // (그래야 다른 메시지와 같은 취급을 받는다).
    shortTimer_ = std::make_unique<Timer::RepeatingTimer>(timerProducer->Context().get_executor());
    shortTimer_->Start(std::chrono::duration_cast<std::chrono::milliseconds>(kShortInterval),
        []
        {
            Pipeline::PushMsg<Pipeline::EProducerType::Timer>(
                EWorldMsg::TimerShortTick, Ids().timer, Pipeline::OwnerId{kGlobalQueryKey});
        });

    longTimer_ = std::make_unique<Timer::RepeatingTimer>(timerProducer->Context().get_executor());
    longTimer_->Start(std::chrono::duration_cast<std::chrono::milliseconds>(kLongInterval),
        []
        {
            Pipeline::PushMsg<Pipeline::EProducerType::Timer>(
                EWorldMsg::TimerLongTick, Ids().timer, Pipeline::OwnerId{kGlobalQueryKey});
        });

    LOG.Info(ELogCategory::General, "주기 작업 타이머 시작")
        .KV("ShortMinutes", kShortInterval.count())
        .KV("LongMinutes", std::chrono::duration_cast<std::chrono::minutes>(kLongInterval).count());
}

void TimerProcessor::Stop()
{
    // 만드는 역순으로 멈춘다. Stop() 뒤에는 새 만기가 예약되지 않으므로, 이 다음에 TIMER
    // 레인을 세우면 이미 들어온 만기까지만 소진되고 끝난다.
    if (longTimer_)
    {
        longTimer_->Stop();
        longTimer_.reset();
    }
    if (shortTimer_)
    {
        shortTimer_->Stop();
        shortTimer_.reset();
    }
}

void TimerProcessor::OnShortTick(const Pipeline::OwnerId& /*owner*/)
{
    ++shortTickCount_;

    // 아직 붙은 작업이 없다. 붙일 때는 **여기서 처리하지 말고** 그 작업의 주인으로 원래
    // 레인에 넣는다 -- 예: PushMsg<Basic>(msgId, Ids().main, OwnerId(playerId), body).
    LOG.Info(ELogCategory::General, "5분 주기 만기")
        .KV("Count", shortTickCount_)
        .KV("Bypass", shortTimer_ ? shortTimer_->BypassCount() : 0);
}

void TimerProcessor::OnLongTick(const Pipeline::OwnerId& /*owner*/)
{
    ++longTickCount_;

    LOG.Info(ELogCategory::General, "1시간 주기 만기")
        .KV("Count", longTickCount_)
        .KV("Bypass", longTimer_ ? longTimer_->BypassCount() : 0);
}
