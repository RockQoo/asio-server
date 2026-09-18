#pragma once

#include "Server/Core/Src/Pipeline/ILaneSet.h"

namespace Pipeline
{
    // 레인 하나 = **스레드 1개 + 큐 1개 + mutex + condvar**. 실무 원본의 MessageConsumer 다.
    //
    // **스레드와 레인이 1:1**이라 `Basic#3`이 곧 3번 레인의 주인이고, 그 레인에 배정된
    // ownerId 의 일은 언제나 그 스레드 하나에서만 돈다 -- 어피니티가 그대로 성립한다.
    // 대가는 편중이다: 3번이 뜨겁고 5번이 놀아도 5번 스레드는 3번을 돕지 못한다.
    //
    // 그 대가를 없앤 것이 StrandLaneSet 이고, 어느 쪽이 나은지는 재서 정한다.
    class QueueLaneSet final : public ILaneSet
    {
    public:
        explicit QueueLaneSet(const size_t laneCount)
            : lanes_(laneCount)
        {
        }

        ~QueueLaneSet() override { Stop(); }

        void Start(const std::string& name) override
        {
            if (running_)
            {
                return;
            }
            running_ = true;

            for (size_t index = 0; index < lanes_.size(); ++index)
            {
                lanes_[index].thread = std::thread([this, name, index]
                {
                    SetThreadName(name + "#" + std::to_string(index));
                    Run(lanes_[index]);
                });
            }
        }

        void Stop() override
        {
            if (!running_)
            {
                return;
            }
            running_ = false;

            // **깨우고 나서 join 한다.** 소비자는 stopping 을 보고 큐를 마저 비운 뒤 나간다.
            for (auto& lane : lanes_)
            {
                {
                    const std::lock_guard<std::mutex> guard(lane.mutex);
                    lane.stopping = true;
                }
                lane.ready.notify_all();
            }

            for (auto& lane : lanes_)
            {
                if (lane.thread.joinable())
                {
                    lane.thread.join();
                }
            }
        }

        void Post(const size_t laneIndex, MessagePtr message) override
        {
            auto& lane = lanes_[laneIndex];
            {
                const std::lock_guard<std::mutex> guard(lane.mutex);
                lane.queue.push_back(std::move(message));
                lane.pending.store(lane.queue.size(), std::memory_order_relaxed);
            }
            lane.ready.notify_one();
        }

        [[nodiscard]] size_t LaneCount() const noexcept override { return lanes_.size(); }

        [[nodiscard]] size_t PendingCount(const size_t laneIndex) const noexcept override
        {
            return lanes_[laneIndex].pending.load(std::memory_order_relaxed);
        }

        [[nodiscard]] std::string_view BackendName() const noexcept override { return "queue"; }

    private:
        struct Lane
        {
            std::mutex mutex;
            std::condition_variable ready;
            std::deque<MessagePtr> queue;
            std::thread thread;
            bool stopping{false};

            // 큐 길이를 락 없이 읽으려고 따로 둔다 -- 통계를 보려고 소비자를 멈춰 세울 수는 없다.
            std::atomic<size_t> pending{0};
        };

        static void Run(Lane& lane)
        {
            for (;;)
            {
                MessagePtr message;
                {
                    std::unique_lock<std::mutex> guard(lane.mutex);
                    lane.ready.wait(guard, [&lane] { return !lane.queue.empty() || lane.stopping; });

                    if (lane.queue.empty())
                    {
                        // 큐가 빈 채로 깨어난 것은 stopping 뿐이다. **남은 것을 다 비운 뒤에만
                        // 나간다** -- 그래서 이 검사가 stopping 검사보다 앞에 온다.
                        return;
                    }

                    message = std::move(lane.queue.front());
                    lane.queue.pop_front();
                    lane.pending.store(lane.queue.size(), std::memory_order_relaxed);
                }

                message->targetProcessor->OnMessage(*message);
            }
        }

        static void SetThreadName(const std::string& name);

        // Lane 은 mutex/condvar 때문에 이동도 복사도 안 된다. deque 는 원소를 제자리에
        // 만들고 재할당하지 않으므로 그대로 쓸 수 있다(vector 는 안 된다).
        std::deque<Lane> lanes_;
        bool running_{false};
    };
}
