#include "Server/WorldServer/Src/pch.h"
#include "Server/WorldServer/Src/Handler/GatewayLinkHandler.h"
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
    GatewayLinkHandler::GatewayLinkHandler(ClientRegistry& clientRegistry, ZoneLinkRegistry& zoneLinkRegistry,
                                            WorldWorker& worldWorker)
        : clientRegistry_(clientRegistry)
        , zoneLinkRegistry_(zoneLinkRegistry)
        , worldWorker_(worldWorker)
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

    void GatewayLinkHandler::OnClosed(const std::shared_ptr<Network::Session>& session, const std::error_code& /*reason*/)
    {
        LOG.Info(ELogCategory::Gateway, "Gateway 연결 종료").KV("SessionId", session->Id());
    }

    void GatewayLinkHandler::HandleClientConnected(const std::shared_ptr<Network::Session>& gatewaySession,
                                                    const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        Network::SessionId clientSessionId{};
        if (!reader.Read(clientSessionId))
        {
            return;
        }

        clientRegistry_.Add(clientSessionId, gatewaySession);

        // 등록된 존 중 첫 존의 중앙으로 입장시킨다. 스폰 좌표를 상수로 박지 않는 이유는
        // ZoneLinkRegistry::FindEntryPoint 주석 참고.
        const auto entry = zoneLinkRegistry_.FindEntryPoint();
        if (!entry)
        {
            LOG.Warning(ELogCategory::Zone, "입장시킬 존이 아직 연결되지 않음")
                .KV("ClientSessionId", clientSessionId);
            return;
        }

        const auto zoneLink = zoneLinkRegistry_.Find(entry->zoneId);
        if (!zoneLink)
        {
            LOG.Warning(ELogCategory::Zone, "존 링크를 찾지 못함")
                .KV("ClientSessionId", clientSessionId).KV("ZoneId", entry->zoneId);
            return;
        }

        clientRegistry_.SetZone(clientSessionId, entry->zoneId);

        PlayerZoneStatePacket enterState{};
        enterState.zoneId = entry->zoneId;
        enterState.clientSessionId = clientSessionId;
        enterState.playerId = static_cast<uint32_t>(clientSessionId);
        enterState.x = entry->x;
        enterState.y = entry->y;
        zoneLink->zoneSession->SendPacket(PacketId::W2ZEnterZoneRequest,
                                           std::as_bytes(std::span(&enterState, 1)));

        LOG.Info(ELogCategory::Gateway, "클라이언트 접속, 입장 존 배정")
            .KV("ClientSessionId", clientSessionId).KV("ZoneId", entry->zoneId)
            .KV("X", entry->x).KV("Y", entry->y);
    }

    void GatewayLinkHandler::HandleClientDisconnected(const std::shared_ptr<Network::Session>& /*gatewaySession*/,
                                                       const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        Network::SessionId clientSessionId{};
        if (!reader.Read(clientSessionId))
        {
            return;
        }

        const auto client = clientRegistry_.Find(clientSessionId);
        clientRegistry_.Remove(clientSessionId);
        if (!client)
        {
            return;
        }

        const auto zoneLink = zoneLinkRegistry_.Find(client->zoneId);
        if (!zoneLink)
        {
            return;
        }

        LeaveZoneNotifyPacket leave{};
        leave.clientSessionId = clientSessionId;
        zoneLink->zoneSession->SendPacket(PacketId::W2ZLeaveZoneNotify,
                                           std::as_bytes(std::span(&leave, 1)));

        LOG.Info(ELogCategory::Gateway, "클라이언트 접속 종료").KV("ClientSessionId", clientSessionId);
    }

    void GatewayLinkHandler::HandleFromClient(const std::shared_ptr<Network::Session>& /*gatewaySession*/,
                                               const std::span<const byte> payload)
    {
        // 헤더의 clientSessionId만 들여다보고 나머지(innerPacketId+원본 바디)는 손대지 않은 채
        // 그대로 Zone에 재전송한다 -- World는 클라이언트 프로토콜 내용을 해석할 필요가 없다.
        if (payload.size() < sizeof(ClientEnvelopeHeader))
        {
            return;
        }

        ClientEnvelopeHeader envelopeHeader{};
        std::memcpy(&envelopeHeader, payload.data(), sizeof(ClientEnvelopeHeader));

        const auto client = clientRegistry_.Find(envelopeHeader.clientSessionId);
        if (!client)
        {
            return;
        }

        const auto zoneLink = zoneLinkRegistry_.Find(client->zoneId);
        if (!zoneLink)
        {
            return;
        }

        zoneLink->zoneSession->SendPacket(PacketId::W2ZRelay, payload);
    }
}
