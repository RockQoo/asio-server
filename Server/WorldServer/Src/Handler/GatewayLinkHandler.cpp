#include "pch.h"
#include "Handler/GatewayLinkHandler.h"

#include "Shared/Core/Src/Packet/OwnerIdPeek.h"
#include "Processor/MainProcessor.h"

#include "Shared/Core/Src/Network/Session.h"

namespace World
{
    GatewayLinkHandler::GatewayLinkHandler(Processor::Group<EProcessorId>& basicGroup,
                                            MainProcessor& mainProcessor)
        : basicGroup_(basicGroup)
        , mainProcessor_(mainProcessor)
    {
    }

    void GatewayLinkHandler::OnSessionOpened(const Network::Session::SPtr& session)
    {
        LOG.Info(ELogCategory::Gateway, "Gateway 연결 수락")
            .KV("SessionId", session->Id()).KV("Remote", session->RemoteAddress());
    }

    void GatewayLinkHandler::OnPacket(const Network::Session::SPtr& session,
                                      const Packet::Header& header,
                                      const std::span<const byte> payload)
    {
        // 여기는 이 연결의 I/O 스레드(Session의 strand)다. 세 패킷 모두 페이로드 맨 앞이
        // clientSessionId라, 그 8바이트만 훔쳐보고 그걸 ownerId로 삼아 BASIC 큐 그룹에 넣는다.
        // 와이어 포맷 해석은 전부 MainProcessor가 배정된 스레드에서 한다.
        const auto packetId = static_cast<PacketId>(header.id);

        const auto ownerId = Packet::PeekOwnerId<Network::SessionId>(payload);
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
                mainProcessor_.DispatchFromGateway(packetId, session, payloadCopy);
            });
    }

    void GatewayLinkHandler::OnClosed(const Network::Session::SPtr& session, const std::error_code& /*reason*/)
    {
        LOG.Info(ELogCategory::Gateway, "Gateway 연결 종료").KV("SessionId", session->Id());
    }
}
