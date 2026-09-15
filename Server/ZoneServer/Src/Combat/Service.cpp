#include "pch.h"
#include "Combat/Service.h"

#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Protocol/Src/ContentLimit.h"
#include "Shared/Protocol/Src/ErrorCode.h"

namespace Combat
{
    namespace
    {
        // 존 사각형 안에서의 상대 위치(0~1). 가장자리를 피해 안쪽에 세운다 -- 경계에 붙여
        // 두면 옆 존으로 넘어가는 플레이어가 지나가는 김에 계속 맞히게 된다.
        constexpr std::array<std::pair<float, float>, kMonstersPerZone> kMonsterSpots{{
            {0.30f, 0.30f},
            {0.70f, 0.35f},
            {0.50f, 0.72f},
        }};

        [[nodiscard]] Zone::UnitSpawnEntry MakeSpawnEntry(const Unit& unit) noexcept
        {
            Zone::UnitSpawnEntry entry{};
            entry.unitId = unit.id.Value();
            entry.kind = static_cast<uint8_t>(unit.kind);
            entry.x = unit.x;
            entry.y = unit.y;
            entry.hp = unit.stat.hp;
            entry.maxHp = unit.stat.maxHp;
            entry.mp = unit.stat.mp;
            entry.maxMp = unit.stat.maxMp;
            return entry;
        }

        // 초당 회복량을 정수 HP로 떨어뜨린다. 남는 소수는 호출부가 이월한다 -- 이월하지
        // 않으면 초당 2 회복이 100ms 틱에서 매번 0으로 잘려 영영 회복하지 않는다.
        [[nodiscard]] int32_t TakeWhole(float& carry, const float ratePerSec, const float deltaSeconds) noexcept
        {
            if (ratePerSec <= 0.0f)
            {
                return 0;
            }

            carry += ratePerSec * deltaSeconds;
            const auto whole = static_cast<int32_t>(carry);
            carry -= static_cast<float>(whole);
            return whole;
        }
    }

    Service::Service(const Protocol::ZoneId zoneId, ISender& sender)
        : zoneId_(zoneId)
        , sender_(sender)
    {
    }

    void Service::SpawnMonsters(const Zone::Def& zoneDef)
    {
        const auto width = zoneDef.xMax - zoneDef.xMin;
        const auto height = zoneDef.yMax - zoneDef.yMin;

        for (const auto& [fractionX, fractionY] : kMonsterSpots)
        {
            const auto& unit = units_.SpawnMonster(zoneDef.xMin + (width * fractionX),
                                                   zoneDef.yMin + (height * fractionY));

            LOG.Info(ELogCategory::Zone, "몬스터 배치")
                .KV("Zone", zoneId_).KV("UnitId", unit.id).KV("X", unit.x).KV("Y", unit.y);
        }
    }

    void Service::OnPlayerEnter(const Network::SessionId clientSessionId, const float x, const float y)
    {
        const auto& unit = units_.SpawnPlayer(clientSessionId, x, y);

        // 들어온 사람에게는 존 전체를, 이미 있던 사람들에게는 새로 온 한 기만 보낸다.
        // **전체 목록을 브로드캐스트하지 않는다** -- 인구가 늘수록 입장 한 번의 비용이
        // 인구의 제곱으로 커진다.
        SendSnapshotTo(clientSessionId);
        BroadcastSpawn(unit, clientSessionId);
    }

    void Service::OnPlayerLeave(const Network::SessionId clientSessionId)
    {
        const auto unitId = UnitRegistry::UnitIdOf(clientSessionId);
        if (!units_.Despawn(unitId))
        {
            return;
        }

        Zone::UnitDespawnPacket despawn{};
        despawn.unitId = unitId.Value();
        sender_.BroadcastToZone(PacketId::Z2CUnitDespawn, std::as_bytes(std::span(&despawn, 1)),
                                clientSessionId);
    }

    void Service::UpdatePlayerPosition(const Network::SessionId clientSessionId, const float x, const float y)
    {
        if (auto* const unit = units_.Find(UnitRegistry::UnitIdOf(clientSessionId)))
        {
            unit->x = x;
            unit->y = y;
        }
    }

