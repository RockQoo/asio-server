#pragma once

#include "Test/MsgId.h"

namespace World
{
    // 테스트 메시지 핸들러 모음. **보내는 쪽과 분리돼 있다** -- 보내는 것은 PushMsg 자유
    // 함수이고(MsgId.h), 이 클래스는 받아서 처리하는 쪽만 맡는다.
    //
    // 전부 static이다. 상태를 두면 BASIC 레인의 여러 스레드가 공유하는 변수가 된다
    // (PlayerMail이 static인 것과 같은 이유).
    class TestProcessor final
    {
    public:
        TestProcessor() = delete;

        // **App::Run에서 KeyBinder::Start()보다 먼저 부른다.** 라우터의 핸들러 표는 레인
        // 스레드들이 락 없이 읽으므로, 도는 중에 바인딩하면 경합한다.
        static void Register(MsgRouter& msgRouter);

    private:
        static void HandleQueueTest(Packet::BinaryReader& binaryReader);
        static void HandleLaneProbe(Packet::BinaryReader& binaryReader);
    };
}
