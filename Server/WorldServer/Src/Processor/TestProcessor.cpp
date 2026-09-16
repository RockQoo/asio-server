#include "pch.h"
#include "Processor/TestProcessor.h"

#include "Test/TestValue.h"

namespace World
{
    void TestProcessor::Register(MsgRouter& msgRouter)
    {
        msgRouter.Bind(EProcessorId::Test, EMsgId::QueueTest, &HandleQueueTest);
        msgRouter.Bind(EProcessorId::Test, EMsgId::LaneProbe, &HandleLaneProbe);
    }

    void TestProcessor::HandleQueueTest(Packet::BinaryReader& binaryReader)
    {
        // **넣은 순서 그대로** 꺼낸다. 순서가 어긋나면 값이 조용히 뒤섞인다.
        int32_t number{};
        std::string text;
        TestValue value{};
        if (!Packet::ReadArgs(binaryReader, number, text, value))
        {
            LOG.Error(ELogCategory::General, "QueueTest 인자 해석 실패 -- 넣은 순서와 꺼내는 순서가 다르다");
            return;
        }

        LOG.Info(ELogCategory::General, "QueueTest 수신(BASIC 레인)")
            .KV("Number", number).KV("Text", text)
            .KV("Hp", value.hp).KV("Speed", value.speed);
    }

    // strand가 무엇을 보장하고 무엇을 보장하지 않는지 로그로 확인하는 관측용 핸들러.
    // 읽는 법: 같은 Owner의 줄에서 **Tid는 바뀌어도 되지만 InFlight는 항상 1이어야 한다.**
    void TestProcessor::HandleLaneProbe(Packet::BinaryReader& binaryReader)
    {
        int32_t seq{};
        uint64_t ownerId{};
        if (!Packet::ReadArgs(binaryReader, seq, ownerId))
        {
            LOG.Error(ELogCategory::General, "LaneProbe 인자 해석 실패");
            return;
        }

        // 클래스에 상태를 두지 않는다는 규칙의 의도된 예외다 -- 겹침을 세는 것 자체가 목적이라
        // 여러 스레드가 공유해야 하고, 그래서 atomic이다.
        static std::atomic<int32_t> inFlight{0};
        const int32_t enteredCount = inFlight.fetch_add(1, std::memory_order_acq_rel) + 1;

        // **레인 스레드를 일부러 붙잡는다.** 핸들러가 순식간에 끝나면 한 스레드가 그 strand의
        // 큐를 통째로 비워버려 다른 스레드로 넘어갈 틈이 생기지 않는다(재배정은 ready_queue를
        // 다 비운 뒤에 일어난다). 관측용이라 허용하는 것이고, 실제 핸들러에서 레인을 재우면
        // 그 레인에 걸린 모든 주인이 같이 멈춘다.
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        LOG.Info(ELogCategory::General, "LaneProbe 수신(BASIC 레인)")
            .KV("Seq", seq).KV("Owner", ownerId)
            .KV("Tid", ::GetCurrentThreadId())
            .KV("InFlight", enteredCount);

        inFlight.fetch_sub(1, std::memory_order_acq_rel);
    }
}
