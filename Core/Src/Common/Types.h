#pragma once

#include "Core/Src/Common/BasicTypes.h"

namespace Network
{
    // TCP 연결(세션)을 수락할 때마다 하나씩 증가하며 부여되는 고유 식별자
    using SessionId = uint64_t;
}
