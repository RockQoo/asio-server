#pragma once

#include <asio.hpp>

namespace Timer
{
    // 드리프트를 보정하는 고정 간격 타이머 (매번 "지금 + interval"이 아니라 다음 "절대 시각"을
    // 예약한다). io_context가 구동한다. 존(zone)의 tick 박자를 I/O 스레드에서 맞춰주되,
    // 실제 tick 작업 자체는 해당 존의 로직 스레드에서 실행되도록 할 때 쓴다.
    class RepeatingTimer
    {
    public:
        using Callback = std::function<void()>;

        explicit RepeatingTimer(asio::io_context& ioContext);

        RepeatingTimer(const RepeatingTimer&) = delete;
        RepeatingTimer& operator=(const RepeatingTimer&) = delete;

        void Start(const std::chrono::milliseconds interval, Callback callback);
        void Stop();

        [[nodiscard]] bool IsRunning() const noexcept { return running_.load(); }

        // 밀려서 건너뛴 주기의 누적 횟수. 0이 아니면 그 타이머가 붙은 레인이 주기를 못 지키고
        // 있다는 뜻이라, 틱 기반 콘텐츠(전투 등)의 이상을 그 콘텐츠 탓으로 오해하기 전에
        // 먼저 볼 값이다.
        [[nodiscard]] int64_t BypassCount() const noexcept { return bypassCount_.load(); }

    private:
        void ScheduleNext();

        // 이 배수만큼 늦으면 그 주기를 통째로 건너뛴다. 밀린 만큼 몰아서 쏘면 처리량이 더
        // 밀리는 악순환이 되므로, 따라잡지 않고 현재 시각으로 기준점을 리셋한다.
        static constexpr int32_t kBypassFactor = 5;

        asio::steady_timer timer_;
        std::chrono::milliseconds interval_{};
        std::chrono::steady_clock::time_point nextTick_{};
        Callback callback_;
        std::atomic<bool> running_{false};

        // 완료 핸들러는 한 번에 하나만 돌고(다음 예약은 그 안에서 한다) 다른 스레드가 읽지
        // 않으므로 bypassing_은 평범한 bool이면 된다. 누적 횟수만 밖에서 읽을 수 있게 atomic.
        bool bypassing_{false};
        std::atomic<int64_t> bypassCount_{0};
    };
}
