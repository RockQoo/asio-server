#include "pch.h"
#include "Handler/ZoneLinkHandler.h"
#include "World/PlayerManager.h"
#include "World/ZoneLinkRegistry.h"
#include "Packet/EnterZoneBody.h"
#include "Packet/OwnerIdPeek.h"
#include "Packet/RelayEnvelope.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Shared/Protocol/Src/CurrencyType.h"
#include "Shared/Protocol/Src/TaskKind.h"
#include "Packet/ZoneLinkPackets.h"
#include "Db/AutoDbCommand.h"

#include "Shared/Core/Src/Common/RUID.h"
#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryReader.h"

namespace World
{
    namespace
    {
        // 현재 시각(유닉스 초). 우편 삭제 시각처럼 DB에 남기는 값에 쓴다.
        [[nodiscard]] int64_t NowUt()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }

        // Mail 카테고리 태스크 하나를 **World 캐시와 DB 양쪽에** 반영한다.
        // BASIC 레인(owner = clientSessionId)에서 불린다 -- DB로 나가는 것은 autoDbCommand가
        // 모았다가 스코프 끝에서 한 번에 DB 레인으로 넘긴다.
        void ApplyMailTask(PlayerManager::Mutexed& playerManager, AutoDbCommand& autoDbCommand,
                           const Protocol::EMailTask subTask, const Network::SessionId clientSessionId,
                           const Protocol::PlayerId playerId, const std::span<const byte> taskPayload)
        {
            Packet::BinaryReader binaryReader(taskPayload);
            Protocol::MailId mailId{};
            std::string title;
            std::string body;
            int64_t sendUt{};
            int64_t endUt{};
            if (!binaryReader.Read(mailId) || !binaryReader.ReadString(title) || !binaryReader.ReadString(body)
                || !binaryReader.Read(sendUt) || !binaryReader.Read(endUt))
            {
                return;
            }

            switch (subTask)
            {
            case Protocol::EMailTask::Added:
                {
                    playerManager.Write()->AddMail(clientSessionId,
                                                   MailInfo{mailId, title, body, sendUt, endUt});
                    // mailId는 존이 이미 확정한 RUID다 -- DB의 IDENTITY를 기다리지 않으므로
                    // 클라이언트는 벌써 이 id로 우편을 들고 있다(cpp-patterns.md의 RUID 절).
                    autoDbCommand.Add(DbCommand{"dbo.usp_mails_upsert",
                        {mailId.Value(), playerId.Value(), std::move(title), std::move(body), sendUt, endUt}});
                }
                break;

            case Protocol::EMailTask::Removed:
                {
                    playerManager.Write()->RemoveMail(clientSessionId, mailId);
                    // 행을 지우지 않고 삭제 시각만 남긴다(Sql/mails.sql의 규칙 5).
                    autoDbCommand.Add(DbCommand{"dbo.usp_mails_delete", {mailId.Value(), NowUt()}});
                }
                break;

            default:
                LOG.Warning(ELogCategory::Db, "알 수 없는 Mail 세부 태스크")
                    .KV("ClientSessionId", clientSessionId).KV("SubTask", static_cast<uint32_t>(subTask));
                break;
            }
        }

