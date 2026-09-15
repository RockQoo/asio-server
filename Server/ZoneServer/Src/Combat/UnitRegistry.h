#pragma once

#include "Combat/Unit.h"

namespace Combat
{
    // 존 하나가 들고 있는 유닛 전부. **존 레인 전용이라 락이 없다**(Unit.h 주석 참고).
    //
    // 플레이어 유닛의 id는 clientSessionId를 그대로 자른 값이라 조회에 별도 색인이 필요 없다
    // -- "세션으로 찾기"와 "유닛 id로 찾기"가 같은 연산이 된다(Protocol/Ids.h의 UnitId 주석).
    class UnitRegistry
    {
    public:
        [[nodiscard]] static Protocol::UnitId UnitIdOf(const Network::SessionId clientSessionId) noexcept
        {
            return Protocol::UnitId{static_cast<uint32_t>(clientSessionId)};
        }

        // 없으면 nullptr. **반환한 포인터를 저장하지 않는다** -- 다음 Spawn/Despawn에서
        // 무효화된다(그래서 명중 적용부가 포인터가 아니라 id를 받는다).
        [[nodiscard]] Unit* Find(const Protocol::UnitId unitId);
        [[nodiscard]] const Unit* Find(const Protocol::UnitId unitId) const;

        Unit& SpawnPlayer(const Network::SessionId clientSessionId, const float x, const float y);
        Unit& SpawnMonster(const float x, const float y);

        // 이미 없으면 아무 일도 하지 않는다 -- 핸드오프 경로에서 두 번 불릴 수 있다
        // (틱이 경계를 넘긴 사람을 지우고, 뒤이어 W2ZEnterZone이 이전 존에 퇴장을 또 알린다).
        bool Despawn(const Protocol::UnitId unitId);

        template <typename F>
        void ForEach(F&& work)
        {
            for (auto& [unitId, unit] : units_)
            {
                work(unit);
            }
        }

        template <typename F>
        void ForEach(F&& work) const
        {
            for (const auto& [unitId, unit] : units_)
            {
                work(unit);
            }
        }

        [[nodiscard]] size_t Count() const noexcept { return units_.size(); }

    private:
        std::unordered_map<Protocol::UnitId, Unit> units_;

        // 몬스터 id 발급 시퀀스. 존마다 따로 세지만 **kMonsterUnitIdBase 대역 안이라**
        // 세션 id와 겹치지 않는다. 존끼리는 겹칠 수 있는데, 유닛은 존 밖으로 나가지 않으므로
        // 상관없다(클라이언트도 존을 옮기면 목록을 비운다).
        uint32_t nextMonsterSeq_{0};
    };
}
