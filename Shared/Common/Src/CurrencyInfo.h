#pragma once

#include <cstdint>

namespace Common
{
    // 재화 하나의 잔액. **World와 Zone이 같은 타입을 쓴다**(MailInfo와 같은 근거).
    //
    // 종류가 ECurrencyType이 아니라 uint8_t인 이유: 와이어에서 uint8이고, **이 빌드가 모르는
    // 종류가 올 수 있다.** enum으로 받으면 그 순간 정의되지 않은 열거값이 되므로, 받는 단계는
    // 정수로 두고 의미를 아는 곳(Model 생성자)에서 걸러낸다 -- 모르는 종류 하나 때문에 입장을
    // 막을 이유가 없다.
    struct CurrencyInfo
    {
        uint8_t type{};
        int64_t amount{};
    };
}
