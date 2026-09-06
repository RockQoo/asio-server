#pragma once

#include "Shared/Core/Src/Common/BasicTypes.h"

#include <string_view>

namespace Zone
{
    // ZoneServer(콘텐츠)만의 로그 카테고리. Core::Log는 게임 콘텐츠를 몰라야 하므로(Shared/Core/Src/Log/
    // LogCategory.h의 Log::ELogCategory에는 Network/Packet/Thread 같은 순수 인프라 값만 있고
    // Zone 같은 콘텐츠 값이 없다), 존/플레이어 개념이 들어가는 카테고리는 여기 ZoneServer
    // 쪽에 따로 둔다. Log::LogEntry<TCategory>는 어떤 scoped enum이든 ToString()만 있으면
    // 그대로 받아주므로, Core를 전혀 건드리지 않고 이렇게 확장할 수 있다.
    enum class ELogCategory : uint8_t
    {
        General,
        Zone,
    };

    // Log::LogEntry<TCategory>가 ADL로 찾아 호출한다 (Log::LogCategoryType concept 참고).
    [[nodiscard]] inline std::string_view ToString(const ELogCategory category)
    {
        switch (category)
        {
        case ELogCategory::General: return "General";
        case ELogCategory::Zone:    return "Zone";
        }
        return "Unknown";
    }
}

// 어디서든 `Zone::` 없이 `ELogCategory::Zone`처럼 바로 쓰기 위한 전역 노출. ZoneServer/TestClient
// 프로젝트에서만 include되므로, Core가 자기 것(Log::ELogCategory)을 같은 이름으로 전역에
// 끌어와도(Shared/Core/Src/Log/LogCategory.h) 서로 다른 프로젝트(PCH)라 충돌하지 않는다.
using Zone::ELogCategory;
