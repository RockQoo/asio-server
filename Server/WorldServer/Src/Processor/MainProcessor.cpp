#include "pch.h"
#include "Processor/MainProcessor.h"

#include "Db/AutoSpCommands.h"
#include "Packet/EnterZoneBody.h"
#include "Shared/Common/Src/Packet/RelayEnvelope.h"
#include "Processor/LoginProcessor.h"
#include "Shared/Common/Src/Enum.h"
#include "Shared/Common/Src/TaskKind.h"

#include "Shared/Core/Src/Base/RUID.h"
#include "Shared/Core/Src/Packet/BinaryReader.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"

namespace
{
    // 현재 시각(유닉스 초). 우편 삭제 시각처럼 DB에 남기는 값에 쓴다.
    [[nodiscard]] int64_t NowUt()
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Mail 카테고리 태스크 하나를 **World 캐시와 DB 양쪽에** 반영한다.
    // BASIC 레인(owner = clientSessionId)에서 불린다 -- DB로 나가는 것은 autoSpCommands가
    // 모았다가 스코프 끝에서 한 번에 DB 레인으로 넘긴다.
    void ApplyMailTask(PlayerManager::Mutexed& playerManager, AutoSpCommands& autoSpCommands,
                       const Common::EMailTask subTask, const Network::SessionId clientSessionId,
                       const Common::PlayerId playerId, const std::span<const byte> taskPayload)
    {
        Packet::BinaryReader binaryReader(taskPayload);
        Common::MailId mailId{};
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
        case Common::EMailTask::Added:
            {
                playerManager.Write()->AddMail(clientSessionId,
                                               Common::MailInfo{mailId, title, body, sendUt, endUt});
                // mailId는 존이 이미 확정한 RUID다 -- DB의 IDENTITY를 기다리지 않으므로
                // 클라이언트는 벌써 이 id로 우편을 들고 있다(cpp-patterns.md의 RUID 절).
                autoSpCommands.Add(DbCommand{"dbo.usp_mails_upsert",
                    {mailId.Value(), playerId.Value(), std::move(title), std::move(body), sendUt, endUt}});
            }
            break;

        case Common::EMailTask::Removed:
            {
                playerManager.Write()->RemoveMail(clientSessionId, mailId);
                // 행을 지우지 않고 삭제 시각만 남긴다(Sql/mails.sql의 규칙 5).
                autoSpCommands.Add(DbCommand{"dbo.usp_mails_delete", {mailId.Value(), NowUt()}});
            }
            break;

        default:
            LOG.Warning(ELogCategory::Db, "알 수 없는 Mail 세부 태스크")
                .KV("ClientSessionId", clientSessionId).KV("SubTask", static_cast<uint32_t>(subTask));
            break;
        }
    }

    void ApplyCurrencyTask(PlayerManager::Mutexed& playerManager, AutoSpCommands& autoSpCommands,
                           const Network::SessionId clientSessionId, const Common::PlayerId playerId,
                           const std::span<const byte> taskPayload)
    {
        Packet::BinaryReader binaryReader(taskPayload);
        Common::ECurrencyType currencyType{};
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
        autoSpCommands.Add(DbCommand{"dbo.usp_currencies_upsert", {playerId.Value(), static_cast<uint8_t>(currencyType), newValue}});

        LOG.Debug(ELogCategory::Db, "Currency 태스크 반영")
            .KV("ClientSessionId", clientSessionId).KV("PlayerId", playerId)
            .KV("CurrencyType", static_cast<uint32_t>(currencyType))
            .KV("OldValue", oldValue).KV("NewValue", newValue);
    }
}

MainProcessor::MainProcessor(PlayerManager::Mutexed& playerManager, ZoneLinkRegistry::Mutexed& zoneLinkRegistry,
                             Processor::Group<EWorldProcessorId>& basicGroup, DbProcessor& dbProcessor,
                             LoginProcessor& loginProcessor)
    : playerManager_(playerManager)
    , zoneLinkRegistry_(zoneLinkRegistry)
    , basicGroup_(basicGroup)
    , dbProcessor_(dbProcessor)
    , loginProcessor_(loginProcessor)
{
    Register();
}

