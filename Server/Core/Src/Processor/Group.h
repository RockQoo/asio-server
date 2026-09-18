#pragma once

#include "Server/Core/Src/Processor/Stats.h"

#include <asio.hpp>

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

    // 큐 그룹 하나 = 소비자 스레드 N개. 메시지를 넣는 길이 **두 가지**이고, 그 둘이 이
    // 저장소의 두 서버가 갈리는 지점이다.
    //
    //   Post(processorId, work)            주인 없음 -> **남는 스레드**가 집어간다.
    //                                      순서 보장이 없으므로, 건드리는 데이터는 스스로
    //                                      지켜야 한다(Thread::Mutexed 등).
    //
    //   Post(processorId, ownerId, work)   주인 있음 -> `ownerId % N`번 strand로 간다.
    //                                      같은 주인의 일은 **겹치지 않고 보낸 순서대로**
    //                                      실행되므로 그 데이터에 락이 필요 없다.
    //
    // **World와 Zone이 이걸 다르게 쓴다.** Zone은 전부 주인을 지정해(clientSessionId/zoneId)
    // 락 없이 돌리고, World는 기본이 주인 없는 쪽이며 순서·정합성이 필요한 것만 주인을 준다 --
    // 그래서 World의 전역 테이블(PlayerManager/ZoneLinkRegistry)은 Mutexed다.
    //
    // **왜 "같은 스레드"가 아니라 strand인가**: 어피니티는 수단이고 목적은 직렬화다. strand는
    // 그 목적을 직접 준다 -- 같은 strand의 작업은 겹치지 않고, 보낸 순서대로 돌고, 앞 작업의
    // 쓰기가 뒤 작업에 보인다(happens-before). 스레드는 매번 달라질 수 있지만 그건 락 없는
    // 접근에 필요한 조건이 아니다. 대신 주인 없는 일을 같은 스레드 풀이 집어갈 수 있어서
    // 한 레인만 뜨거워지는 편중이 사라진다. 대가는 CPU 캐시 지역성과 `thread_local`이다
    // (아래 주의 참고).
    //
    // **주의 1 -- `thread_local`에 기대지 말 것.** 같은 주인의 일이 매번 다른 스레드에서 돌 수
    // 있으므로 "이 주인은 항상 이 thread_local"이 성립하지 않는다. DbConnectionPool이 스레드당
    // 커넥션을 두는 것은 여전히 유효하지만(커넥션 수 = 스레드 수), 커넥션에 세션 상태(임시
    // 테이블, SET 옵션)를 남기고 다음 작업이 그걸 기대하면 깨진다.
    //
    // **주의 2 -- 주인을 지정해도 한 메시지가 주인이 다른 데이터를 함께 만지면 보호가 깨진다.**
    // 그때는 직렬화가 아니라 모델 단위 읽기/쓰기 락이 필요하다.
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
            // 일이 없어도 run()이 끝나지 않게 붙잡아 둔다. Stop()에서 이걸 놓으면 남은 일을
            // 전부 소진한 뒤 run()이 스스로 반환한다 -- 그게 "드레인 후 종료"다.
            , workGuard_(asio::make_work_guard(context_))
        {
            lanes_.reserve(threadCount);
            for (size_t index = 0; index < threadCount; ++index)
            {
                lanes_.push_back(std::make_unique<Lane>(name_ + "#" + std::to_string(index),
                                                        asio::make_strand(context_)));
            }
        }

        ~Group()
        {
            Stop();
        }

        Group(const Group&) = delete;
        Group& operator=(const Group&) = delete;

        void Start()
        {
            if (running_.exchange(true))
            {
                return;
            }

            for (size_t index = 0; index < lanes_.size(); ++index)
            {
                threads_.emplace_back([this, index]
                {
                    // 이 스레드가 쓸 통계 칸을 정해둔다. 주인 없는 일은 어느 스레드가 집어갈지
                    // 모르므로, 통계는 "누가 실행했나"로 가른다 -- 그래야 칸마다 쓰는 스레드가
                    // 하나로 유지되어 Stat의 무락 전제(단일 기록자)가 그대로 성립한다.
                    LaneIndex() = static_cast<int32_t>(index);
                    context_.run();
                });
            }
        }

        // **큐에 남은 것을 전부 소진한 뒤** join한다 -- 급하게 내리면 몇 초 분량의 플레이
        // 결과가 사라진다(App의 종료 순서 주석 참고). context_.stop()을 부르지 않는 이유가
        // 그것이다. 그건 남은 일을 버린다.
        void Stop()
        {
            if (!running_.exchange(false))
            {
                return;
            }

            workGuard_.reset();

            for (auto& thread : threads_)
            {
                if (thread.joinable())
                {
                    thread.join();
                }
            }
            threads_.clear();
        }

        // **주인 없는 메시지.** 남는 스레드가 집어간다 -- 순서 보장이 없으므로, 이 경로로 가는
        // 일은 어느 스레드에서 어느 순서로 돌아도 되는 것이어야 한다.
        template <typename F>
        void Post(const TProcessorId processorId, F&& work)
        {
            sharedPending_.fetch_add(1, std::memory_order_relaxed);
            asio::post(context_,
                       Wrap(processorId, std::forward<F>(work), &sharedPending_));
        }

        // **주인 있는 메시지.** 같은 ownerId의 일은 겹치지 않고 보낸 순서대로 실행된다.
        template <typename F>
        void Post(const TProcessorId processorId, const uint64_t ownerId, F&& work)
        {
            auto& lane = *lanes_[static_cast<size_t>(ownerId % lanes_.size())];
            lane.pending.fetch_add(1, std::memory_order_relaxed);
            asio::post(lane.strand,
                       Wrap(processorId, std::forward<F>(work), &lane.pending));
        }

        [[nodiscard]] size_t ThreadCount() const noexcept { return lanes_.size(); }
        [[nodiscard]] const std::string& Name() const noexcept { return name_; }

        // **이 그룹의 스레드에서 만기시킬 타이머를 걸 때만 쓴다**(TIMER 레인이 그 용도다).
        // 여기에 직접 post하면 strand를 우회해 주인별 직렬화가 깨지므로, 일을 넣는 길은
        // 언제나 Post여야 한다. 타이머가 예외인 이유는 만기 통지에 주인이 없기 때문이다 --
        // 만기 뒤의 실제 작업은 그 작업의 주인으로 다시 Post한다.
        [[nodiscard]] asio::io_context& Context() noexcept { return context_; }

        // 그 strand에 **고정된** 일이 몇 개 밀려 있는지. strand별로 봐야 편중(같은 ownerId에
        // 몰림)이 보이므로 합계가 아니라 인덱스별로 노출한다.
        [[nodiscard]] size_t PendingCount(const size_t laneIndex) const noexcept
        {
            return lanes_[laneIndex]->pending.load(std::memory_order_relaxed);
        }

        // 주인 없이 들어와 아직 아무도 집어가지 않은 일. 이 값이 계속 크면 **스레드가 모자란
        // 것**이다 -- 고정된 일과 달리 편중이 아니라 총량 문제다.
        [[nodiscard]] size_t SharedPendingCount() const noexcept
        {
            return sharedPending_.load(std::memory_order_relaxed);
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

        // 스레드 하나가 처리한 몫. **strand 인덱스가 아니라 실행한 스레드 기준**이다 --
        // 주인 없는 일까지 포함해 "이 스레드가 얼마나 일했나"를 본다.
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
                    .KV("StrandPending", PendingCount(laneIndex));
            }

            LOG.Info(ELogCategory::General, "공유 큐 통계")
                .KV("Group", name_).KV("Pending", SharedPendingCount());
        }

    private:
        using Clock = std::chrono::steady_clock;
        using Strand = asio::strand<asio::io_context::executor_type>;

        // 스레드 하나 + strand 하나 + 그 스레드가 단독으로 쓰는 통계 칸들.
        //
        // **strand 인덱스와 스레드 인덱스가 같은 수만큼 있을 뿐 서로 묶여 있지는 않다.**
        // 3번 strand의 일을 1번 스레드가 집어갈 수 있다. stats는 실행한 스레드가 자기 칸에
        // 적고, pending은 그 strand에 밀린 개수다.
        struct Lane
        {
            Lane(std::string laneName, Strand laneStrand)
                : name(std::move(laneName))
                , strand(std::move(laneStrand))
            {
            }

            std::string name;
            Strand strand;
            std::atomic<size_t> pending{0};
            Stat stats[kProcessorCount]{};
        };

        // 이 스레드가 쓸 통계 칸 번호. 소비자 스레드가 아니면 -1.
        //
        // 함수 지역 static이라 템플릿 인스턴스마다 하나씩 생긴다. 한 스레드는 그룹 하나에만
        // 속하므로(Start가 만든 스레드) 값이 모호해질 일이 없다.
        [[nodiscard]] static int32_t& LaneIndex() noexcept
        {
            static thread_local int32_t index = -1;
            return index;
        }

        // 작업에 계측을 씌운다. 큐에 넣은 시각을 함께 실어 보내야 실행 시작 시각과의 차이로
        // 대기 시간을 알 수 있고, 그게 "스레드가 부족한가, 로직이 무거운가"를 가르는 유일한
        // 지표다.
        template <typename F>
        [[nodiscard]] auto Wrap(const TProcessorId processorId, F&& work, std::atomic<size_t>* pending)
        {
            return [this, processorId, pending, enqueuedAt = Clock::now(),
                    work = std::forward<F>(work)]() mutable
            {
                pending->fetch_sub(1, std::memory_order_relaxed);

                const auto startedAt = Clock::now();
                try
                {
                    work();
                }
                catch (const std::exception& ex)
                {
                    // 여기서 새어나가면 io_context::run()이 예외를 그대로 올리고 그 스레드가
                    // 죽는다 -- 소비자가 하나 줄어든 채로 서버가 계속 돈다
                    // (cpp-patterns.md "asio 비동기 핸들러의 예외 안전" 참고).
                    LOG.Error(ELogCategory::General, "작업 실행 중 예외")
                        .KV("Group", name_).KV("Exception", ex.what());
                }
                const auto finishedAt = Clock::now();

                const auto waitUs = ElapsedUs(enqueuedAt, startedAt);
                const auto workUs = ElapsedUs(startedAt, finishedAt);

                // 실행한 스레드가 자기 칸에 적는다. 칸마다 기록자가 하나라 Stat의 무락 전제가
                // 유지된다(Stats.h 주석 참고).
                if (const auto laneIndex = LaneIndex(); laneIndex >= 0)
                {
                    lanes_[static_cast<size_t>(laneIndex)]
                        ->stats[static_cast<size_t>(processorId)].Record(waitUs, workUs);
                }

                if (slowWarnUs_ > 0 && workUs > slowWarnUs_)
                {
                    // **General이어야 한다** -- 이 헤더는 여러 프로젝트에서 include되고 각자
                    // 자기 ELogCategory를 정의하므로, 공통 항목이 아니면 컴파일이 깨진다.
                    LOG.Warning(ELogCategory::General, "처리 시간이 임계치를 넘은 작업")
                        .KV("Group", name_).KV("Lane", LaneIndex())
                        .KV("Processor", static_cast<uint32_t>(processorId))
                        .KV("WorkUs", workUs).KV("WaitUs", waitUs);
                }
            };
        }

        [[nodiscard]] static uint64_t ElapsedUs(const Clock::time_point from, const Clock::time_point to) noexcept
        {
            const auto delta = std::chrono::duration_cast<std::chrono::microseconds>(to - from).count();
            return delta > 0 ? static_cast<uint64_t>(delta) : 0;
        }

        std::string name_;
        uint64_t slowWarnUs_;

        // **이 그룹 전용 io_context다.** Network::IoContextPool과 공유하지 않는다 -- 소켓
        // I/O와 로직이 같은 스레드를 나눠 쓰면 무거운 로직 하나가 수신을 멈춘다.
        asio::io_context context_;
        asio::executor_work_guard<asio::io_context::executor_type> workGuard_;

        std::vector<std::unique_ptr<Lane>> lanes_;
        std::vector<std::thread> threads_;

        std::atomic<size_t> sharedPending_{0};
        std::atomic<bool> running_{false};
    };
}
