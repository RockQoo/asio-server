#include "Shared/Core/Src/pch.h"
#include "Shared/Core/Src/Common/RequestId.h"

#include <chrono>
#include <cstdlib>
#include <thread>

namespace Common
{
    namespace
    {
        constexpr uint64_t kSequenceMask = (1ull << kSequenceBits) - 1;
        constexpr uint64_t kNodeMask = (1ull << kNodeBits) - 1;
        constexpr uint64_t kMaxElapsedMs = 1ull << kTimestampBits;

        [[nodiscard]] int64_t ElapsedMsSinceEpoch()
        {
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            const auto unixMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            return unixMs - kEpochMs;
        }
    }

    RequestIdGenerator& RequestIdGenerator::Instance()
    {
        static RequestIdGenerator instance;
        return instance;
    }

    void RequestIdGenerator::Initialize(const uint32_t nodeId)
    {
        if (nodeId > kNodeMask)
        {
            // 8비트를 넘으면 조립할 때 밀리초 칸으로 넘쳐서 시각이 미래로 튄다. 그 상태로
            // 발급된 id는 DB에 영구히 남고 나중에 고칠 방법이 없으므로 즉시 드러낸다.
            LOG.Error(ELogCategory::General, "RequestId 노드 번호가 8비트를 넘는다")
                .KV("NodeId", nodeId).KV("Max", kNodeMask);
            std::abort();
        }

        nodeId_ = nodeId;

        LOG.Info(ELogCategory::General, "RequestId 생성기 초기화").KV("NodeId", nodeId);
    }

    RequestId RequestIdGenerator::Next()
    {
        for (;;)
        {
            // CAS가 실패하면 관측값을 갱신해주므로 const로 둘 수 없다.
            uint64_t observed = lastStamp_.load(std::memory_order_relaxed);
            const uint64_t observedMs = observed >> kSequenceBits;
            const uint64_t observedSequence = observed & kSequenceMask;

            const auto elapsed = ElapsedMsSinceEpoch();
            if (elapsed < 0 || static_cast<uint64_t>(elapsed) >= kMaxElapsedMs)
            {
                // epoch 상수가 미래로 잘못 설정됐거나, 서버 시계가 epoch 이전으로 맞춰져
                // 있거나, 41비트(약 69.7년)를 실제로 다 쓴 경우다. 조용히 넘기면 음수이거나
                // 과거 id와 겹치는 값이 DB에 쌓인다.
                LOG.Error(ELogCategory::General, "RequestId 시각이 표현 범위를 벗어났다")
                    .KV("ElapsedMs", elapsed).KV("MaxMs", kMaxElapsedMs);
                std::abort();
            }

            uint64_t nextMs = static_cast<uint64_t>(elapsed);
            uint64_t nextSequence = 0;

            if (nextMs <= observedMs)
            {
                // 같은 밀리초 안이거나, NTP 보정으로 시계가 뒤로 갔다. 둘 다 마지막 발급 시각을
                // 그대로 유지하고 시퀀스만 올린다 -- 뒤로 간 시계를 그대로 쓰면 이미 발급한
                // id와 겹친다.
                nextMs = observedMs;
                nextSequence = observedSequence + 1;

                if (nextSequence > kSequenceMask)
                {
                    // 그 밀리초의 16,384개를 다 썼다 -- 다음 밀리초로 넘어갈 때까지 양보한다.
                    std::this_thread::yield();
                    continue;
                }
            }

            const uint64_t desired = (nextMs << kSequenceBits) | nextSequence;
            if (!lastStamp_.compare_exchange_weak(observed, desired, std::memory_order_relaxed))
            {
                continue;
            }

            const uint64_t assembled = (nextMs << (kNodeBits + kSequenceBits))
                                     | ((static_cast<uint64_t>(nodeId_) & kNodeMask) << kSequenceBits)
                                     | nextSequence;

            // 63비트만 채웠으므로 부호 비트가 0이고, 캐스팅으로 음수가 될 수 없다.
            return static_cast<RequestId>(assembled);
        }
    }
}
