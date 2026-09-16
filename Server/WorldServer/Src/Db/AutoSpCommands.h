#pragma once

#include "Db/DbCommand.h"
#include "Processor/DbProcessor.h"

// SP 커맨드를 모았다가 **스코프를 벗어날 때 한 번에** DbProcessor로 넘기는 RAII 홀더.
//
// **이 클래스가 하는 일은 모으는 것뿐이다.** 커넥션 획득 · 트랜잭션 · 실패 정책 · 콜백은
// 전부 DbProcessor::Execute에 있다 -- 소멸자에 정책을 두면 읽는 사람이 소멸자를 읽어야
// 알게 된다.
//
// Zone의 Task::UnitOfWork와 같은 모양이고, 실제로 짝을 이룬다 -- 존이 보낸 UnitOfWork
// 하나가 여기 AutoSpCommands 하나가 되고, 그게 곧 **트랜잭션 하나**다. 여러 UnitOfWork를
// 모으지 않는다.
//
// 같은 ownerId(=playerId)의 작업은 항상 같은 DB 스레드에 배정되므로, UnitOfWork들 사이의
// 순서는 어피니티가 보장한다. 그래서 순서를 맞추기 위한 별도 장치가 없다.
//
// **파생 없이 final인 이유**: 소멸자에서 일을 마무리하는 클래스는 파생되면 위험하다
// (기반 소멸자에서 가상 함수가 파생 구현으로 불리지 않는다). ZoneUnitOfWork가 같은
// 이유로 final이다.
class AutoSpCommands final
{
public:
    // useTransaction: 쌓인 SP가 여러 개일 때 하나의 트랜잭션으로 묶을지. 읽기/쓰기와는
    //                 무관하다 -- 조회 하나만 보내면서 콜백을 받는 조합도 정상이다.
    AutoSpCommands(DbProcessor& dbProcessor, const uint64_t ownerId, const bool useTransaction,
                   DbCallback callback = {});
    ~AutoSpCommands();

    AutoSpCommands(const AutoSpCommands&) = delete;
    AutoSpCommands& operator=(const AutoSpCommands&) = delete;

    void Add(DbCommand command);

    [[nodiscard]] bool Empty() const noexcept { return commands_.empty(); }

private:
    DbProcessor& dbProcessor_;
    const uint64_t ownerId_;
    const bool useTransaction_;
    DbCallback callback_;
    std::vector<DbCommand> commands_;
};
