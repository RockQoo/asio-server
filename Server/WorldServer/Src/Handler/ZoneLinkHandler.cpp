#include "Server/WorldServer/Src/pch.h"
#include "Server/WorldServer/Src/Handler/ZoneLinkHandler.h"
#include "Server/WorldServer/Src/World/ClientRegistry.h"
#include "Server/WorldServer/Src/World/ZoneLinkRegistry.h"
#include "Server/WorldServer/Src/Worker/WorldWorker.h"
#include "Server/WorldServer/Src/Packet/RelayEnvelope.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Shared/Protocol/Src/TaskKind.h"
#include "Server/WorldServer/Src/Packet/ZoneLinkPackets.h"

#include "Shared/Core/Src/Common/RequestId.h"
#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryReader.h"

#include <cstring>
#include <string>
#include <vector>

namespace World
{
    namespace
    {
        // Mail 카테고리 태스크 하나를 DB에 반영한다. DbWorker 스레드에서 불린다 --
        // owner-hash로 같은 플레이어는 항상 같은 스레드라 순서가 보장되고 락이 없다.
        void ApplyMailTask(const Protocol::EMailTask subTask, const uint64_t ownerId, const uint32_t playerId,
                           const std::span<const byte> taskPayload)
        {
            Packet::BinaryReader reader(taskPayload);
            uint32_t mailId{};
            std::string title;
            std::string body;
            int64_t sendUt{};
            int64_t endUt{};
            if (!reader.Read(mailId) || !reader.ReadString(title) || !reader.ReadString(body)
                || !reader.Read(sendUt) || !reader.Read(endUt))
            {
                return;
            }

            // TODO: 실제로는 여기서 DB에 INSERT/DELETE 쿼리(SP)를 실행한다(Docker DB 연동 후
            // 구현 예정). 지금은 DB 워커 스레드가 owner-hash로 순서대로 태스크를 처리한다는
            // 구조만 보여준다.
            // Debug 레벨 -- 부하 테스트처럼 세션/사이클 수가 많으면 태스크 1건마다 Info로 찍을
            // 경우 로그 I/O 자체가 병목이 되어 "락 경합으로 인한 정체"와 구분이 안 된다. 기본
            // 실행(main.cpp의 Logger::Initialize)은 Info 레벨이라 평소엔 파일에 안 쌓이고,
            // 필요할 때만 Debug로 켜서 본다.
            LOG.Debug(ELogCategory::Db, "Mail 태스크 처리 (DB 반영은 TODO)")
                .KV("OwnerId", ownerId).KV("PlayerId", playerId)
                .KV("SubTask", subTask == Protocol::EMailTask::Added ? "Added" : "Removed")
                .KV("MailId", mailId).KV("Title", title);
        }
    }

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

        zoneLinkRegistry_.Add(registerPacket.zoneId, zoneSession,
                              registerPacket.xMin, registerPacket.xMax,
                              registerPacket.yMin, registerPacket.yMax);

        LOG.Info(ELogCategory::Zone, "Zone 등록")
            .KV("ZoneId", registerPacket.zoneId)
            .KV("XMin", registerPacket.xMin).KV("XMax", registerPacket.xMax)
            .KV("YMin", registerPacket.yMin).KV("YMax", registerPacket.yMax);
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

        const auto targetZoneId = zoneLinkRegistry_.FindZoneContaining(state.x, state.y);
        if (!targetZoneId)
        {
            // 어느 존도 담당하지 않는 좌표다(존 격자에 구멍이 있거나, 그 행을 담당하는 Zone
            // 프로세스가 안 떠 있는 경우). 보낸 존은 이미 자기 상태에서 플레이어를 지웠으므로
            // 여기서 그냥 return하면 그 플레이어는 **아무 존에도 없는 상태로 사라진다**.
            // 그래서 이동을 취소하고 원래 존 안쪽으로 되돌려 넣는다.
            ReturnToSourceZone(state);
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

    void ZoneLinkHandler::ReturnToSourceZone(PlayerZoneStatePacket state) const
    {
        // state.zoneId는 핸드오프를 요청한(= 보낸) 존이다.
        const auto sourceZoneLink = zoneLinkRegistry_.Find(state.zoneId);
        if (!sourceZoneLink)
        {
            // 보낸 존까지 끊긴 상황이라 되돌릴 곳이 없다. 클라이언트는 연결은 유지되지만 어느
            // 존에도 없는 상태가 되므로, 원인을 남겨 둔다.
            LOG.Warning(ELogCategory::Zone, "이동 대상 존도 원래 존도 없어 플레이어를 되돌릴 수 없음")
                .KV("ClientSessionId", state.clientSessionId)
                .KV("FromZoneId", state.zoneId).KV("X", state.x).KV("Y", state.y);
            return;
        }

        // 원래 존 사각형 안쪽으로 최소한만 밀어 넣는다. xMax/yMax는 배타적 경계라 그대로 쓰면
        // 다시 "구간 밖"으로 판정되므로 살짝 안쪽을 쓴다.
        constexpr float Epsilon = 0.001f;
        state.x = std::clamp(state.x, sourceZoneLink->xMin, sourceZoneLink->xMax - Epsilon);
        state.y = std::clamp(state.y, sourceZoneLink->yMin, sourceZoneLink->yMax - Epsilon);

        clientRegistry_.SetZone(state.clientSessionId, state.zoneId);
        sourceZoneLink->zoneSession->SendPacket(PacketId::W2ZEnterZoneRequest,
                                                 std::as_bytes(std::span(&state, 1)));

        LOG.Warning(ELogCategory::Zone, "이동 대상 존이 없어 원래 존으로 되돌림(월드 경계 밖)")
            .KV("ClientSessionId", state.clientSessionId).KV("ZoneId", state.zoneId)
            .KV("X", state.x).KV("Y", state.y);
    }

    void ZoneLinkHandler::HandleUnitOfWorkStream(const std::shared_ptr<Network::Session>& /*zoneSession*/,
                                                  const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        uint32_t playerId{};
        Common::RequestId requestId{};
        if (!reader.Read(playerId) || !reader.Read(requestId))
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
            [playerId, requestId, taskBytes = std::move(taskBytes)]
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

                    // taskKind는 "상위 8비트 = 콘텐츠 카테고리 / 하위 8비트 = 세부 동작"이라
                    // (Shared/Protocol/Src/TaskKind.h) 여기서 2단으로 분기한다. 콘텐츠가 늘면
                    // case가 하나씩 붙을 뿐, 태스크를 실어 나르는 Task::UnitOfWork(Core)는
                    // 여전히 이 의미를 몰라도 된다.
                    switch (Protocol::CategoryOf(kind))
                    {
                    case Protocol::ETaskCategory::Mail:
                        ApplyMailTask(static_cast<Protocol::EMailTask>(Protocol::SubTaskOf(kind)),
                                      ownerId, playerId, *taskPayload);
                        break;
                    default:
                        // 이 빌드가 모르는 카테고리 -- 길이 프리픽스 덕분에 건너뛰기만 하면
                        // 나머지 태스크는 정상 처리된다.
                        LOG.Warning(ELogCategory::Db, "알 수 없는 UnitOfWork 태스크 카테고리")
                            .KV("OwnerId", ownerId).KV("RequestId", requestId).KV("TaskKind", kind);
                        break;
                    }
                }
            });
    }
}
