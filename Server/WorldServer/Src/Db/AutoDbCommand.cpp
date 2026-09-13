#include "pch.h"

#include "Db/AutoDbCommand.h"

#include <utility>

namespace World
{
    AutoDbCommand::AutoDbCommand(DbConnectionPool& pool,
                                 Processor::Group<EProcessorId>& dbGroup,
                                 const uint64_t ownerId, const bool useTransaction,
                                 DbCallback callback)
        : pool_(pool)
        , dbGroup_(dbGroup)
        , ownerId_(ownerId)
        , useTransaction_(useTransaction)
        , callback_(std::move(callback))
    {
    }

    void AutoDbCommand::Add(DbCommand command)
    {
        commands_.push_back(std::move(command));
    }

    AutoDbCommand::~AutoDbCommand()
    {
        if (commands_.empty())
        {
            return;
        }

        // 소멸자에서 던지지 않는 것이 이 클래스의 계약이다 -- 예외가 스택 되감기 중에 나가면
        // std::terminate로 프로세스가 죽는다. 여기서는 큐에 넣기만 하고, 실제 실행과 실패
        // 처리는 DB 레인에서 한다.
        dbGroup_.Post(EProcessorId::Db, ownerId_,
            [&pool = pool_, ownerId = ownerId_, useTransaction = useTransaction_,
             callback = std::move(callback_), commands = std::move(commands_)]
            {
                DbResult result;
                bool succeeded = true;

                try
                {
                    pool.ForCurrentThread().Execute(commands, useTransaction, &result);
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
                    callback(succeeded, result);
                }
            });
    }
}
