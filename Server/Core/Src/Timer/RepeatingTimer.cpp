#include "pch.h"
#include "Server/Core/Src/Timer/RepeatingTimer.h"

namespace Timer
{
    RepeatingTimer::RepeatingTimer(asio::io_context& ioContext)
        : timer_(ioContext)
    {
    }

    RepeatingTimer::RepeatingTimer(asio::any_io_executor executor)
        : timer_(std::move(executor))
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

            // **밀림 판정을 콜백보다 먼저 한다.** 이미 한참 늦은 주기를 굳이 실행하면 다음
            // 주기가 더 밀린다 -- 따라잡지 않고 이번 회차를 통째로 버리는 것이 요점이다.
            const auto now = std::chrono::steady_clock::now();
            if (now > nextTick_ + interval_ * kBypassFactor)
            {
                bypassCount_.fetch_add(1, std::memory_order_relaxed);

                // 밀려 있는 동안은 매 주기가 여기로 들어오므로, 들어온 순간에만 남긴다.
                // 매번 찍으면 정작 원인을 봐야 할 때 로그가 밀림 경고로 덮인다.
                if (!bypassing_)
                {
                    bypassing_ = true;
                    LOG.Warning(ELogCategory::General, "타이머가 밀려 주기를 건너뛴다")
                        .KV("LateMs", std::chrono::duration_cast<std::chrono::milliseconds>(
                                          now - nextTick_).count())
                        .KV("IntervalMs", interval_.count())
                        .KV("BypassCount", bypassCount_.load(std::memory_order_relaxed));
                }

                // 기준점을 현재로 리셋한다. 안 하면 과거 시각으로 계속 예약해 틱이 폭주한다.
                nextTick_ = now + interval_;
                ScheduleNext();
                return;
            }

            if (bypassing_)
            {
                bypassing_ = false;
                LOG.Info(ELogCategory::General, "타이머 주기가 정상으로 돌아왔다")
                    .KV("IntervalMs", interval_.count())
                    .KV("BypassCount", bypassCount_.load(std::memory_order_relaxed));
            }

            if (callback_)
            {
                callback_();
            }

            nextTick_ += interval_;
            ScheduleNext();
        });
    }
}
