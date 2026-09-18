#pragma once

#include "Server/Core/Src/Pipeline/Message.h"

namespace Pipeline
{
    // 레인 N개를 굴리는 방식. **구현이 둘이고 config로 고른다** -- 지우지 않고 둘 다 남긴다.
    //
    //   Queue   스레드 1개 + 큐 1개 + mutex/condvar. **스레드와 레인이 1:1**이라
    //           `Basic#3`이 곧 3번 레인의 주인이다. 실무 원본(MessageConsumer)과 같다.
    //   Strand  asio::strand. 레인에 넣은 일을 **한가한 스레드가 아무거나 집어간다**.
    //           같은 레인끼리 겹치지 않는 것은 그대로다.
    //
    // **둘의 차이가 곧 어피니티 vs 워크 스틸링**이고, 그게 이 인터페이스를 둔 이유다 --
    // 3번 레인이 뜨겁고 5번이 놀 때, Queue 는 5번 스레드가 그냥 놀고 Strand 는 5번 스레드가
    // 3번 레인의 밀린 일을 집어간다. 어느 쪽이 나은지는 재봐야 안다.
    //
    // 인터페이스가 가상 함수라 메시지마다 간접 호출이 하나 붙지만, **두 백엔드에 똑같이**
    // 붙으므로 비교 자체는 흐트러지지 않는다.
    class ILaneSet
    {
    public:
        virtual ~ILaneSet() = default;

        ILaneSet(const ILaneSet&) = delete;
        ILaneSet& operator=(const ILaneSet&) = delete;

        // 레인과 스레드를 만든다. 이름은 `Basic` 처럼 레인 이름이고, 스레드에는 `#N`이 붙는다.
        virtual void Start(const std::string& name) = 0;

        // **밀린 것을 전부 소진한 뒤** 스레드를 끝낸다. 급하게 내리면 몇 초 분량의 플레이
        // 결과가 사라진다.
        virtual void Stop() = 0;

        // laneIndex 번 레인에 넣는다. 어느 스레드에서 불러도 된다.
        virtual void Post(const size_t laneIndex, MessagePtr message) = 0;

        [[nodiscard]] virtual size_t LaneCount() const noexcept = 0;

        // 그 레인에 아직 처리되지 않고 남은 개수. **이 값이 계속 크면 그 레인이 밀린 것**이라
        // 부하에서 제일 먼저 볼 수다. 백엔드마다 재는 방법이 달라 여기로 올려둔다.
        [[nodiscard]] virtual size_t PendingCount(const size_t laneIndex) const noexcept = 0;

        // 이 백엔드의 이름. 기동 로그와 측정 기록에 남긴다 -- 어느 쪽으로 잰 수치인지
        // 나중에 구분할 수 없으면 비교 자체가 무의미해진다.
        [[nodiscard]] virtual std::string_view BackendName() const noexcept = 0;

    protected:
        ILaneSet() = default;
    };
}
