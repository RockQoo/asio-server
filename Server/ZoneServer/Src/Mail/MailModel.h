#pragma once

#include "Shared/Core/Src/Thread/Mutexed.h"
#include "Shared/Protocol/Src/ErrorCode.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Task
{
    class UnitOfWork;
}

namespace Mail
{
    struct MailInfo
    {
        uint32_t mailId{};
        std::string title;
        std::string body;
        int64_t sendUt{};
        int64_t endUt{};
    };

    // taskKind 값은 Protocol::MakeTaskKind(ETaskCategory::Mail, EMailTask::Xxx)로 만든다
    // (Shared/Protocol/Src/TaskKind.h). Zone에서 기록하고 World가 DB에 반영하고 클라이언트가
    // 자기 메모리에 적용하는, 세 프로세스가 공유하는 값이라 ZoneServer 안에 둘 수 없다.

    // 플레이어 한 명의 우편함. 상태를 직접 바꾸고 끝내지 않고, 바뀐 내용을 Task::UnitOfWork에
    // 태스크로 기록만 한다(Unit-of-Work) -- 실제 World 전송은 UnitOfWork가 스코프를 벗어날 때
    // 한 번에 처리한다. 평소(존 로직 스레드에서 클라이언트 요청 처리)와 만료 삭제(별도 유지보수
    // 타이머 스레드)가 같은 인스턴스를 건드릴 수 있어 MailRegistry가 이 클래스를 Mutexed(=
    // Thread::Mutexed<MailModel>)로 감싸서 보관한다 -- MailModel 자신은 락을 전혀 모른다.
    class MailModel
    {
    public:
        using Mutexed = Thread::Mutexed<MailModel>;

        // 자기를 감싼 Mutexed 핸들. 롤백은 이 우편함을 **다시 잠그고** 되돌려야 하는데,
        // 모델 자신은 자기를 감싼 래퍼를 알 수 없어서 만든 쪽(MailRegistry)이 넣어준다.
        // weak_ptr로 두는 이유: 이걸 shared_ptr로 들면 래퍼 <-> 모델이 서로를 붙잡아 절대
        // 소멸하지 않는다(순환 참조).
        void BindSelf(const std::shared_ptr<Mutexed>& self);

        // 실패는 반환값으로 알린다 -- SetError는 호출부가 부른다(cpp-patterns.md "콘텐츠 로직
        // 실패는 ..." 절). 배정된 mailId는 반환하지 않는다: 클라이언트는 Added 태스크를 그대로
        // 받아 적용하므로 그 안에 이미 들어 있다.
        [[nodiscard]] EErrorCode AddMail(MailInfo info, Task::UnitOfWork& unitOfWork);

        // mailId를 서버가 배정하지 않고 **주어진 값 그대로** 되살린다. 삭제를 되돌릴 때
        // (DelMailTask::Rollback) 쓰고, 나중에 DB에서 우편함을 불러올 때도 이 경로를 쓴다 --
        // AddMail은 id를 새로 배정해버려서 원래 id를 복원할 수 없다.
        [[nodiscard]] EErrorCode InsertMail(MailInfo info, Task::UnitOfWork& unitOfWork);

        [[nodiscard]] EErrorCode DelMail(const uint32_t mailId, Task::UnitOfWork& unitOfWork,
                                         const bool isTimeout);

        // 순수 조회 -- 실제 삭제는 호출자가 DelMail로 한다.
        [[nodiscard]] std::vector<uint32_t> TakeExpiredMailIds(const int64_t nowUt) const;

    private:
        [[nodiscard]] std::shared_ptr<Mutexed> LockSelf() const;

        std::weak_ptr<Mutexed> self_;
        uint32_t nextMailId_{1};
        std::unordered_map<uint32_t, MailInfo> mails_;
    };
}
