#pragma once

#include "Shared/Core/Src/Base/BasicTypes.h"

namespace Task
{
    // UnitOfWork에 쌓이는 변경 기록 하나.
    //
    // **태스크는 데이터만 갖는다.** 직렬화도 역연산도 여기 없다 -- 그건 태스크의 뜻을 아는
    // 콘텐츠 계층(파생 UnitOfWork)이 taskKind로 분기해서 한다. Core는 목록을 들고 순서를
    // 지켜줄 뿐이고, 그래야 Core가 게임을 모른다는 규칙이 선다.
    //
    // 파생이 갖는 것은 셋뿐이다:
    //   1) Kind()          -- 무슨 변경인가 (Common::MakeTaskKind로 만든 값)
    //   2) Paired<T> 멤버  -- 적용한 값(New)과 직전 값(Prev)
    //   3) Set(...)        -- 그 멤버를 채우는 진입점. UnitOfWork::AddTask가 부른다
    //
    // **불변 규칙**: 모델 메모리에 적용한 변경은 반드시 여기 태스크로 남아야 한다. 태스크
    // 목록이 곧 "실제로 적용된 변경"이어야 역순 롤백이 정확하고, 클라이언트가 같은 목록을
    // 적용해 서버와 상태가 맞는다.
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

        // World와 클라이언트가 같은 값으로 분기하는 태스크 종류(Common::MakeTaskKind).
        [[nodiscard]] virtual uint16_t Kind() const noexcept = 0;

    protected:
        ITask() = default;
    };
}
