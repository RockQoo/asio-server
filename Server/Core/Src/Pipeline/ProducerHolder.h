#pragma once

#include "Server/Core/Src/Pipeline/MessageProducer.h"

namespace Pipeline
{
    // 이 프로세스의 레인 전부를 배열로 들고 있을 뿐인 자리.
    //
    // **전역이다.** 아래 PushMsg가 객체 없이 한 줄로 끝나게 하려는 것이고, 대가는 "전역
    // 상태에 의존한다"가 시그니처에 안 드러난다는 점이다. 그래서 수명 규약을 여기 박아둔다:
    //
    //   **Shutdown()을 main이 끝나기 전에 반드시 부른다.** 레인이 io_context와 스레드를
    //   소유하므로, 정적 소멸 순서에 맡기면 이미 죽은 객체를 가리키는 콜백이 남는다.
    //   App의 종료 경로에서 Stop() -> Shutdown() 순으로 부르는 것이 규약이다.
    class ProducerHolder final
    {
    public:
        [[nodiscard]] static ProducerHolder& Instance()
        {
            static ProducerHolder instance;
            return instance;
        }

        ProducerHolder(const ProducerHolder&) = delete;
        ProducerHolder& operator=(const ProducerHolder&) = delete;

        // laneCount = strand 개수 = 그 레인이 띄우는 스레드 개수.
        void InitProducer(const EProducerType producerType, const int32_t laneCount)
        {
            producers_[static_cast<size_t>(producerType)] =
                std::make_unique<MessageProducer>(producerType, laneCount);
        }

        [[nodiscard]] MessageProducer* GetProducer(const EProducerType producerType) const noexcept
        {
            return producers_[static_cast<size_t>(producerType)].get();
        }

        void Start()
        {
            for (const auto& producer : producers_)
            {
                if (producer)
                {
                    producer->Start();
                }
            }
        }

        // **EProducerType 선언 순서(= 흐름 순서)대로 내린다.** 앞 단계가 먼저 끊겨야 뒤
        // 단계로 새 일이 안 들어오고, 뒤 단계가 밀린 것을 마저 처리한다.
        void Stop()
        {
            for (const auto& producer : producers_)
            {
                if (producer)
                {
                    producer->Stop();
                }
            }
        }

        // 레인 객체 자체를 버린다. **Stop() 뒤에 App 종료 경로에서 부른다.**
        void Shutdown()
        {
            for (auto& producer : producers_)
            {
                producer.reset();
            }
        }

    private:
        ProducerHolder() = default;

        std::array<std::unique_ptr<MessageProducer>, static_cast<size_t>(EProducerType::Max)> producers_{};
    };

    // ── 자유 함수 ─────────────────────────────────────────────────────────────
    //
    // Pipeline은 객체가 아니라 이름 구획이라 인스턴스가 필요 없다. 전역 상태는 시그니처가
    // 아니라 **함수 몸통 안**에 숨어 있다.
    template <EProducerType T_PRODUCER_TYPE>
    [[nodiscard]] inline MessageProducer* GetProducer()
    {
        return ProducerHolder::Instance().GetProducer(T_PRODUCER_TYPE);
    }

    // 메시지 하나를 보낸다. 세 좌표를 그대로 받는다:
    //   msgType           -> 어느 함수   targetProcessorId -> 어느 객체
    //   msgOwnerId        -> 어느 스레드
    template <EProducerType T_PRODUCER_TYPE, typename TMsgId, typename TBody>
    void PushMsg(const TMsgId msgType, const ProcessorId targetProcessorId, const OwnerId msgOwnerId,
                 TBody body)
    {
        MessageProducer* const producer = GetProducer<T_PRODUCER_TYPE>();
        if (producer == nullptr)
        {
            LOG.Warning(ELogCategory::General, "만들지 않은 레인으로 메시지를 보냈다")
                .KV("Producer", ToString(T_PRODUCER_TYPE));
            return;
        }

        auto message = std::make_unique<Message>();
        message->msgType = static_cast<uint32_t>(msgType);
        message->targetProcessorId = targetProcessorId;
        message->msgOwnerId = msgOwnerId;
        message->body = std::make_shared<TBody>(std::move(body));

        producer->PushMsg(std::move(message));
    }

    // body가 없는 메시지. 주인만으로 뜻이 서는 통지(연결 종료 등)가 여기 해당한다.
    template <EProducerType T_PRODUCER_TYPE, typename TMsgId>
    void PushMsg(const TMsgId msgType, const ProcessorId targetProcessorId, const OwnerId msgOwnerId)
    {
        MessageProducer* const producer = GetProducer<T_PRODUCER_TYPE>();
        if (producer == nullptr)
        {
            LOG.Warning(ELogCategory::General, "만들지 않은 레인으로 메시지를 보냈다")
                .KV("Producer", ToString(T_PRODUCER_TYPE));
            return;
        }

        auto message = std::make_unique<Message>();
        message->msgType = static_cast<uint32_t>(msgType);
        message->targetProcessorId = targetProcessorId;
        message->msgOwnerId = msgOwnerId;

        producer->PushMsg(std::move(message));
    }
}
