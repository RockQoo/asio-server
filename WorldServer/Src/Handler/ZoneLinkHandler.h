#pragma once

#include "Core/Src/Network/IPacketHandler.h"
#include "Core/Src/Packet/PacketDispatcher.h"
#include "Core/Src/Thread/AffinityWorkerPool.h"
#include "WorldServer/Src/Db/DbWorker.h"
#include "WorldServer/Src/Packet/ZoneLinkPacketId.h"

namespace World
{
    class ClientRegistry;
    class ZoneLinkRegistry;
    class WorldWorker;

    // Zone <-> World 연결의 IPacketHandler. Zone 서버 프로세스가 여러 개(존마다 하나) 연결해
    // 오며, 각 연결이 자기 zoneId/담당 구간을 ZoneRegister로 알려온다. 클라이언트로 나가는
    // 응답(ForwardToWorld)은 Gateway 세션으로 릴레이하고, 존 경계 핸드오프(ZoneTransferRequest)는
    // 대상 존을 찾아 라우팅 테이블만 바꾼다. OnPacket은 이 연결의 I/O 스레드에서 바이트만
    // 복사해 WorldWorker로 넘기고, ClientRegistry/ZoneLinkRegistry를 실제로 만지는 Handle*
    // 메서드들은 그 스레드에서 실행된다. UnitOfWork 태스크(UnitOfWorkStream)만은 DB 워커 풀에
    // owner-hash로 별도 위임한다(라우팅 상태와 무관한 별개 관심사라 WorldWorker를 거치지 않음).
    class ZoneLinkHandler final : public Network::IPacketHandler
    {
    public:
        ZoneLinkHandler(ClientRegistry& clientRegistry, ZoneLinkRegistry& zoneLinkRegistry,
                         Thread::AffinityWorkerPool<Db::DbWorker>& dbWorkers, WorldWorker& worldWorker);

        void OnSessionOpened(const std::shared_ptr<Network::Session>& session) override;
        void OnPacket(const std::shared_ptr<Network::Session>& session,
                      const Packet::PacketHeader& header,
                      const std::span<const byte> payload) override;
        void OnClosed(const std::shared_ptr<Network::Session>& session, const std::error_code& reason) override;

    private:
        void RegisterHandlers();

        void HandleZoneRegister(const std::shared_ptr<Network::Session>& zoneSession, const std::span<const byte> payload);
        void HandleForwardToWorld(const std::shared_ptr<Network::Session>& zoneSession, const std::span<const byte> payload);
        void HandleZoneTransferRequest(const std::shared_ptr<Network::Session>& zoneSession, const std::span<const byte> payload);
        void HandleUnitOfWorkStream(const std::shared_ptr<Network::Session>& zoneSession, const std::span<const byte> payload);

        ClientRegistry& clientRegistry_;
        ZoneLinkRegistry& zoneLinkRegistry_;
        Thread::AffinityWorkerPool<Db::DbWorker>& dbWorkers_;
        WorldWorker& worldWorker_;
        Packet::PacketDispatcher<ZoneLinkPacketId, std::shared_ptr<Network::Session>> dispatcher_;
    };
}
