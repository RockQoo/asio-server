#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Packet/PacketDispatcher.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Server/ZoneServer/Src/Game/PlayerState.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>

namespace Zone
{
    class WorldLink;
    class BroadcastDispatcher;
}

namespace Mail
{
    class MailRegistry;
}

namespace Zone
{
    // 존(zone) 하나의 권위 있는 상태. 여기 있는 모든 메서드는 그 존을 담당하는 BASIC 풀
    // 스레드에서만 실행된다고 가정한다 -- players_는 그래서
    // 락이 필요 없다. zoneId % BASIC풀크기로 sticky 라우팅되므로 같은 존은 항상 같은
    // BASIC 스레드로만 온다(ZoneWorkerManager 주석 참고).
    //
    // 주의: TICK 풀은 BASIC과 "다른" 풀이라 같은 zoneId라도 실제로는 다른 OS 스레드다.
    // 지금은 Tick()이 비어있어(placeholder) players_를 안 건드리므로 안전하지만, 나중에
    // Tick에서 실제로 players_ 같은 공유 상태를 직접 만지게 되면 그 순간부터는 BASIC과
    // 동시 접근이 가능해지므로 락(또는 Threading::Synchronized)이 필요해진다.
    //
    // 이 프로세스는 클라이언트와 직접 연결되지 않는다(GatewayServer/WorldServer 경유).
    // World와의 연결 하나(WorldLink) 위에서 clientSessionId로 구분된 여러 플레이어의 패킷을
    // 처리한다.
    class ZoneWorld
    {
    public:
        ZoneWorld(const uint32_t zoneId, const float xMin, const float xMax,
                  WorldLink& worldLink, BroadcastDispatcher& broadcastDispatcher, Mail::MailRegistry& mailRegistry);

        void OnPlayerEnter(const Network::SessionId clientSessionId, const uint32_t playerId,
                            const float x, const float y);
        void OnPlayerLeave(const Network::SessionId clientSessionId);

        // 존 생명주기 이벤트(입장/퇴장)가 아니라 "콘텐츠 패킷"은 전부 이 진입점 하나로 들어온다.
        // 1) clientSessionId로 Player를 먼저 찾고(입장 안 한 세션이면 여기서 버림),
        // 2) 그 패킷 타입으로 RegisterPacketHandlers()에 등록해둔 핸들러를 찾아 콜백한다.
        // WorldLinkHandler(LB 스레드)는 이제 어느 zone인지만 판단하면 되고, 패킷 내용이 뭔지는
        // 몰라도 된다 -- Move/Chat/MailAdd/MailDel 각각의 와이어 포맷 파싱은 전부 여기,
        // BASIC 스레드에서 일어난다.
        void HandleClientPacket(const Network::SessionId clientSessionId, const Protocol::PacketId packetId,
                                 const std::span<const byte> payload);

        // tick 훅 자리(placeholder). 실제로는 AI/물리/회복 등이 여기 들어갈 것이다.
        void Tick(const float deltaSeconds);

        [[nodiscard]] uint32_t GetZoneId() const noexcept { return zoneId_; }
        [[nodiscard]] size_t GetPlayerCount() const noexcept { return players_.size(); }

    private:
        void RegisterPacketHandlers();

        // 등록된 핸들러 각각. HandleClientPacket이 이미 Player를 찾아 넘겨주므로, 여기서는
        // "이 Player가 실제로 존재하는가"를 다시 확인할 필요가 없다.
        void HandleMove(PlayerState& player, const std::span<const byte> payload);
        void HandleChat(const PlayerState& player, const std::span<const byte> payload);
        void HandleMailAdd(const PlayerState& player, const std::span<const byte> payload);
        void HandleMailDel(const PlayerState& player, const std::span<const byte> payload);

        void SendToPlayer(const Network::SessionId clientSessionId, const Protocol::PacketId innerPacketId,
                           const std::span<const byte> payload) const;
        void BroadcastToZone(const Protocol::PacketId innerPacketId, const std::span<const byte> payload,
                             const Network::SessionId excludeClientSessionId = 0) const;
        void RequestZoneTransfer(const Network::SessionId clientSessionId, const uint32_t playerId,
                                  const float x, const float y) const;

        uint32_t zoneId_;
        float xMin_;
        float xMax_;
        WorldLink& worldLink_;
        BroadcastDispatcher& broadcastDispatcher_;
        Mail::MailRegistry& mailRegistry_;
        std::unordered_map<Network::SessionId, PlayerState> players_;

        // 패킷 타입 -> 등록된 핸들러. 콘텐츠가 늘어날수록(예: 존 이동/전투 등) 여기에
        // Register 한 줄만 추가하면 된다 -- HandleClientPacket의 분기 로직은 그대로다.
        // 컨텍스트로 PlayerState*를 쓰는 이유: 등록되는 핸들러가 전부 이 클래스의 private
        // 멤버 함수라 this로 zone 상태(worldLink_/mailRegistry_ 등)에 이미 접근 가능하고,
        // 여기엔 "이미 찾아낸 그 Player"만 넘기면 충분하기 때문이다.
        Packet::PacketDispatcher<Protocol::PacketId, PlayerState*> packetDispatcher_;
    };
}
