#include "pch.h"
#include "Handler/W2GHandler.h"
#include "Shared/Core/Src/Network/SessionHolder.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Network/SessionManager.h"
#include "Shared/Common/Src/Packet/RelayEnvelope.h"

W2GHandler::W2GHandler(Network::SessionManager& sessionManager, Network::SessionHolder& worldLink)
    : sessionManager_(sessionManager)
    , worldLink_(worldLink)
{
    Register();
}

void W2GHandler::Register()
{
    dispatcher_.Register(PacketId::W2GRelay, this, &W2GHandler::HandleToClient);
}

void W2GHandler::OnSessionOpened(const Network::Session::SPtr& session)
{
    worldLink_.Set(session);
    LOG.Info(ELogCategory::World, "World 연결 성공").KV("SessionId", session->Id());
}

void W2GHandler::OnPacket(const Network::Session::SPtr& session,
                                 const Packet::Header& header,
                                 const std::span<const byte> payload)
{
    dispatcher_.Dispatch(static_cast<PacketId>(header.id), session, payload);
}

void W2GHandler::OnClosed(const Network::Session::SPtr& /*session*/, const std::error_code& reason)
{
    // 최초 연결 실패는 Connector가 알아서 재시도하지만, 한 번 연결된 뒤 끊기는 경우의
    // 재연결은 학습 범위 밖으로 남겨둔다(재연결하려면 Connector를 다시 Start()해야 함).
    worldLink_.Clear();
    LOG.Warning(ELogCategory::World, "World 연결 끊김").KV("Message", reason.message());
}

void W2GHandler::HandleToClient(const Network::Session::SPtr& /*worldSession*/,
                                       const std::span<const byte> payload)
{
    const auto relay = Common::UnwrapRelay(payload);
    if (!relay)
    {
        return;
    }

    const auto clientSession = sessionManager_.Find(relay->envelope.clientSessionId);
    if (!clientSession)
    {
        return;
    }

    clientSession->SendPacket(relay->envelope.innerPacketId, relay->innerPayload);
}
