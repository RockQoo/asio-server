#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Combat/Service.h"
#include "Game/Player.h"
#include "Game/Def.h"

namespace Zone
{
    class WorldLink;
    class BroadcastDispatcher;
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
    //
    // **전투가 이 클래스에 얹힌 이유**: 전투는 틱(리젠·리스폰·AI)과 패킷(C2ZAttack)이 같은
    // 상태를 만지는 첫 콘텐츠다. 둘 다 이 레인으로 몰면 락이 한 개도 필요 없다 --
    // 그래서 전투 패킷만 플레이어 레인을 거쳐 여기로 한 번 더 넘어온다
    // (PlayerProcessor::HandleAttack -> WorkerManager::PostToZone).
    // 비교표와 갈아탈 지점: docs/design/combat-lane.md
    class Instance final : private Combat::Service::ISender
    {
    public:
        Instance(const Def& def, WorldLink& worldLink, BroadcastDispatcher& broadcastDispatcher);

        // --- 존 레인에서만 호출 ---
        void OnPlayerEnter(const std::shared_ptr<Player>& player);
        void OnPlayerLeave(const Network::SessionId clientSessionId);
        void Tick(const float deltaSeconds);

        // 전투 요청. 플레이어 레인이 파싱만 하고 넘겨준다 -- 때리는 대상이 남이라 주인을
        // 세션으로 둘 수 없기 때문이다.
        void HandleAttack(const Network::SessionId clientSessionId, const AttackPacket& packet);

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

        [[nodiscard]] Protocol::ZoneId GetZoneId() const noexcept { return def_.zoneId; }
        [[nodiscard]] const Def& GetDef() const noexcept { return def_; }
        [[nodiscard]] size_t GetPlayerCount() const noexcept { return members_.size(); }

    private:
        // --- Combat::Service::ISender ---
        //
        // 전투 코드가 World 링크도 브로드캐스트 레인도 모르게 하는 통로다(Service.h 주석).
        // 여기서는 **로스터를 직접 순회한다** -- 이 레인이 members_의 주인이라 스냅샷을 거칠
        // 이유가 없고, 방금 죽거나 들어온 사람이 즉시 반영된다.
        void SendToClient(const Network::SessionId clientSessionId, const PacketId innerPacketId,
                          const std::span<const byte> payload) override;
        void BroadcastToZone(const PacketId innerPacketId, const std::span<const byte> payload,
                             const Network::SessionId excludeClientSessionId) override;

        // members_가 바뀐 직후에 부른다(존 레인).
        void PublishBroadcastTargets();

        void SendEnterZoneNotify(const Network::SessionId clientSessionId, const Protocol::PlayerId playerId) const;
        void RequestZoneTransfer(const Network::SessionId clientSessionId, const Protocol::PlayerId playerId,
                                  const float x, const float y) const;

        // 담당 구간을 필드로 흩지 않고 정의 그대로 들고 있는다 -- 경계 검사(Def::Contains)를
        // 한 곳에만 두면 x/y 중 한쪽만 빠뜨리는 실수가 안 생긴다.
        Def def_;
        WorldLink& worldLink_;
        BroadcastDispatcher& broadcastDispatcher_;

        // 존 레인 전용이라 락이 없다.
        std::unordered_map<Network::SessionId, std::shared_ptr<Player>> members_;

        // 유닛/HP/공격. **members_와 같은 레인이라 락이 없다**(Combat/Unit.h 주석).
        // 선언 순서상 위의 것들보다 뒤에 와야 한다 -- 생성자에서 *this를 넘기므로.
        Combat::Service combat_;

        std::atomic<std::shared_ptr<const std::vector<Network::SessionId>>> broadcastTargets_;
    };
}
