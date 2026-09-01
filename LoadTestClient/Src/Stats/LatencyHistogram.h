#pragma once

#include <atomic>
#include <array>
#include <cstdint>

namespace Load
{
    // 요청->응답 왕복 지연(RTT)을 마이크로초 단위로 모으는 고정 버킷 히스토그램.
    //
    // 왜 원시 샘플을 다 들고 있지 않는가: 1만 세션 x 수백 사이클이면 샘플이 수백만 개가 되어
    // vector에 다 담아 정렬하는 방식은 메모리도, 종료 시점의 정렬 시간도 부담이다. 반면 고정
    // 버킷은 샘플 수와 무관하게 상수 메모리(461개 카운터)에 O(1) 증가만 하므로 측정 자체가
    // 서버 부하 실험을 방해하지 않는다. 대가는 백분위가 "버킷 상한으로 올림된 근사값"이라는
    // 것이고(오차 상한 = 그 구간의 버킷 폭), 게임 서버 지연 판단에는 이 해상도로 충분하다.
    //
    // 왜 등간격이 아닌가: 관심 있는 해상도가 구간마다 다르다. 1ms 미만은 10us 단위로 촘촘히
    // 봐야 의미가 있지만, 1초를 넘긴 꼬리는 100ms 단위로 뭉뚱그려도 "심하게 느리다"는 결론이
    // 달라지지 않는다. 그래서 10배씩 올라갈 때마다 버킷 폭도 10배로 키운다.
    //
    // 추적 상한을 100초까지 잡은 이유: 처음에 10초까지만 뒀더니 과부하 실험에서 P95/P99가
    // 전부 최상위 버킷에 몰려 "10초"로만 보고됐다(실제 최대는 131초). 백분위가 상한에
    // 붙어버리면 "얼마나 나쁜가"를 구분할 수 없어 지표로서 쓸모가 없다.
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
