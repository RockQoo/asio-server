#include "pch.h"
#include "Zone/Zone.h"

Zone::Zone(const ZoneDef& def, Network::SessionHolder& worldLink)
    : def_(def)
    , worldLink_(worldLink)
{
}

void Zone::Tick(const UnitTickContext& context)
{
    unitContainer_.Tick(context);

    // 순회가 끝난 **뒤에** 확정한다 -- 이 줄이 위로 올라가면 병렬 순회 중에 대상 목록이
    // 바뀌고, 그 순간 raw 포인터가 매달린다.
    unitContainer_.OnEndTick();
}
