#pragma once

#include "Shared/Core/Src/Common/BasicTypes.h"
#include "Shared/Core/Src/Common/RequestId.h"
#include "Shared/Core/Src/Task/ITask.h"

#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace Task
{
    // owner 하나의 상태 변경을 한 요청 스코프 동안 태스크로 누적했다가, **파생 클래스의
    // 소멸자**에서 결말을 내는 범용 Unit-of-Work의 기반 클래스.
    //
    // Core는 태스크의 실제 의미(우편 추가, 재화 차감 등)를 몰라야 하므로 이 클래스가 다루는 건
    // ITask 목록과 에러 코드뿐이다. 전송/통지는 콘텐츠 계층이 파생 클래스에서 구현한다
    // (ZoneServer의 Zone::ZoneUnitOfWork).
    //
    // **왜 커밋이 파생 클래스의 소멸자인가**: 기반 소멸자가 도는 시점에는 파생 부분이 이미
    // 파괴돼 있어서(소멸은 파생 -> 기반 순서) 가상 함수를 불러도 파생 구현으로 가지 않는다.
    // 그래서 기반 소멸자는 아무것도 하지 않고(`= default`), 커밋/롤백은 전부 파생 소멸자에
    // 둔다 -- 파생 소멸자가 도는 동안에는 자기 자신이 아직 온전하므로 가상 함수가 아예 필요
    // 없다. 파생을 `final`로 닫으면 "그 파생을 또 상속해 같은 문제를 만드는" 경로도 없어진다.
    // 그리고 소멸자 커밋이라 **"커밋을 깜빡한다"는 실수 자체가 성립하지 않는다.**
    //
    // 스코프를 벗어나는 순간이 곧 결말이므로, 파생 소멸자는 예외를 밖으로 내보내면 안 된다
    // (`noexcept` + 내부 try/catch). 소멸자에서 예외가 나가면 프로세스가 즉시 종료된다.
    class UnitOfWork
    {
    public:
        // requestId는 이 요청 하나를 가리키는 값이다 -- 발급은 콘텐츠 계층이 한다(Core는
        // 어느 프로세스인지, 노드 번호가 몇인지 모른다).
        UnitOfWork(const uint64_t ownerId, const Common::RequestId requestId);

        UnitOfWork(const UnitOfWork&) = delete;
        UnitOfWork& operator=(const UnitOfWork&) = delete;
        UnitOfWork(UnitOfWork&&) = delete;
        UnitOfWork& operator=(UnitOfWork&&) = delete;

        // 상태를 실제로 바꾼 뒤에만 부른다 -- 태스크 목록이 곧 "실제로 적용된 변경"이어야
        // 역순 롤백이 정확해진다.
        void AddTask(std::unique_ptr<ITask> task);

        // 콘텐츠 계층이 정의하는 에러 코드(Protocol::EErrorCode)를 받는다 -- Core는 이 정수의
        // 의미를 모르고, 0이 아니면 실패로만 취급한다.
        // 이미 에러가 설정돼 있으면 덮어쓰지 않는다: 처음 난 실패가 진짜 원인이고, 그 뒤는
        // 그것 때문에 연쇄로 실패한 것일 가능성이 높다.
        template <typename TErrorCode>
            requires std::is_enum_v<TErrorCode>
        void SetError(const TErrorCode errorCode) noexcept
        {
            if (errorCode_ == 0)
            {
                errorCode_ = static_cast<int32_t>(errorCode);
            }
        }

        [[nodiscard]] bool HasError() const noexcept { return errorCode_ != 0; }
        [[nodiscard]] int32_t GetError() const noexcept { return errorCode_; }
        [[nodiscard]] bool IsEmpty() const noexcept { return tasks_.empty(); }
        [[nodiscard]] uint64_t GetOwnerId() const noexcept { return ownerId_; }
        [[nodiscard]] Common::RequestId GetRequestId() const noexcept { return requestId_; }

    protected:
        // 다형적으로 삭제할 일이 없는(항상 스택에 두는) 타입이라 가상 소멸자를 두지 않는다.
        // protected라 기반 포인터로 delete하는 것 자체가 컴파일되지 않는다 -- 그 경로만이
        // 파생 소멸자를 건너뛸 수 있으므로, 소멸자 커밋을 지키는 마지막 방어선이다.
        ~UnitOfWork() = default;

        // 성공 경로에서만 부른다(실패하면 되돌리므로 내보낼 것이 없다).
        // 와이어 포맷: ownerId(8) + taskCount(2) + taskCount개의 {kind(2) + len(4) + payload}.
        // 길이 프리픽스를 두는 이유: 읽는 쪽이 그 kind를 아직 모르더라도(새 콘텐츠 태스크가
        // 추가되기 전 빌드) 통째로 건너뛰고 다음 태스크로 넘어갈 수 있게 하기 위함.
        [[nodiscard]] std::vector<byte> Serialize() const;

        // 실패 경로에서 기록의 역순으로 되돌린다 -- 나중에 일어난 변경부터 되돌려야 중간
        // 상태를 거치지 않는다. 각 태스크가 자기 역연산을 알고 있으므로 여기에 종류별 분기가
        // 없다(새 태스크를 추가하면서 롤백을 빼먹으면 순수 가상 때문에 컴파일이 안 된다).
        void RollbackAll() noexcept;

    private:
        uint64_t ownerId_;
        Common::RequestId requestId_;
        int32_t errorCode_{};
        std::vector<std::unique_ptr<ITask>> tasks_;
    };

    // 전송 기능이 없는 UnitOfWork. 롤백 중에 모델의 정상 함수를 재사용하려고 이걸 넘긴다 --
    // 쌓인 태스크는 소멸자가 아무것도 하지 않으므로 조용히 버려진다. 원래 UnitOfWork를 넘기면
    // (1) "지급하지 않았는데 삭제했다"는 태스크가 DB로 나가고 (2) 순회 중인 목록에 원소가
    // 추가돼 반복자가 깨진다.
    class RollbackUnitOfWork final : public UnitOfWork
    {
    public:
        // 롤백 중에 쌓이는 태스크는 어차피 버려지므로 요청 id를 새로 태우지 않는다.
        RollbackUnitOfWork()
            : UnitOfWork(0, Common::kInvalidRequestId)
        {
        }

        ~RollbackUnitOfWork() = default;
    };
}
