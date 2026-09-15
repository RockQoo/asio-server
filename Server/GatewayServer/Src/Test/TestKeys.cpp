#include "pch.h"
#include "Test/TestKeys.h"

#include "Shared/Core/Src/Console/KeyBinder.h"

namespace Gateway
{
    namespace
    {
        // F1. **이 함수는 콘솔 입력 스레드에서 돈다 -- 서버 레인이 아니다.**
        // 공유 상태를 직접 만지면 스레드 규약 밖에서 만지는 것이 된다. 레인으로 넘겨야 하는
        // 일이 생기면 WorldServer의 Test/MsgId.h(PushMsg + Message::Router)를 참고할 것.
        void TestFunc1(const bool enabled)
        {
            LOG.Info(ELogCategory::General, "Hello Gateway Server").KV("Enabled", enabled);
        }
    }

    void RegisterTestKeys(Console::KeyBinder& keyBinder)
    {
        keyBinder.Bind(Console::EKey::F1, "TestFunc1", &TestFunc1);
    }
}
