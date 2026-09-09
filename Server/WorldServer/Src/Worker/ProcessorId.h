#pragma once

#include "Shared/Core/Src/Common/BasicTypes.h"

#include <string_view>

namespace World
{
    // 이 프로세스의 프로세서 목록. **프로세서는 스레드가 아니다** -- 하나의 큐 그룹(스레드 N개)
    // 위에 여러 프로세서가 함께 얹히고, 어느 스레드에서 도는지는 메시지의 ownerId가 정한다
    // (Shared/Core/Src/Processor/ProcessorGroup.h 주석 참고). 그래서 Main과 Tool은 같은 BASIC
    // 스레드들을 공유하며, ownerId가 같으면 서로 다른 프로세서의 메시지끼리도 같은 스레드에서
    // 순서대로 처리된다 -- 그게 이 구조가 락 없이 성립하는 이유다.
    //
    // ELogCategory와 같은 이유로 Core가 아니라 여기 둔다: Core는 어떤 프로세서가 있는지
    // 몰라야 한다. Count는 ProcessorGroup이 통계 배열 크기를 잡는 데 쓰므로 항상 마지막이다.
    enum class EProcessorId : uint8_t
    {
        Main,   // 클라이언트 라우팅(ClientRegistry / ZoneLinkRegistry) -- BASIC 그룹
        Tool,   // 운영툴 명령 -- BASIC 그룹(Main과 스레드를 공유한다)
        Db,     // UnitOfWork 스트림 적재 -- DB 그룹(블로킹을 허용하는 별도 그룹)
        Count,
    };

    [[nodiscard]] inline std::string_view ToString(const EProcessorId processorId)
    {
        switch (processorId)
        {
        case EProcessorId::Main:  return "Main";
        case EProcessorId::Tool:  return "Tool";
        case EProcessorId::Db:    return "Db";
        case EProcessorId::Count: break;
        }
        return "Unknown";
    }
}
