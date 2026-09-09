#pragma once

#include "Shared/Core/Src/Common/BasicTypes.h"

#include <cstring>
#include <optional>
#include <span>

namespace World
{
    // I/O 스레드에서 페이로드 앞부분만 훔쳐봐 "이 메시지의 주인이 누구인가"를 뽑는 헬퍼.
    //
    // **왜 I/O 스레드에서 이걸 해야 하는가**: 메시지를 큐 그룹에 넣는 순간 ownerId가 정해져야
    // 스레드가 결정된다(ProcessorGroup 주석 참고). 즉 "일단 아무 스레드에나 넣고 거기서 파싱한
    // 뒤 주인을 알아내는" 것은 성립하지 않는다 -- 그러면 그 스레드가 이미 남의 주인 데이터를
    // 만지고 있는 셈이다. 그래서 라우팅에 필요한 최소한(정수 하나)만 여기서 읽고, 실제 와이어
    // 포맷 해석은 배정된 스레드에서 한다.
    //
    // 고정폭 정수 하나를 memcpy로 꺼내는 게 전부라 I/O 스레드에 부담이 없다.
    template <typename T>
    [[nodiscard]] std::optional<uint64_t> PeekOwnerId(const std::span<const byte> payload, const size_t offset = 0)
    {
        if (payload.size() < offset + sizeof(T))
        {
            return std::nullopt;
        }

        T value{};
        std::memcpy(&value, payload.data() + offset, sizeof(T));
        return static_cast<uint64_t>(value);
    }
}
