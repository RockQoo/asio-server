#include "Server/WorldServer/Src/pch.h"
#include "Server/WorldServer/Src/App/WorldServerApp.h"
#include "Server/WorldServer/Src/Db/DbConnection.h"
#include "Server/WorldServer/Src/Db/PasswordHash.h"

#include "Shared/Core/Src/Common/UniqueId.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Protocol/Src/PacketId.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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
            connection.Execute({World::DbCommand{"dbo.players_select", {std::string("tester1")}}},
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
                std::cout << "[dbcheck] FAIL: players_select의 결과 컬럼 형태가 예상과 다릅니다.\n";
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

    // `WorldServer.exe --idtest <노드번호> <스레드수> <스레드당개수> [random]`
    //
    // UniqueIdGenerator가 다중 스레드 경합에서도 중복 없는 id를 만드는지, 그리고 그 id가
    // 클러스터 인덱스에 순차 삽입되는지 확인한다.
    //
    // **프로세스를 여러 개 띄워 쓴다.** 한 프로세스에서 노드 번호를 바꿔가며 흉내 내지 않는
    // 이유는 생성기가 프로세스당 하나인 싱글턴이기 때문이고, 무엇보다 **실제 사고(노드 번호가
    // 겹치는 배포 실수)는 프로세스 사이에서 나기 때문**이다. 같은 DB 테이블에 부으면 그
    // 겹침이 PK 위반으로 즉시 드러난다.
    [[nodiscard]] int32_t RunIdTest(const std::string& connectionString, const uint32_t nodeId,
                                    const size_t threadCount, const size_t perThread,
                                    const bool randomMode)
    {
        Common::UniqueIdGenerator::Instance().Initialize(nodeId);

        const size_t total = threadCount * perThread;
        std::cout << "[idtest] node=" << nodeId << " threads=" << threadCount
                  << " perThread=" << perThread << " total=" << total
                  << (randomMode ? " (대조군: 무작위 키)" : "") << "\n";

        // **생성과 삽입을 청크 단위로 번갈아 한다.** 전부 만들어 놓고 나중에 몰아서 넣으면
        // 타임스탬프가 좁은 구간에 압축돼(실측: 500만 개가 238ms 안에) "시간순 삽입"이
        // 성립하지 않는다 -- 여러 프로세스가 같은 키 구간에 동시에 꽂는 모양이 되어 페이지
        // 분할이 폭발한다(실측 단편화 98%). 실제 서버는 id를 만들자마자 쓰므로 타임스탬프가
        // 삽입 진행과 함께 앞으로 나아간다. 그 흐름을 그대로 재현한다.
        constexpr size_t kChunkPerThread = 1000;

        std::vector<int64_t> all;
        all.reserve(total);

        uint64_t generateUs = 0;
        uint64_t insertUs = 0;

        try
        {
            World::DbConnection connection(connectionString);
            std::vector<std::vector<int64_t>> chunks(threadCount);

            for (size_t done = 0; done < perThread; done += kChunkPerThread)
            {
                const size_t thisChunk = (perThread - done < kChunkPerThread) ? perThread - done
                                                                              : kChunkPerThread;

                const auto generateStartedAt = std::chrono::steady_clock::now();

                std::vector<std::thread> workers;
                workers.reserve(threadCount);
                for (size_t index = 0; index < threadCount; ++index)
                {
                    workers.emplace_back([&chunks, index, thisChunk, done, randomMode, nodeId]
                    {
                        // 대조군은 무작위 키다. 시간순 키와 단편화를 비교하려면 "키 순서"만
                        // 다르고 나머지 조건은 같아야 하므로 같은 경로로 같은 개수를 만든다.
                        //
                        // **시드에 nodeId가 반드시 들어가야 한다.** 빠뜨리면 프로세스마다
                        // 같은 난수열이 나와서 서로 중복된다(실제로 한 번 겪었다 -- PK가
                        // 그 중복을 잡아줬으니, 탐지 장치가 작동한다는 확인은 덤으로 됐다).
                        std::mt19937_64 randomEngine(
                            (static_cast<uint64_t>(nodeId) << 48) ^ (static_cast<uint64_t>(index) << 32)
                            ^ static_cast<uint64_t>(done) ^ 0x9E3779B97F4A7C15ull);
                        std::uniform_int_distribution<int64_t> distribution(
                            1, std::numeric_limits<int64_t>::max());

                        auto& bucket = chunks[index];
                        bucket.clear();
                        bucket.reserve(thisChunk);
                        for (size_t count = 0; count < thisChunk; ++count)
                        {
                            bucket.push_back(randomMode
                                ? distribution(randomEngine)
                                : Common::UniqueIdGenerator::Instance().Next());
                        }
                    });
                }

                for (auto& worker : workers)
                {
                    worker.join();
                }

                const auto generatedAt = std::chrono::steady_clock::now();
                generateUs += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                    generatedAt - generateStartedAt).count());

                // **생성 순서대로 넣어야 한다.** 스레드별 버킷을 통째로 이어 붙여 넣으면
                // 버킷1의 첫 id가 버킷0의 마지막 id보다 작아서, 시간순 키인데도 인덱스 중간에
                // 꽂는 삽입이 된다(실측: 단일 노드인데도 단편화 93%). 실제 서버는 id를 만든
                // 쪽이 바로 쓰므로 도착 순서가 곧 생성 순서다 -- 라운드 안에서 정렬해 그 흐름을
                // 재현한다. 무작위 키(대조군)는 정렬해도 어차피 순서가 없어 영향이 없다.
                std::vector<int64_t> round;
                round.reserve(threadCount * thisChunk);
                for (const auto& bucket : chunks)
                {
                    round.insert(round.end(), bucket.begin(), bucket.end());
                }
                std::sort(round.begin(), round.end());

                connection.ExecuteMany(randomMode ? "dbo.id_test_random_insert" : "dbo.id_test_insert",
                                       round);
                all.insert(all.end(), round.begin(), round.end());

                insertUs += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - generatedAt).count());
            }
        }
        catch (const World::DbException& ex)
        {
            // 중복 키면 여기로 온다 -- 그게 이 테스트가 잡으려는 실패다.
            std::cout << "[idtest] DB 삽입 실패: " << ex.what()
                      << " (SQLSTATE=" << ex.SqlState() << ")\n";
            return EXIT_FAILURE;
        }

        // 프로세스 안에서도 중복을 본다. DB PK가 이미 막지만, 여기서 걸리면 "어느 프로세스가
        // 만든 것끼리 겹쳤다"가 바로 드러나 원인 추적이 빠르다.
        std::sort(all.begin(), all.end());
        const bool hasDuplicate = std::adjacent_find(all.begin(), all.end()) != all.end();
        const size_t uniqueCount = static_cast<size_t>(std::unique(all.begin(), all.end()) - all.begin());

        const auto perSecond = [](const size_t count, const uint64_t micros)
        {
            return micros > 0 ? count * 1000000 / micros : count;
        };

        std::cout << "[idtest] 생성 " << generateUs / 1000 << "ms (" << perSecond(total, generateUs)
                  << "/초), 고유 " << uniqueCount << " / " << total
                  << (hasDuplicate ? "  메모리 중복검사 FAIL" : "  메모리 중복검사 PASS") << "\n";
        std::cout << "[idtest] DB 삽입 " << insertUs / 1000 << "ms ("
                  << perSecond(total, insertUs) << "/초)\n";

        return hasDuplicate ? EXIT_FAILURE : EXIT_SUCCESS;
    }
}

