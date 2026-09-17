#include "pch.h"
#include "Shared/Core/Src/Packet/Dispatcher.h"

namespace Packet
{
    // **이 한 줄 때문에 .cpp 가 있다.** Dispatcher.h 는 소비자 프로젝트 안에서 컴파일되는데,
    // 거기서는 전역 `ELogCategory` 가 `Common::ELogCategory` 다. Core 것을 쓰려고
    // `Shared/Core/Src/Log/LogCategory.h` 를 헤더에서 include 하면 전역 using 이 둘이 되어
    // 소비자의 기존 `ELogCategory::Zone` 들이 전부 모호해진다.
    //
    // 그래서 카테고리를 아는 코드는 Core 안(여기)에만 둔다.
    void LogParseFailure(const uint16_t packetId, const size_t payloadBytes)
    {
        LOG.Warning(ELogCategory::Packet, "페이로드 형식이 맞지 않아 버린다")
            .KV("PacketId", packetId)
            .KV("PayloadBytes", payloadBytes);
    }
}
