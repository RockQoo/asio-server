#include "pch.h"
#include "Unit/Unit.h"

Unit::Unit(const Common::UnitId unitId, const EType type, const Common::ZoneId zoneId,
           const float x, const float y)
    : unitId_(unitId)
    , type_(type)
    , zoneId_(zoneId)
    , move_(x, y)
{
}

void Unit::Tick(const UnitTickContext& /*context*/)
{
    // 이동 적분. **요청이 없는 틱이 대부분**이라 읽기 잠금으로 먼저 거른다 -- 여기서
    // 바로 쓰기 잠금을 잡으면 그 존의 유닛 전원이 매 틱 배타 구간을 통과한다.
    if (!move_->HasRequest())
    {
        return;
    }

    move_.Write()->ApplyRequest();
}

void Unit::RollbackUoW(const ZoneUnitOfWork& /*unitOfWork*/) noexcept
{
}

Network::SessionId Unit::GetClientSessionId() const noexcept
{
    return Network::SessionId{};
}

Network::SessionHolder* Unit::GetWorldLink() const noexcept
{
    return nullptr;
}
