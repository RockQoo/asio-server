#include "pch.h"
#include "Processor/ZoneNetworkProcessor.h"

#include "Processor/ProcessorIds.h"

#include "Server/Core/Src/Network/Session.h"
#include "Server/Core/Src/Pipeline/ProducerHolder.h"
#include "Server/Common/Src/Packet/ClientPackets.h"
#include "Server/Common/Src/Packet/RelayEnvelope.h"
#include "Server/Common/Src/Packet/Wire.h"
#include "Server/Common/Src/Packet/WorldPackets.h"
#include "Server/Common/Src/Packet/ZoneLinkPackets.h"
#include "Server/Common/Src/Packet/ZonePackets.h"

ZoneNetworkProcessor::ZoneNetworkProcessor(const Common::ZoneId zoneId, Network::SessionHolder& worldLink)
    : zoneId_(zoneId)
    , worldLink_(worldLink)
{
}

void ZoneNetworkProcessor::RegistHandler()
{
    Regist(EZoneMsg::OnRecvStream, &ZoneNetworkProcessor::OnRecvStream);
}

void ZoneNetworkProcessor::OnRecvStream(const Pipeline::OwnerId& /*owner*/, const RecvStreamBody& body)
{
    switch (body.packetId)
    {
    case PacketId::W2ZPlayerStream:
        {
            Common::W2ZPlayerStream packet;
            if (!packet.Parse(body.payload))
            {
                LOG.Warning(ELogCategory::Zone, "봉투가 덜 왔다, 버림")
                    .KV("Zone", zoneId_).KV("Size", body.payload.size());
                return;
            }

            const auto innerPacketId = static_cast<PacketId>(packet.envelope.innerPacketId);
            const Network::SessionId clientSessionId = packet.envelope.clientSessionId;

            if (innerPacketId == PacketId::C2ZEcho)
            {
                ReplyEcho(clientSessionId, packet.innerPayload);
                return;
            }

            PushToBasic(innerPacketId, clientSessionId,
                        Common::PlayerId{packet.envelope.playerId}, packet.innerPayload);
        }
        break;

    case PacketId::W2ZEnterZone:
        {
            // **머리만 훔쳐본다.** 뒤에 가변 길이 콘텐츠가 붙어 있는데, 여기서 필요한 것은
            // 주인뿐이라 전부 해석할 이유가 없다. 완전한 해석은 BASIC 이 한다.
            if (body.payload.size() < sizeof(Common::W2ZEnterZoneHead))
            {
                LOG.Warning(ELogCategory::Zone, "입장 패킷 머리가 덜 왔다, 버림")
                    .KV("Zone", zoneId_).KV("Size", body.payload.size());
                return;
            }

            Common::W2ZEnterZoneHead head{};
            std::memcpy(&head, body.payload.data(), sizeof(head));

            PushToBasic(body.packetId, head.clientSessionId, head.playerId, body.payload);
        }
        break;

    case PacketId::W2ZLeaveZone:
        {
            Common::W2ZLeaveZone packet{};
            if (!Common::FromBytes(packet, body.payload))
            {
                return;
            }

            PushToBasic(body.packetId, packet.clientSessionId, packet.playerId, body.payload);
        }
        break;

    default:
        LOG.Warning(ELogCategory::Zone, "존이 받을 줄 모르는 패킷, 버림")
            .KV("Zone", zoneId_).KV("PacketId", static_cast<uint16_t>(body.packetId));
        break;
    }
}

void ZoneNetworkProcessor::PushToBasic(const PacketId packetId, const Network::SessionId clientSessionId,
                                       const Common::PlayerId playerId,
                                       const std::span<const byte> payload) const
{
    const auto target = Ids().ZoneTarget(zoneId_);

    // **여기서 주인이 playerId 로 바뀐다.** 이 줄 앞까지는 그 존의 수신 전체가 레인 하나로
    // 직렬화돼 있었고, 이 줄 뒤부터 접속자 수만큼 갈라진다.
    Pipeline::PushMsg<Pipeline::EProducerType::Basic>(
        EZoneMsg::FromWorldStream, target.basic, Pipeline::OwnerId{playerId.Value()},
        FromWorldStreamBody{packetId, clientSessionId, playerId,
                            std::vector<byte>(payload.begin(), payload.end())});
}

void ZoneNetworkProcessor::ReplyEcho(const Network::SessionId clientSessionId,
                                     const std::span<const byte> innerPayload) const
{
    const auto worldSession = worldLink_.Get();
    if (!worldSession)
    {
        return;
    }

    // 요청과 응답이 같은 id 를 공유하지 않는 것이 패킷 id 규약이라 id 만 바꿔 되돌린다
    // (본문은 받은 것 그대로).
    Common::Z2CEcho packet;
    packet.Set(innerPayload);

    Common::SendRelay(worldSession, PacketId::Z2WRelay, clientSessionId, packet);
}
