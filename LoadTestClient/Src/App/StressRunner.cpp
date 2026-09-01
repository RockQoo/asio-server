#include "LoadTestClient/Src/pch.h"
#include "LoadTestClient/Src/App/StressRunner.h"

#include <algorithm>
#include <format>
#include <iostream>
#include <thread>

namespace Load
{
    namespace
    {
        [[nodiscard]] size_t ResolveIoThreadCount(const size_t configured)
        {
            if (configured != 0)
            {
                return configured;
            }
            const auto hw = std::thread::hardware_concurrency();
            return hw != 0 ? static_cast<size_t>(hw) : 4;
        }
    }

    StressRunner::StressRunner(StressConfig config)
        : config_(std::move(config))
        , ioPool_(ResolveIoThreadCount(config_.ioThreadCount))
    {
        sessions_.reserve(config_.sessionCount);
        const auto broadcasterCount = std::min(config_.broadcasterCount, config_.sessionCount);
        for (size_t i = 0; i < config_.sessionCount; ++i)
        {
            const bool isBroadcaster = i < broadcasterCount;
            sessions_.push_back(std::make_unique<StressSession>(
                i, ioPool_.Next(), config_.host, config_.port, config_.cyclesPerSession, isBroadcaster, stats_));
        }
    }

    void StressRunner::Run()
    {
        const auto startedAt = std::chrono::steady_clock::now();

        LOG.Info(ELogCategory::General, "부하 테스트 시작")
            .KV("SessionCount", config_.sessionCount).KV("CyclesPerSession", config_.cyclesPerSession)
            .KV("RampUpPerSecond", config_.rampUpPerSecond).KV("IoThreads", ioPool_.Size());

        ioPool_.Run();
        RampUpSessions();
        StartWatchdog();

        auto lastPrint = startedAt;
        while (!AllDone() && std::chrono::steady_clock::now() - startedAt < config_.maxDuration)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            const auto now = std::chrono::steady_clock::now();
            if (now - lastPrint >= std::chrono::seconds(5))
            {
                lastPrint = now;
                std::cout << "[LoadTestClient] 진행 중... 접속=" << stats_.Connected()
                          << "/" << config_.sessionCount << ", 완료=" << stats_.Done()
                          << ", 사이클=" << stats_.CyclesCompleted()
                          << ", 불일치=" << stats_.MismatchTotalCount()
                          << ", 스톨=" << stats_.StalledSessions().size() << '\n';
            }
        }

        if (watchdogTimer_)
        {
            watchdogTimer_->Stop();
        }
        for (auto& session : sessions_)
        {
            session->Stop();
        }
        ioPool_.Stop();
        ioPool_.Join();

