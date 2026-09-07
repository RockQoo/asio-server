#pragma once

#include "Shared/Core/Src/Network/IoContextPool.h"
#include "Shared/Core/Src/Timer/RepeatingTimer.h"
#include "Tool/StressClient/Src/Session/StressSession.h"
#include "Tool/StressClient/Src/Stats/StressStats.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Stress
{
    struct StressConfig
    {
        std::string host{"127.0.0.1"};
        uint16_t port{9000};
        size_t sessionCount{1000};
        uint32_t cyclesPerSession{200};
        size_t rampUpPerSecond{500};
        size_t broadcasterCount{50};
        std::chrono::seconds stallThreshold{15};
        std::chrono::seconds maxDuration{300};
        size_t ioThreadCount{0};  // 0 -> std::thread::hardware_concurrency()
    };

    // 전체 부하 테스트를 조립하고 실행한다: 세션 N개를 ramp-up으로 접속시키고, 워치독으로
    // 정체(데드락 의심) 세션을 감지하고, 종료 시(전부 완료 또는 maxDuration 도달) 요약을
    // 만든다.
    class StressRunner
    {
    public:
        explicit StressRunner(StressConfig config);

        // 블로킹 호출 -- 전 세션이 목표 사이클을 마치거나 maxDuration에 도달하면 반환한다.
        void Run();

        [[nodiscard]] const StressStats& Stats() const noexcept { return stats_; }

    private:
        void RampUpSessions();
        void StartWatchdog();
        [[nodiscard]] bool AllDone() const;
        void PrintSummary(const std::chrono::steady_clock::time_point startedAt) const;

        StressConfig config_;
        Network::IoContextPool ioPool_;
        StressStats stats_;
        std::vector<std::unique_ptr<StressSession>> sessions_;
        std::unique_ptr<Timer::RepeatingTimer> watchdogTimer_;
    };
}
