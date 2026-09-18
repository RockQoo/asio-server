#include "pch.h"
#include "Server/Core/Src/Base/RUID.h"

namespace Base
{
    namespace
    {
        constexpr uint64_t kMaxElapsedMs = static_cast<uint64_t>(kTimestampMask) + 1;
        constexpr uint64_t kSequenceMaskU = static_cast<uint64_t>(kSequenceMask);
        constexpr uint64_t kNodeMaskU = static_cast<uint64_t>(kNodeMask);

        [[nodiscard]] int64_t ElapsedMsSinceEpoch()
        {
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            const auto unixMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
            return unixMs - kEpochMs;
        }
    }

    bool RuidGenerator::Initialize(const uint32_t nodeId) noexcept
    {
        if (initialized_)
        {
            return false;
        }

        if (nodeId == kNodeIdReserved || nodeId > static_cast<uint32_t>(kNodeMask))
        {
            return false;
        }

        nodeId_ = nodeId;
        lastStamp_.store(0, std::memory_order_relaxed);
        initialized_ = true;
        return true;
    }

    RUID RuidGenerator::Next()
    {
        for (;;)
        {
            // CAS가 실패하면 관측값을 갱신해주므로 const로 둘 수 없다.
            uint64_t observed = lastStamp_.load(std::memory_order_acquire);
            const uint64_t observedMs = observed >> kSequenceBits;
            const uint64_t observedSequence = observed & kSequenceMaskU;

            const auto elapsed = ElapsedMsSinceEpoch();
            if (elapsed < 0 || static_cast<uint64_t>(elapsed) >= kMaxElapsedMs)
            {
                // epoch 상수가 미래로 잘못 설정됐거나, 서버 시계가 epoch 이전으로 맞춰져
                // 있거나, 41비트(약 69.7년)를 실제로 다 쓴 경우다. 조용히 넘기면 음수이거나
                // 과거 id와 겹치는 값이 DB에 쌓인다.
                LOG.Error(ELogCategory::General, "RUID 시각이 표현 범위를 벗어났다")
                    .KV("ElapsedMs", elapsed).KV("MaxMs", kMaxElapsedMs);
                std::abort();
            }

            uint64_t nextMs = static_cast<uint64_t>(elapsed);
            uint64_t nextSequence = 0;

            if (nextMs < observedMs)
            {
                // NTP step 보정, VM 스냅샷 복원 등으로 시계가 뒤로 갔다. 그대로 쓰면 이미
                // 발급한 id와 겹치므로 마지막 발급 시각을 하한으로 고정한다 -- **중복 대신
                // 정체**를 택하는 것이고, 그만큼 발급이 느려지므로 Health로 관측한다.
                RecordRollback(static_cast<int64_t>(observedMs - nextMs));
            }

            if (nextMs <= observedMs)
            {
                // 같은 밀리초 안이거나 위의 역행 상황이다. 둘 다 시각을 유지하고 시퀀스만 올린다.
                nextMs = observedMs;
                nextSequence = observedSequence + 1;

                if (nextSequence > kSequenceMaskU)
                {
                    // 그 밀리초의 4,096개를 다 썼다 -- 다음 밀리초로 넘어갈 때까지 양보한다.
                    sequenceExhausted_.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::yield();
                    continue;
                }
            }

            const uint64_t desired = (nextMs << kSequenceBits) | nextSequence;
            if (!lastStamp_.compare_exchange_weak(observed, desired,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire))
            {
                continue;
            }

            const uint64_t assembled = (nextMs << (kNodeBits + kSequenceBits))
                                     | ((static_cast<uint64_t>(nodeId_) & kNodeMaskU) << kSequenceBits)
                                     | nextSequence;

            // 63비트만 채웠으므로 부호 비트가 0이고, 캐스팅으로 음수가 될 수 없다.
            return static_cast<RUID>(assembled);
        }
    }