        PrintSummary(startedAt);
    }

    void StressRunner::RampUpSessions()
    {
        // 한 번에 다 접속시키지 않고 초당 rampUpPerSecond개씩 나눠 접속한다 -- accept 큐가
        // 순간적으로 몰리는 걸 줄인다. Connector 자체는 실패해도 알아서 재시도하므로 완전
        // 실패는 아니지만, 관측(로그/통계)을 깨끗하게 하려는 목적이 크다.
        const auto batchSize = std::max<size_t>(1, config_.rampUpPerSecond / 10);
        for (size_t i = 0; i < sessions_.size(); ++i)
        {
            sessions_[i]->Start();
            if ((i + 1) % batchSize == 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    void StressRunner::StartWatchdog()
    {
        watchdogTimer_ = std::make_unique<Timer::RepeatingTimer>(ioPool_.At(0));
        watchdogTimer_->Start(std::chrono::milliseconds(5000), [this]
        {
            const auto now = std::chrono::steady_clock::now();
            for (const auto& session : sessions_)
            {
                if (session->IsDone())
                {
                    continue;
                }

                if (now - session->LastProgressAt() > config_.stallThreshold)
                {
                    stats_.RecordStalled(static_cast<Network::SessionId>(session->Index()));
                }
                else
                {
                    stats_.ClearStalled(static_cast<Network::SessionId>(session->Index()));
                }
            }
        });
    }

    bool StressRunner::AllDone() const
    {
        for (const auto& session : sessions_)
        {
            if (!session->IsDone())
            {
                return false;
            }
        }
        return true;
    }

    void StressRunner::PrintSummary(const std::chrono::steady_clock::time_point startedAt) const
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startedAt);
        const auto elapsedSec = std::max(0.001, elapsed.count() / 1000.0);
        const auto throughput = static_cast<double>(stats_.CyclesCompleted()) / elapsedSec;

        const auto broadcastExpected = stats_.BroadcastExpectedTotal();
        const auto broadcastReceived = stats_.BroadcastReceivedTotal();
        const auto broadcastRatio = broadcastExpected == 0
            ? 1.0
            : static_cast<double>(broadcastReceived) / static_cast<double>(broadcastExpected);

        const auto stalled = stats_.StalledSessions();
        const auto mismatches = stats_.Mismatches();

        // 백분위는 마이크로초로 모아 밀리초로 환산해 보여준다 -- 사람이 "몇 ms 걸리나"로
        // 읽는 지표라서, 리포트 단위는 ms로 통일한다.
        const auto toMs = [](const uint64_t us) { return static_cast<double>(us) / 1000.0; };

        // 최상위 버킷에 걸린 백분위는 "정확히 100초"가 아니라 "100초 이상"이다 -- 그 구분을
        // 지우면 과부하 상황에서 얼마나 나쁜지를 읽을 수 없으므로 접두사로 드러낸다.
        // 반대로 평균/최대는 버킷을 거치지 않은 실측값이라 접두사를 붙이면 안 된다(정확한
        // 값을 근사값처럼 보이게 만든다).
        const auto pct = [](const uint64_t us)
        {
            return std::format("{}{:.1f}ms", us >= LatencyHistogram::kOverflowUs ? ">=" : "", us / 1000.0);
        };
        const auto exact = [](const uint64_t us) { return std::format("{:.1f}ms", us / 1000.0); };
        const auto printLatency = [&pct, &exact](const char* const label, const LatencyHistogram& histogram)
        {
            if (histogram.Count() == 0)
            {
                std::cout << label << ": 샘플 없음\n";
                return;
            }
            std::cout << label << ": 표본=" << histogram.Count()
                      << ", 평균=" << exact(static_cast<uint64_t>(histogram.AverageUs()))
                      << ", P50=" << pct(histogram.PercentileUs(0.50))
                      << ", P95=" << pct(histogram.PercentileUs(0.95))
                      << ", P99=" << pct(histogram.PercentileUs(0.99))
                      << ", P99.9=" << pct(histogram.PercentileUs(0.999))
                      << ", 최대=" << exact(histogram.MaxUs()) << '\n';
        };
        const auto logLatency = [&toMs](const char* const name, const LatencyHistogram& histogram)
        {
            // 콘솔과 달리 로그는 기계 파싱 대상이라 ">=" 같은 접두사를 값에 섞을 수 없다.
            // 대신 "이 백분위가 추적 상한에 걸렸는가"를 별도 플래그로 남겨야, 나중에 로그만
            // 보고도 100초로 잘린 값인지 실제 100초인지 구분할 수 있다.
            const auto p99 = histogram.PercentileUs(0.99);
            const auto p999 = histogram.PercentileUs(0.999);
            LOG.Info(ELogCategory::General, "지연 백분위").KV("Metric", name)
                .KV("Samples", histogram.Count())
                .KV("AvgMs", histogram.AverageUs() / 1000.0)
                .KV("P50Ms", toMs(histogram.PercentileUs(0.50)))
                .KV("P95Ms", toMs(histogram.PercentileUs(0.95)))
                .KV("P99Ms", toMs(p99))
                .KV("P999Ms", toMs(p999))
                .KV("MaxMs", toMs(histogram.MaxUs()))
                .KV("Clipped", p99 >= LatencyHistogram::kOverflowUs || p999 >= LatencyHistogram::kOverflowUs);
        };

        LOG.Info(ELogCategory::General, "부하 테스트 종료 요약")
            .KV("Attempted", stats_.Attempted()).KV("Connected", stats_.Connected()).KV("Done", stats_.Done())
            .KV("MailAddSent", stats_.MailAddSent()).KV("MailAddAcked", stats_.MailAddAcked())
            .KV("MailDelSent", stats_.MailDelSent()).KV("MailDelAcked", stats_.MailDelAcked())
            .KV("CyclesCompleted", stats_.CyclesCompleted()).KV("MismatchTotal", stats_.MismatchTotalCount())
            .KV("StalledCount", stalled.size()).KV("BroadcastSent", stats_.BroadcastSentCount())
            .KV("BroadcastExpectedTotal", broadcastExpected).KV("BroadcastReceivedTotal", broadcastReceived)
            .KV("BroadcastDeliveryRatio", broadcastRatio).KV("ElapsedSec", elapsedSec).KV("CyclesPerSec", throughput);

        logLatency("MailAddRtt", stats_.MailAddRtt());
        logLatency("MailDelRtt", stats_.MailDelRtt());
        logLatency("CycleTotal", stats_.CycleRtt());

        std::cout << "\n========== LoadTestClient 결과 요약 ==========\n"
                  << "시도/접속/완료 세션: " << stats_.Attempted() << " / " << stats_.Connected()
                  << " / " << stats_.Done() << '\n'
                  << "Mail Add 송신/Ack: " << stats_.MailAddSent() << " / " << stats_.MailAddAcked() << '\n'
                  << "Mail Del 송신/Ack: " << stats_.MailDelSent() << " / " << stats_.MailDelAcked() << '\n'
                  << "완료된 사이클 수: " << stats_.CyclesCompleted() << '\n'
                  << "불일치(id 안 맞음/삭제 실패) 총 건수: " << stats_.MismatchTotalCount()
                  << " (샘플 " << mismatches.size() << "건 기록)\n"
                  << "스톨(데드락 의심) 세션 수: " << stalled.size() << '\n'
                  << "브로드캐스트 전송 횟수: " << stats_.BroadcastSentCount() << '\n'
                  << "브로드캐스트 기대/실제 수신 합계: " << broadcastExpected << " / " << broadcastReceived
                  << " (비율 " << broadcastRatio << ")\n"
                  << "소요 시간: " << elapsedSec << "초, 처리량: " << throughput << " 사이클/초\n"
                  << "---------- 지연(왕복 RTT, 클라이언트 체감) ----------\n";
        printLatency("MailAdd->Ack ", stats_.MailAddRtt());
        printLatency("MailDel->Ack ", stats_.MailDelRtt());
        printLatency("사이클 전체  ", stats_.CycleRtt());
        std::cout << "===============================================\n";

        for (const auto& mismatch : mismatches)
        {
            LOG.Warning(ELogCategory::Session, "불일치 상세")
                .KV("SessionIndex", mismatch.sessionId).KV("Cycle", mismatch.cycleIndex)
                .KV("ExpectedMailId", mismatch.expectedMailId).KV("ActualMailId", mismatch.actualMailId)
                .KV("Reason", mismatch.reason);
        }
        for (const auto stalledIndex : stalled)
        {
            LOG.Warning(ELogCategory::Session, "스톨 세션").KV("SessionIndex", stalledIndex);
        }
    }
}