        void ApplyCurrencyTask(PlayerManager::Mutexed& playerManager, AutoDbCommand& autoDbCommand,
                               const Network::SessionId clientSessionId, const Protocol::PlayerId playerId,
                               const std::span<const byte> taskPayload)
        {
            Packet::BinaryReader binaryReader(taskPayload);
            uint8_t currencyType{};
            int64_t newValue{};
            int64_t oldValue{};
            if (!binaryReader.Read(currencyType) || !binaryReader.Read(newValue) || !binaryReader.Read(oldValue))
            {
                return;
            }

            playerManager.Write()->SetCurrency(clientSessionId, currencyType, newValue);

            // 새 값과 이전 값이 둘 다 실려 오는 이유(ZoneServer의 Currency::CurrencyTask 주석):
            // DB는 새 값으로 UPDATE하면 되고, 이전 값은 감사/추적용이다 -- "누가 언제 얼마에서
            // 얼마로 바뀌었는지"가 한 행에 남으면 재화 사고를 추적할 수 있다. 지금 SP는 새 값만
            // 받으므로 이전 값은 로그로만 남긴다.
            autoDbCommand.Add(DbCommand{"dbo.usp_currencies_upsert", {playerId.Value(), currencyType, newValue}});

            LOG.Debug(ELogCategory::Db, "Currency 태스크 반영")
                .KV("ClientSessionId", clientSessionId).KV("PlayerId", playerId)
                .KV("CurrencyType", static_cast<uint32_t>(currencyType))
                .KV("OldValue", oldValue).KV("NewValue", newValue);
        }
    }

    ZoneLinkHandler::ZoneLinkHandler(PlayerManager::Mutexed& playerManager, ZoneLinkRegistry::Mutexed& zoneLinkRegistry,
                                      Processor::Group<EProcessorId>& basicGroup,
                                      Processor::Group<EProcessorId>& dbGroup, DbConnectionPool& dbPool)
        : playerManager_(playerManager)
        , zoneLinkRegistry_(zoneLinkRegistry)
        , basicGroup_(basicGroup)
        , dbGroup_(dbGroup)
        , dbPool_(dbPool)
    {
        RegisterHandlers();
    }
    void ZoneLinkHandler::RegisterHandlers()
    {
        dispatcher_.Register(PacketId::Z2WZoneRegister, this, &ZoneLinkHandler::HandleZoneRegister);
        dispatcher_.Register(PacketId::Z2WRelay, this, &ZoneLinkHandler::HandleForwardToWorld);
        dispatcher_.Register(PacketId::Z2WZoneTransfer, this, &ZoneLinkHandler::HandleZoneTransfer);
        dispatcher_.Register(PacketId::Z2WUnitOfWorkStream, this, &ZoneLinkHandler::HandleUnitOfWorkStream);
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

        case PacketId::Z2WZoneTransfer:
            // PlayerZoneStatePacket.clientSessionId -- zoneId(uint32) 뒤라 offset 4다.
            // 구조체가 #pragma pack(1)이라 패딩이 없다는 것에 기대고 있다.
            return PeekOwnerId<Network::SessionId>(payload, sizeof(uint32_t));

        case PacketId::Z2WUnitOfWorkStream:
            // Task::UnitOfWork::Serialize가 스트림 맨 앞에 넣어둔 ownerId(= clientSessionId).
            // 그 앞에 Zone이 붙인 playerId(int64) + requestId(int64)가 있어 offset 16이다.
            return PeekOwnerId<uint64_t>(payload, sizeof(Protocol::PlayerId) + sizeof(Common::RUID));

        default:
            return std::nullopt;
        }
    }

    void ZoneLinkHandler::OnSessionOpened(const Network::Session::SPtr& session)
    {
        LOG.Info(ELogCategory::Zone, "Zone 연결 수락")
            .KV("SessionId", session->Id()).KV("Remote", session->RemoteAddress());
    }

    void ZoneLinkHandler::OnPacket(const Network::Session::SPtr& session,
                                   const Packet::Header& header,
                                   const std::span<const byte> payload)
    {
        // 여기는 이 연결의 I/O 스레드(Session의 strand)다. 라우팅에 필요한 정수 하나만 읽고
        // 바이트를 복사해 넘긴다.
        const auto packetId = static_cast<PacketId>(header.id);

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

    void ZoneLinkHandler::OnClosed(const Network::Session::SPtr& session, const std::error_code& /*reason*/)
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

    void ZoneLinkHandler::HandleZoneRegister(const Network::Session::SPtr& zoneSession,
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

    void ZoneLinkHandler::HandleForwardToWorld(const Network::Session::SPtr& /*zoneSession*/,
                                                const std::span<const byte> payload)
    {
        // 헤더의 clientSessionId만 들여다보고 나머지는 그대로 Gateway로 재전송한다.
        if (payload.size() < sizeof(ClientEnvelopeHeader))
        {
            return;
        }

        ClientEnvelopeHeader envelopeHeader{};
        std::memcpy(&envelopeHeader, payload.data(), sizeof(ClientEnvelopeHeader));

        const auto client = playerManager_->Find(envelopeHeader.clientSessionId);
        if (!client || !client->gatewaySession)
        {
            return;
        }

        client->gatewaySession->SendPacket(PacketId::W2GRelay, payload);
    }

    void ZoneLinkHandler::HandleZoneTransfer(const Network::Session::SPtr& /*zoneSession*/,
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

        // **프로세스를 넘는 이동이면 떠난 쪽에 퇴장을 알린다.** 이게 없으면 보낸 프로세스에
        // 그 사람의 Player와 우편함이 유령으로 남아, 만료 스윕이 같은 우편을 한 번 더 지우고
        // 클라이언트도 삭제 통지를 두 번 받는다(RUID의 노드 번호가 서로 다른 두 프로세스를
        // 가리키는 것으로 확인했다). 접속이 끝날 때까지 안 지워지므로 메모리도 샌다.
        //
        // **같은 프로세스 안의 이동(가로)에는 보내지 않는다.** 그쪽은 PlayerProcessor가
        // 살아 있는 Player를 그대로 옮기는데, 여기서 퇴장을 보내면 우편함째 지워버린다.
        // 판정 기준은 zoneId가 아니라 **링크 세션**이다 -- 한 Zone 프로세스가 존 여러 개를
        // 호스팅하므로 존이 다르다고 프로세스가 다른 게 아니다.
        const auto sourceZoneLink = zoneLinkRegistry_->Find(state.zoneId);
        if (sourceZoneLink && sourceZoneLink->zoneSession != targetZoneLink->zoneSession)
        {
            LeaveZoneNotifyPacket leaveZoneNotifyPacket{};
            leaveZoneNotifyPacket.clientSessionId = state.clientSessionId;
            sourceZoneLink->zoneSession->SendPacket(PacketId::W2ZLeaveZone,
                                                    std::as_bytes(std::span(&leaveZoneNotifyPacket, 1)));
        }

        playerManager_.Write()->SetZone(state.clientSessionId, *targetZoneId);
        state.zoneId = *targetZoneId;  // 목표 존으로 덮어써서 그대로 W2ZEnterZone에 재사용

        // **캐시의 콘텐츠를 다시 실어 보낸다.** 이게 없으면 전입한 존이 빈 우편함·빈 지갑으로
        // 시작해서, 존 경계를 넘을 때마다 그 사람의 우편이 사라진다.
        //
        // 이 캐시가 최신이라는 보장은 **UnitOfWork 스트림도 같은 BASIC 레인, 같은 주인**으로
        // 오는 데서 나온다. 존이 "우편 추가" 스트림을 보낸 뒤 핸드오프를 요청하면, 같은 TCP
        // 링크라 도착 순서가 유지되고 같은 strand에서 순서대로 처리되므로 여기서 읽는 캐시에
        // 그 우편이 이미 들어 있다.
        targetZoneLink->zoneSession->SendPacket(PacketId::W2ZEnterZone, EnterZoneBodyFor(state));

        LOG.Info(ELogCategory::Zone, "존 핸드오프(라우팅 테이블만 교체, 클라이언트 재접속 없음)")
            .KV("ClientSessionId", state.clientSessionId).KV("ToZoneId", *targetZoneId)
            .KV("X", state.x).KV("Y", state.y);
    }

    std::vector<byte> ZoneLinkHandler::EnterZoneBodyFor(const PlayerZoneStatePacket& state) const
    {
        const auto info = playerManager_->Find(state.clientSessionId);
        if (!info)
        {
            // 캐시가 없다 = 그 사이 접속이 끊겼다. 존이 빈 모델로 시작하지만 곧 퇴장 통지가
            // 뒤따르므로, 여기서 끊는 것보다 형식을 맞춰 보내는 편이 존 쪽 분기가 단순하다.
            LOG.Warning(ELogCategory::Zone, "핸드오프 대상의 캐시가 없다")
                .KV("ClientSessionId", state.clientSessionId);
            return BuildEnterZoneBody(state, {}, {});
        }

        return BuildEnterZoneBody(state, info->mails, info->currencies);
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

        playerManager_.Write()->SetZone(state.clientSessionId, state.zoneId);
        sourceZoneLink->zoneSession->SendPacket(PacketId::W2ZEnterZone, EnterZoneBodyFor(state));

        LOG.Warning(ELogCategory::Zone, "이동 대상 존이 없어 원래 존으로 되돌림(월드 경계 밖)")
            .KV("ClientSessionId", state.clientSessionId).KV("ZoneId", state.zoneId)
            .KV("X", state.x).KV("Y", state.y);
    }

    void ZoneLinkHandler::HandleUnitOfWorkStream(const Network::Session::SPtr& /*zoneSession*/,
                                                 const std::span<const byte> payload)
    {
        // **여기는 BASIC 레인이고 주인은 clientSessionId다.** 그래서 이 클라이언트의 접속 종료
        // (GatewayLinkHandler::HandleClientDisconnected)와 같은 strand에 들어가고, "이미 지워진
        // 사람의 캐시를 되살리는" 순서 역전이 생기지 않는다. 예전에는 이 스트림만 BASIC을
        // 건너뛰고 DB 그룹으로 직행해서, **World 캐시가 로그인 시점 스냅샷에 멈춰 있었다** --
        // 존에서 만든 우편이 프로세스를 넘는 핸드오프에서 사라지던 원인이 그것이다.
        Packet::BinaryReader binaryReader(payload);
        Protocol::PlayerId zonePlayerId{};
        Common::RUID requestId{};
        uint64_t ownerId{};
        uint16_t taskCount{};
        if (!binaryReader.Read(zonePlayerId) || !binaryReader.Read(requestId)
            || !binaryReader.Read(ownerId) || !binaryReader.Read(taskCount))
        {
            return;
        }

        const auto clientSessionId = static_cast<Network::SessionId>(ownerId);

        // **DB에 쓸 player_id는 캐시에서 꺼낸다.** 존이 실어 보낸 값을 그대로 쓰지 않는 이유는
        // 신뢰 경계다 -- DB 키는 로그인이 확정한 값만 쓴다.
        const auto playerId = playerManager_->FindPlayerId(clientSessionId);
        if (!playerId || !playerId->IsValid())
        {
            // 접속이 이미 끊겨 캐시가 사라진 뒤다. 되돌릴 방법이 없으므로 사실만 남긴다.
            LOG.Error(ELogCategory::Db, "UnitOfWork 스트림의 플레이어를 찾을 수 없어 버린다")
                .KV("ClientSessionId", clientSessionId).KV("RequestId", requestId)
                .KV("TaskCount", taskCount);
            return;
        }

        // 존이 실은 값과 캐시가 다르면 둘 중 하나가 어긋난 것이다. 지금은 캐시를 믿고 진행하되
        // 사실을 남긴다 -- **여기가 위조 검증이 들어갈 자리**다(태스크 내용을 캐시와 대조).
        if (zonePlayerId != *playerId)
        {
            LOG.Error(ELogCategory::Db, "존이 실은 playerId가 캐시와 다르다")
                .KV("ClientSessionId", clientSessionId)
                .KV("FromZone", zonePlayerId).KV("FromCache", *playerId);
        }

        // **UnitOfWork 하나 = AutoDbCommand 하나 = 트랜잭션 하나.** 우편 지급과 골드 차감처럼
        // 모델 두 개에 걸친 변경이 반쪽만 남지 않게 하려는 것이다.
        //
        // 주인이 clientSessionId가 아니라 playerId인 이유: DB 작업은 세션이 아니라 계정에
        // 묶이는 일이라, 재접속해서 세션이 바뀌어도 같은 DB 레인을 유지해야 한 계정의 쓰기가
        // 도착 순서대로 직렬화된다(LoginProcessor::LoadPlayerContent 주석과 짝).
        AutoDbCommand autoDbCommand(dbPool_, dbGroup_, static_cast<uint64_t>(playerId->Value()), true);

        for (uint16_t i = 0; i < taskCount; ++i)
        {
            uint16_t kind{};
            uint32_t payloadLen{};
            if (!binaryReader.Read(kind) || !binaryReader.Read(payloadLen))
            {
                break;
            }

            const auto taskPayload = binaryReader.ReadBytes(payloadLen);
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
                ApplyMailTask(playerManager_, autoDbCommand,
                              static_cast<Protocol::EMailTask>(Protocol::SubTaskOf(kind)),
                              clientSessionId, *playerId, *taskPayload);
                break;
            case Protocol::ETaskCategory::Currency:
                ApplyCurrencyTask(playerManager_, autoDbCommand, clientSessionId, *playerId, *taskPayload);
                break;
            default:
                // 이 빌드가 모르는 카테고리 -- 길이 프리픽스 덕분에 건너뛰기만 하면
                // 나머지 태스크는 정상 처리된다.
                LOG.Warning(ELogCategory::Db, "알 수 없는 UnitOfWork 태스크 카테고리")
                    .KV("ClientSessionId", clientSessionId).KV("RequestId", requestId).KV("TaskKind", kind);
                break;
            }
        }

        // autoDbCommand가 여기서 소멸하며 쌓인 SP를 DB 레인으로 한 번에 넘긴다.
    }
}
