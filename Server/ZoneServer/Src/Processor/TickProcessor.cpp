#include "pch.h"
#include "Processor/TickProcessor.h"

#include "Server/Common/Src/Packet/RelayEnvelope.h"
#include "Server/Common/Src/Packet/Wire.h"
#include "Server/Common/Src/Packet/ZoneLinkPackets.h"
#include "Server/Common/Src/Packet/ZonePackets.h"
#include "Server/Common/Src/PacketId.h"
#include "Server/Core/Src/Network/Session.h"
#include "Server/Core/Src/Packet/BinaryWriter.h"

TickProcessor::TickProcessor(Zone::SPtr zone, Network::SessionHolder& worldLink)
    : zone_(std::move(zone))
    , worldLink_(worldLink)
{
}

void TickProcessor::RegistHandler()
{
    Regist(EZoneMsg::ZoneEnter, &TickProcessor::OnZoneEnter);
    Regist(EZoneMsg::ZoneLeave, &TickProcessor::OnZoneLeave);
    Regist(EZoneMsg::ZoneTick, &TickProcessor::OnZoneTick);
}

void TickProcessor::OnZoneEnter(const Pipeline::OwnerId& /*owner*/, const ZoneEnterBody& body)
{
    const auto clientSessionId = body.player->GetSessionId();
    zone_->AddMember(body.player);

    LOG.Info(ELogCategory::Zone, "플레이어 입장")
        .KV("Zone", zone_->GetZoneId()).KV("ClientSessionId", clientSessionId)
        .KV("Population", zone_->GetPlayerCount());

    SendEnterZoneNotify(clientSessionId, body.player->GetPlayerId());
}

void TickProcessor::OnZoneLeave(const Pipeline::OwnerId& /*owner*/, const ZoneLeaveBody& body)
{
    if (zone_->RemoveMember(body.clientSessionId))
    {
        LOG.Info(ELogCategory::Zone, "플레이어 퇴장")
            .KV("Zone", zone_->GetZoneId()).KV("ClientSessionId", body.clientSessionId)
            .KV("Population", zone_->GetPlayerCount());
    }
}

void TickProcessor::OnZoneTick(const Pipeline::OwnerId& /*owner*/, const ZoneTickBody& /*body*/)
{
    // 경계를 넘은 사람은 순회가 끝난 뒤에 처리한다. 이유가 둘이다:
    //   1) 순회 중 로스터에서 지우면 반복자가 깨진다.
    //   2) 핸드오프 요청은 소켓 전송(= asio post)을 일으키는데, 그때 MoveModel 락을 쥐고
    //      있으면 안 된다(cpp-patterns의 "락을 쥔 채 post 금지").
    std::vector<std::pair<Network::SessionId, Common::Position>> crossed;

    const auto& def = zone_->GetDef();

    for (const auto& [clientSessionId, player] : zone_->Members())
    {
        auto move = player->Move().Write();
        if (!move->HasRequest())
        {
            continue;
        }

        const auto requestedX = move->GetRequestedX();
        const auto requestedY = move->GetRequestedY();

        if (!def.Contains(requestedX, requestedY))
        {
            // 아직 위치를 확정하지 않는다 -- 대상 존을 찾지 못해 World가 되돌려 보낼 수도
            // 있으므로, 확정은 새 존의 입장 패킷이 도착할 때 Teleport로 한다.
            move->CancelRequest();
            crossed.emplace_back(clientSessionId, Common::Position{requestedX, requestedY});
            continue;
        }

        move->ApplyRequest();
    }

    for (const auto& [clientSessionId, target] : crossed)
    {
        const auto& members = zone_->Members();
        const auto found = members.find(clientSessionId);
        if (found == members.end())
        {
            continue;
        }

        const auto playerId = found->second->GetPlayerId();
        zone_->RemoveMember(clientSessionId);

        RequestZoneTransfer(clientSessionId, playerId, target.x, target.y);
    }
}

void TickProcessor::SendEnterZoneNotify(const Network::SessionId clientSessionId,
                                        const Common::PlayerId playerId) const
{
    const auto worldSession = worldLink_.Get();
    if (!worldSession)
    {
        return;
    }

    Common::Z2CEnterZoneNotify notify{};
    notify.playerId = playerId;
    notify.clientSessionId = clientSessionId;
    notify.zoneId = zone_->GetZoneId();

    Common::SendRelay(worldSession, PacketId::Z2WRelay, clientSessionId, notify);
}

void TickProcessor::RequestZoneTransfer(const Network::SessionId clientSessionId,
                                        const Common::PlayerId playerId,
                                        const float x, const float y) const
{
    const auto worldSession = worldLink_.Get();
    if (!worldSession)
    {
        return;
    }

    Common::Z2WZoneTransfer transfer{};
    transfer.zoneId = zone_->GetZoneId();  // 보내는 쪽(현재) 존 -- World 쪽 로그용, 라우팅은 좌표로 결정됨
    transfer.clientSessionId = clientSessionId;
    transfer.playerId = playerId;
    transfer.x = x;
    transfer.y = y;
    Common::SendPacket(worldSession, transfer);

    LOG.Info(ELogCategory::Zone, "존 경계 넘음, World에 핸드오프 요청")
        .KV("Zone", zone_->GetZoneId()).KV("ClientSessionId", clientSessionId).KV("X", x).KV("Y", y);
}
