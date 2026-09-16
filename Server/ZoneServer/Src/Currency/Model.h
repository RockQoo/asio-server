#pragma once

#include "Shared/Common/Src/CurrencyInfo.h"
#include "Shared/Common/Src/Enum.h"
#include "Shared/Common/Src/ErrorCode.h"

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
        // **시작 상태는 생성자로만 들어온다.** W2ZEnterZone이 실어 보낸 잔액을 그대로 받는다.
        //
        // 예전에는 빈 모델을 만든 뒤 Seed()로 채웠는데, 그러면 "아직 안 채워진 모델"이 잠깐
        // 존재하고 그 사이에 누가 읽으면 잔액이 0으로 보인다. 생성자로 받으면 그 틈이 없다.
        //
        // **SetCurrency가 아니라 생성자인 이유**: SetCurrency는 UnitOfWork에 태스크를 남긴다.
        // 적재는 "변경"이 아니라 시작 상태라, 그 경로로 넣으면 방금 DB에서 읽은 값을 도로 DB에
        // 쓰고 클라이언트에도 "잔액이 바뀌었다"고 통지하게 된다.
        explicit Model(const std::vector<Common::CurrencyInfo>& initial) noexcept;


        [[nodiscard]] int64_t Get(const Common::ECurrencyType type) const noexcept;
        // 증감. 실패는 반환값으로 알리고(호출부가 SetError로 옮긴다) 아무 상태도 바꾸지 않는다.
        // **부분 적용을 하지 않는다** -- 골드 50인데 100을 차감하라면 0으로 깎지 말고 에러로
        // 끊는다. 클라이언트는 "100 썼다"로 알고 서버는 "50 썼다"가 되면 그 순간부터 양쪽
        // 상태가 갈린다.
        [[nodiscard]] EErrorCode AddCurrency(const Common::ECurrencyType type, const int64_t amount,
                                             Task::UnitOfWork& unitOfWork);
        [[nodiscard]] EErrorCode DecCurrency(const Common::ECurrencyType type, const int64_t amount,
                                             Task::UnitOfWork& unitOfWork);

        // 증감이 아니라 절대값을 넣는다. 롤백(CurrencyTask::Rollback)이 이전 값을 되돌릴 때 쓴다.
        [[nodiscard]] EErrorCode SetCurrency(const Common::ECurrencyType type, const int64_t value,
                                             Task::UnitOfWork& unitOfWork);

    private:
        [[nodiscard]] int64_t* Field(const Common::ECurrencyType type) noexcept;

        // 값을 바꾸는 **유일한 통로**. 검증 -> 이전 값 포착 -> 태스크 기록 -> 대입을 한 자리에서
        // 하므로, 콘텐츠 코드가 이전 값을 챙기는 걸 깜빡할 수가 없다(깜빡하면 롤백이 불가능해진다).
        [[nodiscard]] EErrorCode SetTracked(const Common::ECurrencyType type, const int64_t newValue,
                                            Task::UnitOfWork& unitOfWork);

        // **0에서 시작한다.** 잔액은 World가 DB에서 읽어 W2ZEnterZone으로 실어 보내고
        // 생성자가 채운다 -- 존이 임의의 값을 만들어내면 그게 DB와 갈리는 첫 지점이 된다.
        int64_t gold_{0};
    };
}
