#include "pch.h"
#include "Handler/Z2WHandler.h"

#include "Processor/ProcessorIds.h"
#include "Processor/WorldMsg.h"
#include "Server/Core/Src/Pipeline/ProducerHolder.h"
#include "Server/Core/Src/Network/Session.h"

void Z2WHandler::OnSessionOpened(const Network::Session::SPtr& session)
{
    LOG.Info(ELogCategory::Zone, "Zone 연결 수락")
        .KV("SessionId", session->Id()).KV("Remote", session->RemoteAddress());
}

void Z2WHandler::OnPacket(const Network::Session::SPtr& session,
                          const Packet::Header& header,
                          const std::span<const byte> payload)
{
    // 여기는 이 연결의 I/O 스레드(Session의 strand)다. **바이트만 넘긴다.**
    RecvStreamBody body{};
    body.session = session;
    body.packetId = static_cast<PacketId>(header.id);
    body.payload.assign(payload.begin(), payload.end());

    Pipeline::PushMsg<Pipeline::EProducerType::Basic>(
        EWorldMsg::OnRecvZoneStream, Ids().main, Pipeline::OwnerId{static_cast<int64_t>(session->Id())},
        std::move(body));
}

void Z2WHandler::OnClosed(const Network::Session::SPtr& session, const std::error_code& /*reason*/)
{
    // 종료 통지도 소켓 스레드에서 온다. 레지스트리를 여기서 직접 건드리지 않고 BASIC으로
    // 넘긴다 -- 주인은 끊긴 세션 자신이라, 그 링크의 등록/해제가 같은 레인에서 순서대로
    // 처리된다.
    Pipeline::PushMsg<Pipeline::EProducerType::Basic>(
        EWorldMsg::OnZoneLinkClosed, Ids().main, Pipeline::OwnerId{static_cast<int64_t>(session->Id())});
}
