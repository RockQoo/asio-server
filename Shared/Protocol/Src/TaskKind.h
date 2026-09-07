#pragma once

#include <cstdint>
#include <type_traits>

namespace Protocol
{
    // Task::UnitOfWork::RecordTask에 실리는 taskKind의 인코딩 규약.
    // 상위 8비트 = 콘텐츠 카테고리, 하위 8비트 = 그 안의 세부 동작.
    //
    // 패킷 id처럼 번호 대역을 자르지 않는 이유: 패킷은 헤더에 id 하나만 실려서 "어느 링크에서
    // 온 값인지"를 값 자체로 판정해야 하지만, 태스크는 이미 어느 UnitOfWork 스트림인지 확정된
    // 뒤에 읽으므로 그럴 필요가 없다. 대신 카테고리를 명시적으로 들고 있으면 읽는 쪽이
    // switch(카테고리) -> switch(세부동작) 2단 분기를 그대로 쓸 수 있다.
    //
    // 이 2단 분기는 같은 모양으로 세 곳에서 재사용된다:
    //   1) World  : 카테고리로 어느 SP 묶음인지, 세부 동작으로 INSERT/DELETE를 고른다
    //   2) Zone   : 롤백 시 카테고리로 어느 모델인지, 세부 동작으로 역연산을 고른다
    //   3) 클라이언트 : 내려받은 태스크를 자기 메모리에 그대로 적용한다
    enum class ETaskCategory : uint8_t
    {
        None = 0,
        Mail = 1,
    };

    // 세부 동작은 카테고리마다 따로 센다(카테고리가 다르면 값이 겹쳐도 무방하다).
    // 0은 "세부 동작 없음"으로 비워둬서 taskKind 0 전체가 유효하지 않은 값이 되게 한다.
    enum class EMailTask : uint8_t
    {
        Added = 1,
        Removed = 2,
    };

    // 카테고리별 세부 동작 enum이 전부 다른 타입이라 템플릿으로 받는다 -- 호출부에서
    // static_cast를 반복하지 않으려는 것이고, is_enum 제약으로 엉뚱한 정수가 섞여 드는 건 막는다.
    template <typename TSubTask>
        requires std::is_enum_v<TSubTask>
    [[nodiscard]] constexpr uint16_t MakeTaskKind(const ETaskCategory category, const TSubTask subTask) noexcept
    {
        return static_cast<uint16_t>((static_cast<uint16_t>(category) << 8)
                                     | static_cast<uint16_t>(static_cast<uint8_t>(subTask)));
    }

    [[nodiscard]] constexpr ETaskCategory CategoryOf(const uint16_t taskKind) noexcept
    {
        return static_cast<ETaskCategory>(static_cast<uint8_t>(taskKind >> 8));
    }

    // 반환값을 그 카테고리의 세부 동작 enum으로 캐스팅해서 쓴다(예: EMailTask).
    [[nodiscard]] constexpr uint8_t SubTaskOf(const uint16_t taskKind) noexcept
    {
        return static_cast<uint8_t>(taskKind & 0xFF);
    }
}
