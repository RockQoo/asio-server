#pragma once

#include <atomic>
#include <cstdint>

namespace Processor
{
    // 프로세서 하나(정확히는 "스레드 하나 × 프로세서 하나" 칸)의 누적 통계.
    //
    // **왜 대기 시간을 따로 재는가**: 처리 시간만 재면 "이 핸들러가 무겁다"까지만 알 수 있고,
    // "큐에서 얼마나 기다렸는가"는 안 보인다. 스레드가 부족한 상황과 로직이 무거운 상황은
    // 대응이 정반대인데(전자는 스레드를 늘리고, 후자는 늘려도 소용없다) 그 둘을 가르는 것이
    // 대기 시간이다. 그래서 이 프로젝트에서는 처리 시간보다 이쪽이 더 중요한 지표다.
    //
    // **왜 락이 없는가**: 각 칸은 그 칸을 소유한 소비자 스레드 하나만 쓴다(ownerId 어피니티로
    // 스레드가 고정되므로). 읽는 쪽(덤프)만 다른 스레드라서, 찢어진 값을 읽지 않을 정도의
    // 보장만 있으면 된다 -- relaxed atomic으로 충분하고 RMW도 필요 없다(쓰는 쪽이 하나뿐이라
    // load -> 계산 -> store가 경합하지 않는다).
    struct ProcessorStat
    {
        std::atomic<uint64_t> callCount{0};
        std::atomic<uint64_t> totalWaitUs{0};
        std::atomic<uint64_t> maxWaitUs{0};
        std::atomic<uint64_t> totalWorkUs{0};
        std::atomic<uint64_t> maxWorkUs{0};

        // 소유 스레드에서만 호출한다.
        void Record(const uint64_t waitUs, const uint64_t workUs) noexcept
        {
            Accumulate(callCount, 1);
            Accumulate(totalWaitUs, waitUs);
            Accumulate(totalWorkUs, workUs);
            Raise(maxWaitUs, waitUs);
            Raise(maxWorkUs, workUs);
        }

    private:
        static void Accumulate(std::atomic<uint64_t>& target, const uint64_t delta) noexcept
        {
            // 쓰는 스레드가 하나뿐이라 fetch_add(RMW)가 필요 없다.
            target.store(target.load(std::memory_order_relaxed) + delta, std::memory_order_relaxed);
        }

        static void Raise(std::atomic<uint64_t>& target, const uint64_t value) noexcept
        {
            if (value > target.load(std::memory_order_relaxed))
            {
                target.store(value, std::memory_order_relaxed);
            }
        }
    };

    // 덤프용 스냅샷. 원자적으로 일관된 한 순간을 보장하지는 않는다 -- 운영 지표라 그 정도
    // 오차는 무의미하고, 일관성을 맞추려면 소비자 경로에 락이 생겨 계측이 계측 대상을
    // 왜곡한다.
    struct ProcessorStatSnapshot
    {
        uint64_t callCount{};
        uint64_t totalWaitUs{};
        uint64_t maxWaitUs{};
        uint64_t totalWorkUs{};
        uint64_t maxWorkUs{};

        [[nodiscard]] uint64_t AvgWaitUs() const noexcept
        {
            return callCount > 0 ? totalWaitUs / callCount : 0;
        }

        [[nodiscard]] uint64_t AvgWorkUs() const noexcept
        {
            return callCount > 0 ? totalWorkUs / callCount : 0;
        }

        [[nodiscard]] bool IsEmpty() const noexcept { return callCount == 0; }

        ProcessorStatSnapshot& operator+=(const ProcessorStatSnapshot& other) noexcept
        {
            callCount += other.callCount;
            totalWaitUs += other.totalWaitUs;
            totalWorkUs += other.totalWorkUs;
            maxWaitUs = other.maxWaitUs > maxWaitUs ? other.maxWaitUs : maxWaitUs;
            maxWorkUs = other.maxWorkUs > maxWorkUs ? other.maxWorkUs : maxWorkUs;
            return *this;
        }

        [[nodiscard]] static ProcessorStatSnapshot From(const ProcessorStat& stat) noexcept
        {
            return ProcessorStatSnapshot{
                stat.callCount.load(std::memory_order_relaxed),
                stat.totalWaitUs.load(std::memory_order_relaxed),
                stat.maxWaitUs.load(std::memory_order_relaxed),
                stat.totalWorkUs.load(std::memory_order_relaxed),
                stat.maxWorkUs.load(std::memory_order_relaxed),
            };
        }
    };
}
