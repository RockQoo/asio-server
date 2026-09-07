#include "Server/WorldServer/Src/pch.h"
#include "Server/WorldServer/Src/Handler/ZoneLinkHandler.h"
#include "Server/WorldServer/Src/World/ClientRegistry.h"
#include "Server/WorldServer/Src/World/ZoneLinkRegistry.h"
#include "Server/WorldServer/Src/Worker/WorldWorker.h"
#include "Server/WorldServer/Src/Packet/RelayEnvelope.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Server/WorldServer/Src/Packet/ZoneLinkPackets.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryReader.h"

#include <cstring>
#include <vector>

namespace World
{
    ZoneLinkHandler::ZoneLinkHandler(ClientRegistry& clientRegistry, ZoneLinkRegistry& zoneLinkRegistry,
                                      Thread::AffinityWorkerPool<Db::DbWorker>& dbWorkers, WorldWorker& worldWorker)
        : clientRegistry_(clientRegistry)
        , zoneLinkRegistry_(zoneLinkRegistry)
        , dbWorkers_(dbWorkers)
        , worldWorker_(worldWorker)
    {
        RegisterHandlers();
    }

    void ZoneLinkHandler::RegisterHandlers()
    {
        dispatcher_.Register(PacketId::Z2WZoneRegister, this, &ZoneLinkHandler::HandleZoneRegister);
        dispatcher_.Register(PacketId::Z2WRelay, this, &ZoneLinkHandler::HandleForwardToWorld);
        dispatcher_.Register(PacketId::Z2WZoneTransferRequest, this, &ZoneLinkHandler::HandleZoneTransferRequest);
        dispatcher_.Register(PacketId::Z2WUnitOfWorkStream, this, &ZoneLinkHandler::HandleUnitOfWorkStream);
    }

    void ZoneLinkHandler::OnSessionOpened(const std::shared_ptr<Network::Session>& session)
    {
        LOG.Info(ELogCategory::Zone, "Zone 연결 수락")
            .KV("SessionId", session->Id()).KV("Remote", session->RemoteAddress());
    }

    void ZoneLinkHandler::OnPacket(const std::shared_ptr<Network::Session>& session,
                                   const Packet::PacketHeader& header,
                                   const std::span<const byte> payload)
    {
        // 여기는 이 연결의 I/O 스레드(Session의 strand)다. 바이트만 복사해서 WorldWorker로
        // 넘기고, 실제 ClientRegistry/ZoneLinkRegistry 접근(RegisterHandlers로 등록해둔
        // Handle* 메서드들)은 그 스레드에서 일어난다.
        const auto packetId = static_cast<PacketId>(header.id);
        std::vector<byte> payloadCopy(payload.begin(), payload.end());

        worldWorker_.PostTask([this, session, packetId, payloadCopy = std::move(payloadCopy)]
        {
            dispatcher_.Dispatch(packetId, session, payloadCopy);
        });
    }

    void ZoneLinkHandler::OnClosed(const std::shared_ptr<Network::Session>& session, const std::error_code& /*reason*/)
    {
        // 세션 종료 통지도 I/O 스레드에서 오므로, zoneLinkRegistry_를 직접 건드리지 않고
        // WorldWorker로 넘긴다.
        const auto sessionId = session->Id();
        worldWorker_.PostTask([this, sessionId]
        {
            // 이 연결이 등록해둔 zoneId가 여러 개일 수 있다(한 Zone 서버 프로세스가 존 여러
            // 개를 호스팅) -- 전부 지워야 끊긴 세션으로 계속 라우팅되는 걸 막을 수 있다.
            zoneLinkRegistry_.RemoveBySession(sessionId);
            LOG.Info(ELogCategory::Zone, "Zone 연결 종료").KV("SessionId", sessionId);
        });
    }

    void ZoneLinkHandler::HandleZoneRegister(const std::shared_ptr<Network::Session>& zoneSession,
                                              const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(ZoneRegisterPacket))
        {
            return;
        }

        ZoneRegisterPacket registerPacket{};
        std::memcpy(&registerPacket, payload.data(), sizeof(ZoneRegisterPacket));

        zoneLinkRegistry_.Add(registerPacket.zoneId, zoneSession, registerPacket.xMin, registerPacket.xMax);

