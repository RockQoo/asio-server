#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/App/ZoneServerApp.h"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // "0,1" 같은 콤마 구분 zoneId 목록을 파싱해 ZoneDef 목록으로 만든다. 각 존은 10칸 폭으로
    // 나란히 붙어 있다고 가정한다(zoneId 0 -> x:[0,10), 1 -> x:[10,20), ...). 한
    // 프로세스가 zoneId 여러 개를 동시에 호스팅할 수 있다는 걸 보여주는 게 목적이라, 실행 인자
    // 하나로 "이 프로세스가 담당할 존 목록"을 그대로 config로 넘긴다.
    std::vector<Zone::ZoneDef> ParseZoneList(const std::string& zoneIdListArg)
    {
        std::vector<Zone::ZoneDef> zones;
        std::stringstream ss(zoneIdListArg);
        std::string token;
        while (std::getline(ss, token, ','))
        {
            if (token.empty())
            {
                continue;
            }

            const auto zoneId = static_cast<uint32_t>(std::stoul(token));
            Zone::ZoneDef def{};
            def.zoneId = zoneId;
            def.xMin = static_cast<float>(zoneId) * 10.0f;
            def.xMax = def.xMin + 10.0f;
            zones.push_back(def);
        }
        return zones;
    }
}

int main(const int argc, char* argv[])
{
    // 실행 인자로 이 프로세스가 담당할 zoneId 목록을 콤마로 구분해서 받는다(예: "0,1"). 인자가
    // 없으면 기본값 "0" 하나만 담당한다.
    const std::string zoneIdListArg = argc > 1 ? argv[1] : "0";
    const auto zones = ParseZoneList(zoneIdListArg);

    // 로그 파일명은 이 프로세스가 담당하는 zoneId 목록으로 구분한다(예: zoneserver-0-1.log).
    std::string zoneIdSuffix;
    for (const auto& def : zones)
    {
        if (!zoneIdSuffix.empty())
        {
            zoneIdSuffix += "-";
        }
        zoneIdSuffix += std::to_string(def.zoneId);
    }
    Log::Logger::Instance().Initialize("logs/zoneserver-" + zoneIdSuffix + ".log");

    try
    {
        Zone::ZoneServerConfig config{};
        config.zones = zones;
        // 아래 스레드 풀 크기는 전부 존 개수와 무관하게 설정 가능하다 -- 실서비스라면 훨씬
        // 크게 잡겠지만 여기선 학습용으로 작게 잡는다.
        config.lbThreadCount = 2;
        config.poolSizes.basicThreadCount = 2;
        config.poolSizes.tickThreadCount = 2;
        config.poolSizes.broadcastThreadCount = 2;
        config.worldHost = "127.0.0.1";
        config.worldPort = 9200;
        config.ioThreadCount = 2;
        config.tickInterval = std::chrono::milliseconds(100);
        config.mailSweepInterval = std::chrono::milliseconds(1000);

        Zone::ZoneServerApp app(std::move(config));
        app.Run();
    }
    catch (const std::exception& ex)
    {
        LOG.Error(ELogCategory::General, "치명적 오류").KV("Message", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
