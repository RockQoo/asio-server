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

        // F2/F3가 한 번에 밀어 넣는 메시지 수. 레인 수(basic_threads)보다 넉넉해야 분산이 보인다.
        constexpr int32_t kProbeCount = 16;

        // F2가 쓰는 고정 주인. 값 자체는 아무 수나 되고, 중요한 것은 **전부 같다**는 것이다
        // -- ownerId % 레인수로 strand가 정해지므로 16개가 전부 같은 strand에 줄을 선다.
        constexpr uint64_t kProbeOwnerId = 7;

        // F2. 주인 하나에 몰아넣는다. 로그의 Tid가 여러 개 찍히면 "strand = 스레드 고정"이
        // 아니라는 증거이고, InFlight가 끝까지 1이면 그래도 겹치지는 않는다는 증거다.
        // Seq가 0..15 순서대로 찍히는 것이 세 번째 보장(순서)이다.
        void TestFunc2(const bool enabled)
        {
            LOG.Info(ELogCategory::General, "F2 같은 주인으로 LaneProbe").KV("Enabled", enabled)
                .KV("Owner", kProbeOwnerId).KV("Count", kProbeCount);

            for (int32_t seq = 0; seq < kProbeCount; ++seq)
            {
                PushMsg(EMsgId::LaneProbe, EProcessorId::Test, kProbeOwnerId, seq, kProbeOwnerId);
            }
        }

        // F3. 대조군 -- 주인을 전부 다르게 준다. strand가 흩어져 여러 스레드가 **동시에** 돌고,
        // 그래서 InFlight가 2 이상으로 찍히는 것이 정상이다(F2와 이 값이 갈리는 것이 핵심).
        void TestFunc3(const bool enabled)
        {
            LOG.Info(ELogCategory::General, "F3 서로 다른 주인으로 LaneProbe").KV("Enabled", enabled)
                .KV("Count", kProbeCount);

            for (int32_t seq = 0; seq < kProbeCount; ++seq)
            {
                const auto ownerId = static_cast<uint64_t>(seq);
                PushMsg(EMsgId::LaneProbe, EProcessorId::Test, ownerId, seq, ownerId);
            }
        }
    }

    void RegisterTestKeys(Console::KeyBinder& keyBinder)
    {
        keyBinder.Bind(Console::EKey::F1, "TestFunc1 (로그 + Test 프로세서로 QueueTest)", &TestFunc1);
        keyBinder.Bind(Console::EKey::F2, "TestFunc2 (같은 주인 16개 -- strand 직렬화 확인)", &TestFunc2);
        keyBinder.Bind(Console::EKey::F3, "TestFunc3 (다른 주인 16개 -- 동시 실행 대조군)", &TestFunc3);
    }
}
