#include "Server/WorldServer/Src/pch.h"
#include "Server/WorldServer/Src/App/App.h"

#include "Server/WorldServer/Src/App/Config.h"
#include "Server/WorldServer/Src/Cli/ConsoleLoop.h"
#include "Server/WorldServer/Src/Cli/DbCheck.h"
#include "Server/WorldServer/Src/Cli/IdTest.h"

#include "Shared/Core/Src/Common/RUID.h"

#include <atomic>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

int main(const int argc, char* argv[])
{
    // Debug 레벨은 기본적으로 끈다 -- UnitOfWork 태스크 로그처럼 요청량에 비례해 늘어나는
    // 항목이 있어(HandleUnitOfWorkStream 참고), 평소 실행에서는 Info부터만 남긴다. 상세히
    // 봐야 할 때만 ELogLevel::Debug로 바꿔서 실행할 것.
    Log::Logger::Instance().Initialize("logs/world_server.log", Log::ELogLevel::Info);

    // 노드 번호는 World 대역(1~99)의 첫 번호를 쓴다. World를 여러 대로 늘리면 2, 3...으로
    // 주면 되고, Zone 대역(100~)과 겹치지 않는다(RUID.h의 대역표 참고).
    //
    // 지금 World가 발급하는 id는 로그인 때 만드는 playerId 정도지만, 존이 실어 보낸 값을
    // 그대로 쓰는 경로(UnitOfWork)와 섞이므로 노드 번호는 처음부터 제대로 잡아둔다.
    Common::RUIDGenerator::Instance().Initialize(Common::kNodeIdWorldBegin);

    try
    {
        // argv[1] 이 --dbcheck/--idtest 같은 모드 스위치일 수 있으므로 -- 로 시작하면 건너뛴다.
        const bool hasConfigArg = argc > 1 && std::string_view(argv[1]).substr(0, 2) != "--";
        auto config = World::LoadConfig(hasConfigArg ? argv[1] : "config/world.cfg");

        // 서버를 띄우지 않고 DB 연결만 확인하는 모드.
        if (argc > 1 && std::string_view(argv[1]) == "--dbcheck")
        {
            return World::RunDbCheck(config.dbConnectionString);
        }

        // RUID 검증 모드. 인자가 모자라면 사용법만 찍고 끝낸다.
        if (argc > 1 && std::string_view(argv[1]) == "--idtest")
        {
            if (argc < 5)
            {
                std::cout << "사용법: WorldServer.exe --idtest <노드번호> <스레드수> <스레드당개수> [random]\n";
                return EXIT_FAILURE;
            }

            const auto nodeId = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10));
            const auto threadCount = static_cast<size_t>(std::strtoull(argv[3], nullptr, 10));
            const auto perThread = static_cast<size_t>(std::strtoull(argv[4], nullptr, 10));
            const bool randomMode = (argc > 5 && std::string_view(argv[5]) == "random");

            if (threadCount == 0 || perThread == 0)
            {
                std::cout << "[idtest] 스레드 수와 개수는 1 이상이어야 합니다.\n";
                return EXIT_FAILURE;
            }

            return World::RunIdTest(config.dbConnectionString, nodeId, threadCount, perThread, randomMode);
        }

        World::App app(std::move(config));

        // 콘솔 REPL은 표준 입력에서 막히므로 별도 스레드다. detach 해도 되는 이유는 app.Run()이
        // 끝나면 프로세스가 곧 종료되고, 이 스레드가 붙잡고 있는 자원이 없기 때문이다.
        std::atomic<bool> running{true};
        std::thread consoleThread(World::RunConsoleLoop, std::ref(app), std::ref(running));
        consoleThread.detach();

        app.Run();
    }
    catch (const std::exception& ex)
    {
        LOG.Error(ELogCategory::General, "치명적 오류").KV("Message", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
