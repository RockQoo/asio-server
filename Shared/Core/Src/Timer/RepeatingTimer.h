#pragma once

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <functional>

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

    private:
        void ScheduleNext();

        asio::steady_timer timer_;
        std::chrono::milliseconds interval_{};
        std::chrono::steady_clock::time_point nextTick_{};
        Callback callback_;
        std::atomic<bool> running_{false};
    };
}
