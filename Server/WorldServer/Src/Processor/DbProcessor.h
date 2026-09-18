#pragma once

#include "Db/DbCommand.h"
#include "Db/DbConnection.h"
#include "Processor/WorldMsg.h"

#include "Server/Core/Src/Pipeline/MessageProcessor.h"

// DB 작업 결과 콜백. **DB 레인 스레드에서 불린다** -- BASIC 상태를 만져야 하면 여기서
// BASIC 레인으로 다시 던져야 한다(그게 로그인 경로가 하는 일이다).
//
// succeeded가 false면 dbResult는 비어 있다. 장애를 콘텐츠가 분기로 처리할 일은 거의 없지만,
// 로그인처럼 **클라이언트에게 실패를 알려야 하는** 경로가 있어 성공 여부를 넘긴다.
using DbCallback = std::function<void(const bool succeeded, const DbResult& dbResult)>;

// `DbExecute` 메시지의 본문. **SP 목록 하나가 트랜잭션 하나다.**
struct DbExecuteBody final
{
    std::vector<DbCommand> commands;
    bool useTransaction{};
    DbCallback callback;
};

// `DbInvoke` 메시지의 본문. SP가 아닌 일을 블로킹 허용 레인에서 돌릴 때만 쓴다.
struct DbInvokeBody final
{
    std::function<void()> work;
};

// DB 레인에서 도는 프로세서.
//
// **왜 클래스로 있는가**: 커넥션 획득 · 트랜잭션 경계 · 실패 정책 · 결과 통지가 한 벌로
// 묶여 다녀야 하는데, 이게 흩어지면 DB 레인에 일을 던지는 자리마다 try/catch를 다시 쓰게
// 된다. 실제로 쿠폰 청크 경로가 그 상태였다.
//
// 아래 static 함수는 **어느 레인에서나** 부를 수 있고(PushMsg가 전역이라 객체가 필요 없다),
// 넘긴 일은 전부 ownerId가 배정한 DB 스레드에서 돈다.
class DbProcessor final : public Pipeline::MessageProcessor
{
public:
    explicit DbProcessor(DbConnectionPool& dbPool);

    [[nodiscard]] std::string_view Name() const override { return "Db"; }
    void RegistHandler() override;

    // SP 목록을 DB 레인에서 실행한다. **여기가 DB 실패 정책이 있는 유일한 자리다.**
    //
    // ownerId는 **계정 단위(playerId)**로 잡는 것이 원칙이다 -- 세션이 아니라 계정에 묶이는
    // 일이라, 재접속으로 세션이 바뀌어도 한 계정의 쓰기가 도착 순서대로 직렬화된다.
    static void Execute(const uint64_t ownerId, std::vector<DbCommand> commands,
                        const bool useTransaction, DbCallback callback);

    // SP가 아닌 일을 DB 레인에서 돌린다. **Execute가 주는 실패 정책이 붙지 않으므로**,
    // SP를 실행할 것이면 반드시 Execute를 쓴다. 블로킹이 허용되는 레인이라는 점만 빌리는
    // 용도다.
    static void Post(const uint64_t ownerId, std::function<void()> work);

private:
    void OnExecute(const Pipeline::OwnerId& owner, const DbExecuteBody& body);
    void OnInvoke(const Pipeline::OwnerId& owner, const DbInvokeBody& body);

    DbConnectionPool& dbPool_;
};