void MainProcessor::Register()
{
    gatewayDispatcher_.Register(PacketId::G2WClientConnected, this, &MainProcessor::HandleClientConnected);
    gatewayDispatcher_.Register(PacketId::G2WClientDisconnected, this, &MainProcessor::HandleClientDisconnected);
    gatewayDispatcher_.Register(PacketId::G2WRelay, this, &MainProcessor::HandleFromClient);

    zoneDispatcher_.Register(PacketId::Z2WZoneRegister, this, &MainProcessor::HandleZoneRegister);
    zoneDispatcher_.Register(PacketId::Z2WRelay, this, &MainProcessor::HandleForwardToWorld);
    zoneDispatcher_.Register(PacketId::Z2WZoneTransfer, this, &MainProcessor::HandleZoneTransfer);
    zoneDispatcher_.Register(PacketId::Z2WUnitOfWorkStream, this, &MainProcessor::HandleUnitOfWorkStream);
}

void MainProcessor::DispatchFromGateway(const PacketId packetId, const Network::Session::SPtr& gatewaySession,
                                        const std::span<const byte> payload)
{
    gatewayDispatcher_.Dispatch(packetId, gatewaySession, payload);
}

void MainProcessor::DispatchFromZone(const PacketId packetId, const Network::Session::SPtr& zoneSession,
                                     const std::span<const byte> payload)
{
    zoneDispatcher_.Dispatch(packetId, zoneSession, payload);
}

void MainProcessor::RemoveZoneLink(const Network::SessionId zoneSessionId)
{
    // 이 연결이 등록해둔 zoneId가 여러 개일 수 있다(한 Zone 서버 프로세스가 존 여러 개를
    // 호스팅) -- 전부 지워야 끊긴 세션으로 계속 라우팅되는 걸 막을 수 있다.
    zoneLinkRegistry_.Write()->RemoveBySession(zoneSessionId);
    LOG.Info(ELogCategory::Zone, "Zone 연결 종료").KV("SessionId", zoneSessionId);
}

void MainProcessor::HandleClientConnected(const Network::Session::SPtr& gatewaySession,
                                          const std::span<const byte> payload)
{
    Packet::BinaryReader binaryReader(payload);
    Network::SessionId clientSessionId{};
    if (!binaryReader.Read(clientSessionId))
    {
        return;
    }

    // **등록만 한다. 존 입장은 여기가 아니라 로그인 성공 시점이다**
    // (LoginProcessor::CompleteLogin). TCP 연결만으로 게임을 시작하던 예전 동작을 바꾼
    // 지점이고, 그래서 이 시점의 PlayerInfo는 authenticated=false / zoneId=0이다.
    playerManager_.Write()->Add(clientSessionId, gatewaySession);

    LOG.Info(ELogCategory::Gateway, "클라이언트 접속, 로그인 대기")
        .KV("ClientSessionId", clientSessionId);
}

void MainProcessor::HandleClientDisconnected(const Network::Session::SPtr& /*gatewaySession*/,
                                             const std::span<const byte> payload)
{
    Packet::BinaryReader binaryReader(payload);
    Network::SessionId clientSessionId{};
    if (!binaryReader.Read(clientSessionId))
    {
        return;
    }

    const auto client = playerManager_->Find(clientSessionId);
    playerManager_.Write()->Remove(clientSessionId);
    if (!client)
    {
        return;
    }

    const auto zoneLink = zoneLinkRegistry_->Find(client->zoneId);
    if (!zoneLink)
    {
        return;
    }

    Common::W2ZLeaveZone leave{};
    leave.clientSessionId = clientSessionId;
    zoneLink->zoneSession->SendPacket(PacketId::W2ZLeaveZone,
                                      std::as_bytes(std::span(&leave, 1)));

    LOG.Info(ELogCategory::Gateway, "클라이언트 접속 종료").KV("ClientSessionId", clientSessionId);
}

