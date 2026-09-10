#include "Server/WorldServer/Src/pch.h"
#include "Server/WorldServer/Src/App/WorldServerApp.h"
#include "Server/WorldServer/Src/Db/DbConnection.h"
#include "Server/WorldServer/Src/Db/PasswordHash.h"

#include "Shared/Core/Src/Common/RequestId.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Protocol/Src/PacketId.h"

#include <atomic>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

namespace
{
    // 콘솔에서 "notice <메시지>"를 입력하면 WorldServer가 Zone을 거치지 않고 접속 중인 모든
    // 클라이언트에게 직접 브로드캐스트한다 -- World가 클라이언트 레지스트리를 직접 들고 있어서
    // 가능한 구조를 수동으로 확인하기 위한 REPL이다.
    void ConsoleLoop(World::WorldServerApp& app, std::atomic<bool>& running)
    {
        std::string line;
        while (running.load() && std::getline(std::cin, line))
        {
            std::istringstream iss(line);
            std::string command;
            iss >> command;

            if (command == "notice")
            {
                std::string message;
                std::getline(iss, message);
                if (!message.empty() && message.front() == ' ')
                {
                    message.erase(0, 1);
                }

                Packet::BinaryWriter writer;
                writer.WriteString(message);
                app.BroadcastToAll(PacketId::W2CNotice, writer.GetBuffer());
                std::cout << "[notice 전송] " << message << "\n";
            }
            else if (command == "quit" || command == "exit")
            {
                running.store(false);
                app.Stop();
                break;
            }
        }
    }

    // 환경 변수 하나를 읽어 값이 있으면 돌려준다. std::getenv는 /sdl 아래에서 C4996으로
    // 걸리므로 getenv_s를 쓴다. 성공 시 length는 널 종단 문자를 포함하므로 값이 있으면 2 이상이다.
    [[nodiscard]] std::optional<std::string> ReadEnv(const char* name)
    {
        char buffer[1024]{};
        size_t length = 0;
        if (getenv_s(&length, buffer, sizeof(buffer), name) == 0 && length > 1)
        {
            return std::string(buffer);
        }
        return std::nullopt;
    }

    // `WorldServer.exe --dbcheck` 로 실행하면 서버를 띄우지 않고 DB 연결만 확인하고 끝낸다.
    //
    // **왜 필요한가**: DB가 붙는 경로는 로그인 -> World 캐시 -> UnitOfWork 반영으로 이어져서,
    // 뭔가 안 되면 "ODBC가 문제인지 / 드라이버 이름이 틀렸는지 / 비밀번호 해시 형식이 다른지"를
    // 서버 로그에서 가려내기 어렵다. 그 세 가지만 따로 떼어 확인한다.
    [[nodiscard]] int32_t RunDbCheck(const std::string& connectionString)
    {
        std::cout << "[dbcheck] 연결 문자열: " << connectionString << "\n";

        try
        {
            World::DbConnection connection(connectionString);

            World::DbResult result;
            connection.Execute({World::DbCommand{"dbo.player_login_select", {std::string("tester1")}}},
                               false, &result);

            if (result.empty())
            {
                std::cout << "[dbcheck] FAIL: tester1 계정이 없습니다. bat\\setup_game_db.bat 을 먼저 실행하세요.\n";
                return EXIT_FAILURE;
            }

            const auto playerId = World::GetInt64(result[0], 0);
            const auto storedHash = World::GetString(result[0], 2);
            if (!playerId || !storedHash)
            {
                std::cout << "[dbcheck] FAIL: player_login_select의 결과 컬럼 형태가 예상과 다릅니다.\n";
                return EXIT_FAILURE;
            }

            std::cout << "[dbcheck] 조회 OK  playerId=" << *playerId << "\n";

            // 시드의 해시는 PowerShell(.NET Rfc2898DeriveBytes)로 만들었다. 여기서 통과한다는 건
            // CNG 구현이 .NET과 같은 값을 낸다는 뜻이라, 두 구현의 교차 검증이기도 하다.
            const bool correct = World::VerifyPassword("0000", *storedHash);
            const bool wrong = World::VerifyPassword("wrong-password", *storedHash);

            std::cout << "[dbcheck] 비밀번호 정답 검증: " << (correct ? "PASS" : "FAIL") << "\n";
            std::cout << "[dbcheck] 오답 거부 검증:   " << (!wrong ? "PASS" : "FAIL") << "\n";

            return (correct && !wrong) ? EXIT_SUCCESS : EXIT_FAILURE;
        }
        catch (const World::DbException& ex)
        {
            std::cout << "[dbcheck] FAIL: " << ex.what() << " (SQLSTATE=" << ex.SqlState() << ")\n";
            return EXIT_FAILURE;
        }
    }
}

