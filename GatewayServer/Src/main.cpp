#include "GatewayServer/Src/pch.h"
#include "GatewayServer/Src/App/GatewayServerApp.h"

#include <cstdlib>
#include <exception>
#include <utility>

int main()
{
    Log::Logger::Instance().Initialize("logs/gatewayserver.log");

    try
    {
        // 클라이언트 포트 9000(기존 ZoneServer가 쓰던 포트를 그대로 물려받음), World는
        // 기본적으로 같은 머신의 9100 포트에서 대기한다고 가정한다.
        Gateway::GatewayServerConfig config{};
        config.clientPort = 9000;
        config.worldHost = "127.0.0.1";
        config.worldPort = 9100;
        config.ioThreadCount = 2;

        Gateway::GatewayServerApp app(std::move(config));
        app.Run();
    }
    catch (const std::exception& ex)
    {
        LOG.Error(ELogCategory::General, "치명적 오류").KV("Message", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
