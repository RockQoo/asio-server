#pragma once

#include "Shared/Protocol/Src/CurrencyType.h"
#include "Shared/Protocol/Src/ErrorCode.h"

#include <cstdint>

namespace Task
{
    class UnitOfWork;
}

namespace Currency
{
    // 플레이어 한 명의 재화. 지금은 골드 하나뿐이지만, 재화가 늘어도 필드만 추가하면 되도록
    // 종류를 인자로 받는 형태로 만들었다.
    //
    // 우편함(Mail::MailModel)과 달리 Mutexed로 감싸지 않는다 -- 이 모델을 건드리는 건 그 존을
    // 담당하는 BASIC 스레드뿐이고, 만료 스윕처럼 다른 스레드에서 도는 유지보수 작업이 없다.
    // "전부 락"도 "전부 무락"도 아니고 모델마다 실제 접근 스레드 수에 맞추는 게 이 프로젝트의
    // 규약이다.
    class CurrencyModel
    {
    public:
        [[nodiscard]] int64_t Get(const Protocol::ECurrencyType type) const noexcept;

        // 증감. 실패는 반환값으로 알리고(호출부가 SetError로 옮긴다) 아무 상태도 바꾸지 않는다.
        // **부분 적용을 하지 않는다** -- 골드 50인데 100을 차감하라면 0으로 깎지 말고 에러로
        // 끊는다. 클라이언트는 "100 썼다"로 알고 서버는 "50 썼다"가 되면 그 순간부터 양쪽
        // 상태가 갈린다.
        [[nodiscard]] EErrorCode AddCurrency(const Protocol::ECurrencyType type, const int64_t amount,
                                             Task::UnitOfWork& unitOfWork);
        [[nodiscard]] EErrorCode DecCurrency(const Protocol::ECurrencyType type, const int64_t amount,
                                             Task::UnitOfWork& unitOfWork);

        // 증감이 아니라 절대값을 넣는다. 롤백(CurrencyTask::Rollback)이 이전 값을 되돌릴 때
        // 쓰고, 나중에 DB에서 잔액을 불러올 때도 이 경로를 쓴다.
        [[nodiscard]] EErrorCode SetCurrency(const Protocol::ECurrencyType type, const int64_t value,
                                             Task::UnitOfWork& unitOfWork);

    private:
        [[nodiscard]] int64_t* Field(const Protocol::ECurrencyType type) noexcept;

        // 값을 바꾸는 **유일한 통로**. 검증 -> 이전 값 포착 -> 태스크 기록 -> 대입을 한 자리에서
        // 하므로, 콘텐츠 코드가 이전 값을 챙기는 걸 깜빡할 수가 없다(깜빡하면 롤백이 불가능해진다).
        [[nodiscard]] EErrorCode SetTracked(const Protocol::ECurrencyType type, const int64_t newValue,
                                            Task::UnitOfWork& unitOfWork);

        // 실제 DB 연동 전까지의 임시 시작 잔액. DB에서 불러오게 되면 0으로 두고 SetCurrency로
        // 채우는 경로만 남는다.
        int64_t gold_{1000};
    };
}
