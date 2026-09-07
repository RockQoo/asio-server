#include "Server/WorldServer/Src/pch.h"
#include "Server/WorldServer/Src/Tool/ToolProcessor.h"
#include "Server/WorldServer/Src/World/ClientRegistry.h"
#include "Server/WorldServer/Src/World/ZoneLinkRegistry.h"
#include "Server/WorldServer/Src/Worker/WorldWorker.h"
#include "Server/WorldServer/Src/Packet/RelayEnvelope.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Server/WorldServer/Src/Packet/ToolLinkPackets.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryReader.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"

// 운영툴이 주입하는 우편/공지는 "새로운 운영 전용 패킷"이 아니라 기존 클라이언트 패킷을
// 그대로 재사용한다(ToolProcessor.h 클래스 주석 참고). main.cpp도 notice REPL 때문에 같은
// 헤더를 include하고 있다.

#include <algorithm>
#include <string_view>

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

    ToolProcessor::ToolProcessor(ClientRegistry& clientRegistry, ZoneLinkRegistry& zoneLinkRegistry,
                                 Thread::AffinityWorkerPool<Db::DbWorker>& dbWorkers, WorldWorker& worldWorker,
                                 std::string sharedSecret)
        : clientRegistry_(clientRegistry)
        , zoneLinkRegistry_(zoneLinkRegistry)
        , dbWorkers_(dbWorkers)
        , worldWorker_(worldWorker)
        , sharedSecret_(std::move(sharedSecret))
    {
        RegisterHandlers();
    }

    void ToolProcessor::RegisterHandlers()
    {
        dispatcher_.Register(Protocol::PacketId::T2WToolHello,
            [this](const auto& session, const auto payload) { HandleToolHello(session, payload); });
        dispatcher_.Register(Protocol::PacketId::T2WNoticeRequest,
            [this](const auto& session, const auto payload) { HandleNoticeRequest(session, payload); });
        dispatcher_.Register(Protocol::PacketId::T2WMailSendRequest,
            [this](const auto& session, const auto payload) { HandleMailSendRequest(session, payload); });
        dispatcher_.Register(Protocol::PacketId::T2WMailDeleteRequest,
            [this](const auto& session, const auto payload) { HandleMailDeleteRequest(session, payload); });
        dispatcher_.Register(Protocol::PacketId::T2WCouponChunkPush,
            [this](const auto& session, const auto payload) { HandleCouponChunkPush(session, payload); });
        dispatcher_.Register(Protocol::PacketId::T2WClientListRequest,
            [this](const auto& session, const auto payload) { HandleClientListRequest(session, payload); });
    }

    void ToolProcessor::OnSessionOpened(const std::shared_ptr<Network::Session>& session)
    {
        LOG.Info(ELogCategory::Tool, "운영툴 연결 수락(인증 대기)")
            .KV("SessionId", session->Id()).KV("Remote", session->RemoteAddress());
    }

    void ToolProcessor::OnPacket(const std::shared_ptr<Network::Session>& session,
                                 const Packet::PacketHeader& header,
                                 const std::span<const byte> payload)
    {
        // 여기는 이 연결의 I/O 스레드다. GatewayLinkHandler/ZoneLinkHandler와 동일하게 바이트만
        // 복사해서 WorldWorker로 넘기고, 실제 레지스트리 접근은 그 스레드에서 한다.
        const auto packetId = static_cast<Protocol::PacketId>(header.id);
        std::vector<byte> payloadCopy(payload.begin(), payload.end());

        worldWorker_.PostTask([this, session, packetId, payloadCopy = std::move(payloadCopy)]
        {
            // ToolHello 이전에는 어떤 요청도 받지 않는다. requestId는 모든 요청 페이로드의 맨
            // 앞 4바이트로 고정이라, 본문 파싱 없이도 여기서 꺼내 거절 응답에 실을 수 있다.
            if (packetId != Protocol::PacketId::T2WToolHello && !IsAuthenticated(session->Id()))
            {
                uint32_t requestId = 0;
                Packet::BinaryReader reader(payloadCopy);
                (void)reader.Read(requestId);

                LOG.Warning(ELogCategory::Tool, "인증 전 운영툴 요청 거절")
                    .KV("SessionId", session->Id()).KV("PacketId", static_cast<uint16_t>(packetId));
                SendCommandAck(session, requestId, EToolResultCode::NotAuthenticated, 0);
                return;
            }

            dispatcher_.Dispatch(packetId, session, payloadCopy);
        });
    }

    void ToolProcessor::OnClosed(const std::shared_ptr<Network::Session>& session, const std::error_code& /*reason*/)
    {
        const auto sessionId = session->Id();
        worldWorker_.PostTask([this, sessionId]
        {
            authenticatedSessions_.erase(sessionId);
            LOG.Info(ELogCategory::Tool, "운영툴 연결 종료").KV("SessionId", sessionId);
        });
    }

    bool ToolProcessor::IsAuthenticated(const Network::SessionId toolSessionId) const
    {
        return authenticatedSessions_.contains(toolSessionId);
    }

    void ToolProcessor::SendCommandAck(const std::shared_ptr<Network::Session>& toolSession, const uint32_t requestId,
                                       const EToolResultCode resultCode, const uint32_t affectedCount) const
    {
        ToolCommandAckPacket ack{};
        ack.requestId = requestId;
        ack.resultCode = static_cast<uint16_t>(resultCode);
        ack.affectedCount = affectedCount;
        toolSession->SendPacket(Protocol::PacketId::W2TToolCommandAck,
                                std::as_bytes(std::span(&ack, 1)));
    }

    bool ToolProcessor::InjectClientPacket(const Network::SessionId clientSessionId, const Protocol::PacketId innerPacketId,
                                            const std::span<const byte> innerPayload) const
    {
        const auto client = clientRegistry_.Find(clientSessionId);
        if (!client)
        {
            return false;
        }

        const auto zoneLink = zoneLinkRegistry_.Find(client->zoneId);
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

        Packet::BinaryWriter writer;
        writer.Write(envelopeHeader);
        writer.WriteBytes(innerPayload);
        zoneLink->zoneSession->SendPacket(Protocol::PacketId::W2ZRelay, writer.GetBuffer());
        return true;
    }

    void ToolProcessor::HandleToolHello(const std::shared_ptr<Network::Session>& toolSession,
                                         const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        uint32_t requestId{};
        uint32_t protocolVersion{};
        std::string sharedSecret;
        std::string operatorName;
        if (!reader.Read(requestId) || !reader.Read(protocolVersion)
            || !reader.ReadString(sharedSecret) || !reader.ReadString(operatorName))
        {
            LOG.Warning(ELogCategory::Tool, "ToolHello 파싱 실패").KV("SessionId", toolSession->Id());
            SendCommandAck(toolSession, requestId, EToolResultCode::BadRequest, 0);
            return;
        }

        const bool versionOk = protocolVersion == kToolLinkProtocolVersion;
        const bool secretOk = SecretEquals(sharedSecret, sharedSecret_);
        const bool accepted = versionOk && secretOk;

        if (accepted)
        {
            authenticatedSessions_.insert(toolSession->Id());
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

        ToolHelloAckPacket ack{};
        ack.requestId = requestId;
        ack.accepted = accepted ? uint8_t{1} : uint8_t{0};
        ack.protocolVersion = kToolLinkProtocolVersion;
        toolSession->SendPacket(Protocol::PacketId::W2TToolHelloAck,
                                std::as_bytes(std::span(&ack, 1)));

        if (!accepted)
        {
            // 인증 실패한 연결은 붙잡아두지 않는다.
            toolSession->Close();
        }
    }

    void ToolProcessor::HandleNoticeRequest(const std::shared_ptr<Network::Session>& toolSession,
                                             const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        uint32_t requestId{};
        std::string message;
        if (!reader.Read(requestId) || !reader.ReadString(message) || message.empty())
        {
            SendCommandAck(toolSession, requestId, EToolResultCode::BadRequest, 0);
            return;
        }

        // WorldServerApp::BroadcastToAll과 하는 일은 같지만, 이미 WorldWorker 스레드 안이라
        // 다시 PostTask하지 않고 여기서 직접 순회한다 -- 그래야 몇 명에게 나갔는지를
        // affectedCount로 그대로 응답에 실을 수 있다(비동기로 넘기면 알 수 없다).
        Packet::BinaryWriter noticeWriter;
        noticeWriter.WriteString(message);
        const auto noticePayload = noticeWriter.GetBuffer();

        uint32_t sentCount = 0;
        clientRegistry_.ForEach([&](const Network::SessionId clientSessionId, const ClientInfo& info)
        {
            if (!info.gatewaySession)
            {
                return;
            }

            ClientEnvelopeHeader envelopeHeader{};
            envelopeHeader.clientSessionId = clientSessionId;
            envelopeHeader.innerPacketId = static_cast<uint16_t>(Protocol::PacketId::W2CNotice);

            Packet::BinaryWriter envelopeWriter;
            envelopeWriter.Write(envelopeHeader);
            envelopeWriter.WriteBytes(noticePayload);
            info.gatewaySession->SendPacket(Protocol::PacketId::W2GRelay,
                                            envelopeWriter.GetBuffer());
            ++sentCount;
        });

        LOG.Info(ELogCategory::Tool, "운영툴 공지 브로드캐스트")
            .KV("RequestId", requestId).KV("SentTo", sentCount).KV("Message", message);
        SendCommandAck(toolSession, requestId, EToolResultCode::Ok, sentCount);
    }

    void ToolProcessor::HandleMailSendRequest(const std::shared_ptr<Network::Session>& toolSession,
                                               const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        uint32_t requestId{};
        uint8_t targetKind{};
        uint64_t clientSessionId{};
        std::string title;
        std::string body;
        int64_t durationSec{};
        if (!reader.Read(requestId) || !reader.Read(targetKind) || !reader.Read(clientSessionId)
            || !reader.ReadString(title) || !reader.ReadString(body) || !reader.Read(durationSec))
        {
            SendCommandAck(toolSession, requestId, EToolResultCode::BadRequest, 0);
            return;
        }

        if (title.empty() || durationSec <= 0)
        {
            SendCommandAck(toolSession, requestId, EToolResultCode::BadRequest, 0);
            return;
        }

        // 존이 기대하는 MailAdd 본문(ZoneWorld::HandleMailAdd)과 정확히 같은 순서로 만든다:
        // String(title) + String(body) + durationSec(int64).
        Packet::BinaryWriter mailWriter;
        mailWriter.WriteString(title);
        mailWriter.WriteString(body);
        mailWriter.Write(durationSec);
        const auto mailPayload = mailWriter.GetBuffer();

        if (targetKind == kMailTargetAllOnline)
        {
            // 대상 목록을 먼저 스냅샷으로 뜬 뒤에 주입한다 -- InjectClientPacket이 다시
            // clientRegistry_를 조회하므로, ForEach 순회 중에 같은 컨테이너를 재진입 조회하는
            // 모양을 만들지 않기 위함이다(같은 스레드라 UB는 아니지만 읽기 좋지 않다).
            std::vector<Network::SessionId> targets;
            targets.reserve(clientRegistry_.Count());
            clientRegistry_.ForEach([&targets](const Network::SessionId clientSessionId, const ClientInfo&)
            {
                targets.push_back(clientSessionId);
            });

            uint32_t sentCount = 0;
            for (const auto target : targets)
            {
                if (InjectClientPacket(target, Protocol::PacketId::C2ZMailAdd, mailPayload))
                {
                    ++sentCount;
                }
            }

            LOG.Info(ELogCategory::Tool, "운영툴 우편 발송(접속 중 전체)")
                .KV("RequestId", requestId).KV("Targets", targets.size()).KV("SentTo", sentCount)
                .KV("Title", title).KV("DurationSec", durationSec);
            SendCommandAck(toolSession, requestId, EToolResultCode::Ok, sentCount);
            return;
        }

        if (targetKind != kMailTargetSingle)
        {
            SendCommandAck(toolSession, requestId, EToolResultCode::BadRequest, 0);
            return;
        }

        if (!clientRegistry_.Find(clientSessionId))
        {
            LOG.Warning(ELogCategory::Tool, "운영툴 우편 대상이 접속 중이 아님")
                .KV("RequestId", requestId).KV("ClientSessionId", clientSessionId);
            SendCommandAck(toolSession, requestId, EToolResultCode::TargetNotFound, 0);
            return;
        }

        if (!InjectClientPacket(clientSessionId, Protocol::PacketId::C2ZMailAdd, mailPayload))
        {
            SendCommandAck(toolSession, requestId, EToolResultCode::ZoneUnavailable, 0);
            return;
        }

        LOG.Info(ELogCategory::Tool, "운영툴 우편 발송(단일 대상)")
            .KV("RequestId", requestId).KV("ClientSessionId", clientSessionId)
            .KV("Title", title).KV("DurationSec", durationSec);
        SendCommandAck(toolSession, requestId, EToolResultCode::Ok, 1);
    }

    void ToolProcessor::HandleMailDeleteRequest(const std::shared_ptr<Network::Session>& toolSession,
                                                 const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(ToolMailDeleteRequestPacket))
        {
            SendCommandAck(toolSession, 0, EToolResultCode::BadRequest, 0);
            return;
        }

        ToolMailDeleteRequestPacket request{};
        std::memcpy(&request, payload.data(), sizeof(ToolMailDeleteRequestPacket));

        if (!clientRegistry_.Find(request.clientSessionId))
        {
            SendCommandAck(toolSession, request.requestId, EToolResultCode::TargetNotFound, 0);
            return;
        }

        // 존이 기대하는 MailDel 본문은 mailId(uint32) 하나다(ZoneWorld::HandleMailDel).
        Packet::BinaryWriter mailWriter;
        mailWriter.Write(request.mailId);

        if (!InjectClientPacket(request.clientSessionId, Protocol::PacketId::C2ZMailDel,
                                mailWriter.GetBuffer()))
        {
            SendCommandAck(toolSession, request.requestId, EToolResultCode::ZoneUnavailable, 0);
            return;
        }

        // 실제 삭제 성공 여부(그 mailId가 존재했는지)는 존이 MailDelAck으로 해당 클라이언트에게
        // 보내므로 운영툴은 알 수 없다 -- 여기서는 "존까지 전달됐다"까지만 보장한다. 운영툴에서
        // 결과까지 봐야 한다면 MailDelAck을 툴로도 되돌리는 경로가 따로 필요하다(현재 미구현).
        LOG.Info(ELogCategory::Tool, "운영툴 우편 삭제 요청 전달")
            .KV("RequestId", request.requestId).KV("ClientSessionId", request.clientSessionId)
            .KV("MailId", request.mailId);
        SendCommandAck(toolSession, request.requestId, EToolResultCode::Ok, 1);
    }

    void ToolProcessor::HandleCouponChunkPush(const std::shared_ptr<Network::Session>& toolSession,
                                               const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        uint32_t requestId{};
        std::string campaignCode;
        uint32_t chunkSeq{};
        uint32_t couponCount{};
        if (!reader.Read(requestId) || !reader.ReadString(campaignCode)
            || !reader.Read(chunkSeq) || !reader.Read(couponCount))
        {
            SendCommandAck(toolSession, requestId, EToolResultCode::BadRequest, 0);
            return;
        }

        std::vector<std::string> couponCodes;
        couponCodes.reserve(couponCount);
        for (uint32_t i = 0; i < couponCount; ++i)
        {
            std::string code;
            if (!reader.ReadString(code))
            {
                SendCommandAck(toolSession, requestId, EToolResultCode::BadRequest, 0);
                return;
            }
            couponCodes.push_back(std::move(code));
        }

        // 캠페인 코드로 해시해 고정된 DbWorker에 위임한다 -- 같은 캠페인의 청크는 항상 같은
        // 스레드에서 chunkSeq 순서대로 처리되므로 락이 필요 없다(ZoneLinkHandler의
        // UnitOfWork 태스크가 clientSessionId로 해시하는 것과 같은 owner-hash 원리).
        const auto ownerHash = std::hash<std::string>{}(campaignCode);
        dbWorkers_.GetWorker(ownerHash).PostTask(
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

        SendCommandAck(toolSession, requestId, EToolResultCode::Ok, couponCount);
    }

    void ToolProcessor::HandleClientListRequest(const std::shared_ptr<Network::Session>& toolSession,
                                                 const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(ToolClientListRequestPacket))
        {
            SendCommandAck(toolSession, 0, EToolResultCode::BadRequest, 0);
            return;
        }

        ToolClientListRequestPacket request{};
        std::memcpy(&request, payload.data(), sizeof(ToolClientListRequestPacket));

        std::vector<ToolClientEntry> entries;
        entries.reserve(std::min(clientRegistry_.Count(), kMaxClientListEntries));
        clientRegistry_.ForEach([&entries](const Network::SessionId clientSessionId, const ClientInfo& info)
        {
            // MaxBodySize를 넘기면 받는 쪽 PacketBuffer가 예외를 던지므로 상한에서 자른다.
            // 이 목록은 우편 대상을 고르기 위한 것이라 전수 조회가 필수는 아니다.
            if (entries.size() >= kMaxClientListEntries)
            {
                return;
            }

            ToolClientEntry entry{};
            entry.clientSessionId = clientSessionId;
            entry.zoneId = info.zoneId;
            entries.push_back(entry);
        });

        Packet::BinaryWriter writer;
        writer.Write(request.requestId);
        writer.Write(static_cast<uint32_t>(entries.size()));
        for (const auto& entry : entries)
        {
            writer.Write(entry);
        }

        toolSession->SendPacket(Protocol::PacketId::W2TClientListReply, writer.GetBuffer());

        LOG.Debug(ELogCategory::Tool, "운영툴 클라이언트 목록 응답")
            .KV("RequestId", request.requestId).KV("Total", clientRegistry_.Count())
            .KV("Returned", entries.size());
    }
}
