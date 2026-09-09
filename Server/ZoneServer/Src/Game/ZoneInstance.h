#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Server/ZoneServer/Src/Game/Player.h"
#include "Server/ZoneServer/Src/Game/ZoneDef.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Zone
{
    class WorldLink;
}

namespace Zone
{
    // 존 하나의 **공간** 상태. 여기 있는 메서드는 그 존을 담당하는 존 레인 스레드
    // (owner = zoneId)에서만 실행된다고 가정한다 -- 그래서 members_에 락이 없다.
    //
    // **예전 구조와 무엇이 달라졌는가**: 원래 이 클래스가 우편/재화까지 다 처리했고, 그래서
    // 존 하나의 모든 콘텐츠가 스레드 하나로 직렬화됐다(1만 세션 부하 테스트가 무너진 원인).
    // 지금은 주인이 다른 두 종류를 갈라 놓았다:
    //
    //   여기(존 레인, owner = zoneId)          PlayerProcessor(플레이어 레인, owner = sessionId)
    //     로스터 members_                        우편 · 재화 · UnitOfWork
    //     틱마다 위치 적분 · 경계 판정            이동 패킷 검증 + 즉시 브로드캐스트
    //
    // **이동은 이 레인이 틱에서만 만진다.** 이동 패킷 자체는 플레이어 레인이 받아 검증하고
    // MoveModel에 요청만 기록하므로, 패킷이 폭주해도 이 레인의 틱 주기가 흔들리지 않는다.
    class ZoneInstance
    {
    public:
        ZoneInstance(const ZoneDef& def, WorldLink& worldLink);

        // --- 존 레인에서만 호출 ---
        void OnPlayerEnter(const std::shared_ptr<Player>& player);
        void OnPlayerLeave(const Network::SessionId clientSessionId);
        void Tick(const float deltaSeconds);

        // --- 어느 레인에서나 호출 가능 ---
        //
        // 브로드캐스트 대상 목록의 스냅샷. 플레이어 레인이 이동/채팅을 **즉시** 뿌려야 하는데
        // (틱을 기다리면 체감 지연이 그만큼 늘어난다) 로스터는 존 레인 소유라 직접 순회할 수
        // 없다. 그래서 로스터가 바뀔 때(입장/퇴장)마다 불변 벡터를 새로 만들어 통째로
        // 갈아끼우고, 읽는 쪽은 그 시점 스냅샷을 shared_ptr로 집어간다.
        //
        // 위치가 바뀔 때는 발행하지 않는다 -- 대상 목록은 "누가 이 존에 있는가"만 바뀌면 되고,
        // 그건 입장/퇴장뿐이기 때문이다(AOI를 넣으면 시야가 바뀔 때도 발행하게 된다).
        // 읽는 쪽이 보는 목록은 최대 "직전 입퇴장 시점"만큼 낡을 수 있는데, 브로드캐스트
        // 대상으로는 허용 가능한 오차다(막 나간 사람에게 한 장 더 가거나, 막 들어온 사람이
        // 한 장 놓치는 정도).
        [[nodiscard]] std::shared_ptr<const std::vector<Network::SessionId>> BroadcastTargets() const
        {
            return broadcastTargets_.load(std::memory_order_acquire);
        }

        [[nodiscard]] uint32_t GetZoneId() const noexcept { return def_.zoneId; }
        [[nodiscard]] const ZoneDef& Def() const noexcept { return def_; }
        [[nodiscard]] size_t GetPlayerCount() const noexcept { return members_.size(); }

    private:
        // members_가 바뀐 직후에 부른다(존 레인).
        void PublishBroadcastTargets();

        void SendEnterZoneNotify(const Network::SessionId clientSessionId, const uint32_t playerId) const;
        void RequestZoneTransfer(const Network::SessionId clientSessionId, const uint32_t playerId,
                                  const float x, const float y) const;

        // 담당 구간을 필드로 흩지 않고 정의 그대로 들고 있는다 -- 경계 검사(ZoneDef::Contains)를
        // 한 곳에만 두면 x/y 중 한쪽만 빠뜨리는 실수가 안 생긴다.
        ZoneDef def_;
        WorldLink& worldLink_;

        // 존 레인 전용이라 락이 없다.
        std::unordered_map<Network::SessionId, std::shared_ptr<Player>> members_;

        std::atomic<std::shared_ptr<const std::vector<Network::SessionId>>> broadcastTargets_;
    };
}
