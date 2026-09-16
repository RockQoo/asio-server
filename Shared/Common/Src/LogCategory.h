#pragma once

#include <cstdint>
#include <string_view>

namespace Common
{
    // 서버와 도구가 함께 쓰는 로그 카테고리.
    //
    // **같은 태그는 모든 로그 파일에서 같은 뜻이어야 한다.** 요청 하나를 RUID 로 쫓을 때
    // world_server.log 와 zone_server_1_2.log 를 나란히 읽기 때문이다. 예전에는 서버마다
    // 자기 enum 을 가져서 World 의 [Zone] 은 "존 링크", Zone 의 [Zone] 은 "존 로직"이었다.
    //
    // 카테고리는 **어느 노드/서브시스템 이야기인가**다. 값을 지워도 재사용하지 않는다.
    //
    // Core 는 자기 것(Log::ELogCategory -- General/Network/Packet/Thread)을 따로 갖는다.
    // Core 가 Gateway/Zone/Db 를 알면 "Core 에 콘텐츠가 종속되면 안 된다"가 깨진다.
    enum class ELogCategory : uint8_t
    {
        General,
        Client,   // 게임 클라이언트 연결/릴레이
        Gateway,  // Gateway 링크
        World,    // World 링크
        Zone,     // 존 -- 링크든 로직이든 그 존에 관한 것
        Db,       // DB 레인(UnitOfWork 반영, 로그인 적재)
        Tool,     // 운영툴(GmTool) 링크/명령
        Session,  // 부하 도구의 시뮬레이션 세션
    };

    // Log::Entry<TCategory> 가 ADL 로 찾아 호출한다 (LogCategoryType concept 참고).
    [[nodiscard]] inline std::string_view ToString(const ELogCategory category)
    {
        switch (category)
        {
        case ELogCategory::General: return "General";
        case ELogCategory::Client:  return "Client";
        case ELogCategory::Gateway: return "Gateway";
        case ELogCategory::World:   return "World";
        case ELogCategory::Zone:    return "Zone";
        case ELogCategory::Db:      return "Db";
        case ELogCategory::Tool:    return "Tool";
        case ELogCategory::Session: return "Session";
        }
        return "Unknown";
    }
}

// 어디서든 `Common::` 없이 `ELogCategory::Zone` 처럼 바로 쓰기 위한 전역 노출.
// Core 는 자기 pch 에서 Log::ELogCategory 를 노출하므로 한 TU 에 하나씩만 보인다.
using Common::ELogCategory;
