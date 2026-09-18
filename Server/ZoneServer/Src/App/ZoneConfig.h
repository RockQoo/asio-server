#pragma once

#include "App/ZoneDef.h"

#include "Server/Core/Src/Pipeline/Types.h"

struct ZoneConfig
{
    // 이 프로세스가 담당하는 존 목록 -- 리스트 길이만큼 이 프로세스가 존을 동시에 호스팅한다.
    std::vector<ZoneDef> zones;

    std::string worldHost{"127.0.0.1"};
    uint16_t worldPort{9200};

    // 소켓 I/O 전용 스레드. 게임 로직을 하지 않고 바이트만 옮긴다.
    size_t ioThreadCount{2};

    Pipeline::ELaneBackend laneBackend{Pipeline::ELaneBackend::Queue};

    // ── 레인 크기 ─────────────────────────────────────────────────────────────
    // 레인마다 크기를 정하는 기준이 다르다. 근거는 config/zone.cfg 주석.

    // LB -- owner 가 링크 세션 id 라 **존 수가 곧 상한**이다. 더 줘도 안 갈린다.
    size_t lbThreadCount{2};

    // BASIC -- owner 가 playerId 라 늘린 만큼 실제로 갈린다. 콘텐츠 처리가 전부 여기 있다.
    size_t basicThreadCount{8};

    // BROADCAST -- owner 가 zoneId 라 **존 수가 곧 상한**이다.
    size_t broadcastThreadCount{4};

    // TICK 은 설정에서 받지 않는다 -- 존 수에서 계산된다(ProcessorIds.h TickLaneCount).
    // 값을 두 군데 적으면 갈리고, 갈리면 주인과 레인의 1:1이 깨진다.

    // ── 주기 ──────────────────────────────────────────────────────────────────
    std::chrono::milliseconds tickInterval{100};

    // 팬아웃 함을 비우는 주기. 짧을수록 체감 지연이 줄고, 길수록 소켓에 쓰는 횟수가 준다.
    std::chrono::milliseconds fanoutFlushInterval{10};

    std::chrono::milliseconds statsDumpInterval{10000};
};

// 설정 파일을 읽어 ZoneConfig 하나를 만든다. 담당 존 목록은 실행 인자에서 오므로 받아서 옮긴다.
//
// **기본값은 위 구조체에만 적는다** -- 읽는 쪽이 구조체의 현재 값을 그대로
// fallback 으로 넘기므로, 기본값이 두 군데에 적혀 갈리는 일이 없다.
// 형식과 정책(없는 키/틀린 값/오타 키)은 docs/design/config-file.md 참고.
[[nodiscard]] ZoneConfig LoadConfig(const std::string& path, std::vector<ZoneDef> zones);
