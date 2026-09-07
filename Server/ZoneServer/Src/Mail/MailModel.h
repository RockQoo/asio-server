#pragma once

#include "Shared/Core/Src/Threading/Synchronized.h"

#include <cstdint>
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
    // 기록만 한다(Unit-of-Work) -- 실제 World 전송은 UnitOfWork가 스코프를 벗어날 때 한 번에
    // 처리한다. 평소(존 로직 스레드에서 클라이언트 요청 처리)와 만료 삭제(별도 유지보수
    // 타이머 스레드)가 같은 인스턴스를 건드릴 수 있어 MailRegistry가 이 클래스를 Sync(=
    // Threading::Synchronized<MailModel>)로 감싸서 보관한다 -- MailModel 자신은 락을 전혀 모른다.
    class MailModel
    {
    public:
        using Sync = Threading::Synchronized<MailModel>;

        // 성공/실패는 반환값이 아니라 unitOfWork에 실린다 -- 한 요청이 여러 모델을 건드릴 때
        // (메일 추가 + 재화 차감 등) 어느 단계에서 실패했든 호출부는 Commit() 한 번으로
        // 롤백까지 끝내야 하기 때문이다. 배정된 mailId도 반환하지 않는다: 클라이언트는
        // Added 태스크를 그대로 받아 적용하므로 그 안에 이미 들어 있다.
        void AddMail(MailInfo info, Task::UnitOfWork& unitOfWork);

        void DelMail(const uint32_t mailId, Task::UnitOfWork& unitOfWork, const bool isTimeout);

        // 순수 조회 -- 실제 삭제는 호출자가 DelMail로 한다.
        [[nodiscard]] std::vector<uint32_t> TakeExpiredMailIds(const int64_t nowUt) const;

        // 롤백 전용 역연산. **UnitOfWork를 받지 않는다** -- 롤백 중에 태스크가 다시 쌓이면
        // 되돌리기가 또 되돌려야 할 변경을 만들어낸다. Zone::ZoneUnitOfWork::OnRollback만
        // 호출한다.
        void UndoAdd(const uint32_t mailId);
        void UndoRemove(MailInfo info);

    private:
        uint32_t nextMailId_{1};
        std::unordered_map<uint32_t, MailInfo> mails_;
    };
}
