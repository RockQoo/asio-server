#include "pch.h"
#include "Tool/ToolProcessor.h"
#include "World/PlayerManager.h"
#include "World/ZoneLinkRegistry.h"
#include "Packet/RelayEnvelope.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Packet/ToolLinkPackets.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryReader.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"

// 운영툴이 주입하는 우편/공지는 "새로운 운영 전용 패킷"이 아니라 기존 클라이언트 패킷을
// 그대로 재사용한다(ToolProcessor.h 클래스 주석 참고). main.cpp도 notice REPL 때문에 같은
// 헤더를 include하고 있다.

namespace World
{
    namespace
    {
        // MailSendRequest::targetKind 값
        constexpr uint8_t kMailTargetAllOnline = 0;
        constexpr uint8_t kMailTargetSingle = 1;

        // 시크릿 비교를 길이만 같으면 항상 같은 시간에 끝내도록 한다. 운영툴 링크는 루프백/
        // 신뢰 네트워크 전제라 타이밍 공격이 현실적 위협은 아니지만, 조기 종료하는 비교 코드를
        // 남겨두면 나중에 이 링크를 넓은 망에 열 때 그대로 약점이 된다.
        [[nodiscard]] bool SecretEquals(const std::string_view lhs, const std::string_view rhs) noexcept
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }

