#include "pch.h"

#include "Db/AutoSpCommands.h"

namespace World
{
    AutoSpCommands::AutoSpCommands(DbProcessor& dbProcessor, const uint64_t ownerId,
                                   const bool useTransaction, DbCallback callback)
        : dbProcessor_(dbProcessor)
        , ownerId_(ownerId)
        , useTransaction_(useTransaction)
        , callback_(std::move(callback))
    {
    }

    void AutoSpCommands::Add(DbCommand command)
    {
        commands_.push_back(std::move(command));
    }

    AutoSpCommands::~AutoSpCommands()
    {
        // 소멸자에서 던지지 않는 것이 이 클래스의 계약이다 -- 예외가 스택 되감기 중에 나가면
        // std::terminate로 프로세스가 죽는다. 여기서는 넘기기만 하고, 실행과 실패 처리는
        // DbProcessor가 DB 레인에서 한다.
        dbProcessor_.Execute(ownerId_, std::move(commands_), useTransaction_, std::move(callback_));
    }
}
