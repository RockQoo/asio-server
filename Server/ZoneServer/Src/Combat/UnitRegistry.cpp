#include "pch.h"
#include "Combat/UnitRegistry.h"

#include "Combat/AttackDef.h"

namespace Combat
{
    Unit* UnitRegistry::Find(const Protocol::UnitId unitId)
    {
        const auto it = units_.find(unitId);
        return it == units_.end() ? nullptr : &it->second;
    }

    const Unit* UnitRegistry::Find(const Protocol::UnitId unitId) const
    {
        const auto it = units_.find(unitId);
        return it == units_.end() ? nullptr : &it->second;
    }

    Unit& UnitRegistry::SpawnPlayer(const Network::SessionId clientSessionId, const float x, const float y)
    {
        const auto unitId = UnitIdOf(clientSessionId);

        // **HP는 존에 들어올 때마다 최대치로 채운다.** 존 서버는 DB를 만지지 않고, 세로
        // 핸드오프에서는 Player 객체 자체가 새로 만들어지므로 이어받을 값이 애초에 없다.
        // 값을 이어받게 하려면 World가 전투 스탯을 들고 날라야 하는데, 그러면 "World는
        // 전투를 모른다"가 깨진다(docs/design/combat-lane.md).
        Unit unit{};
        unit.id = unitId;
        unit.kind = EUnitKind::Player;
        unit.ownerSessionId = clientSessionId;
        unit.x = x;
        unit.y = y;
        unit.boundRadius = kUnitBoundRadius;
        unit.stat = kPlayerStat;
        unit.spawnX = x;
        unit.spawnY = y;

        units_[unitId] = unit;
        return units_[unitId];
    }

    Unit& UnitRegistry::SpawnMonster(const float x, const float y)
    {
        const auto unitId = Protocol::UnitId{Protocol::kMonsterUnitIdBase + ++nextMonsterSeq_};

        Unit unit{};
        unit.id = unitId;
        unit.kind = EUnitKind::Monster;
        unit.x = x;
        unit.y = y;
        unit.boundRadius = kUnitBoundRadius;
        unit.stat = kMonsterStat;
        unit.spawnX = x;
        unit.spawnY = y;

        units_[unitId] = unit;
        return units_[unitId];
    }

    bool UnitRegistry::Despawn(const Protocol::UnitId unitId)
    {
        return units_.erase(unitId) > 0;
    }
}
