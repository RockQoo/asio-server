#include "pch.h"
#include "Handler/C2GHandler.h"
#include "Server/Core/Src/Network/SessionHolder.h"

#include "Server/Core/Src/Network/Session.h"
#include "Server/Core/Src/Network/SessionManager.h"
#include "Server/Core/Src/Packet/BinaryWriter.h"
#include "Server/Common/Src/Packet/RelayEnvelope.h"
#include "Server/Common/Src/PacketId.h"
#include "Server/Common/Src/Packet/LoginPackets.h"
#include "Server/Common/Src/Packet/Wire.h"

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

    Common::G2WClientConnected packet;
    packet.Set(session->Id());
    worldSession->SendPacket(packet.kPacketId, Common::ToBytes(packet));

    LOG.Info(ELogCategory::Client, "클라이언트 접속").KV("SessionId", session->Id())
        .KV("Remote", session->RemoteAddress());
}

void C2GHandler::OnPacket(const Network::Session::SPtr& session,
                                     const Packet::Header& header,
                                     const std::span<const byte> payload)
{
    // **클라이언트가 보낼 수 있는 대역인지만 본다.** 대역 산술 한 번이라 패킷이 늘어도
    // 이 코드는 안 바뀐다 -- 여기서 패킷별 크기 표를 들면 새 패킷마다 Gateway 도 같이
    // 고쳐야 하고, 잊으면 새 패킷이 여기서 조용히 막힌다.
    //
    // 이게 없으면 조작된 id(예: 클라이언트가 보낸 W2ZEnterZone)가 World 를 지나 Zone 까지
    // 가서야 "등록된 핸들러 없음"으로 버려진다. 세 홉을 태울 이유가 없다.
    if (!Common::IsClientPacket(header.id))
    {
        LOG.Warning(ELogCategory::Client, "클라이언트 대역이 아닌 패킷, 버림")
            .KV("ClientSessionId", session->Id()).KV("PacketId", header.id);
        return;
    }

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
        Common::G2WClientDisconnected packet;
        packet.Set(session->Id());
        worldSession->SendPacket(packet.kPacketId, Common::ToBytes(packet));
    }

    LOG.Info(ELogCategory::Client, "클라이언트 접속 종료").KV("SessionId", session->Id());
}
