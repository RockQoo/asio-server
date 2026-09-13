#include "Tool/StressClient/Src/pch.h"
#include "Tool/StressClient/Src/Stats/Stats.h"

namespace Stress
{
    void Stats::RecordMismatch(const Network::SessionId sessionId, const uint32_t cycleIndex,
                                      const uint32_t expectedMailId, const uint32_t actualMailId, std::string reason)
    {
        mismatchTotalCount_.fetch_add(1, std::memory_order_relaxed);

        std::lock_guard lock(detailMutex_);
        if (mismatches_.size() < kMaxMismatchSamples)
        {
            mismatches_.push_back(MismatchRecord{sessionId, cycleIndex, expectedMailId, actualMailId, std::move(reason)});
        }
    }

    void Stats::RecordStalled(const Network::SessionId sessionId)
    {
        std::lock_guard lock(detailMutex_);
        stalledSessions_.insert(sessionId);
    }

    void Stats::ClearStalled(const Network::SessionId sessionId)
    {
        std::lock_guard lock(detailMutex_);
        stalledSessions_.erase(sessionId);
    }

    std::vector<MismatchRecord> Stats::Mismatches() const
    {
        std::lock_guard lock(detailMutex_);
        return mismatches_;
    }

    std::vector<Network::SessionId> Stats::StalledSessions() const
    {
        std::lock_guard lock(detailMutex_);
        return std::vector<Network::SessionId>(stalledSessions_.begin(), stalledSessions_.end());
    }
}
