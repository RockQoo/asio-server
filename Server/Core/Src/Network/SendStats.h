#pragma once

#include "Server/Core/Src/Base/Types.h"

namespace Network
{
    // 세션 송신 큐의 적체. **프로세스 전체 합**이라 세션을 순회하지 않는다.
    //
    // **왜 필요한가**: 레인은 큐 길이가 통계에 그대로 찍히지만, Session::sendQueue_ 는 세션의
    // strand 안이라 밖에서 안 보인다. 그래서 부하에서 느릴 때 "수신이 밀렸나 송신이 밀렸나"를
    // 가를 수단이 없었다. 소켓 단계를 레인으로 만들지 않아 생긴 유일한 실질 손해가 이것이고,
    // 이 클래스가 그 자리를 메운다(docs/design/network-lane.md).
    //
    // **스레드 규약**: 세션마다 다른 io 스레드에서 동시에 갱신하므로 RMW(fetch_add)가 필요하다.
    // Processor::Stat 은 쓰는 스레드가 하나라 RMW 없이 store 로 끝냈지만 여기는 다르다.
    //
    // **전역인 이유**: Session 은 Listener/Connector 가 만들고, 거기까지 참조를 끌고 가려면
    // 순전히 관측용 때문에 생성자 네 개가 바뀐다. 값만 들고 아무것도 소유하지 않으므로
    // 수명 문제도 없다.
    class SendStats final
    {
    public:
        [[nodiscard]] static SendStats& Instance()
        {
            static SendStats instance;
            return instance;
        }

        SendStats(const SendStats&) = delete;
        SendStats& operator=(const SendStats&) = delete;

        // 큐에 한 장 넣은 직후. queueDepth 는 넣고 난 뒤의 큐 길이다.
        void OnEnqueue(const SessionId sessionId, const size_t queueDepth, const size_t frameBytes) noexcept
        {
            pendingFrames_.fetch_add(1, std::memory_order_relaxed);
            pendingBytes_.fetch_add(frameBytes, std::memory_order_relaxed);

            RaisePeak(sessionId, static_cast<uint64_t>(queueDepth));
        }

        // 한 장 실제로 나간 직후.
        void OnSent(const size_t frameBytes) noexcept
        {
            pendingFrames_.fetch_sub(1, std::memory_order_relaxed);
            pendingBytes_.fetch_sub(frameBytes, std::memory_order_relaxed);
        }

        // 연결이 죽어 못 보낸 채로 버려진 몫. **이걸 빼지 않으면 카운터가 영영 안 내려간다.**
        void OnDropped(const size_t frameCount, const size_t frameBytes) noexcept
        {
            if (frameCount == 0)
            {
                return;
            }
            pendingFrames_.fetch_sub(frameCount, std::memory_order_relaxed);
            pendingBytes_.fetch_sub(frameBytes, std::memory_order_relaxed);
        }

        struct Snapshot final
        {
            uint64_t pendingFrames{};  // 지금 큐에 남아 있는 프레임 수
            uint64_t pendingBytes{};   // 그 바이트 합
            uint64_t peakDepth{};      // 지난 덤프 이후 한 세션이 기록한 최대 큐 길이
            SessionId peakSessionId{}; // 그 세션

            [[nodiscard]] bool IsIdle() const noexcept { return pendingFrames == 0 && peakDepth == 0; }
        };

        // **최고치는 읽으면서 0으로 되돌린다** -- 덤프 주기별 최고치를 보려는 것이라
        // 기동 후 누적 최고치는 쓸모가 없다(한 번 튄 뒤로는 계속 그 값만 보인다).
        [[nodiscard]] Snapshot Take() noexcept
        {
            Snapshot snapshot{};
            snapshot.pendingFrames = pendingFrames_.load(std::memory_order_relaxed);
            snapshot.pendingBytes = pendingBytes_.load(std::memory_order_relaxed);
            snapshot.peakDepth = peakDepth_.exchange(0, std::memory_order_relaxed);
            snapshot.peakSessionId = peakSessionId_.load(std::memory_order_relaxed);
            return snapshot;
        }

    private:
        SendStats() = default;

        void RaisePeak(const SessionId sessionId, const uint64_t depth) noexcept
        {
            auto previous = peakDepth_.load(std::memory_order_relaxed);
            while (depth > previous)
            {
                if (peakDepth_.compare_exchange_weak(previous, depth, std::memory_order_relaxed))
                {
                    // 최고치와 세션 id 를 한 번에 바꿀 수 없어서, 두 세션이 동시에 최고치를
                    // 갱신하면 **id 만 어긋날 수 있다**. 어느 링크가 밀리는지 가리키는 힌트
                    // 용도라 그 정도 오차는 감수한다 -- 맞추려면 두 값을 한 워드에 넣어야
                    // 하는데 SessionId 가 64비트라 깊이를 넣을 자리가 없다.
                    peakSessionId_.store(sessionId, std::memory_order_relaxed);
                    break;
                }
            }
        }

        std::atomic<uint64_t> pendingFrames_{0};
        std::atomic<uint64_t> pendingBytes_{0};
        std::atomic<uint64_t> peakDepth_{0};
        std::atomic<SessionId> peakSessionId_{0};
    };
}
