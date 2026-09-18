#include "pch.h"
#include "Handler/G2WHandler.h"

#include "Processor/ProcessorIds.h"
#include "Processor/WorldMsg.h"
#include "Server/Core/Src/Pipeline/ProducerHolder.h"
#include "Server/Core/Src/Network/Session.h"

void G2WHandler::OnSessionOpened(const Network::Session::SPtr& session)
{
    LOG.Info(ELogCategory::Gateway, "Gateway 연결 수락")
        .KV("SessionId", session->Id()).KV("Remote", session->RemoteAddress());
}

void G2WHandler::OnPacket(const Network::Session::SPtr& session,
                          const Packet::Header& header,
                          const std::span<const byte> payload)
{
    // 여기는 이 연결의 I/O 스레드(Session의 strand)다. **바이트만 넘긴다** -- 페이로드를
    // 들여다보기 시작하면 수신 자체가 그만큼 밀린다.
    RecvStreamBody body{};
    body.session = session;
    body.packetId = static_cast<PacketId>(header.id);
    body.payload.assign(payload.begin(), payload.end());

    Pipeline::PushMsg<Pipeline::EProducerType::Basic>(
        EWorldMsg::OnRecvStream, Ids().main, Pipeline::OwnerId{static_cast<int64_t>(session->Id())},
        std::move(body));
}

void G2WHandler::OnClosed(const Network::Session::SPtr& session, const std::error_code& /*reason*/)
{
    LOG.Info(ELogCategory::Gateway, "Gateway 연결 종료").KV("SessionId", session->Id());
}
