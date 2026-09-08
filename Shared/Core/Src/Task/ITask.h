#pragma once

#include "Shared/Core/Src/Common/BasicTypes.h"

#include <cstdint>

namespace Packet
{
    class BinaryWriter;
}

namespace Task
{
    class UnitOfWork;

    // UnitOfWork에 쌓이는 변경 기록 하나. Core는 이 변경이 무슨 뜻인지 모르고, 직렬화와
    // 역연산은 콘텐츠 계층이 파생 클래스에서 구현한다.
    //
    // **왜 직렬화된 바이트가 아니라 타입 있는 객체인가**: 기록 시점에 직렬화해두면 (1) 실패로
    // 끝나는 요청도 직렬화 비용을 내고, (2) 롤백할 때 그 바이트를 다시 파싱해야 한다. 객체로
    // 들고 있으면 직렬화는 성공할 때 한 번만 하고, 롤백은 필드를 그대로 쓴다. 대가는 태스크마다
    // 힙 할당이고, 부하 테스트에서 문제가 되면 종류별 오브젝트 풀을 넣을 자리가 여기다.
    //
    // 이름에 I 접두사를 붙인 건 `namespace Task`와 클래스 이름이 겹치기 때문이다
    // (Network::IPacketHandler와 같은 규약).
    class ITask
    {
    public:
        virtual ~ITask() = default;

        ITask(const ITask&) = delete;
        ITask& operator=(const ITask&) = delete;
        ITask(ITask&&) = delete;
        ITask& operator=(ITask&&) = delete;

        // World와 클라이언트가 같은 값으로 분기하는 태스크 종류(Protocol::MakeTaskKind).
        [[nodiscard]] virtual uint16_t Kind() const noexcept = 0;

        // 성공했을 때만 불린다 -- 여기 쓴 바이트가 그대로 World와 클라이언트로 나간다.
        virtual void Serialize(Packet::BinaryWriter& writer) const = 0;

        // 실패했을 때 기록의 역순으로 불린다. sink는 전송 기능이 없는 UnitOfWork라 되돌리는
        // 과정에서 쌓인 태스크가 조용히 버려진다 -- 그래서 롤백 전용 함수를 따로 만들지 않고
        // 정상 함수(추가를 되돌릴 때 삭제 함수)를 그대로 재사용할 수 있다.
        // 롤백은 조금 전에 성공한 변경을 되돌리는 것뿐이라 실패할 수 없다는 전제다
        // (.claude/rules/cpp-patterns.md "콘텐츠 로직 실패는 ..." 절 참고).
        virtual void Rollback(UnitOfWork& sink) const = 0;

    protected:
        ITask() = default;
    };
}
