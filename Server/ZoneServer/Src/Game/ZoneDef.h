#pragma once

#include <cstdint>

namespace Zone
{
    // 이 존 서버 프로세스가 호스팅하는 존 하나의 정의(config로 조절).
    // 여러 개를 나열하면 한 프로세스가 그만큼의 존을 동시에 담당한다 --
    // ZoneServerConfig::zones 참고.
    struct ZoneDef
    {
        uint32_t zoneId;
        float xMin;
        float xMax;
    };
}