int main(const int argc, char* argv[])
{
    // Debug 레벨은 기본적으로 끈다 -- UnitOfWork 태스크 로그처럼 요청량에 비례해 늘어나는
    // 항목이 있어(HandleUnitOfWorkStream 참고), 평소 실행에서는 Info부터만 남긴다. 상세히
    // 봐야 할 때만 ELogLevel::Debug로 바꿔서 실행할 것.
    Log::Logger::Instance().Initialize("logs/worldserver.log", Log::ELogLevel::Info);

    // 노드 번호는 World 대역(1~99)의 첫 번호를 쓴다. World를 여러 대로 늘리면 2, 3...으로
    // 주면 되고, Zone 대역(100~)과 겹치지 않는다(UniqueId.h의 대역표 참고).
    //
    // 지금 World가 발급하는 id는 로그인 때 만드는 playerId 정도지만, 존이 실어 보낸 값을
    // 그대로 쓰는 경로(UnitOfWork)와 섞이므로 노드 번호는 처음부터 제대로 잡아둔다.
    Common::UniqueIdGenerator::Instance().Initialize(Common::kNodeIdWorldBegin);

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

        // UniqueId 검증 모드. 인자가 모자라면 사용법만 찍고 끝낸다.
        if (argc > 1 && std::string(argv[1]) == "--idtest")
        {
            if (argc < 5)
            {
                std::cout << "사용법: WorldServer.exe --idtest <노드번호> <스레드수> <스레드당개수> [random]\n";
                return EXIT_FAILURE;
            }

            const auto nodeId = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10));
            const auto threadCount = static_cast<size_t>(std::strtoull(argv[3], nullptr, 10));
            const auto perThread = static_cast<size_t>(std::strtoull(argv[4], nullptr, 10));
            const bool randomMode = (argc > 5 && std::string(argv[5]) == "random");

            if (threadCount == 0 || perThread == 0)
            {
                std::cout << "[idtest] 스레드 수와 개수는 1 이상이어야 합니다.\n";
                return EXIT_FAILURE;
            }

            return RunIdTest(config.dbConnectionString, nodeId, threadCount, perThread, randomMode);
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
