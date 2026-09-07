#pragma once

#include <type_traits>

namespace Common
{
    // scoped enum을 비트 플래그로 쓸 때 매크로 대신 쓰는 헬퍼. `enum class`는 정수로 암묵
    // 변환되지 않아서(그게 이 프로젝트가 `enum class`만 쓰는 이유다) 조합 검사에 매번
    // static_cast가 필요한데, 그 캐스팅을 여기 한 곳에만 두려는 것이다.
    template <typename E>
        requires std::is_enum_v<E>
    [[nodiscard]] constexpr bool HasFlag(const E value, const E flag) noexcept
    {
        using U = std::underlying_type_t<E>;
        return (static_cast<U>(value) & static_cast<U>(flag)) == static_cast<U>(flag);
    }
}
