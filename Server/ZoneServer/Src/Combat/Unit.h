#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Protocol/Src/Ids.h"

namespace Combat
{
    // 전투에 참여하는 개체 하나. **존 레인(owner = zoneId) 전용이라 락이 없다** --
    // 틱(리젠·리스폰·AI)과 전투 패킷(C2ZAttack)이 둘 다 이 레인에서 돌기 때문이다.
    // 이 한 줄이 전투 코드에 뮤텍스가 하나도 없는 이유이고, 근거는 docs/design/combat-lane.md.
    //
    // Zone::Player와 나누어 둔 이유: Player의 모델들(우편·재화)은 **플레이어 레인** 소유라
    // 주인이 다르다. 한 객체에 주인이 둘인 필드를 섞으면 Player.h의 접근 규칙 표가 무너진다.
    enum class EUnitKind : uint8_t
    {
        None = 0,
        Player = 1,
        Monster = 2,
    };

    // HP/MP/ATK/DEF만 있다. 크리티컬·회피·명중·속성은 넣지 않는다 -- 이 프로젝트에서 전투는
    // 밸런스가 아니라 "틱과 패킷이 같은 데이터를 만지는 문제"를 보여주는 사례다.
    //
    // **폭이 int32인 것이 의도다.** 재화(int64)와 달리 전투 스탯은 누적되지 않고, 틱마다
    // 유닛 수만큼 와이어로 나가는 값이라 폭이 그대로 대역폭이 된다.
    struct Stat
    {
        int32_t hp;
        int32_t maxHp;
        int32_t mp;
        int32_t maxMp;
        int32_t atk;
        int32_t def;

        // 초당 회복량. 틱 주기(100ms)보다 작은 값이 나오므로 소수부는 Unit이 이월한다.
        float regenHpPerSec;
        float regenMpPerSec;

        [[nodiscard]] constexpr bool IsDead() const noexcept { return hp <= 0; }
    };

    // 존 안의 유닛 한 기. POD라 public 필드에 밑줄을 붙이지 않는다(CLAUDE.md).
    struct Unit
    {
        Protocol::UnitId id;
        EUnitKind kind;

        // Player일 때만 유효하다. 시전자 응답(Z2CAttackResult)을 이 값으로 되돌려 보낸다.
        Network::SessionId ownerSessionId;

        // 존 레인이 보는 좌표. 플레이어는 틱마다 MoveModel에서 실어 오고(공격 직전에도 한 번
        // 더 맞춘다), 몬스터는 제자리에 서 있으므로 스폰 좌표 그대로다.
        float x;
        float y;

        // 사거리 판정에 양쪽을 다 더한다 -- 덩치가 커지면 "붙었는데 사거리 밖"이 나온다.
        float boundRadius;

        Stat stat;

        // 리스폰 지점. 몬스터는 스폰 자리로 돌아가고, 플레이어는 죽은 자리에서 일어난다.
        float spawnX;
        float spawnY;

        std::chrono::steady_clock::time_point lastAttackAt;
        std::chrono::steady_clock::time_point respawnAt;

        // 소수점 이하 회복량 이월. 이게 없으면 초당 2 회복이 100ms 틱에서 0으로 잘려 영영
        // 회복하지 않는다.
        float regenCarryHp;
        float regenCarryMp;

        // 이번 틱에 hp/mp가 바뀌었는가. 틱 끝에 dirty인 유닛만 모아 한 장으로 내보낸다
        // -- 공격 한 번에 한 장씩 뿌리면 대규모 전투에서 브로드캐스트가 폭발한다.
        bool dirty;
    };
}
