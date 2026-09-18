#pragma once

#include "Unit/Unit.h"

class Player;

// 존 하나에 들어와 있는 유닛 전부. **플레이어는 두 맵에 같이 들어간다** -- 전체를 도는
// 일(틱)과 사람만 도는 일(브로드캐스트 대상, 접속자 수)이 둘 다 잦기 때문이다.
//
// **락이 네 개인 이유는 "BASIC 조회 / TICK 순회"가 아니다.**
// 실제로는 **BASIC 이 맵을 바꾸는 동안 TICK 이 돌고 있다.**
//
//   BASIC  조회(FindUnit/FindPlayer) + 순회(ForEachPlayer) + **AddUnit/RemoveUnit(입장·퇴장)**
//   TICK   순회(Tick) + **OnEndTick 에서 실제 편입·파괴 확정**
//
// 그 충돌을 락으로 버티지 않고 **지연 파괴**로 푼다: `RemoveUnit` 은 맵에서 빼고 대기줄에
// 넣기만 하고, 실제로 목록에서 빠지는 것은 틱이 끝난 뒤다. 그래서 틱이 도는
// `readiedUnits_` 는 맵이 아니라 **락 없는 스냅샷**이고, raw 포인터여도 안전하다
// (이번 틱 동안은 `removingUnits_` 가 shared_ptr 로 붙들고 있어 절대 죽지 않는다).
//
// 스냅샷이라 **병렬 틱이 가능한 것이기도 하다** -- 맵을 돌면 반복자가 흔들려서 못 나눈다.
//
// **BROADCAST 와 LB 는 이 컨테이너를 보지 않는다.** 브로드캐스트는 TICK 이 만들어 넘겨준
// 대상 목록 사본만 받고, LB 는 봉투만 까고 넘긴다.
class UnitContainer final
{
public:
    UnitContainer() = default;

    UnitContainer(const UnitContainer&) = delete;
    UnitContainer& operator=(const UnitContainer&) = delete;

    // ── BASIC 레인 ────────────────────────────────────────────────────────────

    // 맵에 넣고 **다음 틱 시작 때 편입되도록 예약한다.** 넣은 즉시 틱이 도는 목록에
    // 끼워 넣지 않는 이유는 그 목록이 지금 순회 중일 수 있기 때문이다.
    void AddUnit(const Unit::SPtr& unit);

    // 맵에서 빼고 **파괴를 예약한다.** 틱이 끝날 때까지는 살아 있다.
    bool RemoveUnit(const Common::UnitId unitId);

    [[nodiscard]] Unit::SPtr FindUnit(const Common::UnitId unitId) const;
    [[nodiscard]] std::shared_ptr<Player> FindPlayer(const Common::UnitId unitId) const;

    // **func 안에서 Add/Remove 를 부르지 말 것** -- 같은 뮤텍스를 다시 잡는다.
    void ForEachPlayer(const std::function<void(const std::shared_ptr<Player>&)>& func) const;

    [[nodiscard]] size_t GetUnitCount() const;
    [[nodiscard]] size_t GetPlayerCount() const;

    // 팬아웃 대상 목록. **사본을 만들어 넘긴다** -- 받는 쪽(BROADCAST)이 이 컨테이너를
    // 다시 보지 않아야 그 레인이 이 락에 묶이지 않는다.
    [[nodiscard]] std::vector<Network::SessionId> CollectClientSessionIds() const;

    // ── TICK 레인 ─────────────────────────────────────────────────────────────

    // 스냅샷을 **병렬로** 돈다. 유닛 하나가 던진 예외가 나머지 전원의 틱을 죽이지 않도록
    // 각 호출을 개별로 막는다(std::execution::par 는 예외가 새면 std::terminate 다).
    void Tick(const UnitTickContext& context);

    // 틱 끝. 예약된 파괴를 확정하고 새로 들어온 것을 편입한다. **Tick 과 같은 레인에서,
    // 순회가 끝난 뒤에 부른다.**
    void OnEndTick();

private:
    mutable std::shared_mutex unitsMutex_;
    std::unordered_map<Common::UnitId, Unit::SPtr> units_;

    // 플레이어만 따로 색인한다. units_ 와 **같은 키**이고 같은 객체를 가리킨다.
    mutable std::shared_mutex playersMutex_;
    std::unordered_map<Common::UnitId, Unit::SPtr> players_;

    // BASIC 이 예약하고 TICK 이 확정하는 두 대기줄. shared_ptr 로 붙들고 있는 것이
    // 지연 파괴의 핵심이다 -- 이게 없으면 readiedUnits_ 의 raw 포인터가 매달린다.
    std::mutex removingMutex_;
    std::vector<Unit::SPtr> removingUnits_;

    std::mutex readyingMutex_;
    std::vector<Unit::SPtr> readyingUnits_;

    // **TICK 전용이라 락이 없다.** 소유권은 units_ 와 위 두 대기줄에 있다.
    std::vector<Unit*> readiedUnits_;
};
