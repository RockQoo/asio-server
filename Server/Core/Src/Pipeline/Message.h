#pragma once

#include "Server/Core/Src/Pipeline/Types.h"

namespace Pipeline
{
    class MessageProcessor;

    // 레인의 큐에 실려 다니는 것.
    //
    // body의 실제 타입은 **핸들러만 안다**. 보내는 쪽과 받는 쪽이 같은 구조체를 쓰기로
    // 약속하고 msgType이 그 약속의 이름이다 -- 그래서 **어긋나면 컴파일이 아니라 런타임에
    // 깨진다.** msgType 하나에 body 구조체 하나가 이 구조의 유일한 규율이다.
    //
    // 직렬화하지 않고 shared_ptr로 넘기는 것은 같은 프로세스 안이기 때문이고, 덕분에
    // Session::SPtr 같은 것도 그대로 실린다.
    struct Message final
    {
        uint32_t msgType{};                 // 어떤 핸들러를 부를지
        ProcessorId targetProcessorId{};    // 어느 프로세서가 처리할지
        OwnerId msgOwnerId{};               // 어느 레인으로 갈지
        std::shared_ptr<void> body;         // 페이로드 (실제 타입은 핸들러가 안다)

        // **PushMsg 시점(= 보내는 스레드)에 미리 찾아 박아둔다.** 레인 스레드는 조회 없이
        // 호출만 한다 -- 맵 조회도, 문자열 비교도, switch도 없다.
        MessageProcessor* targetProcessor{};
    };

    using MessagePtr = std::unique_ptr<Message>;
}
