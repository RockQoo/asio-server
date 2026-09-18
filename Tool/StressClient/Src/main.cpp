#include "pch.h"
#include "App/Runner.h"

int main(const int argc, char** argv)
{
    Log::Logger::Instance().Initialize("logs/stress_client.log", Log::ELogLevel::Info);

    try
    {
        // 인자: host port sessionCount cyclesPerSession rampUpPerSecond stallThresholdSec
        //       maxDurationSec broadcasterCount moveIntervalMs
        // 전부 생략 가능(기본값 사용). 예: StressClient.exe 127.0.0.1 9000 10000 200 500 15 300
        //
        // **이동만 재려면 cyclesPerSession 을 0 으로** 준다 -- 우편을 안 보내므로 세션이
        // 스스로 끝나지 않고 maxDurationSec 이 끝을 정한다. 그때 broadcasterCount 를
        // sessionCount 와 같게 주면 전원이 이동을 보낸다.
        //   예(전원 이동, 100ms 간격, 60초): StressClient.exe 127.0.0.1 9000 500 0 500 15 60 500 100
        Stress::Config config{};
        if (argc > 1) { config.host = argv[1]; }
        if (argc > 2) { config.port = static_cast<uint16_t>(std::stoi(argv[2])); }
        if (argc > 3) { config.sessionCount = static_cast<size_t>(std::stoull(argv[3])); }
        if (argc > 4) { config.cyclesPerSession = static_cast<uint32_t>(std::stoul(argv[4])); }
        if (argc > 5) { config.rampUpPerSecond = static_cast<size_t>(std::stoull(argv[5])); }
        if (argc > 6) { config.stallThreshold = std::chrono::seconds(std::stoll(argv[6])); }
        if (argc > 7) { config.maxDuration = std::chrono::seconds(std::stoll(argv[7])); }
        if (argc > 8) { config.broadcasterCount = static_cast<size_t>(std::stoull(argv[8])); }
        if (argc > 9) { config.moveInterval = std::chrono::milliseconds(std::stoll(argv[9])); }
        if (argc > 10) { config.roamWorld = std::stoi(argv[10]) != 0; }

        Stress::Runner runner(std::move(config));
        runner.Run();
    }
    catch (const std::exception& ex)
    {
        LOG.Error(ELogCategory::General, "치명적 오류").KV("Message", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
