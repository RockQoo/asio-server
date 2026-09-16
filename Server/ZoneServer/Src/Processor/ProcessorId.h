#pragma once

#include "Shared/Core/Src/Base/BasicTypes.h"

// 이 프로세스의 프로세서 목록. **프로세서는 스레드가 아니다** -- 큐 그룹(스레드 N개) 위에
// 얹히고, 어느 스레드에서 도는지는 메시지의 ownerId가 정한다
// (Shared/Core/Src/Processor/Group.h 주석 참고).
//
// **태그 하나 = Src/Processor/ 의 클래스 하나**다:
//
//   Player    -- PlayerProcessor    owner = clientSessionId. 우편/재화/UnitOfWork처럼 그 사람만의 것
//   Zone      -- ZoneProcessor      owner = zoneId.          로스터/좌표/경계처럼 존 전체가 공유하는 것
//   Broadcast -- BroadcastProcessor owner = zoneId.          팬아웃 전송
//
// Player와 Zone을 한 레인에 두면 owner를 하나로 못 정해서 결국 존 키로 통일되고, 그러면 그
// 존의 모든 콘텐츠가 스레드 하나로 직렬화된다(1만 세션 부하 테스트가 무너진 원인이 정확히
// 그것).
//
// **예전에 있던 Lb 태그는 없앴다.** 수신 파싱을 전담하던 레인인데, I/O 스레드가 뽑는
// ownerId와 그 다음 단계의 ownerId가 **어차피 같은 clientSessionId**라 홉만 하나 더
// 늘리고 있었다. 지금은 I/O에서 곧장 플레이어 레인으로 넘기고 파싱도 거기서 한다.
// 중간 단계가 값을 하려면 "주인을 페이로드에서 못 뽑는 패킷"이 있어야 하는데 하나도 없다.
enum class EZoneProcessorId : uint8_t
{
    Player,     // 수신 파싱 + 우편/재화/ZoneUnitOfWork (owner = clientSessionId) -- BASIC 그룹
    Zone,       // 로스터/좌표/경계/틱              (owner = zoneId)          -- TICK 그룹
    Broadcast,  // 팬아웃 전송                      (owner = zoneId)          -- BROADCAST 그룹
    Count,
};

[[nodiscard]] inline std::string_view ToString(const EZoneProcessorId processorId)
{
    switch (processorId)
    {
    case EZoneProcessorId::Player:    return "Player";
    case EZoneProcessorId::Zone:      return "Zone";
    case EZoneProcessorId::Broadcast: return "Broadcast";
    case EZoneProcessorId::Count:     break;
    }
    return "Unknown";
}
