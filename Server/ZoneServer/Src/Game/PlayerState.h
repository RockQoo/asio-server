#pragma once

#include "Shared/Core/Src/Common/Types.h"

#include <cstdint>

namespace Zone
{
    // 권위 있는(authoritative) 플레이어별 상태. 그 플레이어의 존을 담당하는 TaskWorker
    // 스레드에서만 건드려야 하며, I/O 스레드에서 직접 접근해서는 안 된다.
    struct PlayerState
    {
        Network::SessionId sessionId{};
        uint32_t playerId{};
        float x{};
        float y{};
    };
}
