#pragma once

#include "Server/Core/Src/Pipeline/ILaneSet.h"

#include <asio.hpp>

namespace Pipeline
{
    // 레인 하나 = **asio::strand**. 스레드는 레인 수만큼 띄우지만 **레인에 묶이지 않는다** --
    // 한가한 스레드가 아무 레인의 일이나 집어간다(같은 레인끼리 겹치지 않는 것은 그대로).
    //
    // QueueLaneSet 과의 차이는 그것 하나이고, 그게 **워크 스틸링**이다. 3번 레인이 뜨겁고
    // 5번이 놀 때 5번 스레드가 3번의 밀린 일을 처리할 수 있다.
    //
    // **대신 `Basic#3`이 3번 레인을 뜻하지 않는다** -- 그냥 세 번째 일꾼이다. 스레드 이름으로
    // 레인을 가르려면 QueueLaneSet 쪽이어야 한다.
    class StrandLaneSet final : public ILaneSet
    {
    public:
        explicit StrandLaneSet(const size_t laneCount)
            : laneCount_(laneCount)
        {
        }

        ~StrandLaneSet() override { Stop(); }

        void Start(const std::string& name) override
        {
            if (running_)
            {
                return;
            }
            running_ = true;

            lanes_.reserve(laneCount_);
            pending_ = std::vector<std::atomic<size_t>>(laneCount_);
            for (size_t index = 0; index < laneCount_; ++index)
            {
                lanes_.emplace_back(asio::make_strand(ioContext_));
            }

            // 일이 잠깐 비어도 run()이 끝나버리지 않게 붙잡아 둔다.
            workGuard_.emplace(asio::make_work_guard(ioContext_));

            for (size_t index = 0; index < laneCount_; ++index)
            {
                threads_.emplace_back([this, name, index]
                {
                    SetThreadName(name + "#" + std::to_string(index));
                    ioContext_.run();
                });
            }
        }

        // work_guard 만 놓으면 **밀린 것을 전부 소진한 뒤** run()이 반환한다.
        // `ioContext_.stop()`을 부르지 않는 이유가 그것이다 -- 그건 남은 일을 버린다.
        void Stop() override
        {
            if (!running_)
            {
                return;
            }
            running_ = false;

            workGuard_.reset();

            for (auto& thread : threads_)
            {
                if (thread.joinable())
                {
                    thread.join();
                }
            }
            threads_.clear();
            lanes_.clear();
        }

        void Post(const size_t laneIndex, MessagePtr message) override
        {
            pending_[laneIndex].fetch_add(1, std::memory_order_relaxed);
            asio::post(lanes_[laneIndex],
                [this, laneIndex, held = std::move(message)]
                {
                    pending_[laneIndex].fetch_sub(1, std::memory_order_relaxed);
                    held->targetProcessor->OnMessage(*held);
                });
        }

        [[nodiscard]] size_t LaneCount() const noexcept override { return laneCount_; }

        // strand 는 큐 길이를 노출하지 않으므로 넣고 뺄 때 직접 센다.
        [[nodiscard]] size_t PendingCount(const size_t laneIndex) const noexcept override
        {
            return pending_[laneIndex].load(std::memory_order_relaxed);
        }

        [[nodiscard]] std::string_view BackendName() const noexcept override { return "strand"; }

    private:
        static void SetThreadName(const std::string& name);

        const size_t laneCount_{};

        asio::io_context ioContext_;
        std::vector<asio::strand<asio::io_context::executor_type>> lanes_;
        std::vector<std::atomic<size_t>> pending_;

        std::optional<asio::executor_work_guard<asio::io_context::executor_type>> workGuard_;
        std::vector<std::thread> threads_;
        bool running_{false};
    };
}
