#pragma once

#include "Server/Core/Src/Base/CoreException.h"
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
        // 레인 하나의 배정 계수기. **캐시 라인 하나를 통째로 차지한다** -- 붙여 놓으면
        // 서로 다른 레인을 세는 스레드들이 같은 라인을 뺏고 뺏겨서(거짓 공유) 세는 일이
        // 병목이 된다. 64 는 x64 캐시 라인 크기다.
        // C4324(맞춤 때문에 구조체가 채워졌다)는 여기서 **의도한 결과**다 -- 8바이트 원자
        // 하나를 64바이트로 부풀리는 것이 목적이다. 경고 0 빌드를 유지하려고 이 선언에서만
        // 끈다(전역 /wd 로 끄면 진짜 실수를 놓친다).
#pragma warning(push)
#pragma warning(disable : 4324)
        struct alignas(64) LaneCounter
        {
            std::atomic<uint64_t> value{0};
        };
#pragma warning(pop)

        // useHash -- 주인을 레인에 배정하는 방식. 기본은 해시이고, **주인이 0부터 촘촘한
        // 서수인 레인만 끈다**(그래야 "N번 주인은 반드시 N번 레인"이 보장된다).
        // 자세한 건 LaneIndexOf 주석.
        MessageProducer(const EProducerType producerType, const int32_t laneCount,
                        const ELaneBackend backend, const bool useHash)
            : producerType_(producerType)
            , laneCount_(laneCount)
            , useHash_(useHash)
            , lanes_(MakeLaneSet(backend, static_cast<size_t>(laneCount)))
            , pushCounts_(std::make_unique<std::vector<LaneCounter>>(
                  static_cast<size_t>(laneCount > 0 ? laneCount : 1)))
        {
            if (laneCount <= 0)
            {
                throw Base::CoreException(Base::ECoreErrorCode::InvalidArgument,
                                          "MessageProducer: laneCount는 0보다 커야 한다");
            }
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
            // ① ownerId -> 어느 레인으로 갈지. 같은 ownerId는 항상 같은 레인이라 순서가
            //    보장되고 그 주인의 상태에 락이 필요 없다.
            //    **레인 밖에서 같은 규칙으로 샤딩하는 쪽도 LaneIndexOf를 불러야 한다.**
            const auto laneIndex = LaneIndexOf(message->msgOwnerId, lanes_->LaneCount(), useHash_);

            // **배정이 의도대로 되는지 보려면 이 수가 있어야 한다.** 밀린 개수(PendingCount)는
            // "지금 막혔나"만 알려주고, 평소에 고르게 갈리는지는 못 알려준다 -- 노는 레인이
            // 있어도 밀린 게 없으면 0 으로 똑같이 보인다.
            (*pushCounts_)[laneIndex].value.fetch_add(1, std::memory_order_relaxed);

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

        // 그 레인으로 **배정된 누적 개수**. 밀린 개수와 쓰임이 다르다 --
        // 이쪽은 "주인이 레인에 고르게 갈리는가"를 본다.
        [[nodiscard]] uint64_t PushCount(const size_t laneIndex) const noexcept
        {
            return (*pushCounts_)[laneIndex].value.load(std::memory_order_relaxed);
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
        const bool useHash_{true};

        std::vector<std::unique_ptr<MessageProcessor>> processors_;

        // PushMsg(const) 안에서도 넣어야 하므로 포인터가 가리키는 쪽은 const 가 아니다.
        const std::unique_ptr<ILaneSet> lanes_;

        // 레인별 배정 누적. lanes_ 와 같은 이유로 포인터 뒤에 둔다.
        //
        // **relaxed 로 센다** -- 통계라 순서 보장이 필요 없고, 여기에 동기화를 걸면
        // 재는 행위가 재려는 대상을 느리게 만든다.
        //
        // **캐시 라인 단위로 띄운다.** 8바이트짜리를 붙여 놓으면 레인 8개의 계수기가 한
        // 라인에 다 들어가서, 서로 다른 레인을 세는 스레드들이 같은 라인을 계속 뺏고 뺏긴다
        // (거짓 공유). 그러면 부하가 커질수록 이 통계 자체가 병목이 된다.
        const std::unique_ptr<std::vector<LaneCounter>> pushCounts_;
    };
}
