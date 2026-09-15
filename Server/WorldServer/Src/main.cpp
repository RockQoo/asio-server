#include "pch.h"
#include "App/App.h"

#include "App/Config.h"
#include "Cli/DbCheck.h"
#include "Cli/IdTest.h"

#include "Shared/Core/Src/Common/RUID.h"

int main(const int argc, char* argv[])
{
    // Debug 레벨은 기본적으로 끈다 -- UnitOfWork 태스크 로그처럼 요청량에 비례해 늘어나는
    // 항목이 있어(HandleUnitOfWorkStream 참고), 평소 실행에서는 Info부터만 남긴다. 상세히
    // 봐야 할 때만 ELogLevel::Debug로 바꿔서 실행할 것.
    Log::Logger::Instance().Initialize("logs/world_server.log", Log::ELogLevel::Info);

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

        // 노드 번호는 World 대역(1~99)의 첫 번호를 쓴다. World를 여러 대로 늘리면 2, 3...으로
        // 주면 되고, Zone 대역(100~)과 겹치지 않는다(RUID.h의 대역표 참고).
        //
        // 지금 World가 발급하는 id는 로그인 때 만드는 playerId 정도지만, 존이 실어 보낸 값을
        // 그대로 쓰는 경로(UnitOfWork)와 섞이므로 노드 번호는 처음부터 제대로 잡아둔다.
        //
        // **--idtest 분기보다 뒤에 있어야 한다.** 그 모드는 인자로 받은 노드 번호로 자기가
        // 초기화하는데, 여기서 먼저 잡아버리면 두 번째 호출이 되어 Ruid::Init이 중단시킨다.
        Common::Ruid::Init(Common::kNodeIdWorldBegin);

        World::App app(std::move(config));
        app.Run();
    }
    catch (const std::exception& ex)
    {
        LOG.Error(ELogCategory::General, "치명적 오류").KV("Message", ex.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
