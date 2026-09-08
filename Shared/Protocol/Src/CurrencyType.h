#pragma once

#include <cstdint>

namespace Protocol
{
    // 재화 종류. Zone이 판정하고 World가 DB에 반영하고 클라이언트가 화면에 표시하는, 세 쪽이
    // 공유하는 계약이라 PacketId/ErrorCode와 같은 이유로 여기 있다.
    // 0은 "종류 없음/미지정" 예약값이다 -- 기본값으로 초기화된 값이 실수로 골드를 가리키는
    // 것보다, 알 수 없는 종류로 즉시 거부되는 게 낫다(zoneId를 1부터 세는 것과 같은 판단).
    enum class ECurrencyType : uint8_t
    {
        None = 0,
        Gold = 1,
    };
}

// PacketId/ErrorCode와 같은 이유로 `Protocol::` 없이 바로 쓰기 위한 전역 노출.
using Protocol::ECurrencyType;
