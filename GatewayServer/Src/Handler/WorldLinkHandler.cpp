#include "GatewayServer/Src/pch.h"
#include "GatewayServer/Src/Handler/WorldLinkHandler.h"
#include "GatewayServer/Src/World/WorldLink.h"

#include "Core/Src/Network/Session.h"
#include "Core/Src/Network/SessionManager.h"
#include "WorldServer/Src/Packet/RelayEnvelope.h"

#include <cstring>

namespace Gateway
{
    WorldLinkHandler::WorldLinkHandler(Network::SessionManager& sessionManager, WorldLink& worldLink)
        : sessionManager_(sessionManager)
        , worldLink_(worldLink)
    {
        RegisterHandlers();
    }

    void WorldLinkHandler::RegisterHandlers()
    {
        dispatcher_.Register(World::GatewayLinkPacketId::ToClient,
            [this](const auto& session, const auto payload) { HandleToClient(session, payload); });
    }

    void WorldLinkHandler::OnSessionOpened(const std::shared_ptr<Network::Session>& session)
    {
        worldLink_.Set(session);
        LOG.Info(ELogCategory::World, "World 연결 성공").KV("SessionId", session->Id());
    }

    void WorldLinkHandler::OnPacket(const std::shared_ptr<Network::Session>& session,
                                     const Packet::PacketHeader& header,
                                     const std::span<const byte> payload)
    {
        dispatcher_.Dispatch(static_cast<World::GatewayLinkPacketId>(header.id), session, payload);
    }

    void WorldLinkHandler::OnClosed(const std::shared_ptr<Network::Session>& /*session*/, const std::error_code& reason)
    {
        // 최초 연결 실패는 Connector가 알아서 재시도하지만, 한 번 연결된 뒤 끊기는 경우의
        // 재연결은 학습 범위 밖으로 남겨둔다(재연결하려면 Connector를 다시 Start()해야 함).
        worldLink_.Clear();
        LOG.Warning(ELogCategory::World, "World 연결 끊김").KV("Message", reason.message());
    }

    void WorldLinkHandler::HandleToClient(const std::shared_ptr<Network::Session>& /*worldSession*/,
                                           const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(World::ClientEnvelopeHeader))
        {
            return;
        }

        World::ClientEnvelopeHeader envelopeHeader{};
        std::memcpy(&envelopeHeader, payload.data(), sizeof(World::ClientEnvelopeHeader));

        const auto clientSession = sessionManager_.Find(envelopeHeader.clientSessionId);
        if (!clientSession)
        {
            return;
        }

        const auto innerPayload = payload.subspan(sizeof(World::ClientEnvelopeHeader));
        clientSession->SendPacket(envelopeHeader.innerPacketId, innerPayload);
    }
}
