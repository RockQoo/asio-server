#include "pch.h"
#include "Unit/Unit.h"

#include "Zone/Zone.h"

Unit::Unit(const Common::UnitId unitId, const EType type, const Common::ZoneId zoneId,
           const float x, const float y)
    : unitId_(unitId)
    , type_(type)
    , zoneId_(zoneId)
    , move_(x, y)
{
}

void Unit::Tick(const UnitTickContext& context)
{
    // 이동 적분. **요청이 없는 틱이 대부분**이라 읽기 잠금으로 먼저 거른다 -- 여기서
    // 바로 쓰기 잠금을 잡으면 그 존의 유닛 전원이 매 틱 배타 구간을 통과한다.
    if (!move_->HasRequest())
    {
        return;
    }

    float crossedX = 0.0f;
    float crossedY = 0.0f;
    bool crossed = false;

    {
        auto move = move_.Write();
        if (!move->HasRequest())
        {
            // 읽기 잠금을 놓은 사이에 누가 취소했다.
            return;
        }

        const auto requestedX = move->GetRequestedX();
        const auto requestedY = move->GetRequestedY();

        if (context.zone != nullptr && !context.zone->GetDef().Contains(requestedX, requestedY))
        {
            // **아직 위치를 확정하지 않는다.** 대상 존을 찾지 못해 World 가 되돌려 보낼 수도
            // 있으므로, 확정은 새 존의 입장 처리가 Teleport 로 한다.
            move->CancelRequest();
            crossedX = requestedX;
            crossedY = requestedY;
            crossed = true;
        }
        else
        {
            move->ApplyRequest();
        }
    }

    if (!crossed)
    {
        return;
    }

    // **쓰기 잠금을 놓은 뒤에 신고한다.** 신고가 다시 락을 잡으므로(존의 수집 통) 겹쳐
    // 잡으면 잠금 순서가 두 개가 된다.
    if (TryBeginZoneCrossing())
    {
        context.zone->ReportCrossing(GetUnitId(), crossedX, crossedY);
    }
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