    RuidGenerator::Health RuidGenerator::GetHealth() const noexcept
    {
        return Health{
            .clockRollbackCount = rollbackCount_.load(std::memory_order_relaxed),
            .maxRollbackMs = maxRollbackMs_.load(std::memory_order_relaxed),
            .sequenceExhaustedCount = sequenceExhausted_.load(std::memory_order_relaxed),
        };
    }

    void RuidGenerator::RecordRollback(const int64_t backwardMs) noexcept
    {
        rollbackCount_.fetch_add(1, std::memory_order_relaxed);

        // CAS가 실패하면 current에 최신값이 담기므로 그대로 재비교한다.
        auto current = maxRollbackMs_.load(std::memory_order_relaxed);
        while (backwardMs > current
            && !maxRollbackMs_.compare_exchange_weak(current, backwardMs, std::memory_order_relaxed))
        {
        }
    }

    RuidGenerator& Ruid::Generator() noexcept
    {
        // C++11부터 함수 지역 static의 초기화는 스레드 안전하다.
        static RuidGenerator generator;
        return generator;
    }

    void Ruid::Init(const uint32_t nodeId)
    {
        if (nodeId == kNodeIdReserved)
        {
            // 0은 "초기화를 빠뜨렸다"를 잡기 위한 예약값이다. 이걸 허용하면 Init을 안 부른
            // 프로세스와 0번을 쓰는 프로세스가 구분되지 않아, 중복 발급이 조용히 섞인다.
            LOG.Error(ELogCategory::General, "RUID 노드 번호 0은 예약값이다")
                .KV("WorldBegin", kNodeIdWorldBegin).KV("ZoneBegin", kNodeIdZoneBegin);
            std::abort();
        }

        if (nodeId > static_cast<uint32_t>(kNodeMask))
        {
            // 10비트를 넘으면 조립할 때 밀리초 칸으로 넘쳐서 시각이 미래로 튄다. 그 상태로
            // 발급된 id는 DB에 영구히 남고 나중에 고칠 방법이 없으므로 즉시 드러낸다.
            LOG.Error(ELogCategory::General, "RUID 노드 번호가 비트 폭을 넘는다")
                .KV("NodeId", nodeId).KV("Max", kNodeMask);
            std::abort();
        }

        if (!Generator().Initialize(nodeId))
        {
            // 두 번째 호출이다. 노드 번호가 바뀌면 앞서 발급한 id와 뒤에 발급할 id의 주인이
            // 갈리고, 시퀀스가 리셋되면 이미 나간 값을 다시 내준다.
            LOG.Error(ELogCategory::General, "RUID 생성기를 두 번 초기화했다")
                .KV("NodeId", nodeId).KV("Current", Generator().NodeId());
            std::abort();
        }

        LOG.Info(ELogCategory::General, "RUID 생성기 초기화").KV("NodeId", nodeId);
    }

    bool Ruid::Initialized() noexcept
    {
        return Generator().Initialized();
    }

    uint32_t Ruid::NodeId() noexcept
    {
        return Generator().NodeId();
    }

    RUID Ruid::Create()
    {
        // Init을 빠뜨리면 노드 0으로 발급돼 다른 프로세스와 조용히 겹친다. 분기 하나는
        // 예측 가능해서 비용이 없고, 놓쳤을 때의 대가는 되돌릴 수 없다.
        if (!Generator().Initialized())
        {
            LOG.Error(ELogCategory::General, "RUID 생성기를 초기화하지 않고 발급을 시도했다");
            std::abort();
        }

        return Generator().Next();
    }

    Ruid::Health Ruid::GetHealth() noexcept
    {
        return Generator().GetHealth();
    }

    std::string Ruid::Describe(const RUID id)
    {
        const std::chrono::sys_time<std::chrono::milliseconds> when{
            std::chrono::milliseconds{TimestampMsOf(id)}};

        return std::format("{:>20} | {:%F %T} | node={:<4} | seq={:<5}",
                           id, when, NodeIdOf(id), SequenceOf(id));
    }
}
