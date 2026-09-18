#pragma once

#include "Processor/ZoneMsg.h"

#include "Server/Core/Src/Pipeline/MessageProcessor.h"
#include "Server/Core/Src/Timer/RepeatingTimer.h"
#include "Server/Common/Src/Ids.h"

// **TIMER 레인**의 처리기. 주기 작업의 **만기만** 맡는다. 프로세스에 하나다.
//
// **이 레인의 규약 -- 여기서 일을 하지 않는다.**
// 스레드가 하나뿐이라 무거운 작업을 여기서 하면 그동안 다른 주기 작업 전부가 밀린다.
// 만기가 오면 판정만 하고, 실제 작업은 **그 작업의 주인으로 원래 레인에 다시 넣는다** --
// 틱은 그 존의 TICK 으로, 팬아웃 비우기는 그 존의 BROADCAST 로.
//
// **만기 감시는 asio steady_timer 가 하고, 만기 뒤의 일만 이 레인이 한다.** 시한 대기를
// 레인 안에 직접 구현하지 않는 것은 asio 가 이미 그 일을 하기 때문이다.
//
// **수명**: 타이머가 this 를 캡처하므로 Stop() 이 App 종료 경로에서, **레인을 세우기
// 전에** 불려야 한다(안 그러면 이미 닫히는 레인에 만기가 들어간다).
class TimerProcessor final : public Pipeline::MessageProcessor
{
public:
    TimerProcessor(std::vector<Common::ZoneId> zoneIds,
                   const std::chrono::milliseconds tickInterval,
                   const std::chrono::milliseconds fanoutFlushInterval);
    ~TimerProcessor() override;

    [[nodiscard]] std::string_view Name() const override { return "Timer"; }
    void RegistHandler() override;

    // 타이머를 건다. **레인이 Start 된 뒤에 부른다.**
    void Start(asio::io_context& timerContext);

    // 여러 번 불려도 안전하다(소멸자도 부른다).
    void Stop();

private:
    void OnTimerTick(const Pipeline::OwnerId& owner);
    void OnTimerFanoutFlush(const Pipeline::OwnerId& owner);

    const std::vector<Common::ZoneId> zoneIds_;
    const std::chrono::milliseconds tickInterval_;
    const std::chrono::milliseconds fanoutFlushInterval_;

    std::unique_ptr<Timer::RepeatingTimer> tickTimer_;
    std::unique_ptr<Timer::RepeatingTimer> fanoutFlushTimer_;

    // 직전 만기 시각. **벽시계가 아니라 이 값의 차이를 틱 간격으로 쓴다** -- 레인이 밀리면
    // 실제 간격이 설정값보다 길어지는데, 설정값을 그대로 쓰면 그만큼 세계가 느려진다.
    std::chrono::steady_clock::time_point lastTickAt_{};

    uint64_t tickCount_{0};
};
