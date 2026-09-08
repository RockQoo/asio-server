#pragma once

#include <cstdint>

namespace Zone
{
    // 이 존 서버 프로세스가 호스팅하는 존 하나의 정의(config로 조절).
    // 여러 개를 나열하면 한 프로세스가 그만큼의 존을 동시에 담당한다 --
    // ZoneServerConfig::zones 참고.
    //
    // 담당 구간은 x/y 양쪽을 갖는 사각형이다. y를 나중에 붙인 이유가 있어서 적어둔다: 처음에는
    // 존이 x축 한 줄로만 늘어서서 y 검사가 필요 없었는데, 존을 격자로 배치하려면(1 2 / 3 4)
    // y 경계도 존마다 달라야 한다. 필드를 나열하는 대신 이 구조체를 통째로 넘기는 이유는,
    // 앞으로 배치를 CSV에서 읽게 되면 여기에 필드가 더 붙기 때문이다(그때 생성자 시그니처를
    // 다시 고치지 않아도 된다).
    struct ZoneDef
    {
        uint32_t zoneId;
        float xMin;
        float xMax;
        float yMin;
        float yMax;

        [[nodiscard]] constexpr bool Contains(const float x, const float y) const noexcept
        {
            return x >= xMin && x < xMax && y >= yMin && y < yMax;
        }
    };
}
