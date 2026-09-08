#pragma once

#include "Shared/Core/Src/Common/Types.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

namespace Network
{
    class Session;
}

namespace World
{
    struct ZoneLinkInfo
    {
        std::shared_ptr<Network::Session> zoneSession;
        float xMin{};
        float xMax{};
        float yMin{};
        float yMax{};

        [[nodiscard]] constexpr bool Contains(const float x, const float y) const noexcept
        {
            return x >= xMin && x < xMax && y >= yMin && y < yMax;
        }
    };

    // 접속해 있는 Zone 서버 프로세스들의 "zoneId -> 연결/담당 사각형" 테이블. ClientRegistry와
    // 마찬가지로 WorldWorker 스레드 하나만 건드리므로 락이 필요 없다. FindZoneContaining이
    // 존 핸드오프 시 "이 좌표는 어느 존 담당인가"를 결정하는 유일한 지점이다(존 몇 개짜리
    // 테이블이라 선형 탐색으로 충분 -- 존이 많아지면 격자 인덱스가 필요해진다).
    //
    // World는 존 배치 규칙을 모른다. 각 존이 등록할 때 알려준 사각형만 보고 판단하므로,
    // 배치가 격자에서 CSV 기반 불규칙 배치로 바뀌어도 이 클래스는 그대로다.
    class ZoneLinkRegistry
    {
    public:
        void Add(const uint32_t zoneId, const std::shared_ptr<Network::Session>& zoneSession,
                 const float xMin, const float xMax, const float yMin, const float yMax);
        void Remove(const uint32_t zoneId);

        // 연결 하나가 여러 zoneId를 등록했을 수 있으므로(한 Zone 서버 프로세스가 존 여러 개를
        // 호스팅), 그 연결이 끊기면 이 세션이 걸려 있는 zoneId 항목을 전부 지운다.
        void RemoveBySession(const Network::SessionId sessionId);

        [[nodiscard]] std::optional<ZoneLinkInfo> Find(const uint32_t zoneId) const;
        [[nodiscard]] std::optional<uint32_t> FindZoneContaining(const float x, const float y) const;

        // 신규 접속을 어디에 넣을지 정할 때 쓴다. zoneId가 가장 작은 존을 "첫 존"으로 보고 그
        // 중앙 좌표를 함께 준다 -- 스폰 좌표를 상수로 박아두면 배치가 바뀔 때(존 격자화처럼)
        // 그 좌표가 어느 존에도 속하지 않게 되어 아무도 입장하지 못한다. 실제로 그렇게 깨졌다.
        struct EntryPoint
        {
            uint32_t zoneId{};
            float x{};
            float y{};
        };
        [[nodiscard]] std::optional<EntryPoint> FindEntryPoint() const;

    private:
        std::unordered_map<uint32_t, ZoneLinkInfo> zones_;
    };
}
