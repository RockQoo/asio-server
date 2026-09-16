#include "pch.h"
#include "Handler/C2GHandler.h"
#include "Shared/Core/Src/Network/SessionHolder.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Network/SessionManager.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Common/Src/Packet/RelayEnvelope.h"
#include "Shared/Common/Src/PacketId.h"

C2GHandler::C2GHandler(Network::SessionManager& sessionManager, Network::SessionHolder& worldLink)
    : sessionManager_(sessionManager)
    , worldLink_(worldLink)
{
}

void C2GHandler::OnSessionOpened(const Network::Session::SPtr& session)
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

void C2GHandler::OnPacket(const Network::Session::SPtr& session,
                                     const Packet::Header& header,
                                     const std::span<const byte> payload)
{
    const auto worldSession = worldLink_.Get();
    if (!worldSession)
    {
        return;
    }

    worldSession->SendPacket(PacketId::G2WRelay,
                             Common::WrapRelay(session->Id(), header.id, payload));
}

void C2GHandler::OnClosed(const Network::Session::SPtr& session, const std::error_code& /*reason*/)
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
