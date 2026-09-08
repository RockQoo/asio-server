#include "Tool/StressClient/Src/pch.h"
#include "Tool/StressClient/Src/App/StressRunner.h"

#include <cstdlib>
#include <exception>
#include <string>
#include <utility>

int main(const int argc, char** argv)
{
    Log::Logger::Instance().Initialize("logs/stressclient.log", Log::ELogLevel::Info);

    try
    {
        // 인자: host port sessionCount cyclesPerSession rampUpPerSecond stallThresholdSec maxDurationSec
        // 전부 생략 가능(기본값 사용). 예: StressClient.exe 127.0.0.1 9000 10000 200 500 15 300
        Stress::StressConfig config{};
        if (argc > 1) { config.host = argv[1]; }
        if (argc > 2) { config.port = static_cast<uint16_t>(std::stoi(argv[2])); }
        if (argc > 3) { config.sessionCount = static_cast<size_t>(std::stoull(argv[3])); }
        if (argc > 4) { config.cyclesPerSession = static_cast<uint32_t>(std::stoul(argv[4])); }
        if (argc > 5) { config.rampUpPerSecond = static_cast<size_t>(std::stoull(argv[5])); }
        if (argc > 6) { config.stallThreshold = std::chrono::seconds(std::stoll(argv[6])); }
        if (argc > 7) { config.maxDuration = std::chrono::seconds(std::stoll(argv[7])); }

        Stress::StressRunner runner(std::move(config));
        runner.Run();
    }
    catch (const std::exception& ex)
    {
        LOG.Error(ELogCategory::General, "치명적 오류").KV("Message", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