void MainProcessor::HandleFromClient(const Network::Session::SPtr& gatewaySession,
                                     const std::span<const byte> payload)
{
    if (payload.size() < sizeof(Common::RelayEnvelope))
    {
        return;
    }

    Common::RelayEnvelope envelopeHeader{};
    std::memcpy(&envelopeHeader, payload.data(), sizeof(Common::RelayEnvelope));

    const auto innerPacketId = static_cast<PacketId>(envelopeHeader.innerPacketId);

    // **C2W 대역만 World가 끝점이다.** 봉투를 벗겨 직접 처리하고 존으로 넘기지 않는다.
    // 대역으로 가르므로 로그인 말고 다른 C2W 패킷이 생겨도 이 분기는 그대로다.
    if (Common::DirectionOf(innerPacketId) == Common::EPacketDirection::C2W)
    {
        loginProcessor_.HandleClientPacket(gatewaySession, envelopeHeader.clientSessionId, innerPacketId,
                                           payload.subspan(sizeof(Common::RelayEnvelope)));
        return;
    }

    // 여기부터는 clientSessionId만 보고 나머지는 손대지 않은 채 Zone에 재전송한다.
    // World는 게임 패킷의 내용을 해석할 필요가 없다.
    const auto client = playerManager_->Find(envelopeHeader.clientSessionId);
    if (!client)
    {
        return;
    }

    // 로그인을 통과하지 않은 연결의 게임 패킷은 존까지 가지 않는다. 존은 입장한 사람만
    // 알고 있어서 어차피 버려지지만, 인증 판정을 World 한 곳에 두는 편이 추적이 쉽다.
    if (!client->authenticated)
    {
        LOG.Warning(ELogCategory::Gateway, "로그인 전 게임 패킷, 버림")
            .KV("ClientSessionId", envelopeHeader.clientSessionId)
            .KV("InnerPacketId", envelopeHeader.innerPacketId);
        return;
    }

    const auto zoneLink = zoneLinkRegistry_->Find(client->zoneId);
    if (!zoneLink)
    {
        return;
    }

    zoneLink->zoneSession->SendPacket(PacketId::W2ZRelay, payload);
}

void MainProcessor::HandleZoneRegister(const Network::Session::SPtr& zoneSession,
                                       const std::span<const byte> payload)
{
    if (payload.size() < sizeof(Common::Z2WZoneRegister))
    {
        return;
    }

    Common::Z2WZoneRegister registerPacket{};
    std::memcpy(&registerPacket, payload.data(), sizeof(Common::Z2WZoneRegister));

    zoneLinkRegistry_.Write()->Add(registerPacket.zoneId, zoneSession,
                                   registerPacket.xMin, registerPacket.xMax,
                                   registerPacket.yMin, registerPacket.yMax);

    LOG.Info(ELogCategory::Zone, "Zone 등록")
        .KV("ZoneId", registerPacket.zoneId)
        .KV("XMin", registerPacket.xMin).KV("XMax", registerPacket.xMax)
        .KV("YMin", registerPacket.yMin).KV("YMax", registerPacket.yMax);
}

void MainProcessor::HandleForwardToWorld(const Network::Session::SPtr& /*zoneSession*/,
                                         const std::span<const byte> payload)
{
    // 헤더의 clientSessionId만 들여다보고 나머지는 그대로 Gateway로 재전송한다.
    if (payload.size() < sizeof(Common::RelayEnvelope))
    {
        return;
    }

    Common::RelayEnvelope envelopeHeader{};
    std::memcpy(&envelopeHeader, payload.data(), sizeof(Common::RelayEnvelope));

    const auto client = playerManager_->Find(envelopeHeader.clientSessionId);
    if (!client || !client->gatewaySession)
    {
        return;
    }

    client->gatewaySession->SendPacket(PacketId::W2GRelay, payload);
}