            unsigned char diff = 0;
            for (size_t i = 0; i < lhs.size(); ++i)
            {
                diff |= static_cast<unsigned char>(lhs[i]) ^ static_cast<unsigned char>(rhs[i]);
            }
            return diff == 0;
        }
    }

    ToolProcessor::ToolProcessor(PlayerManager::Mutexed& playerManager, ZoneLinkRegistry::Mutexed& zoneLinkRegistry,
                                 Processor::Group<EProcessorId>& basicGroup,
                                 Processor::Group<EProcessorId>& dbGroup,
                                 std::string sharedSecret)
        : playerManager_(playerManager)
        , zoneLinkRegistry_(zoneLinkRegistry)
        , basicGroup_(basicGroup)
        , dbGroup_(dbGroup)
        , sharedSecret_(std::move(sharedSecret))
    {
        RegisterHandlers();
    }

    std::vector<Network::SessionId> ToolProcessor::SnapshotOnlineClients() const
    {
        // 목록을 먼저 값으로 떠서 락을 벗어난 뒤에 쓴다. 순회 중에 전송/주입을 하면 읽기 락을
        // 잡은 채로 오래 머무는 데다, 그 안에서 매니저를 다시 변경하면 교착한다.
        std::vector<Network::SessionId> targets;
        targets.reserve(playerManager_->Count());
        playerManager_->ForEach(
            [&targets](const Network::SessionId clientSessionId, const PlayerInfo&)
            {
                targets.push_back(clientSessionId);
            });
        return targets;
    }

    void ToolProcessor::RegisterHandlers()
    {
        dispatcher_.Register(PacketId::T2WHello, this, &ToolProcessor::HandleHello);
        dispatcher_.Register(PacketId::T2WNotice, this, &ToolProcessor::HandleNotice);
        dispatcher_.Register(PacketId::T2WMailSend, this, &ToolProcessor::HandleMailSend);
        dispatcher_.Register(PacketId::T2WMailDelete, this, &ToolProcessor::HandleMailDelete);
        dispatcher_.Register(PacketId::T2WCouponChunkPush, this, &ToolProcessor::HandleCouponChunkPush);
        dispatcher_.Register(PacketId::T2WClientList, this, &ToolProcessor::HandleClientList);
    }

    void ToolProcessor::OnSessionOpened(const std::shared_ptr<Network::Session>& session)
    {
        LOG.Info(ELogCategory::Tool, "운영툴 연결 수락(인증 대기)")
            .KV("SessionId", session->Id()).KV("Remote", session->RemoteAddress());
    }

    void ToolProcessor::OnPacket(const std::shared_ptr<Network::Session>& session,
                                 const Packet::Header& header,
                                 const std::span<const byte> payload)
    {
        // 여기는 이 연결의 I/O 스레드다. 다른 두 링크 핸들러와 동일하게 바이트만 복사해서
        // BASIC 그룹으로 넘긴다.
        //
        // **ownerId를 운영툴 세션 id로 잡는 이유**: 운영툴 명령의 "주인"은 대상이 아니라 명령을
        // 보낸 그 연결이다 -- 대상이 전역(공지)이거나 캠페인 코드(쿠폰)라 하나로 못 정하기
        // 때문이다. 이렇게 두면 한 운영툴 연결이 보낸 명령들끼리는 보낸 순서대로 처리된다.
        //
        // **대상의 상태는 PlayerManager가 Mutexed라 어느 레인에서 읽어도 안전하다.** 예전에는
        // 그 매니저가 어피니티 전제로 샤딩돼 있어서, 단일 대상 명령(우편 발송/삭제)이 운영툴
        // 레인에서 남의 샤드를 읽는 레이스가 있었다.
        const auto packetId = static_cast<PacketId>(header.id);
        std::vector<byte> payloadCopy(payload.begin(), payload.end());

        basicGroup_.Post(EProcessorId::Tool, session->Id(),
            [this, session, packetId, payloadCopy = std::move(payloadCopy)]
        {
            // ToolHello 이전에는 어떤 요청도 받지 않는다. requestId는 모든 요청 페이로드의 맨
            // 앞 4바이트로 고정이라, 본문 파싱 없이도 여기서 꺼내 거절 응답에 실을 수 있다.
            if (packetId != PacketId::T2WHello && !IsAuthenticated(session->Id()))
            {
                uint32_t requestId = 0;
                Packet::BinaryReader binaryReader(payloadCopy);
                (void)binaryReader.Read(requestId);

                LOG.Warning(ELogCategory::Tool, "인증 전 운영툴 요청 거절")
                    .KV("SessionId", session->Id()).KV("PacketId", static_cast<uint16_t>(packetId));
                SendCommandResult(session, requestId, EToolResultCode::NotAuthenticated, 0);
                return;
            }

            dispatcher_.Dispatch(packetId, session, payloadCopy);
        });
    }

    void ToolProcessor::OnClosed(const std::shared_ptr<Network::Session>& session, const std::error_code& /*reason*/)
    {
        const auto sessionId = session->Id();
        basicGroup_.Post(EProcessorId::Tool, sessionId, [this, sessionId]
        {
            authenticatedSessions_.Write()->erase(sessionId);
            LOG.Info(ELogCategory::Tool, "운영툴 연결 종료").KV("SessionId", sessionId);
        });
    }

    bool ToolProcessor::IsAuthenticated(const Network::SessionId toolSessionId) const
    {
        return authenticatedSessions_->contains(toolSessionId);
    }

    void ToolProcessor::SendCommandResult(const std::shared_ptr<Network::Session>& toolSession, const uint32_t requestId,
                                       const EToolResultCode resultCode, const uint32_t affectedCount) const
    {
        ToolCommandResultPacket ack{};
        ack.requestId = requestId;
        ack.resultCode = static_cast<uint16_t>(resultCode);
        ack.affectedCount = affectedCount;
        toolSession->SendPacket(PacketId::W2TCommandResult,
                                std::as_bytes(std::span(&ack, 1)));
    }

    bool ToolProcessor::InjectClientPacket(const Network::SessionId clientSessionId, const PacketId innerPacketId,
                                            const std::span<const byte> innerPayload) const
    {
        const auto client = playerManager_->Find(clientSessionId);
        if (!client)
        {
            return false;
        }

        const auto zoneLink = zoneLinkRegistry_->Find(client->zoneId);
        if (!zoneLink || !zoneLink->zoneSession)
        {
            return false;
        }

        // GatewayLinkHandler::HandleFromClient가 게이트웨이에서 받아 그대로 넘기는 것과 완전히
        // 같은 형태로 조립한다 -- 존 쪽에서는 이 우편이 운영툴에서 왔는지 클라이언트에서
        // 왔는지 구분할 수 없고, 구분할 필요도 없다.
        ClientEnvelopeHeader envelopeHeader{};
        envelopeHeader.clientSessionId = clientSessionId;
        envelopeHeader.innerPacketId = static_cast<uint16_t>(innerPacketId);

        Packet::BinaryWriter binaryWriter;
        binaryWriter.Write(envelopeHeader);
        binaryWriter.WriteBytes(innerPayload);
        zoneLink->zoneSession->SendPacket(PacketId::W2ZRelay, binaryWriter.GetBuffer());
        return true;
    }

    void ToolProcessor::HandleHello(const std::shared_ptr<Network::Session>& toolSession,
                                         const std::span<const byte> payload)
    {
        Packet::BinaryReader binaryReader(payload);
        uint32_t requestId{};
        uint32_t protocolVersion{};
        std::string sharedSecret;
        std::string operatorName;
        if (!binaryReader.Read(requestId) || !binaryReader.Read(protocolVersion)
            || !binaryReader.ReadString(sharedSecret) || !binaryReader.ReadString(operatorName))
        {
            LOG.Warning(ELogCategory::Tool, "ToolHello 파싱 실패").KV("SessionId", toolSession->Id());
            SendCommandResult(toolSession, requestId, EToolResultCode::BadRequest, 0);
            return;
        }

        const bool versionOk = protocolVersion == kToolLinkProtocolVersion;
        const bool secretOk = SecretEquals(sharedSecret, sharedSecret_);
        const bool accepted = versionOk && secretOk;

        if (accepted)
        {
            authenticatedSessions_.Write()->insert(toolSession->Id());
            LOG.Info(ELogCategory::Tool, "운영툴 인증 성공")
                .KV("SessionId", toolSession->Id()).KV("Operator", operatorName)
                .KV("ProtocolVersion", protocolVersion);
        }
        else
        {
            // 어느 쪽이 틀렸는지는 서버 로그에만 남긴다 -- 툴에게는 accepted=0만 돌려준다.
            LOG.Warning(ELogCategory::Tool, "운영툴 인증 실패")
                .KV("SessionId", toolSession->Id()).KV("Operator", operatorName)
                .KV("VersionOk", versionOk).KV("SecretOk", secretOk);
        }

        ToolHelloResultPacket ack{};
        ack.requestId = requestId;
        ack.accepted = accepted ? uint8_t{1} : uint8_t{0};
        ack.protocolVersion = kToolLinkProtocolVersion;
        toolSession->SendPacket(PacketId::W2THelloResult,
                                std::as_bytes(std::span(&ack, 1)));

        if (!accepted)
        {
            // 인증 실패한 연결은 붙잡아두지 않는다.
            toolSession->Close();
        }
    }

    void ToolProcessor::HandleNotice(const std::shared_ptr<Network::Session>& toolSession,
                                             const std::span<const byte> payload)
    {
        Packet::BinaryReader binaryReader(payload);
        uint32_t requestId{};
        std::string message;
        if (!binaryReader.Read(requestId) || !binaryReader.ReadString(message) || message.empty())
        {
            SendCommandResult(toolSession, requestId, EToolResultCode::BadRequest, 0);
            return;
        }

        Packet::BinaryWriter noticeBinaryWriter;
        noticeBinaryWriter.WriteString(message);
        const auto noticePayload = noticeBinaryWriter.GetBuffer();

        // **읽기 락 한 번으로 끝난다.** 매니저가 샤딩이던 시절에는 전부 순회할 수 있는
        // 스레드가 없어서 샤드마다 메시지를 던지고 합계를 취합해야 했다(ScatterToShards).
        uint32_t sentCount = 0;
        playerManager_->ForEach(
            [&](const Network::SessionId clientSessionId, const PlayerInfo& info)
            {
                if (!info.gatewaySession)
                {
                    return;
                }

                ClientEnvelopeHeader envelopeHeader{};
                envelopeHeader.clientSessionId = clientSessionId;
                envelopeHeader.innerPacketId = static_cast<uint16_t>(PacketId::W2CNotice);

                Packet::BinaryWriter envelopeBinaryWriter;
                envelopeBinaryWriter.Write(envelopeHeader);
                envelopeBinaryWriter.WriteBytes(noticePayload);
                info.gatewaySession->SendPacket(PacketId::W2GRelay, envelopeBinaryWriter.GetBuffer());
                ++sentCount;
            });

        LOG.Info(ELogCategory::Tool, "운영툴 공지 브로드캐스트")
            .KV("RequestId", requestId).KV("SentTo", sentCount).KV("Message", message);
        SendCommandResult(toolSession, requestId, EToolResultCode::Ok, sentCount);
    }

    void ToolProcessor::HandleMailSend(const std::shared_ptr<Network::Session>& toolSession,
                                               const std::span<const byte> payload)
    {
        Packet::BinaryReader binaryReader(payload);
        uint32_t requestId{};
        uint8_t targetKind{};
        uint64_t clientSessionId{};
        std::string title;
        std::string body;
        int64_t durationSec{};
        if (!binaryReader.Read(requestId) || !binaryReader.Read(targetKind) || !binaryReader.Read(clientSessionId)
            || !binaryReader.ReadString(title) || !binaryReader.ReadString(body) || !binaryReader.Read(durationSec))
        {
            SendCommandResult(toolSession, requestId, EToolResultCode::BadRequest, 0);
            return;
        }

        if (title.empty() || durationSec <= 0)
        {
            SendCommandResult(toolSession, requestId, EToolResultCode::BadRequest, 0);
            return;
        }

        // 존이 기대하는 MailAdd 본문(Instance::HandleMailAdd)과 정확히 같은 순서로 만든다:
        // String(title) + String(body) + durationSec(int64).
        Packet::BinaryWriter mailBinaryWriter;
        mailBinaryWriter.WriteString(title);
        mailBinaryWriter.WriteString(body);
        mailBinaryWriter.Write(durationSec);
        const auto mailPayload = mailBinaryWriter.GetBuffer();

        if (targetKind == kMailTargetAllOnline)
        {
            // **대상 목록을 먼저 스냅샷으로 뜬 뒤 락 밖에서 주입한다** --
            // InjectClientPacket이 매니저를 다시 조회하므로, 읽기 락을 잡은 채로 부르면
            // 같은 스레드가 락을 재진입하게 된다.
            uint32_t sentCount = 0;
            for (const auto target : SnapshotOnlineClients())
            {
                if (InjectClientPacket(target, PacketId::C2ZMailAdd, mailPayload))
                {
                    ++sentCount;
                }
            }

            LOG.Info(ELogCategory::Tool, "운영툴 우편 발송(접속 중 전체)")
                .KV("RequestId", requestId).KV("SentTo", sentCount)
                .KV("Title", title).KV("DurationSec", durationSec);
            SendCommandResult(toolSession, requestId, EToolResultCode::Ok, sentCount);
            return;
        }

        if (targetKind != kMailTargetSingle)
        {
            SendCommandResult(toolSession, requestId, EToolResultCode::BadRequest, 0);
            return;
        }

        if (!playerManager_->Find(clientSessionId))
        {
            LOG.Warning(ELogCategory::Tool, "운영툴 우편 대상이 접속 중이 아님")
                .KV("RequestId", requestId).KV("ClientSessionId", clientSessionId);
            SendCommandResult(toolSession, requestId, EToolResultCode::TargetNotFound, 0);
            return;
        }

        if (!InjectClientPacket(clientSessionId, PacketId::C2ZMailAdd, mailPayload))
        {
            SendCommandResult(toolSession, requestId, EToolResultCode::ZoneUnavailable, 0);
            return;
        }

        LOG.Info(ELogCategory::Tool, "운영툴 우편 발송(단일 대상)")
            .KV("RequestId", requestId).KV("ClientSessionId", clientSessionId)
            .KV("Title", title).KV("DurationSec", durationSec);
        SendCommandResult(toolSession, requestId, EToolResultCode::Ok, 1);
    }

    void ToolProcessor::HandleMailDelete(const std::shared_ptr<Network::Session>& toolSession,
                                                 const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(ToolMailDeletePacket))
        {
            SendCommandResult(toolSession, 0, EToolResultCode::BadRequest, 0);
            return;
        }

        ToolMailDeletePacket request{};
        std::memcpy(&request, payload.data(), sizeof(ToolMailDeletePacket));

        if (!playerManager_->Find(request.clientSessionId))
        {
            SendCommandResult(toolSession, request.requestId, EToolResultCode::TargetNotFound, 0);
            return;
        }

        // 존이 기대하는 MailDel 본문은 mailId(int64 RUID) 하나다(Instance::HandleMailDel).
        Packet::BinaryWriter mailBinaryWriter;
        mailBinaryWriter.Write(request.mailId);

        if (!InjectClientPacket(request.clientSessionId, PacketId::C2ZMailDel,
                                mailBinaryWriter.GetBuffer()))
        {
            SendCommandResult(toolSession, request.requestId, EToolResultCode::ZoneUnavailable, 0);
            return;
        }

        // 실제 삭제 성공 여부(그 mailId가 존재했는지)는 존이 MailDelAck으로 해당 클라이언트에게
        // 보내므로 운영툴은 알 수 없다 -- 여기서는 "존까지 전달됐다"까지만 보장한다. 운영툴에서
        // 결과까지 봐야 한다면 MailDelAck을 툴로도 되돌리는 경로가 따로 필요하다(현재 미구현).
        LOG.Info(ELogCategory::Tool, "운영툴 우편 삭제 요청 전달")
            .KV("RequestId", request.requestId).KV("ClientSessionId", request.clientSessionId)
            .KV("MailId", request.mailId);
        SendCommandResult(toolSession, request.requestId, EToolResultCode::Ok, 1);
    }

    void ToolProcessor::HandleCouponChunkPush(const std::shared_ptr<Network::Session>& toolSession,
                                               const std::span<const byte> payload)
    {
        Packet::BinaryReader binaryReader(payload);
        uint32_t requestId{};
        std::string campaignCode;
        uint32_t chunkSeq{};
        uint32_t couponCount{};
        if (!binaryReader.Read(requestId) || !binaryReader.ReadString(campaignCode)
            || !binaryReader.Read(chunkSeq) || !binaryReader.Read(couponCount))
        {
            SendCommandResult(toolSession, requestId, EToolResultCode::BadRequest, 0);
            return;
        }

        std::vector<std::string> couponCodes;
        couponCodes.reserve(couponCount);
        for (uint32_t i = 0; i < couponCount; ++i)
        {
            std::string code;
            if (!binaryReader.ReadString(code))
            {
                SendCommandResult(toolSession, requestId, EToolResultCode::BadRequest, 0);
                return;
            }
            couponCodes.push_back(std::move(code));
        }

        // 캠페인 코드로 해시해 고정된 DbWorker에 위임한다 -- 같은 캠페인의 청크는 항상 같은
        // 스레드에서 chunkSeq 순서대로 처리되므로 락이 필요 없다(ZoneLinkHandler의
        // UnitOfWork 태스크가 clientSessionId로 해시하는 것과 같은 owner-hash 원리).
        const auto ownerHash = std::hash<std::string>{}(campaignCode);
        dbGroup_.Post(EProcessorId::Db, ownerHash,
            [campaignCode, chunkSeq, couponCodes = std::move(couponCodes)]
            {
                // TODO: 실제로는 여기서 쿠폰 테이블에 벌크 INSERT를 실행한다(WorldServer의 DB
                // 연동 자체가 아직 TODO -- Db/DbWorker.h 주석 참고). 현재 쿠폰의 권위 저장소는
                // 운영툴 쪽 MySQL이고, 이 경로는 "게임 서버도 같은 청크를 순서대로 받아 적재할
                // 수 있다"는 구조만 미리 갖춰둔 것이다.
                LOG.Info(ELogCategory::Tool, "쿠폰 청크 수신 (DB 적재는 TODO)")
                    .KV("CampaignCode", campaignCode).KV("ChunkSeq", chunkSeq)
                    .KV("CouponCount", couponCodes.size())
                    .KV("FirstCode", couponCodes.empty() ? std::string{"(없음)"} : couponCodes.front());
            });

        SendCommandResult(toolSession, requestId, EToolResultCode::Ok, couponCount);
    }

    void ToolProcessor::HandleClientList(const std::shared_ptr<Network::Session>& toolSession,
                                                 const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(ToolClientListPacket))
        {
            SendCommandResult(toolSession, 0, EToolResultCode::BadRequest, 0);
            return;
        }

        ToolClientListPacket request{};
        std::memcpy(&request, payload.data(), sizeof(ToolClientListPacket));

        // 읽기 락 한 번으로 모은다. 샤딩이던 시절에는 샤드 스레드들이 공유 버퍼에 밀어 넣고
        // 마지막 스레드가 취합했는데, 그 취합 버퍼를 지키려고 여기에만 Mutexed가 하나 더
        // 있었다 -- 매니저 자체가 Mutexed가 되면서 둘 다 없어졌다.
        std::vector<ToolClientEntry> collected;
        uint32_t totalCount = 0;

        playerManager_->ForEach(
            [&](const Network::SessionId clientSessionId, const PlayerInfo& info)
            {
                ++totalCount;

                // MaxBodySize를 넘기면 받는 쪽 Buffer가 예외를 던지므로 상한에서 자른다.
                // 이 목록은 우편 대상을 고르기 위한 것이라 전수 조회가 필수는 아니다.
                if (collected.size() >= kMaxClientListEntries)
                {
                    return;
                }

                ToolClientEntry entry{};
                entry.clientSessionId = clientSessionId;
                entry.zoneId = info.zoneId;
                collected.push_back(entry);
            });

        Packet::BinaryWriter binaryWriter;
        binaryWriter.Write(request.requestId);
        binaryWriter.Write(static_cast<uint32_t>(collected.size()));
        for (const auto& entry : collected)
        {
            binaryWriter.Write(entry);
        }

        toolSession->SendPacket(PacketId::W2TClientList, binaryWriter.GetBuffer());

        LOG.Debug(ELogCategory::Tool, "운영툴 클라이언트 목록 응답")
            .KV("RequestId", request.requestId).KV("Total", totalCount)
            .KV("Returned", collected.size());
    }
}
