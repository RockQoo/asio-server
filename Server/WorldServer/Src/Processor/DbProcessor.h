#pragma once

#include "Db/DbCommand.h"
#include "Db/DbConnection.h"
#include "Worker/ProcessorId.h"

#include "Shared/Core/Src/Processor/Group.h"

namespace World
{
    // DB 작업 결과 콜백. **DB 레인 스레드에서 불린다** -- BASIC 상태를 만져야 하면 여기서
    // basicGroup으로 다시 던져야 한다(그게 로그인 경로가 하는 일이다).
    //
    // succeeded가 false면 dbResult는 비어 있다. 장애를 콘텐츠가 분기로 처리할 일은 거의 없지만,
    // 로그인처럼 **클라이언트에게 실패를 알려야 하는** 경로가 있어 성공 여부를 넘긴다.
    using DbCallback = std::function<void(const bool succeeded, const DbResult& dbResult)>;

    // DB 레인(EProcessorId::Db)에서 도는 일을 맡는 프로세서.
    //
    // **왜 클래스로 있는가**: 커넥션 획득 · 트랜잭션 경계 · 실패 정책 · 결과 통지가 한 벌로
    // 묶여 다녀야 하는데, 이게 흩어지면 DB 레인에 일을 던지는 자리마다 try/catch를 다시 쓰게
    // 된다. 실제로 쿠폰 청크 경로가 그 상태였다.
    //
    // 호출은 어느 레인에서나 가능하고, 넘긴 일은 전부 ownerId가 배정한 DB 스레드에서 돈다.
    class DbProcessor
    {
    public:
        DbProcessor(DbConnectionPool& dbPool, Processor::Group<EProcessorId>& dbGroup);

        // SP 목록을 DB 레인에서 실행한다. **여기가 DB 실패 정책이 있는 유일한 자리다.**
        //
        // ownerId는 **계정 단위(playerId)**로 잡는 것이 원칙이다 -- 세션이 아니라 계정에 묶이는
        // 일이라, 재접속으로 세션이 바뀌어도 한 계정의 쓰기가 도착 순서대로 직렬화된다.
        void Execute(const uint64_t ownerId, std::vector<DbCommand> commands,
                     const bool useTransaction, DbCallback callback);

        // SP가 아닌 일을 DB 레인에서 돌린다. **Execute가 주는 실패 정책이 붙지 않으므로**,
        // SP를 실행할 것이면 반드시 Execute를 쓴다. 블로킹이 허용되는 레인이라는 점만 빌리는
        // 용도다.
        template <typename F>
        void Post(const uint64_t ownerId, F&& work)
        {
            dbGroup_.Post(EProcessorId::Db, ownerId, std::forward<F>(work));
        }

    private:
        DbConnectionPool& dbPool_;
        Processor::Group<EProcessorId>& dbGroup_;
    };
}