        LOG.Info(ELogCategory::Zone, "Zone 등록")
            .KV("ZoneId", registerPacket.zoneId).KV("XMin", registerPacket.xMin).KV("XMax", registerPacket.xMax);
    }

    void ZoneLinkHandler::HandleForwardToWorld(const std::shared_ptr<Network::Session>& /*zoneSession*/,
                                                const std::span<const byte> payload)
    {
        // 헤더의 clientSessionId만 들여다보고 나머지는 그대로 Gateway로 재전송한다.
        if (payload.size() < sizeof(ClientEnvelopeHeader))
        {
            return;
        }

        ClientEnvelopeHeader envelopeHeader{};
        std::memcpy(&envelopeHeader, payload.data(), sizeof(ClientEnvelopeHeader));

        const auto client = clientRegistry_.Find(envelopeHeader.clientSessionId);
        if (!client || !client->gatewaySession)
        {
            return;
        }

        client->gatewaySession->SendPacket(PacketId::W2GRelay, payload);
    }

    void ZoneLinkHandler::HandleZoneTransferRequest(const std::shared_ptr<Network::Session>& /*zoneSession*/,
                                                      const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(PlayerZoneStatePacket))
        {
            return;
        }

        PlayerZoneStatePacket state{};
        std::memcpy(&state, payload.data(), sizeof(PlayerZoneStatePacket));

        const auto targetZoneId = zoneLinkRegistry_.FindZoneContainingX(state.x);
        if (!targetZoneId)
        {
            LOG.Warning(ELogCategory::Zone, "핸드오프 대상 존을 찾지 못함")
                .KV("ClientSessionId", state.clientSessionId).KV("X", state.x);
            return;
        }

        const auto targetZoneLink = zoneLinkRegistry_.Find(*targetZoneId);
        if (!targetZoneLink)
        {
            return;
        }

        clientRegistry_.SetZone(state.clientSessionId, *targetZoneId);
        state.zoneId = *targetZoneId;  // 목표 존으로 덮어써서 그대로 EnterZoneRequest에 재사용
        targetZoneLink->zoneSession->SendPacket(PacketId::W2ZEnterZoneRequest,
                                                 std::as_bytes(std::span(&state, 1)));

        LOG.Info(ELogCategory::Zone, "존 핸드오프(라우팅 테이블만 교체, 클라이언트 재접속 없음)")
            .KV("ClientSessionId", state.clientSessionId).KV("ToZoneId", *targetZoneId)
            .KV("X", state.x).KV("Y", state.y);
    }

    void ZoneLinkHandler::HandleUnitOfWorkStream(const std::shared_ptr<Network::Session>& /*zoneSession*/,
                                                  const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        uint32_t playerId{};
        if (!reader.Read(playerId))
        {
            return;
        }

        // 남은 바이트(Core::Task::UnitOfWork가 직렬화한 제너릭 태스크 목록)는 DB 워커
        // 스레드에서 처리할 것이므로, payload(I/O 스레드가 곧 재사용할 버퍼)에서 복사해
        // 소유권을 옮긴다. 와이어 포맷 상세는 ZoneLinkPackets.h 주석 참고.
        const auto remaining = reader.RemainingBytes();
        std::vector<byte> taskBytes(remaining.begin(), remaining.end());

        Packet::BinaryReader ownerPeek(taskBytes);
        uint64_t ownerId{};
        if (!ownerPeek.Read(ownerId))
        {
            return;
        }

        // ownerId(=clientSessionId)로 해시해 고정된 DbWorker에 위임한다 -- 같은 플레이어의
        // UnitOfWork 태스크는 항상 같은 스레드에서 순서대로 처리되므로 락이 필요 없다
        // (TaskWorker와 동일한 owner-hash 원리).
        dbWorkers_.GetWorker(static_cast<size_t>(ownerId)).PostTask(
            [playerId, taskBytes = std::move(taskBytes)]
            {
                Packet::BinaryReader taskReader(taskBytes);
                uint64_t ownerId{};
                uint16_t taskCount{};
                if (!taskReader.Read(ownerId) || !taskReader.Read(taskCount))
                {
                    return;
                }

                for (uint16_t i = 0; i < taskCount; ++i)
                {
                    uint16_t kind{};
                    uint32_t payloadLen{};
                    if (!taskReader.Read(kind) || !taskReader.Read(payloadLen))
                    {
                        break;
                    }

                    const auto taskPayload = taskReader.ReadBytes(payloadLen);
                    if (!taskPayload)
                    {
                        break;
                    }

                    // 지금은 Mail 태스크(kind 0=Added/1=Removed)만 있다 -- 새 콘텐츠 태스크가
                    // 추가되면 kind별 분기만 늘어난다(이 레이어는 그 의미를 몰라도 되는 게
                    // Task::UnitOfWork를 Core로 올린 이유다).
                    Packet::BinaryReader mailReader(*taskPayload);
                    uint32_t mailId{};
                    std::string title;
                    std::string body;
                    int64_t sendUt{};
                    int64_t endUt{};
                    if (!mailReader.Read(mailId) || !mailReader.ReadString(title) || !mailReader.ReadString(body)
                        || !mailReader.Read(sendUt) || !mailReader.Read(endUt))
                    {
                        continue;
                    }

                    // TODO: 실제로는 여기서 DB에 INSERT/DELETE 쿼리를 실행한다(Docker DB 연동 후
                    // 구현 예정). 지금은 DB 워커 스레드가 owner-hash로 순서대로 태스크를
                    // 처리한다는 구조만 보여준다.
                    // Debug 레벨 -- 부하 테스트처럼 세션/사이클 수가 많으면 태스크 1건마다
                    // Info로 찍을 경우 로그 I/O 자체가 병목이 되어 "락 경합으로 인한 정체"와
                    // 구분이 안 된다. 기본 실행(main.cpp의 Logger::Initialize)은 Info 레벨이라
                    // 평소엔 파일에 안 쌓이고, 필요할 때만 Debug로 켜서 본다.
                    LOG.Debug(ELogCategory::Db, "UnitOfWork 태스크 처리 (DB 반영은 TODO)")
                        .KV("OwnerId", ownerId).KV("PlayerId", playerId)
                        .KV("Kind", kind == 0 ? "Add" : "Del").KV("MailId", mailId).KV("Title", title);
                }
            });
    }
}
