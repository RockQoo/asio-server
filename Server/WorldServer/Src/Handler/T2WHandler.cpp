#include "pch.h"
#include "Handler/T2WHandler.h"

#include "Processor/ProcessorIds.h"
#include "Processor/WorldMsg.h"
#include "Server/Core/Src/Pipeline/ProducerHolder.h"
#include "Server/Core/Src/Network/Session.h"

void T2WHandler::OnSessionOpened(const Network::Session::SPtr& session)
{
    LOG.Info(ELogCategory::Tool, "운영툴 연결 수락(인증 대기)")
        .KV("SessionId", session->Id()).KV("Remote", session->RemoteAddress());
}

void T2WHandler::OnPacket(const Network::Session::SPtr& session,
                          const Packet::Header& header,
                          const std::span<const byte> payload)
{
    // **owner = 운영툴 세션 id.** 명령의 주인은 대상이 아니라 명령을 보낸 그 연결이다
    // (대상이 전역이거나 캠페인 코드라 하나로 못 정한다).
    RecvStreamBody body{};
    body.session = session;
    body.packetId = static_cast<PacketId>(header.id);
    body.payload.assign(payload.begin(), payload.end());

    Pipeline::PushMsg<Pipeline::EProducerType::Basic>(
        EWorldMsg::OnRecvToolStream, Ids().tool, Pipeline::OwnerId{static_cast<int64_t>(session->Id())},
        std::move(body));
}

void T2WHandler::OnClosed(const Network::Session::SPtr& session, const std::error_code& /*reason*/)
{
    Pipeline::PushMsg<Pipeline::EProducerType::Basic>(
        EWorldMsg::OnToolLinkClosed, Ids().tool, Pipeline::OwnerId{static_cast<int64_t>(session->Id())});
}
