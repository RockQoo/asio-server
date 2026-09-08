#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Currency/CurrencyTask.h"

#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Core/Src/Task/UnitOfWork.h"
#include "Shared/Protocol/Src/TaskKind.h"

namespace Currency
{
    CurrencyTask::CurrencyTask(CurrencyModel& model, const Protocol::ECurrencyType type,
                               const int64_t newValue, const int64_t oldValue)
        : model_(model)
        , type_(type)
        , newValue_(newValue)
        , oldValue_(oldValue)
    {
    }

    uint16_t CurrencyTask::Kind() const noexcept
    {
        return Protocol::MakeTaskKind(Protocol::ETaskCategory::Currency, Protocol::ECurrencyTask::Updated);
    }

    void CurrencyTask::Serialize(Packet::BinaryWriter& writer) const
    {
        writer.Write(static_cast<uint8_t>(type_));
        writer.Write(newValue_);
        writer.Write(oldValue_);
    }

    void CurrencyTask::Rollback(Task::UnitOfWork& sink) const
    {
        // 이전 값을 그대로 되돌린다. 검증할 게 없어서(이전 값은 방금까지 유효했던 값이다)
        // 실패할 수 없고, 그게 값 복원 방식을 고른 이유다.
        if (const auto errorCode = model_.SetCurrency(type_, oldValue_, sink);
            errorCode != EErrorCode::Success)
        {
            LOG.Error(ELogCategory::Zone, "재화 롤백 실패 -- 메모리와 DB가 어긋난다")
                .KV("CurrencyType", static_cast<uint32_t>(type_))
                .KV("OldValue", oldValue_).KV("ErrorCode", static_cast<int32_t>(errorCode));
        }
    }
}