void MainProcessor::HandleZoneTransfer(const Network::Session::SPtr& /*zoneSession*/,
                                       const std::span<const byte> payload)
{
    if (payload.size() < sizeof(Common::Z2WZoneTransfer))
    {
        return;
    }

    Common::Z2WZoneTransfer transfer{};
    std::memcpy(&transfer, payload.data(), sizeof(Common::Z2WZoneTransfer));

    const auto targetZoneId = zoneLinkRegistry_->FindZoneContaining(transfer.x, transfer.y);
    if (!targetZoneId)
    {
        // 어느 존도 담당하지 않는 좌표다(존 격자에 구멍이 있거나, 그 행을 담당하는 Zone
        // 프로세스가 안 떠 있는 경우). 보낸 존은 이미 자기 상태에서 플레이어를 지웠으므로
        // 여기서 그냥 return하면 그 플레이어는 **아무 존에도 없는 상태로 사라진다**.
        // 그래서 이동을 취소하고 원래 존 안쪽으로 되돌려 넣는다.
        ReturnToSourceZone(transfer);
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
    const auto sourceZoneLink = zoneLinkRegistry_->Find(transfer.zoneId);
    if (sourceZoneLink && sourceZoneLink->zoneSession != targetZoneLink->zoneSession)
    {
        Common::W2ZLeaveZone leaveZoneNotifyPacket{};
        leaveZoneNotifyPacket.clientSessionId = transfer.clientSessionId;
        sourceZoneLink->zoneSession->SendPacket(PacketId::W2ZLeaveZone,
                                                std::as_bytes(std::span(&leaveZoneNotifyPacket, 1)));
    }

    playerManager_.Write()->SetZone(transfer.clientSessionId, *targetZoneId);

    // 여기서 zoneId의 뜻이 "보낸 존"에서 "목표 존"으로 바뀜다 -- 타입도 같이 바뀜다.
    const Common::W2ZEnterZoneHead enterZone = Common::ToEnterZoneHead(transfer, *targetZoneId);

    // **캐시의 콘텐츠를 다시 실어 보낸다.** 이게 없으면 전입한 존이 빈 우편함·빈 지갑으로
    // 시작해서, 존 경계를 넘을 때마다 그 사람의 우편이 사라진다.
    //
    // 이 캐시가 최신이라는 보장은 **UnitOfWork 스트림도 같은 BASIC 레인, 같은 주인**으로
    // 오는 데서 나온다. 존이 "우편 추가" 스트림을 보낸 뒤 핸드오프를 요청하면, 같은 TCP
    // 링크라 도착 순서가 유지되고 같은 strand에서 순서대로 처리되므로 여기서 읽는 캐시에
    // 그 우편이 이미 들어 있다.
    targetZoneLink->zoneSession->SendPacket(PacketId::W2ZEnterZone, EnterZoneBodyFor(enterZone));

    LOG.Info(ELogCategory::Zone, "존 핸드오프(라우팅 테이블만 교체, 클라이언트 재접속 없음)")
        .KV("ClientSessionId", enterZone.clientSessionId).KV("ToZoneId", *targetZoneId)
        .KV("X", enterZone.x).KV("Y", enterZone.y);
}

std::vector<byte> MainProcessor::EnterZoneBodyFor(const Common::W2ZEnterZoneHead& enterZone) const
{
    const auto info = playerManager_->Find(enterZone.clientSessionId);
    if (!info)
    {
        // 캐시가 없다 = 그 사이 접속이 끊겼다. 존이 빈 모델로 시작하지만 곧 퇴장 통지가
        // 뒤따르므로, 여기서 끊는 것보다 형식을 맞춰 보내는 편이 존 쪽 분기가 단순하다.
        LOG.Warning(ELogCategory::Zone, "핸드오프 대상의 캐시가 없다")
            .KV("ClientSessionId", enterZone.clientSessionId);
        return BuildEnterZoneBody(enterZone, {}, {});
    }

    return BuildEnterZoneBody(enterZone, info->mails, info->currencies);
}

void MainProcessor::ReturnToSourceZone(Common::Z2WZoneTransfer transfer) const
{
    // transfer.zoneId는 핸드오프를 요청한(= 보낸) 존이다. 되돌리는 것이라 목표도 같다.
    const auto sourceZoneLink = zoneLinkRegistry_->Find(transfer.zoneId);
    if (!sourceZoneLink)
    {
        // 보낸 존까지 끊긴 상황이라 되돌릴 곳이 없다. 클라이언트는 연결은 유지되지만 어느
        // 존에도 없는 상태가 되므로, 원인을 남겨 둔다.
        LOG.Warning(ELogCategory::Zone, "이동 대상 존도 원래 존도 없어 플레이어를 되돌릴 수 없음")
            .KV("ClientSessionId", transfer.clientSessionId)
            .KV("FromZoneId", transfer.zoneId).KV("X", transfer.x).KV("Y", transfer.y);
        return;
    }

    // 원래 존 사각형 안쪽으로 최소한만 밀어 넣는다. xMax/yMax는 배타적 경계라 그대로 쓰면
    // 다시 "구간 밖"으로 판정되므로 살짝 안쪽을 쓴다.
    constexpr float Epsilon = 0.001f;
    transfer.x = std::clamp(transfer.x, sourceZoneLink->xMin, sourceZoneLink->xMax - Epsilon);
    transfer.y = std::clamp(transfer.y, sourceZoneLink->yMin, sourceZoneLink->yMax - Epsilon);

    const Common::W2ZEnterZoneHead enterZone = Common::ToEnterZoneHead(transfer, transfer.zoneId);

    playerManager_.Write()->SetZone(enterZone.clientSessionId, enterZone.zoneId);
    sourceZoneLink->zoneSession->SendPacket(PacketId::W2ZEnterZone, EnterZoneBodyFor(enterZone));

    LOG.Warning(ELogCategory::Zone, "이동 대상 존이 없어 원래 존으로 되돌림(월드 경계 밖)")
        .KV("ClientSessionId", enterZone.clientSessionId).KV("ZoneId", enterZone.zoneId)
        .KV("X", enterZone.x).KV("Y", enterZone.y);
}

void MainProcessor::HandleUnitOfWorkStream(const Network::Session::SPtr& /*zoneSession*/,
                                           const std::span<const byte> payload)
{
    // **여기는 BASIC 레인이고 주인은 clientSessionId다.** 그래서 이 클라이언트의 접속 종료
    // (HandleClientDisconnected)와 같은 strand에 들어가고, "이미 지워진 사람의 캐시를
    // 되살리는" 순서 역전이 생기지 않는다. 예전에는 이 스트림만 BASIC을 건너뛰고 DB 그룹으로
    // 직행해서, **World 캐시가 로그인 시점 스냅샷에 멈춰 있었다** -- 존에서 만든 우편이
    // 프로세스를 넘는 핸드오프에서 사라지던 원인이 그것이다.
    Packet::BinaryReader binaryReader(payload);
    Common::PlayerId zonePlayerId{};
    Base::RUID requestId{};
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

    // **UnitOfWork 하나 = AutoSpCommands 하나 = 트랜잭션 하나.** 우편 지급과 골드 차감처럼
    // 모델 두 개에 걸친 변경이 반쪽만 남지 않게 하려는 것이다.
    //
    // 주인이 clientSessionId가 아니라 playerId인 이유: DB 작업은 세션이 아니라 계정에
    // 묶이는 일이라, 재접속해서 세션이 바뀌어도 같은 DB 레인을 유지해야 한 계정의 쓰기가
    // 도착 순서대로 직렬화된다(LoginProcessor::LoadPlayerContent 주석과 짝).
    AutoSpCommands autoSpCommands(dbProcessor_, static_cast<uint64_t>(playerId->Value()), true);

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
        // (Shared/Common/Src/TaskKind.h) 여기서 2단으로 분기한다. 콘텐츠가 늘면
        // case가 하나씩 붙을 뿐, 태스크를 실어 나르는 Task::UnitOfWork(Core)는
        // 여전히 이 의미를 몰라도 된다.
        switch (Common::CategoryOf(kind))
        {
        case Common::ETaskCategory::Mail:
            ApplyMailTask(playerManager_, autoSpCommands,
                          static_cast<Common::EMailTask>(Common::SubTaskOf(kind)),
                          clientSessionId, *playerId, *taskPayload);
            break;
        case Common::ETaskCategory::Currency:
            ApplyCurrencyTask(playerManager_, autoSpCommands, clientSessionId, *playerId, *taskPayload);
            break;
        default:
            // 이 빌드가 모르는 카테고리 -- 길이 프리픽스 덕분에 건너뛰기만 하면
            // 나머지 태스크는 정상 처리된다.
            LOG.Warning(ELogCategory::Db, "알 수 없는 UnitOfWork 태스크 카테고리")
                .KV("ClientSessionId", clientSessionId).KV("RequestId", requestId).KV("TaskKind", kind);
            break;
        }
    }

    // autoSpCommands가 여기서 소멸하며 쌓인 SP를 DB 레인으로 한 번에 넘긴다.
}

