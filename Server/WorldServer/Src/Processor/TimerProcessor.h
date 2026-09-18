#pragma once

#include "Processor/WorldMsg.h"

#include "Server/Core/Src/Pipeline/MessageProcessor.h"
#include "Server/Core/Src/Timer/RepeatingTimer.h"

// TIMER 레인에서 도는 프로세서. 주기 작업의 **만기만** 맡는다.
//
// **이 레인의 규약 -- 여기서 일을 하지 않는다.**
// 스레드가 하나뿐이라 무거운 작업을 여기서 하면 그동안 다른 주기 작업 전부가 밀린다. 그래서
// 만기가 오면 판정만 하고, 실제 작업은 **그 작업의 주인으로 원래 레인에 다시 넣는다**
// (예: 만료 스윕이면 BASIC 레인에 playerId를 주인으로). 그 되돌리는 코드가 아직 없는 것은
// 붙일 주기 작업이 아직 없어서지, 여기서 해도 된다는 뜻이 아니다.
//
// **왜 타이머를 TIMER 레인의 executor에 거는가**: 만기 감시 자체가 이 레인이 맡은 일이다.
// 소켓 쪽 컨텍스트에 걸면 수신 처리와 박자가 같은 스레드를 다투게 되고, 그러면 "레인을
// 나눈다"가 이름뿐이 된다.
//
// **수명**: 타이머가 this를 캡처하므로 Stop()이 App 종료 경로에서, **레인을 세우기 전에**
// 불려야 한다(안 그러면 이미 닫히는 레인에 만기가 들어간다).
class TimerProcessor final : public Pipeline::MessageProcessor
{
public:
    TimerProcessor() = default;
    ~TimerProcessor() override;

    [[nodiscard]] std::string_view Name() const override { return "Timer"; }
    void RegistHandler() override;

    // TIMER 레인의 executor에 두 타이머를 건다. **레인이 Start된 뒤에 부른다.**
    // **만기 감시는 아직 I/O 스레드에서 한다.** 노션은 TIMER 레인이 스스로 만기를 보지만,
    // 레인 백엔드가 큐일 때는 그 자리에 타이머를 걸 곳이 없다(condvar 의 시한 대기로
    // 옮기는 것이 다음 단계다). 만기 뒤의 일은 지금도 TIMER 레인에서 돈다.
    void Start(asio::io_context& timerContext);

    // 타이머를 멈춘다. 여러 번 불려도 안전하다(소멸자도 부른다).
    void Stop();

private:
    // 만기 뒤 실제로 도는 몸통. 둘 다 TIMER 레인에서 실행된다.
    void OnShortTick(const Pipeline::OwnerId& owner);
    void OnLongTick(const Pipeline::OwnerId& owner);

    // 아직 붙은 주기 작업이 없어 간격은 여기 상수로 둔다. 쓰임이 정해지면 그때 config로
    // 올린다 -- 값 하나를 두 군데에 적지 않으려는 것이다.
    static constexpr std::chrono::minutes kShortInterval{5};
    static constexpr std::chrono::hours kLongInterval{1};

    // Start()에서 만들어진다. TIMER 레인이 없으면 둘 다 비어 있고 아무 일도 하지 않는다.
    std::unique_ptr<Timer::RepeatingTimer> shortTimer_;
    std::unique_ptr<Timer::RepeatingTimer> longTimer_;

    // 만기 횟수. 레인이 밀리면 이 수가 벽시계와 어긋나므로 RepeatingTimer::BypassCount()와
    // 나란히 보면 박자를 못 지킨 것이 바로 보인다.
    uint64_t shortTickCount_{0};
    uint64_t longTickCount_{0};
};
