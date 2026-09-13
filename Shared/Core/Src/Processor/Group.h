#pragma once

#include "Shared/Core/Src/Processor/Stats.h"
#include "Shared/Core/Src/Thread/WorkerThread.h"

namespace Processor
{
    // 그룹에 담을 수 있는 프로세서 id enum의 요건: 통계 배열 크기를 잡을 `Count`와, 덤프에
    // 이름을 찍을 ADL `ToString()`. 콘텐츠 계층이 각자 자기 enum을 정의한다(설계 근거 문서).
    template <typename T>
    concept ProcessorId = std::is_enum_v<T> && requires (T value)
    {
        T::Count;
        { ToString(value) } -> std::convertible_to<std::string_view>;
    };

    // 큐 그룹 하나 = 소비자 스레드 N개. `ownerId % N`으로 스레드가 정해지므로 같은 주인의
    // 일은 항상 같은 스레드에서 순서대로 처리된다 -- 그 주인의 데이터에는 락이 필요 없다.
    // (processorId는 배정과 무관한 계측용 태그다.)
    //
    // **주의 -- 한 메시지가 주인이 다른 데이터를 함께 만지면 이 보호가 깨진다.** 그때는
    // 어피니티 대신 모델 단위 읽기/쓰기 락(Thread::Mutexed)이 필요하다.
    //
    // 설계 근거: docs/design/processor-group.md
    template <ProcessorId TProcessorId>
    class Group
    {
    public:
        static constexpr size_t kProcessorCount = static_cast<size_t>(TProcessorId::Count);

        // slowWarnThreshold를 0으로 두면 느린 작업 경고를 끈다.
        Group(std::string name, const size_t threadCount,
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

        Group(const Group&) = delete;
        Group& operator=(const Group&) = delete;

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
                    // **General이어야 한다** -- 이 헤더는 여러 프로젝트에서 include되고 각자
                    // 자기 ELogCategory를 정의하므로, 공통 항목이 아니면 컴파일이 깨진다.
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
        [[nodiscard]] StatSnapshot Snapshot(const TProcessorId processorId) const
        {
            StatSnapshot total{};
            for (const auto& lane : lanes_)
            {
                total += StatSnapshot::From(lane->stats[static_cast<size_t>(processorId)]);
            }
            return total;
        }

        // 스레드 하나 안에서 프로세서 하나의 통계. 어피니티가 실제로 고르게 퍼지는지
        // (= ownerId 종류가 충분한지) 확인할 때 쓴다.
        [[nodiscard]] StatSnapshot Snapshot(const size_t laneIndex, const TProcessorId processorId) const
        {
            return StatSnapshot::From(lanes_[laneIndex]->stats[static_cast<size_t>(processorId)]);
        }

        // 주기적으로 불러 지금 상태를 로그로 남긴다. **대기 시간이 이 덤프의 핵심**이고,
        // 스레드별로도 찍는 건 편중을 보려는 것이다(읽는 법: docs/design/processor-group.md).
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

        // 스레드 하나 + 그 스레드가 단독으로 쓰는 통계 칸들(따로 두는 이유는 설계 근거 문서).
        struct Lane
        {
            explicit Lane(std::string laneName)
                : worker(std::move(laneName))
            {
            }

            Thread::WorkerThread worker;
            Stat stats[kProcessorCount]{};
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
