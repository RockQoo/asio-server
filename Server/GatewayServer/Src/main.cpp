#include "Server/GatewayServer/Src/pch.h"
#include "Server/GatewayServer/Src/App/App.h"

#include "Shared/Core/Src/Common/ConfigFile.h"

#include <cstdlib>
#include <exception>
#include <string>
#include <utility>

int main(const int argc, char** argv)
{
    Log::Logger::Instance().Initialize("logs/gatewayserver.log");

    try
    {
        // 설정 파일 경로는 인자로 덮을 수 있다 -- 같은 실행 파일로 다른 설정을 띄울 때 쓴다.
        const std::string configPath = argc > 1 ? argv[1] : "config/gateway.cfg";
        const auto configFile = Common::ConfigFile::Load(configPath);
        if (!configFile.IsLoaded())
        {
            LOG.Warning(ELogCategory::General, "설정 파일이 없어 기본값으로 뜬다").KV("Path", configPath);
        }

        // 구조체의 기본값을 fallback 으로 넘긴다 -- 기본값이 두 군데(구조체와 여기)에 적히면
        // 한쪽만 고쳤을 때 갈린다.
        Gateway::Config config{};
        config.clientPort = configFile.GetPort("client_port", config.clientPort);
        config.worldHost = configFile.GetString("world_host", config.worldHost);
        config.worldPort = configFile.GetPort("world_port", config.worldPort);
        config.ioThreadCount = configFile.GetSize("io_threads", config.ioThreadCount);
        configFile.WarnUnusedKeys();

        Gateway::App app(std::move(config));
        app.Run();
    }
    catch (const std::exception& ex)
    {
        LOG.Error(ELogCategory::General, "치명적 오류").KV("Message", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