    void Service::HandleAttack(const Network::SessionId clientSessionId, const Zone::AttackPacket& packet)
    {
        // **어떤 경로로 끝나든 시전자에게 응답이 간다.** 클라이언트가 무응답으로 멈추는
        // 경로를 만들지 않는 것이 이 함수의 규약이다.
        const auto now = std::chrono::steady_clock::now();

        if (packet.attackKind > static_cast<uint8_t>(EAttackKind::Ranged))
        {
            ReplyAttackResult(clientSessionId, EErrorCode::InvalidPayload, packet, 0, 0);
            return;
        }

        const auto attackKind = static_cast<EAttackKind>(packet.attackKind);
        const auto& attackDef = DefOf(attackKind);

        auto* const attacker = units_.Find(UnitRegistry::UnitIdOf(clientSessionId));
        if (!attacker)
        {
            ReplyAttackResult(clientSessionId, EErrorCode::CombatUnitNotFound, packet, 0, 0);
            return;
        }

        if (attacker->stat.IsDead())
        {
            ReplyAttackResult(clientSessionId, EErrorCode::CombatSelfDead, packet, 0, 0);
            return;
        }

        auto* const target = units_.Find(Protocol::UnitId{packet.targetUnitId});
        if (!target)
        {
            ReplyAttackResult(clientSessionId, EErrorCode::CombatNoTarget, packet, 0, 0);
            return;
        }

        if (target->stat.IsDead())
        {
            ReplyAttackResult(clientSessionId, EErrorCode::CombatTargetDead, packet, 0, target->stat.hp);
            return;
        }

        if (target->kind == attacker->kind)
        {
            ReplyAttackResult(clientSessionId, EErrorCode::CombatSameKind, packet, 0, target->stat.hp);
            return;
        }

        if (now - attacker->lastAttackAt < attackDef.cooldown)
        {
            ReplyAttackResult(clientSessionId, EErrorCode::CombatOnCooldown, packet, 0, target->stat.hp);
            return;
        }

        if (!InRange(*attacker, *target, attackDef.range))
        {
            ReplyAttackResult(clientSessionId, EErrorCode::CombatOutOfRange, packet, 0, target->stat.hp);
            return;
        }

        if (attacker->stat.mp < attackDef.costMp)
        {
            ReplyAttackResult(clientSessionId, EErrorCode::CombatNotEnoughMp, packet, 0, target->stat.hp);
            return;
        }

        // --- 여기부터 실패하지 않는다. 검증을 전부 통과한 뒤에 자원을 소모하므로 롤백이 없다 ---
        // 시전 자원을 되돌리는 코드가 필요해지는 건 시전 시간이 있을 때다. 즉발만 있으면
        // "소모했는데 실패했다"가 만들어지지 않는다.
        attacker->stat.mp -= attackDef.costMp;
        attacker->lastAttackAt = now;
        attacker->dirty = true;

        const auto attackerId = attacker->id;
        const auto targetId = target->id;
        const auto hit = ApplyHit(attackerId, targetId, attackDef, now);

        ReplyAttackResult(clientSessionId, EErrorCode::Success, packet, hit.damage, hit.targetHp);

        if (const auto* const refreshed = units_.Find(attackerId))
        {
            BroadcastAttack(*refreshed, targetId, attackKind, hit.damage);
        }
    }

    Service::HitResult Service::ApplyHit(const Protocol::UnitId attackerId, const Protocol::UnitId targetId,
                                         const AttackDef& attackDef,
                                         const std::chrono::steady_clock::time_point now)
    {
        auto* const attacker = units_.Find(attackerId);
        auto* const target = units_.Find(targetId);

        // 즉발인 지금은 한 번도 참이 되지 않는다(같은 strand에서 바로 불리므로 사라질 틈이
        // 없다). 그래도 써 두는 이유는 Service.h의 ApplyHit 주석 참고.
        if (!attacker || !target || target->stat.IsDead())
        {
            return HitResult{0, 0, false};
        }

        const auto damage = CalcDamage(attacker->stat, target->stat, attackDef);
        const auto assigned = AssignDamage(target->stat, damage);
        target->dirty = true;

        if (assigned.dead)
        {
            OnUnitDead(*target, attackerId, now);
        }

        return HitResult{assigned.applied, target->stat.hp, assigned.dead};
    }

    void Service::OnUnitDead(Unit& unit, const Protocol::UnitId killerId,
                             const std::chrono::steady_clock::time_point now)
    {
        unit.respawnAt = now + kRespawnDelay;

        // 사망은 **틱을 기다리지 않고 즉시** 나간다. 상태 동기화(HP 0)와 같이 묶으면 최대
        // 한 틱만큼 쓰러지는 연출이 늦고, 그 사이 클라이언트가 죽은 대상을 계속 때린다.
        Zone::UnitDeadPacket dead{};
        dead.unitId = unit.id.Value();
        dead.killerUnitId = killerId.Value();
        sender_.BroadcastToZone(PacketId::Z2CUnitDead, std::as_bytes(std::span(&dead, 1)), 0);

        LOG.Info(ELogCategory::Zone, "유닛 사망")
            .KV("Zone", zoneId_).KV("UnitId", unit.id).KV("KillerUnitId", killerId);
    }

