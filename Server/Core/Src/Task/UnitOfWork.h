#pragma once

#include "Server/Core/Src/Base/BasicTypes.h"
#include "Server/Core/Src/Base/RUID.h"
#include "Server/Core/Src/Task/ITask.h"
#include "Server/Core/Src/Task/Paired.h"

namespace Task
{
    // owner 하나의 상태 변경을 한 요청 스코프 동안 태스크로 누적했다가, **파생 클래스의
    // 소멸자**에서 결말을 내는 범용 Unit-of-Work 기반 클래스.
    //
    // **Core는 태스크의 뜻을 모른다.** 목록과 순서, ownerId/requestId/errorCode만 다룬다.
    // 직렬화(성공 경로)와 롤백(실패 경로)은 둘 다 태스크가 무엇인지 알아야 하므로 파생이
    // 한다 -- `Tasks()`로 목록을 받아 taskKind로 분기하면 된다.
    //
    // **기반 소멸자에 커밋을 두면 안 된다** -- 그 시점에는 파생이 이미 파괴돼 있어(소멸은
    // 파생 -> 기반) 가상 함수가 파생 구현으로 가지 않는다. 그래서 기반은 `= default`이고
    // 커밋/롤백은 전부 파생 소멸자에 있다. 파생은 `final`로 닫는다.
    //
    // **파생 소멸자는 예외를 밖으로 내보내면 안 된다**(`noexcept` + 내부 try/catch).
    // 소멸자에서 예외가 나가면 프로세스가 즉시 종료된다.
    //
    // 설계 근거: docs/design/unit-of-work.md
    class UnitOfWork
    {
    public:
        using TaskList = std::vector<std::unique_ptr<ITask>>;

        // requestId는 이 요청 하나를 가리키는 값이다 -- 발급은 콘텐츠 계층이 한다(Core는
        // 어느 프로세스인지, 노드 번호가 몇인지 모른다).
        UnitOfWork(const uint64_t ownerId, const Base::RUID requestId);

        UnitOfWork(const UnitOfWork&) = delete;
        UnitOfWork& operator=(const UnitOfWork&) = delete;
        UnitOfWork(UnitOfWork&&) = delete;
        UnitOfWork& operator=(UnitOfWork&&) = delete;

        // **모델 메모리를 바꾼 직후에 부른다.** 태스크 목록이 곧 "실제로 적용된 변경"이어야
        // 역순 롤백이 정확하고 클라이언트 상태가 맞는다(불변 규칙, ITask 주석 참고).
        //
        //   unitOfWork.AddTask<AddMailTask>(newInfo, prevInfo);
        //
        // 인자는 그대로 TTask::Set(...)으로 전달된다 -- 태스크마다 필요한 값이 달라서
        // (재화는 종류가 하나 더 붙는다) 생성자가 아니라 Set으로 받는다.
        template <typename TTask, typename... TArgs>
            requires std::is_base_of_v<ITask, TTask>
        TTask& AddTask(TArgs&&... args)
        {
            auto task = std::make_unique<TTask>();
            TTask& ref = *task;
            ref.Set(std::forward<TArgs>(args)...);
            tasks_.push_back(std::move(task));
            return ref;
        }

        // 콘텐츠 계층이 정의하는 에러 코드(Common::EErrorCode)를 받는다 -- Core는 이 정수의
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
        [[nodiscard]] Base::RUID GetRequestId() const noexcept { return requestId_; }

    protected:
        // 다형적으로 삭제할 일이 없는(항상 스택에 두는) 타입이라 가상 소멸자를 두지 않는다.
        // protected라 기반 포인터로 delete하는 것 자체가 컴파일되지 않는다 -- 그 경로만이
        // 파생 소멸자를 건너뛸 수 있으므로, 소멸자 커밋을 지키는 마지막 방어선이다.
        ~UnitOfWork() = default;

        // 파생이 기록 순서대로(직렬화) 또는 역순으로(롤백) 훑는다.
        [[nodiscard]] const TaskList& Tasks() const noexcept { return tasks_; }

        // 롤백을 끝낸 파생이 부른다 -- 되돌린 기록을 남겨두면 두 번 되돌릴 수 있다.
        void ClearTasks() noexcept { tasks_.clear(); }

    private:
        uint64_t ownerId_;
        Base::RUID requestId_;
        int32_t errorCode_{};
        TaskList tasks_;
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
            : UnitOfWork(0, Base::kInvalidRUID)
        {
        }

        ~RollbackUnitOfWork() = default;
    };
}
