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

    // Task::UnitOfWork::RecordTask에 넘기는 taskKind 값 -- Mail 콘텐츠가 정의하는 값이라
    // Core는 이 enum을 모른다. WorldServer의 HandleUnitOfWorkStream이 이 값으로 각 태스크가
    // 추가인지 삭제인지 구분해서 읽는다.
    enum class MailTaskKind : uint8_t
    {
        Added = 0,
        Removed = 1,
    };

    // 플레이어 한 명의 우편함. 상태를 직접 바꾸고 끝내지 않고, 바뀐 내용을 Task::UnitOfWork에
    // 기록만 한다(Unit-of-Work) -- 실제 World 전송은 UnitOfWork가 스코프를 벗어날 때 한 번에
    // 처리한다. 평소(존 로직 스레드에서 클라이언트 요청 처리)와 만료 삭제(별도 유지보수
    // 타이머 스레드)가 같은 인스턴스를 건드릴 수 있어 MailRegistry가 이 클래스를 Sync(=
    // Threading::Synchronized<MailModel>)로 감싸서 보관한다 -- MailModel 자신은 락을 전혀 모른다.
    class MailModel
    {
    public:
        using Sync = Threading::Synchronized<MailModel>;

        // 실제로 배정된 mailId를 반환한다 -- 호출자(ZoneInstance::HandleMailAdd)가 이 값을
        // MailAddAck으로 클라이언트에 돌려줘야 클라이언트가 자기가 만든 메일을 나중에 지울 수
        // 있다.
        [[nodiscard]] uint32_t AddMail(MailInfo info, Task::UnitOfWork& unitOfWork);

        // 실제로 찾아서 지웠는지를 반환한다(false면 이미 없는 mailId) -- 호출자가 MailDelAck의
        // 성공 여부로 그대로 돌려준다.
        [[nodiscard]] bool DelMail(const uint32_t mailId, Task::UnitOfWork& unitOfWork, const bool isTimeout);

        // 순수 조회 -- 실제 삭제는 호출자가 DelMail로 한다.
        [[nodiscard]] std::vector<uint32_t> TakeExpiredMailIds(const int64_t nowUt) const;

    private:
        uint32_t nextMailId_{1};
        std::unordered_map<uint32_t, MailInfo> mails_;
    };
}
