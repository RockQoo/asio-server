#include "Server/GatewayServer/Src/pch.h"
#include "Server/GatewayServer/Src/App/App.h"

#include "Server/GatewayServer/Src/App/Config.h"

#include <cstdlib>
#include <exception>
#include <string>
#include <utility>

int main(const int argc, char** argv)
{
    Log::Logger::Instance().Initialize("logs/gateway_server.log");

    try
    {
        // 설정 파일 경로는 인자로 덮을 수 있다 -- 같은 실행 파일로 다른 설정을 띄울 때 쓴다.
        auto config = Gateway::LoadConfig(argc > 1 ? argv[1] : "config/gateway.cfg");

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
