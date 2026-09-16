#include "pch.h"
#include "Processor/DbProcessor.h"

namespace World
{
    DbProcessor::DbProcessor(DbConnectionPool& dbPool, Processor::Group<EProcessorId>& dbGroup)
        : dbPool_(dbPool)
        , dbGroup_(dbGroup)
    {
    }

    void DbProcessor::Execute(const uint64_t ownerId, std::vector<DbCommand> commands,
                              const bool useTransaction, DbCallback callback)
    {
        if (commands.empty())
        {
            return;
        }

        dbGroup_.Post(EProcessorId::Db, ownerId,
            [&dbPool = dbPool_, ownerId, useTransaction,
             callback = std::move(callback), commands = std::move(commands)]
            {
                DbResult dbResult;
                bool succeeded = true;

                try
                {
                    // 커넥션은 **DB 레인 스레드마다 thread_local로 하나씩**이라 여기에 락이 없다.
                    dbPool.ForCurrentThread().Execute(commands, useTransaction, &dbResult);
                }
                catch (const DbException& ex)
                {
                    succeeded = false;

                    // DB 실패는 되돌리지 않는다(Fire-and-Forget). 메모리를 권위로 보고, 어긋난
                    // 사실만 남겨 운영이 대응한다 -- 그래서 이 로그가 찍힌다는 것 자체가 사고
                    // 신호이고, Debug가 아니라 Error 레벨이어야 한다.
                    LOG.Error(ELogCategory::Db, "DB 작업 실패")
                        .KV("OwnerId", ownerId)
                        .KV("SqlState", ex.SqlState())
                        .KV("Message", ex.what());
                }

                if (callback)
                {
                    callback(succeeded, dbResult);
                }
            });
    }
}
