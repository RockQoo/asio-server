#pragma once

#include "Shared/Core/Src/Common/BasicTypes.h"

#include <string_view>

namespace Zone
{
    // 이 프로세스의 프로세서 목록. **프로세서는 스레드가 아니다** -- 큐 그룹(스레드 N개) 위에
    // 얹히고, 어느 스레드에서 도는지는 메시지의 ownerId가 정한다
    // (Shared/Core/Src/Processor/ProcessorGroup.h 주석 참고).
    //
    // Zone은 World와 달리 프로세서가 그룹마다 거의 하나씩인데, 그건 **owner가 다르기 때문**이다.
    // World는 Main/Tool이 둘 다 clientSessionId를 주인으로 삼아서 한 그룹을 공유할 수 있었지만,
    // 여기서는 주인이 갈린다:
    //
    //   Player    -- owner = clientSessionId. 우편/재화/UnitOfWork처럼 그 사람만의 것
    //   ZoneSpace -- owner = zoneId.          로스터/좌표/경계처럼 존 전체가 공유하는 것
    //
    // 이 둘을 한 레인에 두면 owner를 하나로 못 정해서 결국 존 키로 통일되고, 그러면 그 존의
    // 모든 콘텐츠가 스레드 하나로 직렬화된다(1만 세션 부하 테스트가 무너진 원인이 정확히 그것).
    enum class EProcessorId : uint8_t
    {
        Lb,         // World 링크 수신 파싱 + 1차 분기 -- LB 그룹
        Player,     // 우편/재화/UnitOfWork (owner = clientSessionId) -- BASIC 그룹
        ZoneSpace,  // 로스터/좌표/경계/틱      (owner = zoneId)       -- TICK 그룹
        Broadcast,  // 팬아웃 전송              (owner = zoneId)       -- BROADCAST 그룹
        Count,
    };

    [[nodiscard]] inline std::string_view ToString(const EProcessorId processorId)
    {
        switch (processorId)
        {
        case EProcessorId::Lb:        return "Lb";
        case EProcessorId::Player:    return "Player";
        case EProcessorId::ZoneSpace: return "ZoneSpace";
        case EProcessorId::Broadcast: return "Broadcast";
        case EProcessorId::Count:     break;
        }
        return "Unknown";
    }
}
