#pragma once

#include <atomic>
#include <array>
#include <cstdint>

namespace Stress
{
    // 왕복 지연(RTT)을 마이크로초 단위로 모으는 고정 버킷 히스토그램. 상수 메모리(551개
    // 카운터) + O(1)이라 수백만 샘플에도 측정이 실험 자체를 방해하지 않는다. 대가는 백분위가
    // 버킷 상한으로 올림된 근사값이라는 것이다.
    //
    // 버킷 폭은 10배씩 올라갈 때마다 10배로 키운다 -- 1ms 미만은 10us 해상도가 필요하지만
    // 1초를 넘긴 꼬리는 100ms로 뭉뚱그려도 결론이 달라지지 않는다.
    //
    // **추적 상한이 100초인 이유**: 10초로 뒀더니 과부하 실험에서 P95/P99가 전부 최상위
    // 버킷에 몰려 "10초"로만 보고됐다(실제 최대 131초). 상한에 붙은 백분위는 지표가 아니다.
    //
    // 측정 방법과 결과: docs/Performance.html
    class LatencyHistogram
    {
    public:
        // 각 스케일 구간을 90~100개 버킷으로 쪼갠다(아래 IndexOf/UpperBoundUs와 반드시 같이 수정).
        static constexpr size_t kBucketCount = 551;

        // 이 값 이상은 전부 마지막 버킷에 뭉친다. 백분위가 이 값으로 나왔다면 "정확히 100초"가
        // 아니라 "100초 이상"이라는 뜻이므로, 리포트에서 그렇게 구분해 표기해야 한다.
        static constexpr uint64_t kOverflowUs = 100'000'000;

        // 어느 스레드에서 호출해도 안전하다 -- 세션마다 자기 io 스레드에서 동시에 기록한다.
        // 카운터 간 순서는 의미가 없어 relaxed로 충분하다(집계는 전부 끝난 뒤에만 읽는다).
        void Record(const uint64_t latencyUs) noexcept
        {
            buckets_[IndexOf(latencyUs)].fetch_add(1, std::memory_order_relaxed);
            totalCount_.fetch_add(1, std::memory_order_relaxed);
            totalUs_.fetch_add(latencyUs, std::memory_order_relaxed);

            // 최대값만은 버킷 올림이 아니라 실제 관측값을 보고해야 꼬리의 심각도를 숨기지 않는다.
            uint64_t observed = maxUs_.load(std::memory_order_relaxed);
            while (latencyUs > observed && !maxUs_.compare_exchange_weak(observed, latencyUs,
                                                                         std::memory_order_relaxed))
            {
            }
        }

        [[nodiscard]] uint64_t Count() const noexcept { return totalCount_.load(std::memory_order_relaxed); }
        [[nodiscard]] uint64_t MaxUs() const noexcept { return maxUs_.load(std::memory_order_relaxed); }
        [[nodiscard]] double AverageUs() const noexcept;

        // ratio는 0.0~1.0(예: 0.95 = P95). 반환값은 "샘플의 ratio 비율이 이 값 이하"인 상한
        // 마이크로초다. 샘플이 없으면 0.
        [[nodiscard]] uint64_t PercentileUs(const double ratio) const noexcept;

    private:
        [[nodiscard]] static constexpr size_t IndexOf(const uint64_t us) noexcept
        {
            if (us < 1'000) { return static_cast<size_t>(us / 10); }                        // ~1ms  : 10us 간격
            if (us < 10'000) { return 100 + static_cast<size_t>((us - 1'000) / 100); }      // ~10ms : 100us 간격
            if (us < 100'000) { return 190 + static_cast<size_t>((us - 10'000) / 1'000); }  // ~100ms: 1ms 간격
            if (us < 1'000'000) { return 280 + static_cast<size_t>((us - 100'000) / 10'000); }      // ~1s  : 10ms
            if (us < 10'000'000) { return 370 + static_cast<size_t>((us - 1'000'000) / 100'000); }  // ~10s : 100ms
            if (us < kOverflowUs) { return 460 + static_cast<size_t>((us - 10'000'000) / 1'000'000); }  // ~100s: 1s
            return kBucketCount - 1;                                                        // 100s 이상 전부
        }

        [[nodiscard]] static constexpr uint64_t UpperBoundUs(const size_t index) noexcept
        {
            if (index < 100) { return (index + 1) * 10; }
            if (index < 190) { return 1'000 + (index - 100 + 1) * 100; }
            if (index < 280) { return 10'000 + (index - 190 + 1) * 1'000; }
            if (index < 370) { return 100'000 + (index - 280 + 1) * 10'000; }
            if (index < 460) { return 1'000'000 + (index - 370 + 1) * 100'000; }
            if (index < 550) { return 10'000'000 + (index - 460 + 1) * 1'000'000; }
            return kOverflowUs;
        }

        std::array<std::atomic<uint64_t>, kBucketCount> buckets_{};
        std::atomic<uint64_t> totalCount_{0};
        std::atomic<uint64_t> totalUs_{0};
        std::atomic<uint64_t> maxUs_{0};
    };
}
