#include "pch.h"
#include "Cli/IdTest.h"

#include "Db/DbConnection.h"

#include "Shared/Core/Src/Common/RUID.h"

namespace World
{

    // `WorldServer.exe --idtest <노드번호> <스레드수> <스레드당개수> [random]`
    //
    // RUIDGenerator가 다중 스레드 경합에서도 중복 없는 id를 만드는지, 그리고 그 id가
    // 클러스터 인덱스에 순차 삽입되는지 확인한다.
    //
    // **프로세스를 여러 개 띄워 쓴다.** 한 프로세스에서 노드 번호를 바꿔가며 흉내 내지 않는
    // 이유는 생성기가 프로세스당 하나인 싱글턴이기 때문이고, 무엇보다 **실제 사고(노드 번호가
    // 겹치는 배포 실수)는 프로세스 사이에서 나기 때문**이다. 같은 DB 테이블에 부으면 그
    // 겹침이 PK 위반으로 즉시 드러난다.
    int32_t RunIdTest(const std::string& connectionString, const uint32_t nodeId,
                                    const size_t threadCount, const size_t perThread,
                                    const bool randomMode)
    {
        Common::RUIDGenerator::Instance().Initialize(nodeId);

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

        // 이 노드가 남긴 행 수. 여러 프로세스가 같은 테이블에 동시에 넣으므로 전체 COUNT(*)로는
        // 어느 프로세스 몫인지 가릴 수 없어, id의 노드 비트로 걸러서 센다.
        const auto countRowsForNode = [&connectionString, nodeId]() -> std::optional<int64_t>
        {
            try
            {
                DbConnection connection(connectionString);
                DbResult result;
                connection.Execute({DbCommand{"dbo.usp_unique_keys_count_by_node",
                                                     {static_cast<int64_t>(nodeId)}}},
                                   false, &result);
                const auto* const row = FirstRow(result);
                return row != nullptr ? GetInt64(*row, 0) : std::nullopt;
            }
            catch (const DbException& ex)
            {
                std::cout << "[idtest] 행 수 조회 실패: " << ex.what() << "\n";
                return std::nullopt;
            }
        };

        const auto beforeCount = randomMode ? std::nullopt : countRowsForNode();

        try
        {
            DbConnection connection(connectionString);
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
                                : Common::RUIDGenerator::Instance().Next());
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

                connection.ExecuteMany(randomMode ? "dbo.usp_unique_keys_random_insert" : "dbo.usp_unique_keys_insert",
                                       round);
                all.insert(all.end(), round.begin(), round.end());

                insertUs += static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - generatedAt).count());
            }
        }
        catch (const DbException& ex)
        {
            // 연결이 끊겼거나 SP 자체를 못 부른 경우다.
            //
            // **중복 키는 더 이상 여기로 오지 않는다.** SP가 규약대로 CATCH를 갖게 되면서
            // 오류가 RETURN 코드로 바뀌는데, 배치 경로(ExecuteMany)는 그 값을 읽지 못한다.
            // 그래서 중복 검증은 아래의 "행 수 대조"가 맡는다.
            std::cout << "[idtest] DB 삽입 실패: " << ex.what()
                      << " (SQLSTATE=" << ex.SqlState() << ")\n";
            return EXIT_FAILURE;
        }

        const auto afterCount = randomMode ? std::nullopt : countRowsForNode();

        // 넣은 개수와 실제로 늘어난 행 수를 대조한다 -- 중복이 조용히 건너뛰어졌다면 여기서 갈린다.
        //
        // **절대값이 아니라 증가분을 본다.** 테이블을 비우지 않고 다시 돌리면 이전 실행이 남긴
        // 행이 그대로 있어서, 절대값으로 비교하면 멀쩡한 실행이 실패로 뒤집힌다.
        //
        // 난수 대조군은 id에 노드 비트가 없어 어느 프로세스 몫인지 가릴 수 없으므로 건너뛴다
        // (대조군의 목적은 단편화 비교이고, 63비트 난수 500만 개의 충돌 확률은 무시할 수준이다).
        bool rowCountMatched = true;
        if (!randomMode)
        {
            const auto inserted = (afterCount && beforeCount) ? *afterCount - *beforeCount : -1;
            rowCountMatched = (inserted == static_cast<int64_t>(total));

            std::cout << "[idtest] 행 수 대조 " << inserted << " / " << total
                      << (rowCountMatched ? "  PASS" : "  FAIL(중복이 건너뛰어졌다)") << "\n";
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

        return (hasDuplicate || !rowCountMatched) ? EXIT_FAILURE : EXIT_SUCCESS;
    }
}
