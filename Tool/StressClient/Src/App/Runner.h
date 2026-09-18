#pragma once

#include "Server/Core/Src/Network/IoContextPool.h"
#include "Server/Core/Src/Timer/RepeatingTimer.h"
#include "Session/Session.h"
#include "Stats/Stats.h"

namespace Stress
{
    struct Config
    {
        std::string host{"127.0.0.1"};
        uint16_t port{9000};
        size_t sessionCount{1000};
        uint32_t cyclesPerSession{200};
        size_t rampUpPerSecond{500};
        // 이동을 보내는 세션 수. 이동 부하를 재려면 sessionCount 와 같게 준다.
        size_t broadcasterCount{50};

        // 이동 하나를 보내는 간격. 실제 클라이언트는 초당 여러 번 보내므로, 이동을 재는
        // 회차에서는 이 값을 짧게 준다.
        std::chrono::milliseconds moveInterval{2500};

        // 봇이 존 경계를 넘어 월드 전체를 돌아다니는가. 핸드오프까지 같이 재는 회차에서 켠다.
        bool roamWorld{false};
        std::chrono::seconds stallThreshold{15};
        std::chrono::seconds maxDuration{300};
        size_t ioThreadCount{0};  // 0 -> std::thread::hardware_concurrency()

        // 세션마다 다른 계정으로 로그인한다. 없는 계정은 서버가 로그인 때 만든다.
        // **접두사를 고정하는 이유**는 테스트로 생긴 계정만 나중에 골라 지우기 위해서다.
        // 1 회차 실행에는 자동 가입 쓰기가 섞이므로, 순수 로그인 수치는 2 회차부터다.
        std::string accountPrefix{"stress_"};
        std::string accountPassword{"0000"};
    };

    // 전체 부하 테스트를 조립하고 실행한다: 세션 N개를 ramp-up으로 접속시키고, 워치독으로
    // 정체(데드락 의심) 세션을 감지하고, 종료 시(전부 완료 또는 maxDuration 도달) 요약을
    // 만든다.
    class Runner
    {
    public:
        explicit Runner(Config config);

        // 블로킹 호출 -- 전 세션이 목표 사이클을 마치거나 maxDuration에 도달하면 반환한다.
        void Run();

        [[nodiscard]] const Stats& GetStats() const noexcept { return stats_; }

    private:
        void RampUpSessions();
        void StartWatchdog();
        [[nodiscard]] bool AllDone() const;
        void PrintSummary(const std::chrono::steady_clock::time_point startedAt) const;

        Config config_;
        Network::IoContextPool ioPool_;
        Stats stats_;
        std::vector<std::unique_ptr<Session>> sessions_;
        std::unique_ptr<Timer::RepeatingTimer> watchdogTimer_;
    };
}
