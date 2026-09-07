#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Network/IPacketHandler.h"
#include "Shared/Core/Src/Thread/AffinityWorkerPool.h"
#include "Server/WorldServer/Src/Packet/RelayEnvelope.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Server/ZoneServer/Src/Game/ZoneDef.h"
#include "Server/ZoneServer/Src/Worker/TaskWorker.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace Zone
{
    class ZoneWorkerManager;
    class WorldLink;

    // World와의 연결(Network::Connector로 생성) 하나의 IPacketHandler. "recv 처리 전용" LB
    // 풀을 안에 둔다: NETWORK 스레드(Session의 strand, OnPacket이 실행되는 곳)는 바이트만
    // 복사해서 LB 풀에 넘기고, 패킷 id 파싱과 1차 분기(EnterZoneRequest/
    // LeaveZoneNotify/ForwardToZone 구분)는 LB 스레드에서 일어난다. ForwardToZone 안의
    // innerPacketId는 Echo만 예외적으로 여기서 곧바로 되돌려 보내고(공유 게임 상태가 필요
    // 없어 BASIC까지 갈 이유가 없다), 그 외에는 어떤 패킷인지 들여다보지 않고 그대로
    // ZoneInstance::HandleClientPacket으로 넘긴다 -- "패킷 내용이 뭔지"는 BASIC 스레드(ZoneInstance)
    // 만 알면 된다. 이 프로세스가 여러 존을 호스팅할 수 있으므로, LB 스레드가 clientSessionId로
    // "이 프로세스 안에서 지금 어느 zoneId에 있는지"를 찾아(clientLocalZone_) 그 zoneId로
    // BASIC 풀에 라우팅한다.
    class WorldLinkHandler final : public Network::IPacketHandler
    {
    public:
        WorldLinkHandler(ZoneWorkerManager& zoneWorkers, WorldLink& worldLink, std::vector<ZoneDef> zoneDefs,
                         const size_t lbThreadCount);

        void Start();
        void Stop();

        void OnSessionOpened(const std::shared_ptr<Network::Session>& session) override;
        void OnPacket(const std::shared_ptr<Network::Session>& session,
                      const Packet::PacketHeader& header,
                      const std::span<const byte> payload) override;
        void OnClosed(const std::shared_ptr<Network::Session>& session, const std::error_code& reason) override;

    private:
        void DecodeAndDispatch(const uint16_t packetId, const std::span<const byte> payload);

        void HandleEnterZoneRequest(const std::span<const byte> payload);
        void HandleLeaveZoneNotify(const std::span<const byte> payload);
        void HandleForwardToZone(const std::span<const byte> payload);

        [[nodiscard]] std::optional<uint32_t> FindLocalZone(const Network::SessionId clientSessionId) const;
        void SetLocalZone(const Network::SessionId clientSessionId, const uint32_t zoneId);
        void RemoveLocalZone(const Network::SessionId clientSessionId);

        ZoneWorkerManager& zoneWorkers_;
        WorldLink& worldLink_;
        std::vector<ZoneDef> zoneDefs_;

        // recv 처리(패킷 파싱/1차 분기) 전용 LB 풀.
        Thread::AffinityWorkerPool<TaskWorker> lbPool_;
        std::atomic<size_t> lbRoundRobin_{0};

        // clientSessionId -> 이 프로세스 안에서 그 플레이어가 지금 있는 zoneId. LB 풀이
        // 여러 스레드로 구성되므로(서로 다른 클라이언트의 이벤트가 서로 다른 LB 스레드에서
        // 동시에 이 맵을 건드릴 수 있다) shared_mutex로 보호한다 -- World와의 연결이 하나뿐이던
        // 이전 버전(LB 풀 없이 단일 strand)과 달리 이제는 진짜 락이 필요하다.
        mutable std::shared_mutex clientLocalZoneMutex_;
        std::unordered_map<Network::SessionId, uint32_t> clientLocalZone_;
    };
}
