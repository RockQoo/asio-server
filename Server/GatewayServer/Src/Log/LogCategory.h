#pragma once

#include "Shared/Core/Src/Common/BasicTypes.h"

#include <string_view>

namespace Gateway
{
    // GatewayServer(콘텐츠)만의 로그 카테고리 -- Core::Log는 게임 콘텐츠를 몰라야 하므로 여기
    // 따로 둔다(ZoneServer/WorldServer의 Log/LogCategory.h와 같은 이유).
    enum class ELogCategory : uint8_t
    {
        General,
        Client,  // 게임 클라이언트 accept/릴레이 관련
        World,   // World로 나가는 연결 관련
    };

    [[nodiscard]] inline std::string_view ToString(const ELogCategory category)
    {
        switch (category)
        {
        case ELogCategory::General: return "General";
        case ELogCategory::Client:  return "Client";
        case ELogCategory::World:   return "World";
        }
        return "Unknown";
    }
}

using Gateway::ELogCategory;
