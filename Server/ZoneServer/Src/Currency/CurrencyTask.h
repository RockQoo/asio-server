#pragma once

#include "Server/ZoneServer/Src/Currency/CurrencyModel.h"

#include "Shared/Core/Src/Task/ITask.h"
#include "Shared/Protocol/Src/CurrencyType.h"

#include <cstdint>

namespace Currency
{
    // 재화 변경 기록 하나. **새 값과 이전 값을 둘 다** 싣는다:
    //   - 클라이언트는 새 값으로 덮어쓴다. 증감량을 누적하는 방식이 아니라서 통지 하나가
    //     유실돼도 다음 값에서 자동으로 맞춰진다.
    //   - 롤백은 이전 값을 그대로 되돌린다. "차감의 반대인 증가"를 부르는 역연산 방식은
    //     요청한 양과 실제 바뀐 양이 다를 때 틀리는데(상한/하한에 걸린 경우), 이전 값을
    //     들고 있으면 그런 경우가 없다.
    // 한쪽만 싣으면 다른 쪽이 곤란해지므로 둘 다 싣는 게 싸다(합쳐서 16바이트).
    //
    // 모델을 참조로 들고 있어도 안전한 이유: 이 태스크의 수명은 요청 스코프(UnitOfWork)이고,
    // 그 안에서 소유자 Player가 사라질 수 없다(퇴장 처리도 같은 BASIC 스레드에서 돈다).
    class CurrencyTask final : public Task::ITask
    {
    public:
        CurrencyTask(CurrencyModel& model, const Protocol::ECurrencyType type,
                     const int64_t newValue, const int64_t oldValue);

        [[nodiscard]] uint16_t Kind() const noexcept override;
        void Serialize(Packet::BinaryWriter& writer) const override;
        void Rollback(Task::UnitOfWork& sink) const override;

    private:
        CurrencyModel& model_;
        Protocol::ECurrencyType type_;
        int64_t newValue_;
        int64_t oldValue_;
    };
}
