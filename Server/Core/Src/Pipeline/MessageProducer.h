#pragma once

#include "Server/Core/Src/Pipeline/MessageProcessor.h"
#include "Server/Core/Src/Pipeline/StrandLane.h"

namespace Pipeline
{
    // 레인 하나. **자기 io_context + 스레드 + strand + 프로세서 목록을 전부 소유한다.**
    // 레인끼리 아무것도 공유하지 않으므로, 한 레인이 밀려도 다른 레인의 스레드를 뺏지 않는다.
    //
    // **여기만 asio::thread_pool을 쓰지 않는다.** thread_pool이 io_context + 스레드 +
    // work_guard를 대신해 주지만 **스레드 생성 훅이 없어 이름을 못 붙인다.** 이름이 없으면
    // 대기 중인 스레드가 어느 레인인지 구분되지 않아(모든 풀이 asio::detail::scheduler::run
    // 으로 똑같이 보인다) "스레드 배분이 제대로 가는가"를 볼 수가 없다. 그게 이 저장소의
    // 목적이라 코드 몇 줄보다 비싸다.
    //
    // **strand는 "같은 스레드"를 약속하지 않는다.** 약속하는 것은 셋뿐이다 -- 같은 strand의
    // 일은 겹치지 않고, 넣은 순서대로 돌고, 앞 일의 쓰기가 뒤 일에 보인다. 스레드는 매번
    // 달라질 수 있지만 락 없는 접근에 필요한 것은 그 셋이다. 대신 `thread_local`로 주인별
    // 상태를 들고 있으면 깨진다(스레드별 상태는 괜찮다 -- DB 커넥션이 그렇다).
    class MessageProducer final
    {
    public:
        MessageProducer(const EProducerType producerType, const int32_t laneCount)
            : producerType_(producerType)
            , laneCount_(laneCount)
        {
        }

        ~MessageProducer() { Stop(); }

        MessageProducer(const MessageProducer&) = delete;
        MessageProducer& operator=(const MessageProducer&) = delete;

        // 프로세서를 등록하고 ProcessorId(= 배열 인덱스)를 돌려준다.
        // **이 id를 보관해야 다른 곳에서 여기로 메시지를 보낼 수 있다.**
        ProcessorId AddProcessor(std::unique_ptr<MessageProcessor> processor)
        {
            const ProcessorId processorId{static_cast<int32_t>(processors_.size())};
            processor->processorId_ = processorId;
            processor->RegistHandler();
            processors_.push_back(std::move(processor));
            return processorId;
        }

        // 레인(strand)을 laneCount_개 만들고 같은 수의 스레드를 띄운다.
        // **PushMsg보다 반드시 먼저** -- 레인이 없으면 보낼 곳이 없다.
        void Start()
        {
            if (running_)
            {
                return;
            }
            running_ = true;

            // ① 레인(strand)을 laneCount_개 만든다.
            lanes_.reserve(static_cast<size_t>(laneCount_));
            for (int32_t index = 0; index < laneCount_; ++index)
            {
                lanes_.emplace_back(asio::make_strand(ioContext_));
            }

            // ② 일이 잠깐 비어도 run()이 끝나버리지 않게 붙잡아 둔다.
            //    없으면 첫 메시지가 오기 전에 스레드가 전부 죽는다.
            workGuard_.emplace(asio::make_work_guard(ioContext_));

            // ③ 스레드를 laneCount_개 띄워 같은 io_context를 돌린다.
            //    **스레드와 레인이 1:1로 묶이지 않는다** -- 한가한 스레드가 아무 레인이나
            //    집어간다. 그래도 같은 레인끼리는 동시에 실행되지 않아 직렬성은 그대로다.
            //    그래서 이름이 말해주는 것은 "이 스레드가 어느 레인 소속인가"까지다.
            const std::string name{ToString(producerType_)};
            for (int32_t index = 0; index < laneCount_; ++index)
            {
                threads_.emplace_back([this, name, index]
                {
                    SetThreadName(name + "#" + std::to_string(index));
                    ioContext_.run();
                });
            }
        }

        // **큐에 남은 것을 전부 소진한 뒤** join한다. `ioContext_.stop()`을 부르지 않는 이유가
        // 그것이다 -- 그건 남은 일을 버린다(몇 초 분량의 플레이 결과가 사라진다).
        void Stop()
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

        // ── 라우팅 ────────────────────────────────────────────────────────────
        void PushMsg(MessagePtr message) const
        {
            if (lanes_.empty())
            {
                LOG.Warning(ELogCategory::General, "기동 전 레인으로 메시지를 보냈다")
                    .KV("Producer", ToString(producerType_));
                return;
            }

            // ① ownerId 해시 -> 어느 레인으로 갈지. 같은 ownerId는 항상 같은 레인이라
            //    순서가 보장되고 그 주인의 상태에 락이 필요 없다.
            const auto laneIndex = std::hash<int64_t>()(message->msgOwnerId.value) % lanes_.size();

            // ② ProcessorId -> 프로세서를 찾아 메시지에 박아둔다(아직 호출하지 않는다).
            //    **여기서 실패해야 호출자에게 즉시 알릴 수 있다.** 큐에 넣은 뒤엔 못 알린다.
            if (!message->targetProcessorId.IsValid() ||
                processors_.size() <= static_cast<size_t>(message->targetProcessorId.value))
            {
                LOG.Warning(ELogCategory::General, "없는 프로세서로 메시지를 보냈다")
                    .KV("Producer", ToString(producerType_))
                    .KV("ProcessorId", message->targetProcessorId.value);
                return;
            }
            message->targetProcessor = processors_[static_cast<size_t>(message->targetProcessorId.value)].get();

            // ③ 레인에 넣고 리턴. 보내는 스레드는 여기서 끝난다.
            asio::post(lanes_[laneIndex],
                [held = std::move(message)] { held->targetProcessor->OnMessage(*held); });
        }

        [[nodiscard]] EProducerType GetProducerType() const noexcept { return producerType_; }
        [[nodiscard]] int32_t LaneCount() const noexcept { return laneCount_; }

        // **이 레인의 스레드에서 만기시킬 타이머를 걸 때만 쓴다**(TIMER 레인이 그 용도다).
        // 여기에 직접 post하면 strand를 우회해 주인별 직렬화가 깨지므로, 일을 넣는 길은
        // 언제나 PushMsg여야 한다. 타이머가 예외인 이유는 만기 통지에 주인이 없기 때문이다.
        [[nodiscard]] asio::io_context& Context() const noexcept { return ioContext_; }

        [[nodiscard]] bool IsRunning() const noexcept { return running_; }

    private:
        static void SetThreadName(const std::string& name);

        const EProducerType producerType_{};
        const int32_t laneCount_{};

        std::vector<std::unique_ptr<MessageProcessor>> processors_;

        // io_context와 레인은 PushMsg(const)/Context(const) 안에서도 써야 하므로 mutable.
        mutable asio::io_context ioContext_;
        mutable std::vector<StrandLane> lanes_;

        std::optional<asio::executor_work_guard<asio::io_context::executor_type>> workGuard_;
        std::vector<std::thread> threads_;
        bool running_{false};
    };
}
