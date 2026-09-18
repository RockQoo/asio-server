#pragma once

#include <cstdint>

#include "Server/Common/Src/Enum.h"

namespace Common
{
    // 재화 하나의 잔액. **World와 Zone이 같은 타입을 쓴다**(MailInfo와 같은 근거).
    //
    // 이 빌드가 모르는 종류가 와이어로 올 수 있지만, `enum class ... : uint8_t`는 밑바탕
    // 타입이 고정이라 열거자에 없는 값을 담아도 정의된 동작이다. 그래서 정수로 받을 이유가
    // 없고, 모르는 종류를 걸러내는 건 의미를 아는 곳(Currency::Model 생성자)에서 한다 --
    // 모르는 종류 하나 때문에 입장을 막지 않는다.
    struct CurrencyInfo
    {
        ECurrencyType type{};
        int64_t amount{};
    };
}
