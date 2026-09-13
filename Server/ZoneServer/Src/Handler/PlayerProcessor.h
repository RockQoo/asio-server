#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Packet/Dispatcher.h"
#include "Game/Player.h"
#include "Game/PlayerRegistry.h"
#include "Shared/Protocol/Src/PacketId.h"

#include <cstdint>
#include <memory>
#include <span>

namespace Zone
{
    class WorldLink;
    class WorkerManager;
    class BroadcastDispatcher;
}

namespace Mail
{
    class Registry;
}

namespace Zone
{
    // 핸들러 하나가 처리하는 동안 필요한 것들. **zoneId를 멤버로 들고 있으면 안 된다** --
    // 이 처리기 객체 하나를 플레이어 레인의 모든 스레드가 공유하므로, 멤버에 쓰는 순간
    // 서로 다른 플레이어를 처리하던 스레드들이 같은 변수를 덮어쓴다. 그래서 호출마다
    // 스택으로 들고 다닌다.
    struct PlayerContext
    {
        Player& player;
        uint32_t zoneId;
    };

    // **플레이어 레인(owner = clientSessionId)**의 콘텐츠 처리기 -- 우편·재화·UnitOfWork와
    // 이동 패킷의 검증까지 "그 사람만의 것"을 담당한다. 존이 아니라 플레이어가 주인이므로
    // **같은 존의 서로 다른 플레이어가 서로 다른 스레드에서 동시에 처리된다.**
    //
    // 이동은 네 단계로 나뉘고, 요점은 **이동 패킷이 존 레인을 건드리지 않는다**는 것이다:
    //   ① 여기서 파싱·검증  ② MoveModel에 요청만 기록
    //   ③ 브로드캐스트는 즉시(틱을 기다리면 체감 지연이 커진다)
    //   ④ 적분·경계 판정은 존 레인이 다음 틱에 (Instance::Tick)
    //
    // 흐름 전체: docs/sequences/zone-handoff.html
    class PlayerProcessor
    {
    public:
        PlayerProcessor(PlayerRegistry& playerRegistry, WorkerManager& zoneWorkers,
                        BroadcastDispatcher& broadcastDispatcher, WorldLink& worldLink,
                        Mail::Registry& mailRegistry);

        // 아래 셋은 전부 그 clientSessionId를 담당하는 플레이어 레인 스레드에서 호출된다.
        void OnPlayerEnter(const Network::SessionId clientSessionId, const uint32_t playerId,
                           const uint32_t zoneId, const float x, const float y);
        void OnPlayerLeave(const Network::SessionId clientSessionId);

        // 콘텐츠 패킷 진입점. 어느 존인지는 Player가 들고 있으므로 LB가 알려줄 필요가 없다.
        void HandleClientPacket(const Network::SessionId clientSessionId,
                                const PacketId packetId, const std::span<const byte> payload);

    private:
        void RegisterPacketHandlers();

        // 등록된 핸들러들. HandleClientPacket이 이미 Player를 찾아 넘겨주므로 여기서 다시
        // "이 사람이 존재하는가"를 확인할 필요가 없다.
        void HandleMove(const PlayerContext& context, const std::span<const byte> payload);
        void HandleChat(const PlayerContext& context, const std::span<const byte> payload);
        void HandleMailAdd(const PlayerContext& context, const std::span<const byte> payload);
        void HandleMailDel(const PlayerContext& context, const std::span<const byte> payload);

        // 우편 지급 + 골드 차감을 한 트랜잭션으로 처리한다 -- 모델 두 개에 걸친 변경이라
        // 뒤(골드)에서 실패하면 앞(우편)이 역순으로 되돌아가는 걸 실제로 밟는 경로다.
        void HandleMailBuy(const PlayerContext& context, const std::span<const byte> payload);

        void SendToPlayer(const Network::SessionId clientSessionId, const PacketId innerPacketId,
                          const std::span<const byte> payload) const;

        // 존 레인이 발행해둔 대상 스냅샷을 읽어 BROADCAST 그룹으로 넘긴다.
        // 로스터를 직접 순회하지 않으므로 존 레인과 겹치지 않는다.
        void BroadcastToZone(const uint32_t zoneId, const PacketId innerPacketId,
                             const std::span<const byte> payload,
                             const Network::SessionId excludeClientSessionId = 0) const;

        PlayerRegistry& playerRegistry_;
        WorkerManager& zoneWorkers_;
        BroadcastDispatcher& broadcastDispatcher_;
        WorldLink& worldLink_;
        Mail::Registry& mailRegistry_;

        // 패킷 타입 -> 핸들러. **존마다도 플레이어마다도 아니고 이 처리기에 하나만** 둔다 --
        // 개체마다 테이블을 갖는 구조는 등록 내용이 개체별로 다를 때만 의미가 있고, 그렇지
        // 않으면 접속 수만큼 테이블이 통째로 복제된다(항목 하나가 수십 바이트라도 만 단위
        // 동접이면 무시할 수 없는 양이 된다).
        // 등록은 생성자에서 끝나고 이후로는 읽기 전용이라, 여러 스레드가 동시에 Dispatch해도
        // 안전하다.
        Packet::Dispatcher<PacketId, PlayerContext> packetDispatcher_;
    };
}
