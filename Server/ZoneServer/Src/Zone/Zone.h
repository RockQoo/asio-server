#pragma once

#include "App/ZoneDef.h"
#include "Unit/UnitContainer.h"

#include "Server/Core/Src/Network/SessionHolder.h"

// 존 하나의 **게임 세계**. 로스터와 공간 상태가 여기 있고, 그 위에서 도는 처리기
// (`ZoneProcessor` · `TickProcessor` · `BroadcastProcessor`)는 **데이터를 갖지 않는다.**
//
// **왜 처리기가 아니라 여기에 두는가**: 같은 존을 여러 레인이 본다.
//
//   ZoneProcessor(BASIC, owner = playerId)  판단하고 예약한다 -- 이동 검증, 콘텐츠, 입·퇴장
//   TickProcessor(TICK,  owner = 존 틱 주인) 예약된 것을 실행한다 -- 적분, 경계 판정, 만기
//
// 처리기마다 상태를 들면 같은 세계가 여러 벌이 된다. 그래서 세계는 이 객체 하나뿐이고,
// 처리기들이 `shared_ptr` 로 같은 것을 가리킨다.
//
// **World 링크를 존이 소유한다.** 연결이 zoneId 하나당 하나라, "이 유닛의 변경을 어디로
// 올리나"의 답이 곧 그 유닛이 속한 존이다.
class Zone final
{
public:
    using SPtr = std::shared_ptr<Zone>;

    Zone(const ZoneDef& def, Network::SessionHolder& worldLink);

    Zone(const Zone&) = delete;
    Zone& operator=(const Zone&) = delete;

    [[nodiscard]] Common::ZoneId GetZoneId() const noexcept { return def_.zoneId; }
    [[nodiscard]] const ZoneDef& GetDef() const noexcept { return def_; }

    // 이 존의 변경을 World 로 올리는 링크. Player 가 생성될 때 이걸 받아 든다.
    [[nodiscard]] Network::SessionHolder& GetWorldLink() const noexcept { return worldLink_; }

    [[nodiscard]] UnitContainer& Units() noexcept { return unitContainer_; }
    [[nodiscard]] const UnitContainer& Units() const noexcept { return unitContainer_; }

    // **TICK 레인 전용.** 유닛 전원을 (병렬로) 돌린 뒤, 그 사이에 예약된 입·퇴장을 확정한다.
    // 순서를 바꾸면 순회 중에 목록이 바뀐다.
    void Tick(const UnitTickContext& context);

private:
    // 담당 구간을 필드로 흩지 않고 정의 그대로 들고 있는다 -- 경계 검사(ZoneDef::Contains)를
    // 한 곳에만 두면 x/y 중 한쪽만 빠뜨리는 실수가 안 생긴다.
    const ZoneDef def_;

    Network::SessionHolder& worldLink_;

    UnitContainer unitContainer_;
};
