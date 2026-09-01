#include "Core/Src/pch.h"
#include "Core/Src/Timer/RepeatingTimer.h"

#include <system_error>

namespace Timer
{
    RepeatingTimer::RepeatingTimer(asio::io_context& ioContext)
        : timer_(ioContext)
    {
    }

    void RepeatingTimer::Start(const std::chrono::milliseconds interval, Callback callback)
    {
        interval_ = interval;
        callback_ = std::move(callback);
        running_ = true;
        nextTick_ = std::chrono::steady_clock::now() + interval_;
        ScheduleNext();
    }

    void RepeatingTimer::Stop()
    {
        running_ = false;

        // ASIO_NO_DEPRECATED 설정 때문에 error_code를 받는 cancel() 오버로드가 제거되어
        // 예외를 던지는 버전만 남았다. 타이머가 이미 취소/만료된 상태여도 문제없이 진행되도록 감싼다.
        try
        {
            timer_.cancel();
        }
        catch (const std::system_error&)
        {
        }
    }

    void RepeatingTimer::ScheduleNext()
    {
        timer_.expires_at(nextTick_);
        timer_.async_wait([this](const std::error_code ec)
        {
            if (ec || !running_.load())
            {
                return;
            }

            if (callback_)
            {
                callback_();
            }

            nextTick_ += interval_;

            // 너무 많이 밀렸다면(예: 긴 GC/디버그 정지) catch-up tick을 몰아서 쏘는 대신
            // 현재 시각 기준으로 다시 맞춘다.
            const auto now = std::chrono::steady_clock::now();
            if (now > nextTick_ + interval_ * 5)
            {
                nextTick_ = now + interval_;
            }

            ScheduleNext();
        });
    }
}
