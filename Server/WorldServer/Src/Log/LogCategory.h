#pragma once

#include "Shared/Core/Src/Common/BasicTypes.h"

#include <string_view>

namespace World
{
    // WorldServer(콘텐츠)만의 로그 카테고리. Core::Log는 게임 콘텐츠를 몰라야 하므로 여기
    // 따로 둔다(Server/ZoneServer/Src/Log/LogCategory.h와 같은 이유, CLAUDE.md 참고).
    enum class ELogCategory : uint8_t
    {
        General,
        Gateway,  // Gateway <-> World 연결/릴레이 관련
        Zone,     // Zone <-> World 연결/라우팅/핸드오프 관련
        Db,       // DB 워커 풀(UnitOfWork 태스크 처리) 관련
        Tool,     // 운영툴(GmTool) <-> World 연결/공지/우편/쿠폰 관련
    };

    [[nodiscard]] inline std::string_view ToString(const ELogCategory category)
    {
        switch (category)
        {
        case ELogCategory::General: return "General";
        case ELogCategory::Gateway: return "Gateway";
        case ELogCategory::Zone:    return "Zone";
        case ELogCategory::Db:      return "Db";
        case ELogCategory::Tool:    return "Tool";
        }
        return "Unknown";
    }
}

// 어디서든 `World::` 없이 `ELogCategory::Zone`처럼 바로 쓰기 위한 전역 노출. WorldServer
// 프로젝트에서만 include되므로 다른 프로젝트의 동명 enum과 충돌하지 않는다.
using World::ELogCategory;
