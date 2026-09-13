#pragma once

#include "Server/WorldServer/Src/Db/DbCommand.h"
#include "Server/WorldServer/Src/Db/DbConnection.h"
#include "Server/WorldServer/Src/Worker/ProcessorId.h"

#include "Shared/Core/Src/Processor/Group.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace World
{
    // DB 작업 결과 콜백. **DB 레인 스레드에서 불린다** -- BASIC 상태를 만져야 하면 여기서
    // basicGroup으로 다시 던져야 한다(그게 로그인 경로가 하는 일이다).
    //
    // succeeded가 false면 result는 비어 있다. 장애를 콘텐츠가 분기로 처리할 일은 거의 없지만,
    // 로그인처럼 **클라이언트에게 실패를 알려야 하는** 경로가 있어 성공 여부를 넘긴다.
    using DbCallback = std::function<void(const bool succeeded, const DbResult& result)>;

    // 변경 목록을 모았다가 **스코프를 벗어날 때 한 번에** DB 큐 그룹으로 보내는 RAII 홀더.
    //
    // Zone의 Task::UnitOfWork와 같은 모양이고, 실제로 짝을 이룬다 -- 존이 보낸 UnitOfWork
    // 하나가 여기 AutoDbCommand 하나가 되고, 그게 곧 **트랜잭션 하나**다. 여러 UnitOfWork를
    // 모으지 않는다.
    //
    // 같은 ownerId(=playerId)의 작업은 항상 같은 DB 스레드에 배정되므로, UnitOfWork들 사이의
    // 순서는 어피니티가 보장한다. 그래서 순서를 맞추기 위한 별도 장치가 없다.
    //
    // **파생 없이 final인 이유**: 소멸자에서 일을 마무리하는 클래스는 파생되면 위험하다
    // (기반 소멸자에서 가상 함수가 파생 구현으로 불리지 않는다). Zone::UnitOfWork가 같은
    // 이유로 final이다.
    class AutoDbCommand final
    {
    public:
        // useTransaction: 쌓인 SP가 여러 개일 때 하나의 트랜잭션으로 묶을지. 읽기/쓰기와는
        //                 무관하다 -- 조회 하나만 보내면서 콜백을 받는 조합도 정상이다.
        AutoDbCommand(DbConnectionPool& pool, Processor::Group<EProcessorId>& dbGroup,
                      const uint64_t ownerId, const bool useTransaction, DbCallback callback = {});
        ~AutoDbCommand();

        AutoDbCommand(const AutoDbCommand&) = delete;
        AutoDbCommand& operator=(const AutoDbCommand&) = delete;

        void Add(DbCommand command);

        [[nodiscard]] bool Empty() const noexcept { return commands_.empty(); }

    private:
        DbConnectionPool& pool_;
        Processor::Group<EProcessorId>& dbGroup_;
        const uint64_t ownerId_;
        const bool useTransaction_;
        DbCallback callback_;
        std::vector<DbCommand> commands_;
    };
}
