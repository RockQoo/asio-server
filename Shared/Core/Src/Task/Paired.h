#pragma once

#include <utility>

namespace Task
{
    // 태스크가 들고 있는 값 하나. **New와 Prev를 항상 짝으로 갖는다.**
    //
    // **왜 짝이어야 하는가**: 태스크는 "메모리에 무엇을 적용했는가"의 기록이고, 그 기록으로
    // 두 가지를 해야 한다 --
    //   New  -> 성공 시 World(DB)와 클라이언트로 내보낸다. 최신 값이다.
    //   Prev -> 실패 시 되돌린다. 변경 직전의 값이다.
    // 한쪽만 들고 있으면 나머지 하나를 역연산으로 추측해야 하는데, 그건 상한/하한에 걸려
    // 요청한 양과 실제 바뀐 양이 다를 때 틀린다. 값을 둘 다 들고 있으면 그런 경우가 없다.
    //
    // 이전 상태가 없는 변경(우편 추가 등)은 **Prev에 빈 값을 넣는다** -- "없었다"도 상태다.
    //
    // 멤버를 이 타입으로 선언하면 짝을 빠뜨릴 수 없다. 태스크에 값을 직접 두면 New만 넣고
    // 끝내기 쉬운데, 그러면 롤백할 때 되돌릴 대상이 없다.
    template <typename T>
    class Paired
    {
    public:
        void Set(T newValue, T prevValue)
        {
            new_ = std::move(newValue);
            prev_ = std::move(prevValue);
        }

        [[nodiscard]] const T& New() const noexcept { return new_; }
        [[nodiscard]] const T& Prev() const noexcept { return prev_; }

    private:
        T new_{};
        T prev_{};
    };
}
