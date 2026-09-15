#include "pch.h"
#include "Test/TestProcessor.h"

#include "Test/TestValue.h"

namespace World
{
    void TestProcessor::Register(MsgRouter& msgRouter)
    {
        msgRouter.Bind(EProcessorId::Test, EMsgId::QueueTest, &HandleQueueTest);
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
}
