#pragma once

#include "Shared/Protocol/Src/CurrencyType.h"
#include "Shared/Protocol/Src/ErrorCode.h"

namespace Task
{
    class UnitOfWork;
}

namespace Currency
{
    // 플레이어 한 명의 재화. 지금은 골드 하나뿐이지만, 재화가 늘어도 필드만 추가하면 되도록
    // 종류를 인자로 받는 형태로 만들었다.
    //
    // 우편함(Mail::Model)과 달리 Mutexed로 감싸지 않는다 -- 이 모델을 건드리는 건 그 존을
    // 담당하는 BASIC 스레드뿐이고, 만료 스윕처럼 다른 스레드에서 도는 유지보수 작업이 없다.
    // "전부 락"도 "전부 무락"도 아니고 모델마다 실제 접근 스레드 수에 맞추는 게 이 프로젝트의
    // 규약이다.
    class Model
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

        // 증감이 아니라 절대값을 넣는다. 롤백(CurrencyTask::Rollback)이 이전 값을 되돌릴 때 쓴다.
        [[nodiscard]] EErrorCode SetCurrency(const Protocol::ECurrencyType type, const int64_t value,
                                             Task::UnitOfWork& unitOfWork);

        // World가 DB에서 읽어 실어 보낸 잔액으로 **시작 상태를 채운다**(W2ZEnterZone).
        //
        // **SetCurrency가 아니라 별도 경로인 이유**: SetCurrency는 UnitOfWork에 태스크를 남긴다.
        // 적재는 "변경"이 아니라 시작 상태라, 그 경로로 넣으면 방금 DB에서 읽은 값을 도로 DB에
        // 쓰고 클라이언트에도 "잔액이 바뀌었다"고 통지하게 된다.
        //
        // 모르는 종류는 조용히 버린다 -- 이 빌드가 아직 모르는 재화가 DB에 있을 수 있고,
        // 그것 때문에 로그인을 막을 이유는 없다.
        void Seed(const Protocol::ECurrencyType type, const int64_t value) noexcept;

    private:
        [[nodiscard]] int64_t* Field(const Protocol::ECurrencyType type) noexcept;

        // 값을 바꾸는 **유일한 통로**. 검증 -> 이전 값 포착 -> 태스크 기록 -> 대입을 한 자리에서
        // 하므로, 콘텐츠 코드가 이전 값을 챙기는 걸 깜빡할 수가 없다(깜빡하면 롤백이 불가능해진다).
        [[nodiscard]] EErrorCode SetTracked(const Protocol::ECurrencyType type, const int64_t newValue,
                                            Task::UnitOfWork& unitOfWork);

        // **0에서 시작한다.** 잔액은 World가 DB에서 읽어 W2ZEnterZone으로 실어 보내고
        // Seed가 채운다 -- 존이 임의의 값을 만들어내면 그게 DB와 갈리는 첫 지점이 된다.
        int64_t gold_{0};
    };
}
