#include "pch.h"
#include "Processor/DbProcessor.h"

#include "Processor/ProcessorIds.h"
#include "Server/Core/Src/Pipeline/ProducerHolder.h"

DbProcessor::DbProcessor(DbConnectionPool& dbPool)
    : dbPool_(dbPool)
{
}

void DbProcessor::RegistHandler()
{
    Regist(EWorldMsg::DbExecute, &DbProcessor::OnExecute);
    Regist(EWorldMsg::DbInvoke,  &DbProcessor::OnInvoke);
}

void DbProcessor::Execute(const uint64_t ownerId, std::vector<DbCommand> commands,
                          const bool useTransaction, DbCallback callback)
{
    if (commands.empty())
    {
        return;
    }

    Pipeline::PushMsg<Pipeline::EProducerType::Db>(
        EWorldMsg::DbExecute, Ids().db, Pipeline::OwnerId{static_cast<int64_t>(ownerId)},
        DbExecuteBody{std::move(commands), useTransaction, std::move(callback)});
}

void DbProcessor::Post(const uint64_t ownerId, std::function<void()> work)
{
    Pipeline::PushMsg<Pipeline::EProducerType::Db>(
        EWorldMsg::DbInvoke, Ids().db, Pipeline::OwnerId{static_cast<int64_t>(ownerId)},
        DbInvokeBody{std::move(work)});
}

void DbProcessor::OnExecute(const Pipeline::OwnerId& owner, const DbExecuteBody& body)
{
    DbResult dbResult;
    bool succeeded = true;

    try
    {
        // 커넥션은 **DB 레인 스레드마다 thread_local로 하나씩**이라 여기에 락이 없다.
        dbPool_.ForCurrentThread().Execute(body.commands, body.useTransaction, &dbResult);
    }
    catch (const DbException& ex)
    {
        succeeded = false;

        // DB 실패는 되돌리지 않는다(Fire-and-Forget). 메모리를 권위로 보고, 어긋난
        // 사실만 남겨 운영이 대응한다 -- 그래서 이 로그가 찍힌다는 것 자체가 사고
        // 신호이고, Debug가 아니라 Error 레벨이어야 한다.
        LOG.Error(ELogCategory::Db, "DB 작업 실패")
            .KV("OwnerId", owner.value)
            .KV("SqlState", ex.SqlState())
            .KV("Message", ex.what());
    }

    if (body.callback)
    {
        body.callback(succeeded, dbResult);
    }
}

void DbProcessor::OnInvoke(const Pipeline::OwnerId& /*owner*/, const DbInvokeBody& body)
{
    if (body.work)
    {
        body.work();
    }
}