int main(const int argc, char* argv[])
{
    // Debug 레벨은 기본적으로 끈다 -- UnitOfWork 태스크 로그처럼 요청량에 비례해 늘어나는
    // 항목이 있어(HandleUnitOfWorkStream 참고), 평소 실행에서는 Info부터만 남긴다. 상세히
    // 봐야 할 때만 ELogLevel::Debug로 바꿔서 실행할 것.
    Log::Logger::Instance().Initialize("logs/worldserver.log", Log::ELogLevel::Info);

    // World는 RequestId를 스스로 발급하지 않고 Zone이 실어 보낸 값을 그대로 쓰지만, 나중에
    // World가 만드는 변경(운영툴 명령 등)이 생길 자리를 미리 잡아둔다. 0번은 zoneId가
    // 1부터 시작하므로 어느 Zone 프로세스와도 겹치지 않는다.
    Common::RequestIdGenerator::Instance().Initialize(0);

    try
    {
        World::WorldServerConfig config{};
        config.gatewayPort = 9100;
        config.zonePort = 9200;
        config.toolPort = 9300;
        config.ioThreadCount = 2;
        // BASIC은 Main/Tool 프로세서가 공유하는 큐 그룹이고, 실제 병렬도는 스레드 수가 아니라
        // **서로 다른 ownerId의 개수**로 정해진다(대부분 clientSessionId라 충분히 많다).
        config.basicThreadCount = 8;
        // DB는 커넥션 풀 크기와 1:1이 원칙이다. 실제 DB 연동 전이라 개발 머신 기준 임시값.
        config.dbThreadCount = 4;

        // 시크릿과 DB 연결 문자열은 소스에 박힌 개발 기본값(WorldServerConfig)을 쓰되, 환경
        // 변수가 있으면 그걸 우선한다 -- 공개 저장소에 실제 값을 커밋하지 않기 위한 최소 장치다.
        // 운영툴 쪽도 같은 이름의 환경 변수(또는 appsettings)를 읽으므로 둘을 같이 바꿔야 한다.
        if (const auto secret = ReadEnv("ASIO_SERVER_TOOL_SECRET"))
        {
            config.toolSharedSecret = *secret;
            LOG.Info(ELogCategory::General, "운영툴 시크릿을 환경 변수에서 로드");
        }

        if (const auto connectionString = ReadEnv("ASIO_SERVER_DB_CONN"))
        {
            config.dbConnectionString = *connectionString;
            LOG.Info(ELogCategory::General, "DB 연결 문자열을 환경 변수에서 로드");
        }

        // 서버를 띄우지 않고 DB 연결만 확인하는 모드. 실패해도 서버 기동에는 영향이 없다.
        if (argc > 1 && std::string(argv[1]) == "--dbcheck")
        {
            return RunDbCheck(config.dbConnectionString);
        }

        World::WorldServerApp app(std::move(config));

        std::atomic<bool> running{true};
        std::thread consoleThread(ConsoleLoop, std::ref(app), std::ref(running));
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
