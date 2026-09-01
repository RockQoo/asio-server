#pragma once

#include "Core/Src/Common/BasicTypes.h"

#include <string_view>

namespace Log
{
    // Core 자신의 로그 계층 구분(Network/Packet/Thread 폴더와 대응). 게임
    // 콘텐츠(존/플레이어 등)는 전혀 모르는 순수 인프라 값들만 담는다 -- 콘텐츠에 종속적인
    // 카테고리(예: ZoneServer의 "Zone")는 Core가 아니라 그걸 쓰는 프로젝트가 스스로 자기
    // 네임스페이스에 정의해서 쓴다 (ZoneServer/Src/Log/LogCategory.h의 Zone::ELogCategory 참고).
    // Log::LogEntry<TCategory>는 이 타입을 특별 취급하지 않는다 -- 그냥 흔히 쓸 만한 기본
    // 카테고리 집합일 뿐, 어떤 scoped enum이든 ToString()만 있으면 동일하게 동작한다.
    enum class ELogCategory : uint8_t
    {
        General,
        Network,
        Packet,
        Thread,
    };

    // Log::LogEntry<TCategory>가 ADL로 찾아 호출한다 (LogCategoryType concept 참고).
    [[nodiscard]] inline std::string_view ToString(const ELogCategory category)
    {
        switch (category)
        {
        case ELogCategory::General: return "General";
        case ELogCategory::Network: return "Network";
        case ELogCategory::Packet:  return "Packet";
        case ELogCategory::Thread:  return "Thread";
        }
        return "Unknown";
    }
}

// 어디서든 `Log::` 없이 `ELogCategory::Packet`처럼 바로 쓰기 위한 전역 노출. Core 프로젝트
// 내부에서만 include되므로(pch.h 경유), ZoneServer가 자기 것(Zone::ELogCategory)을 똑같은
// 이름으로 전역에 끌어와도 서로 다른 프로젝트(PCH)라 충돌하지 않는다.
using Log::ELogCategory;
