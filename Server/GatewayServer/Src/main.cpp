#include "pch.h"
#include "App/GatewayApp.h"

#include "App/GatewayConfig.h"

int main(const int argc, char** argv)
{
    Log::Logger::Instance().Initialize("logs/gateway_server.log");

    try
    {
        // 설정 파일 경로는 인자로 덮을 수 있다 -- 같은 실행 파일로 다른 설정을 띄울 때 쓴다.
        auto config = LoadConfig(argc > 1 ? argv[1] : "config/gateway.cfg");

        GatewayApp app(std::move(config));
        app.Run();
    }
    catch (const std::exception& ex)
    {
        LOG.Error(ELogCategory::General, "치명적 오류").KV("Message", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
