#include "Server/WorldServer/Src/pch.h"
#include "Server/WorldServer/Src/Handler/ZoneLinkHandler.h"
#include "Server/WorldServer/Src/World/ClientRegistry.h"
#include "Server/WorldServer/Src/World/ZoneLinkRegistry.h"
#include "Server/WorldServer/Src/Packet/OwnerIdPeek.h"
#include "Server/WorldServer/Src/Packet/RelayEnvelope.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Shared/Protocol/Src/CurrencyType.h"
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

        void ApplyCurrencyTask(const uint64_t ownerId, const uint32_t playerId,
                               const std::span<const byte> taskPayload)
        {
            Packet::BinaryReader reader(taskPayload);
            uint8_t currencyType{};
            int64_t newValue{};
            int64_t oldValue{};
            if (!reader.Read(currencyType) || !reader.Read(newValue) || !reader.Read(oldValue))
            {
                return;
            }

            // 새 값과 이전 값이 둘 다 실려 오는 이유(ZoneServer의 Currency::CurrencyTask 주석):
            // DB는 새 값으로 UPDATE하면 되고, 이전 값은 감사/추적용이다 -- "누가 언제 얼마에서
            // 얼마로 바뀌었는지"가 한 행에 남으면 재화 사고를 추적할 수 있다.
            LOG.Debug(ELogCategory::Db, "Currency 태스크 처리 (DB 반영은 TODO)")
                .KV("OwnerId", ownerId).KV("PlayerId", playerId)
                .KV("CurrencyType", static_cast<uint32_t>(currencyType))
                .KV("OldValue", oldValue).KV("NewValue", newValue);
        }
    }

    ZoneLinkHandler::ZoneLinkHandler(ClientRegistry& clientRegistry, ZoneLinkRegistry::Mutexed& zoneLinkRegistry,
                                      Processor::ProcessorGroup<EProcessorId>& basicGroup,
                                      Processor::ProcessorGroup<EProcessorId>& dbGroup)
        : clientRegistry_(clientRegistry)
        , zoneLinkRegistry_(zoneLinkRegistry)
        , basicGroup_(basicGroup)
        , dbGroup_(dbGroup)
    {
        RegisterHandlers();
    }

    void ZoneLinkHandler::RegisterHandlers()
    {
        // UnitOfWorkStream은 여기 등록하지 않는다 -- BASIC을 거치지 않고 OnPacket에서 곧바로
        // DB 그룹으로 가기 때문이다(헤더 주석 참고).
        dispatcher_.Register(PacketId::Z2WZoneRegister, this, &ZoneLinkHandler::HandleZoneRegister);
        dispatcher_.Register(PacketId::Z2WRelay, this, &ZoneLinkHandler::HandleForwardToWorld);
        dispatcher_.Register(PacketId::Z2WZoneTransferRequest, this, &ZoneLinkHandler::HandleZoneTransferRequest);
    }

    std::optional<uint64_t> ZoneLinkHandler::OwnerIdOf(const PacketId packetId, const std::span<const byte> payload)
    {
        switch (packetId)
        {
        case PacketId::Z2WZoneRegister:
            // ZoneRegisterPacket.zoneId (offset 0)
            return PeekOwnerId<uint32_t>(payload);

        case PacketId::Z2WRelay:
            // ClientEnvelopeHeader.clientSessionId (offset 0)
            return PeekOwnerId<Network::SessionId>(payload);

        case PacketId::Z2WZoneTransferRequest:
            // PlayerZoneStatePacket.clientSessionId -- zoneId(uint32) 뒤라 offset 4다.
            // 구조체가 #pragma pack(1)이라 패딩이 없다는 것에 기대고 있다.
            return PeekOwnerId<Network::SessionId>(payload, sizeof(uint32_t));

        default:
            return std::nullopt;
        }
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
        // 여기는 이 연결의 I/O 스레드(Session의 strand)다. 라우팅에 필요한 정수 하나만 읽고
        // 바이트를 복사해 넘긴다.
        const auto packetId = static_cast<PacketId>(header.id);

        if (packetId == PacketId::Z2WUnitOfWorkStream)
        {
            PostUnitOfWorkStream(payload);
            return;
        }

        const auto ownerId = OwnerIdOf(packetId, payload);
        if (!ownerId)
        {
            LOG.Warning(ELogCategory::Zone, "ownerId를 읽을 수 없는 패킷, 버림")
                .KV("PacketId", header.id).KV("PayloadSize", payload.size());
            return;
        }

        std::vector<byte> payloadCopy(payload.begin(), payload.end());

        basicGroup_.Post(EProcessorId::Main, *ownerId,
            [this, session, packetId, payloadCopy = std::move(payloadCopy)]
            {
                dispatcher_.Dispatch(packetId, session, payloadCopy);
            });
    }

    void ZoneLinkHandler::OnClosed(const std::shared_ptr<Network::Session>& session, const std::error_code& /*reason*/)
    {
        // 세션 종료 통지도 I/O 스레드에서 오므로, 여기서 레지스트리를 직접 건드리지 않고
        // BASIC 그룹으로 넘긴다. 주인은 끊긴 세션 자신이다 -- 등록/해제가 같은 스레드에서
        // 순서대로 처리되게 하려는 것이고, 레지스트리 자체는 Mutexed가 따로 지킨다.
        const auto sessionId = session->Id();
        basicGroup_.Post(EProcessorId::Main, sessionId, [this, sessionId]
        {
            // 이 연결이 등록해둔 zoneId가 여러 개일 수 있다(한 Zone 서버 프로세스가 존 여러
            // 개를 호스팅) -- 전부 지워야 끊긴 세션으로 계속 라우팅되는 걸 막을 수 있다.
            zoneLinkRegistry_.Write()->RemoveBySession(sessionId);
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

        zoneLinkRegistry_.Write()->Add(registerPacket.zoneId, zoneSession,
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

        const auto targetZoneId = zoneLinkRegistry_->FindZoneContaining(state.x, state.y);
        if (!targetZoneId)
        {
            // 어느 존도 담당하지 않는 좌표다(존 격자에 구멍이 있거나, 그 행을 담당하는 Zone
            // 프로세스가 안 떠 있는 경우). 보낸 존은 이미 자기 상태에서 플레이어를 지웠으므로
            // 여기서 그냥 return하면 그 플레이어는 **아무 존에도 없는 상태로 사라진다**.
            // 그래서 이동을 취소하고 원래 존 안쪽으로 되돌려 넣는다.
            ReturnToSourceZone(state);
            return;
        }

        const auto targetZoneLink = zoneLinkRegistry_->Find(*targetZoneId);
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
        const auto sourceZoneLink = zoneLinkRegistry_->Find(state.zoneId);
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

    void ZoneLinkHandler::PostUnitOfWorkStream(const std::span<const byte> payload)
    {
        // 여기는 아직 I/O 스레드다 -- 라우팅에 필요한 만큼만 읽는다.
        Packet::BinaryReader reader(payload);
        uint32_t playerId{};
        Common::RequestId requestId{};
        if (!reader.Read(playerId) || !reader.Read(requestId))
        {
            return;
        }

        // 남은 바이트(Core::Task::UnitOfWork가 직렬화한 제너릭 태스크 목록)는 DB 그룹
        // 스레드에서 처리할 것이므로, payload(I/O 스레드가 곧 재사용할 버퍼)에서 복사해
        // 소유권을 옮긴다. 와이어 포맷 상세는 ZoneLinkPackets.h 주석 참고.
        const auto remaining = reader.RemainingBytes();
        std::vector<byte> taskBytes(remaining.begin(), remaining.end());

        // 스트림 맨 앞의 ownerId(=clientSessionId)가 곧 이 메시지의 주인이다 -- UnitOfWork가
        // 직렬화할 때 이미 넣어둔 값이라 따로 실어 보낼 필요가 없다.
        Packet::BinaryReader ownerPeek(taskBytes);
        uint64_t ownerId{};
        if (!ownerPeek.Read(ownerId))
        {
            return;
        }

        // BASIC을 거치지 않고 DB 그룹으로 직행한다. 같은 플레이어의 UnitOfWork 태스크는 항상
        // 같은 DB 스레드에서 도착 순서대로 처리되므로 락이 필요 없다.
        dbGroup_.Post(EProcessorId::Db, ownerId,
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
                    case Protocol::ETaskCategory::Currency:
                        ApplyCurrencyTask(ownerId, playerId, *taskPayload);
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
