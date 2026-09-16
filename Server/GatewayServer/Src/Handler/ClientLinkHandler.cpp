#include "pch.h"
#include "Handler/ClientLinkHandler.h"
#include "World/WorldLink.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Network/SessionManager.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Server/WorldServer/Src/Packet/RelayEnvelope.h"
#include "Shared/Common/Src/PacketId.h"

namespace Gateway
{
    ClientLinkHandler::ClientLinkHandler(Network::SessionManager& sessionManager, WorldLink& worldLink)
        : sessionManager_(sessionManager)
        , worldLink_(worldLink)
    {
    }

    void ClientLinkHandler::OnSessionOpened(const Network::Session::SPtr& session)
    {
        sessionManager_.Add(session);

        const auto worldSession = worldLink_.Get();
        if (!worldSession)
        {
            LOG.Warning(ELogCategory::World, "World 연결이 아직 없어 접속 통지를 보내지 못함")
                .KV("SessionId", session->Id());
            return;
        }

        const Network::SessionId clientSessionId = session->Id();
        worldSession->SendPacket(PacketId::G2WClientConnected,
                                  std::as_bytes(std::span(&clientSessionId, 1)));

        LOG.Info(ELogCategory::Client, "클라이언트 접속").KV("SessionId", clientSessionId)
            .KV("Remote", session->RemoteAddress());
    }

    void ClientLinkHandler::OnPacket(const Network::Session::SPtr& session,
                                         const Packet::Header& header,
                                         const std::span<const byte> payload)
    {
        const auto worldSession = worldLink_.Get();
        if (!worldSession)
        {
            return;
        }

        World::ClientEnvelopeHeader envelopeHeader{};
        envelopeHeader.clientSessionId = session->Id();
        envelopeHeader.innerPacketId = header.id;

        Packet::BinaryWriter binaryWriter;
        binaryWriter.Write(envelopeHeader);
        binaryWriter.WriteBytes(payload);
        worldSession->SendPacket(PacketId::G2WRelay, binaryWriter.GetBuffer());
    }

    void ClientLinkHandler::OnClosed(const Network::Session::SPtr& session, const std::error_code& /*reason*/)
    {
        sessionManager_.Remove(session->Id());

        if (const auto worldSession = worldLink_.Get())
        {
            const Network::SessionId clientSessionId = session->Id();
            worldSession->SendPacket(PacketId::G2WClientDisconnected,
                                      std::as_bytes(std::span(&clientSessionId, 1)));
        }

        LOG.Info(ELogCategory::Client, "클라이언트 접속 종료").KV("SessionId", session->Id());
    }
}
