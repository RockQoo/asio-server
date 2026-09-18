#include "pch.h"
#include "Unit/UnitContainer.h"

#include "Player/Player.h"

void UnitContainer::AddUnit(const Unit::SPtr& unit)
{
    if (unit == nullptr)
    {
        return;
    }

    const auto unitId = unit->GetUnitId();

    {
        std::unique_lock lock(unitsMutex_);
        units_[unitId] = unit;
    }

    if (unit->IsPlayer())
    {
        std::unique_lock lock(playersMutex_);
        players_[unitId] = unit;
    }

    {
        std::scoped_lock lock(readyingMutex_);
        readyingUnits_.push_back(unit);
    }
}

bool UnitContainer::RemoveUnit(const Common::UnitId unitId)
{
    Unit::SPtr removed;

    {
        std::unique_lock lock(unitsMutex_);
        const auto found = units_.find(unitId);
        if (found == units_.end())
        {
            return false;
        }

        removed = found->second;
        units_.erase(found);
    }

    if (removed->IsPlayer())
    {
        std::unique_lock lock(playersMutex_);
        players_.erase(unitId);
    }

    // **여기서 지우지 않는다.** 지금 이 유닛은 틱 스냅샷에 들어 있을 수 있고, 그 순회가
    // 끝나기 전에 죽으면 매달린 포인터가 된다. OnEndTick 이 확정한다.
    {
        std::scoped_lock lock(removingMutex_);
        removingUnits_.push_back(std::move(removed));
    }

    return true;
}

Unit::SPtr UnitContainer::FindUnit(const Common::UnitId unitId) const
{
    std::shared_lock lock(unitsMutex_);
    const auto found = units_.find(unitId);
    return found == units_.end() ? nullptr : found->second;
}

std::shared_ptr<Player> UnitContainer::FindPlayer(const Common::UnitId unitId) const
{
    std::shared_lock lock(playersMutex_);
    const auto found = players_.find(unitId);
    if (found == players_.end())
    {
        return nullptr;
    }

    // players_ 에 들어가는 조건이 IsPlayer() 하나라, 여기 있으면 Player 인 것이 보장된다.
    return std::static_pointer_cast<Player>(found->second);
}

void UnitContainer::ForEachPlayer(const std::function<void(const std::shared_ptr<Player>&)>& func) const
{
    std::shared_lock lock(playersMutex_);
    for (const auto& [unitId, unit] : players_)
    {
        func(std::static_pointer_cast<Player>(unit));
    }
}

size_t UnitContainer::GetUnitCount() const
{
    std::shared_lock lock(unitsMutex_);
    return units_.size();
}

size_t UnitContainer::GetPlayerCount() const
{
    std::shared_lock lock(playersMutex_);
    return players_.size();
}

std::vector<Network::SessionId> UnitContainer::CollectClientSessionIds() const
{
    std::vector<Network::SessionId> sessionIds;

    std::shared_lock lock(playersMutex_);
    sessionIds.reserve(players_.size());
    for (const auto& [unitId, unit] : players_)
    {
        sessionIds.push_back(unit->GetClientSessionId());
    }

    return sessionIds;
}

void UnitContainer::Tick(const UnitTickContext& context)
{
    // **맵이 아니라 스냅샷을 돈다.** BASIC 이 이번 틱 도중에 units_ 를 바꿔도 이 벡터는
    // 흔들리지 않고, 그래서 락 없이 나눠 돌 수 있다.
    std::for_each(std::execution::par, readiedUnits_.begin(), readiedUnits_.end(),
                  [&context](Unit* unit)
                  {
                      // 여기서 예외가 새면 std::terminate 다 -- 유닛 하나 때문에 프로세스가
                      // 죽지 않도록 개별로 막는다.
                      try
                      {
                          unit->Tick(context);
                      }
                      catch (const std::exception& ex)
                      {
                          LOG.Error(ELogCategory::Zone, "유닛 틱 중 예외")
                              .KV("UnitId", unit->GetUnitId()).KV("What", ex.what());
                      }
                      catch (...)
                      {
                          LOG.Error(ELogCategory::Zone, "유닛 틱 중 알 수 없는 예외")
                              .KV("UnitId", unit->GetUnitId());
                      }
                  });
}

void UnitContainer::OnEndTick()
{
    // ── 파괴 확정 ──
    std::vector<Unit::SPtr> removing;
    {
        std::scoped_lock lock(removingMutex_);
        removing.swap(removingUnits_);
    }

    if (!removing.empty())
    {
        std::unordered_set<const Unit*> removingSet;
        removingSet.reserve(removing.size());
        for (const auto& unit : removing)
        {
            removingSet.insert(unit.get());
        }

        std::erase_if(readiedUnits_, [&removingSet](const Unit* unit)
                      {
                          return removingSet.contains(unit);
                      });
    }

    // ── 편입 확정 ──
    // 여기서 비로소 스냅샷에 들어간다. 이 줄이 끝나면 다음 틱부터 돈다.
    std::vector<Unit::SPtr> readying;
    {
        std::scoped_lock lock(readyingMutex_);
        readying.swap(readyingUnits_);
    }

    readiedUnits_.reserve(readiedUnits_.size() + readying.size());
    for (const auto& unit : readying)
    {
        readiedUnits_.push_back(unit.get());
    }

    // removing 과 readying 의 shared_ptr 은 이 함수가 끝날 때 풀린다 -- 파괴 대상은
    // 여기서 마지막 참조가 사라지므로, **틱이 끝난 뒤에** 실제로 소멸한다.
}
