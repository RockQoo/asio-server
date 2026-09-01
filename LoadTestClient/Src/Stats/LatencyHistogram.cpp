#include "LoadTestClient/Src/pch.h"
#include "LoadTestClient/Src/Stats/LatencyHistogram.h"

#include <algorithm>

namespace Load
{
    double LatencyHistogram::AverageUs() const noexcept
    {
        const auto count = totalCount_.load(std::memory_order_relaxed);
        if (count == 0)
        {
            return 0.0;
        }
        return static_cast<double>(totalUs_.load(std::memory_order_relaxed)) / static_cast<double>(count);
    }

    uint64_t LatencyHistogram::PercentileUs(const double ratio) const noexcept
    {
        const auto count = totalCount_.load(std::memory_order_relaxed);
        if (count == 0)
        {
            return 0;
        }

        // 목표 순위(1-based). ceil로 올려야 P100이 마지막 샘플을 포함한다.
        const auto clamped = std::clamp(ratio, 0.0, 1.0);
        auto target = static_cast<uint64_t>(static_cast<double>(count) * clamped);
        if (target == 0)
        {
            target = 1;
        }

        uint64_t cumulative = 0;
        for (size_t i = 0; i < kBucketCount; ++i)
        {
            cumulative += buckets_[i].load(std::memory_order_relaxed);
            if (cumulative >= target)
            {
                // 버킷 상한을 돌려준다 -- 실제 값은 이 이하임이 보장되므로 지연을 과소보고하지 않는다.
                return std::min(UpperBoundUs(i), MaxUs());
            }
        }
        return MaxUs();
    }
}