void MainProcessor::BroadcastToAll(const PacketId clientPacketId, const std::span<const byte> payload)
{
    // **이 함수만 호출 스레드를 가리지 않는다.** 콘솔 입력 스레드처럼 레인 밖에서도 불릴 수
    // 있어서 바로 돌지 않고 BASIC 그룹으로 넘긴다. payload는 호출자의 지역 버퍼를 가리키므로
    // 넘어가기 전에 복사해서 소유권을 옮긴다.
    //
    // **ownerId를 주지 않는다** -- 대상이 전 클라이언트라 주인이 없고, 공지끼리 순서를
    // 맞출 필요도 없다. 주인을 억지로 0으로 주면 공지가 항상 0번 strand로만 가서, 전체
    // 순회라는 무거운 일이 레인 하나에 쌓인다. 남는 스레드가 집어가게 둔다.
    //
    // **PlayerManager가 Mutexed로 남아 있는 이유가 이 경로다** -- 주인이 없으니 어피니티로
    // 막을 수 없다.
    std::vector<byte> payloadCopy(payload.begin(), payload.end());

    basicGroup_.Post(EWorldProcessorId::Main,
        [this, clientPacketId, payloadCopy = std::move(payloadCopy)]
        {
            Common::RelayEnvelope header{};
            header.innerPacketId = static_cast<uint16_t>(clientPacketId);

            size_t sentCount = 0;
            size_t registeredCount = 0;

            playerManager_->ForEach(
                [&](const Network::SessionId clientSessionId, const PlayerInfo& info)
                {
                    ++registeredCount;
                    if (!info.gatewaySession)
                    {
                        return;
                    }

                    header.clientSessionId = clientSessionId;
                    Packet::BinaryWriter envelopeBinaryWriter;
                    envelopeBinaryWriter.Write(header);
                    envelopeBinaryWriter.WriteBytes(payloadCopy);
                    info.gatewaySession->SendPacket(PacketId::W2GRelay, envelopeBinaryWriter.GetBuffer());
                    ++sentCount;
                });

            LOG.Info(ELogCategory::General, "전체 브로드캐스트 처리")
                .KV("PacketId", static_cast<uint16_t>(clientPacketId))
                .KV("RegisteredClients", registeredCount)
                .KV("SentTo", sentCount);
        });
}