    void Service::Tick(const std::chrono::steady_clock::time_point now, const float deltaSeconds)
    {
        UpdateRegen(deltaSeconds);
        UpdateRespawn(now);
        UpdateMonsterAi(now);
        FlushDirtyUnits();
    }

    void Service::UpdateRegen(const float deltaSeconds)
    {
        units_.ForEach([deltaSeconds](Unit& unit)
        {
            if (unit.stat.IsDead())
            {
                return;
            }

            if (const auto healed = TakeWhole(unit.regenCarryHp, unit.stat.regenHpPerSec, deltaSeconds);
                healed > 0 && unit.stat.hp < unit.stat.maxHp)
            {
                unit.stat.hp = std::min(unit.stat.maxHp, unit.stat.hp + healed);
                unit.dirty = true;
            }

            if (const auto restored = TakeWhole(unit.regenCarryMp, unit.stat.regenMpPerSec, deltaSeconds);
                restored > 0 && unit.stat.mp < unit.stat.maxMp)
            {
                unit.stat.mp = std::min(unit.stat.maxMp, unit.stat.mp + restored);
                unit.dirty = true;
            }
        });
    }

    void Service::UpdateRespawn(const std::chrono::steady_clock::time_point now)
    {
        // 되살아난 유닛은 등장 통지로 다시 알린다 -- 순회 중에 전송하면 이 레인이 팬아웃만큼
        // 멈추므로, 만든 항목을 모았다가 순회가 끝난 뒤에 보낸다.
        std::vector<Zone::UnitSpawnEntry> respawned;

        units_.ForEach([&respawned, now](Unit& unit)
        {
            if (!unit.stat.IsDead() || now < unit.respawnAt)
            {
                return;
            }

            unit.stat.hp = unit.stat.maxHp;
            unit.stat.mp = unit.stat.maxMp;
            unit.dirty = true;

            // 몬스터만 제자리로 돌린다. 플레이어 좌표의 권위는 MoveModel이라 여기서 옮겨도
            // 다음 틱의 UpdatePlayerPosition이 곧바로 덮어쓴다.
            if (unit.kind == EUnitKind::Monster)
            {
                unit.x = unit.spawnX;
                unit.y = unit.spawnY;
            }

            respawned.push_back(MakeSpawnEntry(unit));
        });

        SendChunked(respawned, [this](const std::span<const Zone::UnitSpawnEntry> chunk)
        {
            Packet::BinaryWriter binaryWriter;
            binaryWriter.Write(static_cast<uint16_t>(chunk.size()));
            binaryWriter.WriteBytes(std::as_bytes(chunk));
            sender_.BroadcastToZone(PacketId::Z2CUnitSpawn, binaryWriter.GetBuffer(), 0);
        });
    }

    void Service::UpdateMonsterAi(const std::chrono::steady_clock::time_point now)
    {
        // AI는 한 줄이다: **사거리 안에서 가장 가까운 플레이어를 때린다.** 어그로 테이블도
        // 경로 탐색도 넣지 않는다 -- 몬스터는 "붙으면 반격하는 더미"까지가 이 스코프다.
        struct PendingAttack
        {
            Protocol::UnitId attackerId;
            Protocol::UnitId targetId;
        };

        std::vector<PendingAttack> pending;

        units_.ForEach([this, &pending, now](const Unit& monster)
        {
            if (monster.kind != EUnitKind::Monster || monster.stat.IsDead()
                || now - monster.lastAttackAt < kMelee.cooldown)
            {
                return;
            }

            const Unit* nearest = nullptr;
            auto nearestDistanceSquared = std::numeric_limits<float>::max();

            units_.ForEach([&monster, &nearest, &nearestDistanceSquared](const Unit& player)
            {
                if (player.kind != EUnitKind::Player || player.stat.IsDead()
                    || !InRange(monster, player, kMelee.range))
                {
                    return;
                }

                const auto dx = player.x - monster.x;
                const auto dy = player.y - monster.y;
                if (const auto distanceSquared = (dx * dx) + (dy * dy);
                    distanceSquared < nearestDistanceSquared)
                {
                    nearestDistanceSquared = distanceSquared;
                    nearest = &player;
                }
            });

            if (nearest)
            {
                pending.push_back(PendingAttack{monster.id, nearest->id});
            }
        });

        // 적용은 순회 밖에서 한다 -- 안에서 하면 같은 컨테이너를 훑는 중에 값이 바뀐다.
        for (const auto& [attackerId, targetId] : pending)
        {
            auto* const monster = units_.Find(attackerId);
            if (!monster)
            {
                continue;
            }

            monster->lastAttackAt = now;

            const auto hit = ApplyHit(attackerId, targetId, kMelee, now);
            if (hit.damage <= 0)
            {
                continue;
            }

            // 몬스터에게는 세션이 없으므로 AttackResult가 없다. 맞은 사람도 이 통지로
            // 연출을 본다(HP 값 자체는 틱 끝의 상태 동기화가 나른다).
            BroadcastAttack(*monster, targetId, EAttackKind::Melee, hit.damage);
        }
    }

