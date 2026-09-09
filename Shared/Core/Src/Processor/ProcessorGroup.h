#pragma once

#include "Shared/Core/Src/Processor/ProcessorStats.h"
#include "Shared/Core/Src/Thread/WorkerThread.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Processor
{
    // 그룹에 담을 수 있는 프로세서 id enum의 요건. 통계를 프로세서별 배열 칸으로 잡아야 해서
    // 마지막 원소로 개수를 알려주는 `Count`를 요구한다(ELogCategory처럼 콘텐츠 계층이 각자
    // 자기 enum을 정의한다 -- Core는 어떤 프로세서가 있는지 몰라야 한다).
    // 통계를 프로세서별 배열 칸으로 잡아야 해서 마지막 원소로 개수를 알려주는 `Count`를,
    // 덤프에 이름을 찍으려고 같은 네임스페이스의 ADL `ToString()`을 요구한다
    // (Log::LogCategoryType과 같은 방식 -- Core는 어떤 프로세서가 있는지 몰라야 한다).
    template <typename T>
    concept ProcessorId = std::is_enum_v<T> && requires (T value)
    {
        T::Count;
        { ToString(value) } -> std::convertible_to<std::string_view>;
    };

    // 큐 그룹 하나 = 소비자 스레드 N개. 메시지는 세 가지를 갖는다:
    //
    //   ownerId     -- **스레드를 결정한다**(ownerId % N). 같은 주인의 일은 언제나 같은
    //                  스레드에서 순서대로 처리되므로, 그 주인의 데이터에는 락이 필요 없다.
    //   processorId -- **스레드 배정에 관여하지 않는다.** 그 스레드에서 누구의 핸들러를
    //                  부르는지를 나타내는 태그이고, 계측과 로그에서 작업 종류를 가르는 데
    //                  쓴다. 그래서 한 그룹 안에 여러 프로세서(Main/Tool/...)가 스레드를
    //                  공유하며 공존한다.
    //   내용        -- 프로세스 내부 전달이라 **직렬화하지 않는다.** 호출 가능 객체를 타입
    //                  그대로 옮긴다(직렬화는 프로세스 경계, 즉 패킷에서만 한다).
    //
    // **ownerId를 무엇으로 할지는 호출부가 정한다.** 기준은 "이 메시지가 건드릴 데이터의
    // 주인"이다 -- 세션 작업이면 sessionId, 존 공간 상태를 만지면 zoneId, 길드 동기화면
    // guildId. 그래서 이 클래스는 ownerId의 의미를 모르고 uint64_t로만 받는다.
    //
    // **주의 -- 한 메시지가 주인이 다른 데이터를 함께 만지면 이 보호가 깨진다.** 그때는
    // ownerId 어피니티(1층) 대신 모델 단위 읽기/쓰기 락(Thread::Mutexed, 2층)이 필요하다.
    // 어피니티는 "대부분의 경합을 없애는" 장치이지 "모든 경합을 없애는" 장치가 아니다.
    template <ProcessorId TProcessorId>
    class ProcessorGroup
    {
    public:
        static constexpr size_t kProcessorCount = static_cast<size_t>(TProcessorId::Count);

        // slowWarnThreshold를 0으로 두면 느린 작업 경고를 끈다.
        ProcessorGroup(std::string name, const size_t threadCount,
                       const std::chrono::microseconds slowWarnThreshold = std::chrono::microseconds{0})
            : name_(std::move(name))
            , slowWarnUs_(static_cast<uint64_t>(slowWarnThreshold.count()))
        {
            lanes_.reserve(threadCount);
            for (size_t index = 0; index < threadCount; ++index)
            {
                lanes_.push_back(std::make_unique<Lane>(name_ + "#" + std::to_string(index)));
            }
        }

        ProcessorGroup(const ProcessorGroup&) = delete;
        ProcessorGroup& operator=(const ProcessorGroup&) = delete;

        void Start()
        {
            for (const auto& lane : lanes_)
            {
                lane->worker.Start();
            }
        }

        void Stop()
        {
            for (const auto& lane : lanes_)
            {
                lane->worker.Stop();
            }
        }

        // 메시지 하나를 그룹에 넣는다. 어느 스레드에서든 호출할 수 있다.
        template <typename F>
        void Post(const TProcessorId processorId, const uint64_t ownerId, F&& work)
        {
            auto& lane = LaneOf(ownerId);

            // 큐에 넣은 시각을 함께 실어 보낸다 -- 실행 시작 시각과의 차이가 곧 대기 시간이고,
            // 그게 "스레드가 부족한가, 로직이 무거운가"를 가르는 유일한 지표다.
            lane.worker.PostTask([this, &lane, processorId, enqueuedAt = Clock::now(),
                                  work = std::forward<F>(work)]() mutable
            {
                const auto startedAt = Clock::now();
                work();
                const auto finishedAt = Clock::now();

                const auto waitUs = ElapsedUs(enqueuedAt, startedAt);
                const auto workUs = ElapsedUs(startedAt, finishedAt);
                lane.stats[static_cast<size_t>(processorId)].Record(waitUs, workUs);

                if (slowWarnUs_ > 0 && workUs > slowWarnUs_)
                {
                    // 이 헤더는 여러 프로젝트(Core/WorldServer/ZoneServer/...)에서 include되고,
                    // 프로젝트마다 자기 ELogCategory를 따로 정의한다(cpp-patterns.md 참고) --
                    // 전부가 공통으로 갖는 General이어야 어디서 include되든 컴파일된다
                    // (Thread::Mutexed가 같은 이유로 General을 쓴다. WorldServer에서는
                    // `ELogCategory::Thread`가 아예 Thread 네임스페이스로 해석돼 버린다).
                    LOG.Warning(ELogCategory::General, "처리 시간이 임계치를 넘은 작업")
                        .KV("Group", name_).KV("Lane", lane.worker.Name())
                        .KV("Processor", static_cast<uint32_t>(processorId))
                        .KV("WorkUs", workUs).KV("WaitUs", waitUs);
                }
            });
        }

        [[nodiscard]] size_t ThreadCount() const noexcept { return lanes_.size(); }
        [[nodiscard]] const std::string& Name() const noexcept { return name_; }

        // 지금 이 스레드에 몇 개가 밀려 있는지. 스레드별로 봐야 편중(같은 ownerId에 몰림)이
        // 보이므로 합계가 아니라 인덱스별로 노출한다.
        [[nodiscard]] size_t PendingCount(const size_t laneIndex) const
        {
            return lanes_[laneIndex]->worker.QueueSize();
        }

        // 프로세서 하나의 통계를 전 스레드에 걸쳐 합친 값.
        [[nodiscard]] ProcessorStatSnapshot Snapshot(const TProcessorId processorId) const
        {
            ProcessorStatSnapshot total{};
            for (const auto& lane : lanes_)
            {
                total += ProcessorStatSnapshot::From(lane->stats[static_cast<size_t>(processorId)]);
            }
            return total;
        }

        // 스레드 하나 안에서 프로세서 하나의 통계. 어피니티가 실제로 고르게 퍼지는지
        // (= ownerId 종류가 충분한지) 확인할 때 쓴다.
        [[nodiscard]] ProcessorStatSnapshot Snapshot(const size_t laneIndex, const TProcessorId processorId) const
        {
            return ProcessorStatSnapshot::From(lanes_[laneIndex]->stats[static_cast<size_t>(processorId)]);
        }

        // 주기적으로 불러 지금 상태를 로그로 남긴다. **대기 시간이 이 덤프의 핵심**이다:
        //   대기가 길고 CPU가 남는다  -> 스레드 부족. 늘리면 나아진다
        //   대기가 길고 CPU가 꽉 찼다 -> 스레드를 늘려도 무의미. 로직이나 구조를 고쳐야 한다
        // 처리 시간만 봐서는 이 둘이 구분되지 않는다.
        //
        // 스레드별로도 찍는 이유는 **편중**을 보려는 것이다 -- ownerId 종류가 적으면 한
        // 스레드만 뜨겁고 나머지는 논다. 그게 이 프로젝트가 겪은 병목의 모양이었다.
        void LogStats() const
        {
            for (size_t processorIndex = 0; processorIndex < kProcessorCount; ++processorIndex)
            {
                const auto processorId = static_cast<TProcessorId>(processorIndex);
                const auto total = Snapshot(processorId);
                if (total.IsEmpty())
                {
                    continue;
                }

                LOG.Info(ELogCategory::General, "프로세서 통계")
                    .KV("Group", name_).KV("Processor", ToString(processorId))
                    .KV("Calls", total.callCount)
                    .KV("AvgWaitUs", total.AvgWaitUs()).KV("MaxWaitUs", total.maxWaitUs)
                    .KV("AvgWorkUs", total.AvgWorkUs()).KV("MaxWorkUs", total.maxWorkUs);
            }

            for (size_t laneIndex = 0; laneIndex < lanes_.size(); ++laneIndex)
            {
                uint64_t laneCalls = 0;
                uint64_t laneMaxWaitUs = 0;
                for (size_t processorIndex = 0; processorIndex < kProcessorCount; ++processorIndex)
                {
                    const auto stat = Snapshot(laneIndex, static_cast<TProcessorId>(processorIndex));
                    laneCalls += stat.callCount;
                    laneMaxWaitUs = stat.maxWaitUs > laneMaxWaitUs ? stat.maxWaitUs : laneMaxWaitUs;
                }

                LOG.Info(ELogCategory::General, "레인 통계")
                    .KV("Group", name_).KV("Lane", laneIndex)
                    .KV("Calls", laneCalls).KV("MaxWaitUs", laneMaxWaitUs)
                    .KV("Pending", PendingCount(laneIndex));
            }
        }

    private:
        using Clock = std::chrono::steady_clock;

        // 스레드 하나 + 그 스레드가 단독으로 쓰는 통계 칸들. 통계를 스레드마다 따로 두는 이유는
        // 경합을 없애려는 것이기도 하지만, 그보다 **어느 스레드에 일이 몰렸는지**를 보려는
        // 것이다 -- ownerId 종류가 적으면 스레드 하나만 뜨겁고 나머지는 논다.
        struct Lane
        {
            explicit Lane(std::string laneName)
                : worker(std::move(laneName))
            {
            }

            Thread::WorkerThread worker;
            ProcessorStat stats[kProcessorCount]{};
        };

        [[nodiscard]] Lane& LaneOf(const uint64_t ownerId) noexcept
        {
            return *lanes_[static_cast<size_t>(ownerId % lanes_.size())];
        }

        [[nodiscard]] static uint64_t ElapsedUs(const Clock::time_point from, const Clock::time_point to) noexcept
        {
            const auto delta = std::chrono::duration_cast<std::chrono::microseconds>(to - from).count();
            return delta > 0 ? static_cast<uint64_t>(delta) : 0;
        }

        std::string name_;
        uint64_t slowWarnUs_;
        std::vector<std::unique_ptr<Lane>> lanes_;
    };
}
