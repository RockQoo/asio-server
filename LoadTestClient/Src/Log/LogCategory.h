#pragma once

#include "Core/Src/Common/BasicTypes.h"

#include <string_view>

namespace Load
{
    // LoadTestClient(부하 테스트 도구)만의 로그 카테고리 -- 다른 프로젝트와 동일한 이유로
    // Core::Log와 분리해 여기 따로 둔다.
    enum class ELogCategory : uint8_t
    {
        General,
        Session,  // 시뮬레이션 클라이언트 세션 1개(연결/사이클 진행)
    };

    [[nodiscard]] inline std::string_view ToString(const ELogCategory category)
    {
        switch (category)
        {
        case ELogCategory::General: return "General";
        case ELogCategory::Session: return "Session";
        }
        return "Unknown";
    }
}

using Load::ELogCategory;
