#pragma once

#include "Server/Core/Src/Pipeline/MessageProcessor.h"
#include "Server/Core/Src/Pipeline/QueueLaneSet.h"
#include "Server/Core/Src/Pipeline/StrandLaneSet.h"

namespace Pipeline
{
    // 레인 하나. **자기 레인 묶음 + 프로세서 목록을 소유한다.** 레인끼리 아무것도 공유하지
    // 않으므로 한 레인이 밀려도 다른 레인의 스레드를 뺏지 않는다.
    //
    // 레인을 굴리는 방식은 **config 로 고른다**(ILaneSet.h 참고) -- queue 는 스레드↔레인이
    // 1:1 인 어피니티, strand 는 한가한 스레드가 집어가는 워크 스틸링이다. 어느 쪽이 나은지
    // 재보려고 둘 다 남겨둔다.
    class MessageProducer final
    {
    public:
        MessageProducer(const EProducerType producerType, const int32_t laneCount,
                        const ELaneBackend backend)
            : producerType_(producerType)
            , laneCount_(laneCount)
            , lanes_(MakeLaneSet(backend, static_cast<size_t>(laneCount)))
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

        void Start() { lanes_->Start(std::string(ToString(producerType_))); }
        void Stop() { lanes_->Stop(); }

        // ── 라우팅 ────────────────────────────────────────────────────────────
        void PushMsg(MessagePtr message) const
        {
            // ① ownerId 해시 -> 어느 레인으로 갈지. 같은 ownerId는 항상 같은 레인이라
            //    순서가 보장되고 그 주인의 상태에 락이 필요 없다.
            const auto laneIndex =
                std::hash<int64_t>()(message->msgOwnerId.value) % lanes_->LaneCount();

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
            lanes_->Post(laneIndex, std::move(message));
        }

        [[nodiscard]] EProducerType GetProducerType() const noexcept { return producerType_; }
        [[nodiscard]] int32_t LaneCount() const noexcept { return laneCount_; }
        [[nodiscard]] std::string_view BackendName() const noexcept { return lanes_->BackendName(); }

        // 그 레인에 밀려 있는 개수. **부하에서 제일 먼저 볼 값**이다.
        [[nodiscard]] size_t PendingCount(const size_t laneIndex) const noexcept
        {
            return lanes_->PendingCount(laneIndex);
        }

    private:
        [[nodiscard]] static std::unique_ptr<ILaneSet> MakeLaneSet(const ELaneBackend backend,
                                                                   const size_t laneCount)
        {
            if (backend == ELaneBackend::Strand)
            {
                return std::make_unique<StrandLaneSet>(laneCount);
            }
            return std::make_unique<QueueLaneSet>(laneCount);
        }

        const EProducerType producerType_{};
        const int32_t laneCount_{};

        std::vector<std::unique_ptr<MessageProcessor>> processors_;

        // PushMsg(const) 안에서도 넣어야 하므로 포인터가 가리키는 쪽은 const 가 아니다.
        const std::unique_ptr<ILaneSet> lanes_;
    };
}
