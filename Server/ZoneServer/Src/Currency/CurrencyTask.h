#pragma once

#include "Shared/Common/Src/CurrencyType.h"
#include "Shared/Common/Src/TaskKind.h"

#include "Shared/Core/Src/Task/ITask.h"
#include "Shared/Core/Src/Task/Paired.h"

namespace Currency
{
    // 재화 변경 기록 하나. **데이터만 갖는다**(ITask 주석 참고).
    //
    // **새 값과 이전 값을 둘 다** 싣는다:
    //   - 클라이언트는 New로 덮어쓴다. 증감량을 누적하는 방식이 아니라서 통지 하나가 유실돼도
    //     다음 값에서 자동으로 맞춰진다.
    //   - 롤백은 Prev를 그대로 되돌린다. "차감의 반대인 증가"를 부르는 역연산 방식은 요청한
    //     양과 실제 바뀐 양이 다를 때 틀리는데(상한/하한에 걸린 경우), 이전 값을 들고 있으면
    //     그런 경우가 없다.
    // 합쳐서 16바이트라 둘 다 싣는 게 싸다.
    //
    // **재화 종류는 짝이 아니다** -- 바뀌는 것은 값이고 종류는 "어느 값인가"를 가리키는
    // 식별자라, New/Prev로 나눌 대상이 아니다.
    class CurrencyTask final : public Task::ITask
    {
    public:
        void Set(const int64_t newValue, const int64_t prevValue, const Common::ECurrencyType type)
        {
            value_.Set(newValue, prevValue);
            type_ = type;
        }

        [[nodiscard]] uint16_t Kind() const noexcept override
        {
            return static_cast<uint16_t>(Common::ETaskType::CurrencyUpdate);
        }

        [[nodiscard]] const Task::Paired<int64_t>& Value() const noexcept { return value_; }
        [[nodiscard]] Common::ECurrencyType Type() const noexcept { return type_; }

    private:
        Task::Paired<int64_t> value_;
        Common::ECurrencyType type_{};
    };
}
