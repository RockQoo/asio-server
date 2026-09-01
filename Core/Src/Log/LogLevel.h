#pragma once

#include "Core/Src/Common/BasicTypes.h"

namespace Log
{
    // 원소가 적은 일반 enum은 uint8_t를 기본 underlying type으로 쓴다.
    enum class ELogLevel : uint8_t
    {
        Debug,
        Info,
        Warning,
        Error,
    };
}
