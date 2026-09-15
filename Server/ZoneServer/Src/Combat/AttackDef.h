#pragma once

#include "Combat/Unit.h"

namespace Combat
{
    // 근접/원거리의 차이를 **사거리 + 위력 + MP 소모 + 쿨다운** 네 숫자로만 표현한다.
    // 스킬 트리도 시전 시간도 없다.
    enum class EAttackKind : uint8_t
    {
        Melee = 0,
        Ranged = 1,
    };

    struct AttackDef
    {
        float range;        // 월드 단위(unit). 존 한 변이 10이다
        int32_t power;      // 배율이 아니라 가산값 -- 곱셈이 들어가면 스탯 한 점의 의미가 스킬마다 달라진다
        int32_t costMp;
        std::chrono::milliseconds cooldown;
    };

    // **투사체 비행 시간은 서버에 두지 않는다.** 활을 못 쏜다는 뜻이 아니라, 화살이 날아가는
    // 것은 클라이언트가 그리고 서버는 명중을 즉시 확정한다는 뜻이다. 서버가 비행 시간을 갖는
    // 순간 "아직 확정되지 않은 데미지가 떠 있는 상태"가 생기고, 그러면 대상이 죽었을 때/존을
    // 나갔을 때/시전자가 끊겼을 때의 답을 전부 정해야 한다. 설계 근거: docs/design/combat-lane.md
    inline constexpr AttackDef kMelee{1.2f, 10, 0, std::chrono::milliseconds(800)};
    inline constexpr AttackDef kRanged{5.0f, 7, 5, std::chrono::milliseconds(1200)};

    [[nodiscard]] constexpr const AttackDef& DefOf(const EAttackKind attackKind) noexcept
    {
        return attackKind == EAttackKind::Melee ? kMelee : kRanged;
    }

    // 존에 입장할 때 채워 넣는 시작 스탯. **HP는 영속 대상이 아니라서** DB에서 읽지 않고
    // 여기서 만든다 -- 그래서 전투는 DB 스키마를 한 줄도 건드리지 않는다.
    inline constexpr Stat kPlayerStat{200, 200, 100, 100, 15, 5, 2.0f, 5.0f};
    inline constexpr Stat kMonsterStat{120, 120, 0, 0, 8, 3, 0.0f, 0.0f};

    inline constexpr float kUnitBoundRadius = 0.375f;

    // 죽은 뒤 다시 서기까지. 유닛마다 타이머를 걸지 않고 **틱에서 시각을 비교**한다 --
    // 타이머를 걸면 유닛 수만큼 타이머가 생기고, 유닛이 사라질 때 취소하는 코드가 또 붙는다.
    inline constexpr std::chrono::seconds kRespawnDelay{8};

    // 존 하나에 세워 둘 더미 몬스터 수. config가 아니라 여기 있는 이유는 주기나 스레드 수가
    // 아니라 **콘텐츠 수치**이기 때문이다(config/*.cfg는 운영이 만지는 값만 담는다).
    inline constexpr size_t kMonstersPerZone = 3;

    // --- 아래 셋은 순수 함수다. 상태를 안 만지므로 값만 넣어보면 검증된다 ---

    // DEF가 ATK보다 높아도 **최소 1은 들어간다.** 0 데미지는 "공격이 안 먹히는 버그"처럼 보인다.
    [[nodiscard]] constexpr int32_t CalcDamage(const Stat& attacker, const Stat& target,
                                               const AttackDef& attackDef) noexcept
    {
        const auto raw = attacker.atk + attackDef.power;
        return std::max(1, raw - target.def);
    }

    // 거리 비교는 **제곱으로** 한다(sqrt를 안 쓴다). 사거리에 양쪽 바운드 반경을 더한다.
    [[nodiscard]] constexpr bool InRange(const Unit& attacker, const Unit& target,
                                         const float range) noexcept
    {
        const auto reach = range + attacker.boundRadius + target.boundRadius;
        const auto dx = target.x - attacker.x;
        const auto dy = target.y - attacker.y;
        return (dx * dx) + (dy * dy) <= reach * reach;
    }

    // 데미지를 **남은 HP로 먼저 잘라낸다.** 그래야 "3만 데미지로 HP 100을 죽였다"는 로그가
    // 안 생기고, 사망 판정과 데미지 보고가 어긋나지 않는다.
    struct DamageResult
    {
        bool dead;
        int32_t applied;
    };

    [[nodiscard]] constexpr DamageResult AssignDamage(Stat& target, const int32_t damage) noexcept
    {
        const auto applied = std::min(damage, target.hp);
        target.hp -= applied;
        return DamageResult{target.hp <= 0, applied};
    }
}
