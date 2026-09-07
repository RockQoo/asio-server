#include "Tool/StressClient/Src/pch.h"
#include "Tool/StressClient/Src/Stats/LatencyHistogram.h"

#include <algorithm>
#include <cmath>

namespace Stress
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

        // 목표 순위(1-based)는 올림이어야 한다 -- 내림으로 잡으면 표본이 적을 때 순위가 하나
        // 낮아져 실제보다 낙관적인 값이 나온다(count=7, P95면 내림 6위 vs 올림 7위).
        const auto clamped = std::clamp(ratio, 0.0, 1.0);
        auto target = static_cast<uint64_t>(std::ceil(static_cast<double>(count) * clamped));
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
