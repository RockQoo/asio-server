#include "pch.h"
#include "Handler/GatewayLinkHandler.h"
#include "Login/LoginProcessor.h"
#include "World/PlayerManager.h"
#include "World/ZoneLinkRegistry.h"
#include "Packet/OwnerIdPeek.h"
#include "Packet/RelayEnvelope.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Packet/ZoneLinkPackets.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryReader.h"

namespace World
{
    GatewayLinkHandler::GatewayLinkHandler(PlayerManager::Mutexed& playerManager, ZoneLinkRegistry::Mutexed& zoneLinkRegistry,
                                            Processor::Group<EProcessorId>& basicGroup,
                                            LoginProcessor& loginProcessor)
        : playerManager_(playerManager)
        , zoneLinkRegistry_(zoneLinkRegistry)
        , basicGroup_(basicGroup)
        , loginProcessor_(loginProcessor)
    {
        RegisterHandlers();
    }

    void GatewayLinkHandler::RegisterHandlers()
    {
        dispatcher_.Register(PacketId::G2WClientConnected, this, &GatewayLinkHandler::HandleClientConnected);
        dispatcher_.Register(PacketId::G2WClientDisconnected, this, &GatewayLinkHandler::HandleClientDisconnected);
        dispatcher_.Register(PacketId::G2WRelay, this, &GatewayLinkHandler::HandleFromClient);
    }

    void GatewayLinkHandler::OnSessionOpened(const std::shared_ptr<Network::Session>& session)
    {
        LOG.Info(ELogCategory::Gateway, "Gateway 연결 수락")
            .KV("SessionId", session->Id()).KV("Remote", session->RemoteAddress());
    }

    void GatewayLinkHandler::OnPacket(const std::shared_ptr<Network::Session>& session,
                                      const Packet::Header& header,
                                      const std::span<const byte> payload)
    {
        // 여기는 이 연결의 I/O 스레드(Session의 strand)다. 세 패킷 모두 페이로드 맨 앞이
        // clientSessionId라, 그 8바이트만 훔쳐보고 그걸 ownerId로 삼아 BASIC 큐 그룹에 넣는다.
        // 와이어 포맷 해석(RegisterHandlers로 등록해둔 Handle* 메서드들)은 전부 배정된
        // 스레드에서 일어난다.
        const auto packetId = static_cast<PacketId>(header.id);

        const auto ownerId = PeekOwnerId<Network::SessionId>(payload);
        if (!ownerId)
        {
            LOG.Warning(ELogCategory::Gateway, "ownerId를 읽을 수 없는 패킷, 버림")
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

    void GatewayLinkHandler::OnClosed(const std::shared_ptr<Network::Session>& session, const std::error_code& /*reason*/)
    {
        LOG.Info(ELogCategory::Gateway, "Gateway 연결 종료").KV("SessionId", session->Id());
    }

    void GatewayLinkHandler::HandleClientConnected(const std::shared_ptr<Network::Session>& gatewaySession,
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

    void GatewayLinkHandler::HandleClientDisconnected(const std::shared_ptr<Network::Session>& /*gatewaySession*/,
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

        LeaveZoneNotifyPacket leave{};
        leave.clientSessionId = clientSessionId;
        zoneLink->zoneSession->SendPacket(PacketId::W2ZLeaveZone,
                                           std::as_bytes(std::span(&leave, 1)));

        LOG.Info(ELogCategory::Gateway, "클라이언트 접속 종료").KV("ClientSessionId", clientSessionId);
    }

    void GatewayLinkHandler::HandleFromClient(const std::shared_ptr<Network::Session>& gatewaySession,
                                               const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(ClientEnvelopeHeader))
        {
            return;
        }

        ClientEnvelopeHeader envelopeHeader{};
        std::memcpy(&envelopeHeader, payload.data(), sizeof(ClientEnvelopeHeader));

        const auto innerPacketId = static_cast<PacketId>(envelopeHeader.innerPacketId);

        // **C2W 대역만 World가 끝점이다.** 봉투를 벗겨 직접 처리하고 존으로 넘기지 않는다.
        // 대역으로 가르므로 로그인 말고 다른 C2W 패킷이 생겨도 이 분기는 그대로다.
        if (Protocol::DirectionOf(innerPacketId) == Protocol::EPacketDirection::C2W)
        {
            loginProcessor_.HandleClientPacket(gatewaySession, envelopeHeader.clientSessionId, innerPacketId,
                                               payload.subspan(sizeof(ClientEnvelopeHeader)));
            return;
        }

        // 여기부터는 예전과 같다 -- clientSessionId만 보고 나머지는 손대지 않은 채 Zone에
        // 재전송한다. World는 게임 패킷의 내용을 해석할 필요가 없다.
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
}
