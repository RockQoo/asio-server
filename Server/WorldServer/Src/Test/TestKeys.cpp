#include "pch.h"
#include "Test/TestKeys.h"

#include "Test/MsgId.h"
#include "Test/TestValue.h"

#include "Shared/Core/Src/Console/KeyBinder.h"

namespace World
{
    namespace
    {
        // F1. **이 함수는 콘솔 입력 스레드에서 돈다 -- 서버 레인이 아니다.**
        // 그래서 여기서 직접 할 수 있는 것은 로그처럼 공유 상태를 안 만지는 일뿐이고,
        // 서버 상태를 만지는 일은 PushMsg로 해당 프로세서의 레인에 넘긴다.
        void TestFunc1(const bool enabled)
        {
            LOG.Info(ELogCategory::General, "F1 TestFunc1").KV("Enabled", enabled);

            // 넣은 순서 그대로 꺼내진다 -> TestProcessor::HandleQueueTest에서
            // Number=10, Text="zeus", Hp=100, Speed=1.5 로 찍힌다.
            //
            // 두 번째 인자만 바꾸면 같은 메시지가 다른 레인에서 처리된다 -- 받는 쪽 객체를
            // 여기서 알 필요가 없다는 것이 이 통로의 핵심이다.
            PushMsg(EMsgId::QueueTest, EProcessorId::Test, 0, 10, "zeus",
                    TestValue{.hp = 100, .speed = 1.5f});
        }
    }

    void RegisterTestKeys(Console::KeyBinder& keyBinder)
    {
        keyBinder.Bind(Console::EKey::F1, "TestFunc1 (로그 + Test 프로세서로 QueueTest)", &TestFunc1);
    }
}