    void Service::FlushDirtyUnits()
    {
        dirtyBuffer_.clear();

        units_.ForEach([this](Unit& unit)
        {
            if (!unit.dirty)
            {
                return;
            }

            unit.dirty = false;
            dirtyBuffer_.push_back(Zone::UnitStateEntry{unit.id.Value(), unit.stat.hp, unit.stat.mp});
        });

        if (dirtyBuffer_.empty())
        {
            return;
        }

        SendChunked(dirtyBuffer_, [this](const std::span<const Zone::UnitStateEntry> chunk)
        {
            Packet::BinaryWriter binaryWriter;
            binaryWriter.Write(static_cast<uint16_t>(chunk.size()));
            binaryWriter.WriteBytes(std::as_bytes(chunk));
            sender_.BroadcastToZone(PacketId::Z2CUnitStateSync, binaryWriter.GetBuffer(), 0);
        });
    }

    void Service::ReplyAttackResult(const Network::SessionId clientSessionId, const EErrorCode errorCode,
                                    const Zone::AttackPacket& packet, const int32_t damage,
                                    const int32_t targetHp) const
    {
        Zone::AttackResultPacket result{};
        result.errorCode = static_cast<int32_t>(errorCode);
        result.targetUnitId = packet.targetUnitId;
        result.attackKind = packet.attackKind;
        result.damage = damage;
        result.targetHp = targetHp;

        sender_.SendToClient(clientSessionId, PacketId::Z2CAttackResult,
                             std::as_bytes(std::span(&result, 1)));
    }

    void Service::BroadcastAttack(const Unit& attacker, const Protocol::UnitId targetId,
                                  const EAttackKind attackKind, const int32_t damage) const
    {
        Zone::UnitAttackNotifyPacket notify{};
        notify.attackerUnitId = attacker.id.Value();
        notify.targetUnitId = targetId.Value();
        notify.attackKind = static_cast<uint8_t>(attackKind);
        notify.damage = damage;

        // 시전자는 자기 AttackResult로 같은 연출을 그리므로 뺀다. 몬스터는 세션이 0이라
        // 아무도 빠지지 않는다.
        sender_.BroadcastToZone(PacketId::Z2CUnitAttackNotify, std::as_bytes(std::span(&notify, 1)),
                                attacker.ownerSessionId);
    }

    void Service::BroadcastSpawn(const Unit& unit, const Network::SessionId excludeClientSessionId) const
    {
        const auto entry = MakeSpawnEntry(unit);

        Packet::BinaryWriter binaryWriter;
        binaryWriter.Write(static_cast<uint16_t>(1));
        binaryWriter.Write(entry);
        sender_.BroadcastToZone(PacketId::Z2CUnitSpawn, binaryWriter.GetBuffer(), excludeClientSessionId);
    }

    void Service::SendSnapshotTo(const Network::SessionId clientSessionId) const
    {
        std::vector<Zone::UnitSpawnEntry> entries;
        entries.reserve(units_.Count());
        units_.ForEach([&entries](const Unit& unit) { entries.push_back(MakeSpawnEntry(unit)); });

        SendChunked(entries, [this, clientSessionId](const std::span<const Zone::UnitSpawnEntry> chunk)
        {
            Packet::BinaryWriter binaryWriter;
            binaryWriter.Write(static_cast<uint16_t>(chunk.size()));
            binaryWriter.WriteBytes(std::as_bytes(chunk));
            sender_.SendToClient(clientSessionId, PacketId::Z2CUnitSpawn, binaryWriter.GetBuffer());
        });
    }

    template <typename TEntry, typename TSend>
    void Service::SendChunked(const std::vector<TEntry>& entries, TSend&& send)
    {
        for (size_t offset = 0; offset < entries.size(); offset += Protocol::kMaxUnitsPerPacket)
        {
            const auto count = std::min(Protocol::kMaxUnitsPerPacket, entries.size() - offset);
            send(std::span(entries.data() + offset, count));
        }
    }
}
