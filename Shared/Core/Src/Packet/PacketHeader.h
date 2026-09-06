#pragma once

#include <cstddef>
#include <cstdint>

namespace Packet
{
    // 모든 패킷 앞에 붙는 고정 크기 헤더: [bodySize:2바이트][id:2바이트][본문...]
    // 캡슐화된 클래스가 아니라 순수 데이터(POD) 구조체라서 멤버 변수 네이밍 컨벤션(trailing
    // underscore)을 적용하지 않고 일반 필드 이름을 그대로 사용한다.
#pragma pack(push, 1)
    struct PacketHeader
    {
        uint16_t bodySize{};  // 헤더를 제외한 본문(payload) 크기(바이트)
        uint16_t id{};        // 애플리케이션이 정의하는 패킷 타입 id

        [[nodiscard]] static constexpr size_t HeaderSize() noexcept
        {
            return sizeof(uint16_t) * 2;
        }

        // 본문 크기의 상한값. 조작되었거나 잘못된 길이 값으로부터 PacketBuffer를 보호한다.
        [[nodiscard]] static constexpr uint16_t MaxBodySize() noexcept
        {
            return 8192;
        }
    };
#pragma pack(pop)

    static_assert(sizeof(PacketHeader) == 4, "PacketHeader는 4바이트로 빈틈없이 패킹되어야 한다");
}
