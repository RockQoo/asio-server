#include "pch.h"
#include "Currency/Model.h"
#include "Currency/CurrencyTask.h"

#include "Shared/Core/Src/Task/UnitOfWork.h"

namespace Currency
{
    Model::Model(const std::vector<Common::CurrencyInfo>& initial) noexcept
    {
        for (const auto& info : initial)
        {
            // 모르는 종류는 조용히 버린다 -- 이 빌드가 아직 모르는 재화가 DB에 있을 수 있고,
            // 그것 때문에 입장을 막을 이유는 없다(Field가 nullptr을 돌려준다).
            if (auto* const field = Field(static_cast<Common::ECurrencyType>(info.type)); field != nullptr)
            {
                *field = info.amount;
            }
        }
    }

    int64_t Model::Get(const Common::ECurrencyType type) const noexcept
    {
        switch (type)
        {
        case Common::ECurrencyType::Gold:
            return gold_;
        default:
            return 0;
        }
    }

    EErrorCode Model::AddCurrency(const Common::ECurrencyType type, const int64_t amount,
                                          Task::UnitOfWork& unitOfWork)
    {
        if (amount < 0)
        {
            // 음수를 넘겨 차감하는 걸 허용하면 "증가"와 "감소"가 한 함수로 섞여서, 로그만 보고
            // 어느 쪽 의도였는지 알 수 없게 된다.
            return EErrorCode::InvalidCurrencyAmount;
        }

        const auto* const field = Field(type);
        if (!field)
        {
            return EErrorCode::UnknownCurrencyType;
        }

        return SetTracked(type, *field + amount, unitOfWork);
    }

    EErrorCode Model::DecCurrency(const Common::ECurrencyType type, const int64_t amount,
                                          Task::UnitOfWork& unitOfWork)
    {
        if (amount < 0)
        {
            return EErrorCode::InvalidCurrencyAmount;
        }

        const auto* const field = Field(type);
        if (!field)
        {
            return EErrorCode::UnknownCurrencyType;
        }

        return SetTracked(type, *field - amount, unitOfWork);
    }

    EErrorCode Model::SetCurrency(const Common::ECurrencyType type, const int64_t value,
                                          Task::UnitOfWork& unitOfWork)
    {
        return SetTracked(type, value, unitOfWork);
    }


    int64_t* Model::Field(const Common::ECurrencyType type) noexcept
    {
        switch (type)
        {
        case Common::ECurrencyType::Gold:
            return &gold_;
        default:
            // None을 포함한 알 수 없는 종류. 0을 유효한 재화로 취급하지 않으려고 nullptr을
            // 돌려주고, 호출부가 UnknownCurrencyType으로 끊는다.
            return nullptr;
        }
    }

    EErrorCode Model::SetTracked(const Common::ECurrencyType type, const int64_t newValue,
                                         Task::UnitOfWork& unitOfWork)
    {
        auto* const field = Field(type);
        if (!field)
        {
            return EErrorCode::UnknownCurrencyType;
        }

        if (newValue < 0)
        {
            // 잔액 부족. 여기서 0으로 깎지 않는 이유는 Model.h 주석 참고(부분 적용 금지).
            return EErrorCode::NotEnoughCurrency;
        }

        const auto oldValue = *field;
        if (newValue == oldValue)
        {
            // 바뀐 게 없으면 태스크를 남기지 않는다 -- 아무 일도 안 한 변경이 DB와 클라이언트로
            // 나가면 로그만 늘고 읽는 쪽은 매번 무의미한 갱신을 하게 된다.
            return EErrorCode::Success;
        }

        *field = newValue;
        unitOfWork.AddTask<CurrencyTask>(newValue, oldValue, type);

        return EErrorCode::Success;
    }
}
