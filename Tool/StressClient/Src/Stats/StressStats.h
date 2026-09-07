#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Tool/StressClient/Src/Stats/LatencyHistogram.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace Stress
{
    // 세션 하나의 Mail Add->Del 사이클 중 하나가 어긋났을 때 남기는 기록. 상한이 있는
    // 벡터에만 쌓는다 -- 1만 세션 전부가 어긋나는 최악의 경우에도 메모리가 안 터지게.
    struct MismatchRecord
    {
        Network::SessionId sessionId{};
        uint32_t cycleIndex{};
        uint32_t expectedMailId{};
        uint32_t actualMailId{};
        std::string reason;
    };

    // 부하 테스트 전체의 집계 지점. 카운터는 std::atomic으로(모든 StressSession이 각자의
    // io 스레드에서 동시에 건드림), 상세 기록(불일치/스톨 세션)만 mutex로 보호한다 -- 개수를
    // 세는 빈도가 압도적으로 높고 상세 기록은 드물게(정상 상황에서는 0건) 발생하므로 이렇게
    // 나누는 게 락 경합을 최소화한다.
    class StressStats
    {
    public:
        void RecordAttempted() noexcept { attempted_.fetch_add(1, std::memory_order_relaxed); }
        void RecordConnected() noexcept
        {
            connected_.fetch_add(1, std::memory_order_relaxed);
            activeSessionCount_.fetch_add(1, std::memory_order_relaxed);
        }
        void RecordSessionEnded() noexcept { activeSessionCount_.fetch_sub(1, std::memory_order_relaxed); }
        void RecordSessionDone() noexcept { done_.fetch_add(1, std::memory_order_relaxed); }

        void RecordMailAddSent() noexcept { mailAddSent_.fetch_add(1, std::memory_order_relaxed); }
        void RecordMailAddAcked() noexcept { mailAddAcked_.fetch_add(1, std::memory_order_relaxed); }
        void RecordMailDelSent() noexcept { mailDelSent_.fetch_add(1, std::memory_order_relaxed); }
        void RecordMailDelAcked() noexcept { mailDelAcked_.fetch_add(1, std::memory_order_relaxed); }
        void RecordCycleCompleted() noexcept { cyclesCompleted_.fetch_add(1, std::memory_order_relaxed); }

        void RecordBroadcastSent(const uint64_t expectedRecipients) noexcept
        {
            broadcastSentCount_.fetch_add(1, std::memory_order_relaxed);
            broadcastExpectedTotal_.fetch_add(expectedRecipients, std::memory_order_relaxed);
        }
        void RecordBroadcastReceived() noexcept { broadcastReceivedTotal_.fetch_add(1, std::memory_order_relaxed); }

        // 지연은 "몇 개 처리했나"(처리량)만으로는 안 보이는 꼬리를 드러내려고 따로 모은다 --
        // 평균이 멀쩡해도 P99가 수백 ms면 그 존은 실사용에서 렉으로 체감된다. Add/Del을 따로
        // 두는 이유는 둘이 서로 다른 경로(신규 mailId 배정 vs 기존 항목 삭제)를 타기 때문이다.
        void RecordMailAddLatency(const uint64_t latencyUs) noexcept { mailAddRtt_.Record(latencyUs); }
        void RecordMailDelLatency(const uint64_t latencyUs) noexcept { mailDelRtt_.Record(latencyUs); }
        void RecordCycleLatency(const uint64_t latencyUs) noexcept { cycleRtt_.Record(latencyUs); }

        void RecordMismatch(const Network::SessionId sessionId, const uint32_t cycleIndex,
                             const uint32_t expectedMailId, const uint32_t actualMailId, std::string reason);
        void RecordStalled(const Network::SessionId sessionId);
        void ClearStalled(const Network::SessionId sessionId);

        [[nodiscard]] uint64_t ActiveSessionCount() const noexcept
        {
            return activeSessionCount_.load(std::memory_order_relaxed);
        }

        [[nodiscard]] uint64_t Attempted() const noexcept { return attempted_.load(std::memory_order_relaxed); }
        [[nodiscard]] uint64_t Connected() const noexcept { return connected_.load(std::memory_order_relaxed); }
        [[nodiscard]] uint64_t Done() const noexcept { return done_.load(std::memory_order_relaxed); }
        [[nodiscard]] uint64_t MailAddSent() const noexcept { return mailAddSent_.load(std::memory_order_relaxed); }
        [[nodiscard]] uint64_t MailAddAcked() const noexcept { return mailAddAcked_.load(std::memory_order_relaxed); }
        [[nodiscard]] uint64_t MailDelSent() const noexcept { return mailDelSent_.load(std::memory_order_relaxed); }
        [[nodiscard]] uint64_t MailDelAcked() const noexcept { return mailDelAcked_.load(std::memory_order_relaxed); }
        [[nodiscard]] uint64_t CyclesCompleted() const noexcept { return cyclesCompleted_.load(std::memory_order_relaxed); }
        [[nodiscard]] uint64_t BroadcastSentCount() const noexcept { return broadcastSentCount_.load(std::memory_order_relaxed); }
        [[nodiscard]] uint64_t BroadcastExpectedTotal() const noexcept { return broadcastExpectedTotal_.load(std::memory_order_relaxed); }
        [[nodiscard]] uint64_t BroadcastReceivedTotal() const noexcept { return broadcastReceivedTotal_.load(std::memory_order_relaxed); }

        // atomic 배열이라 복사가 안 되고, 복사할 이유도 없다(리포트 시점에 읽기만 한다).
        [[nodiscard]] const LatencyHistogram& MailAddRtt() const noexcept { return mailAddRtt_; }
        [[nodiscard]] const LatencyHistogram& MailDelRtt() const noexcept { return mailDelRtt_; }
        [[nodiscard]] const LatencyHistogram& CycleRtt() const noexcept { return cycleRtt_; }

        [[nodiscard]] std::vector<MismatchRecord> Mismatches() const;
        [[nodiscard]] std::vector<Network::SessionId> StalledSessions() const;
        [[nodiscard]] uint64_t MismatchTotalCount() const noexcept { return mismatchTotalCount_.load(std::memory_order_relaxed); }

        static constexpr size_t kMaxMismatchSamples = 200;

    private:
        std::atomic<uint64_t> attempted_{0};
        std::atomic<uint64_t> connected_{0};
        std::atomic<uint64_t> done_{0};
        std::atomic<uint64_t> activeSessionCount_{0};

        std::atomic<uint64_t> mailAddSent_{0};
        std::atomic<uint64_t> mailAddAcked_{0};
        std::atomic<uint64_t> mailDelSent_{0};
        std::atomic<uint64_t> mailDelAcked_{0};
        std::atomic<uint64_t> cyclesCompleted_{0};

        std::atomic<uint64_t> broadcastSentCount_{0};
        std::atomic<uint64_t> broadcastExpectedTotal_{0};
        std::atomic<uint64_t> broadcastReceivedTotal_{0};

        std::atomic<uint64_t> mismatchTotalCount_{0};

        LatencyHistogram mailAddRtt_;
        LatencyHistogram mailDelRtt_;
        LatencyHistogram cycleRtt_;

        mutable std::mutex detailMutex_;
        std::vector<MismatchRecord> mismatches_;
        std::unordered_set<Network::SessionId> stalledSessions_;
    };
}
