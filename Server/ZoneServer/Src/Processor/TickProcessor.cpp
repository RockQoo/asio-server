#include "pch.h"
#include "Processor/TickProcessor.h"

#include "Player/Player.h"

#include "Server/Core/Src/Network/Session.h"
#include "Server/Common/Src/Packet/Wire.h"
#include "Server/Common/Src/Packet/ZoneLinkPackets.h"
#include "Server/Common/Src/PacketId.h"

TickProcessor::TickProcessor(Zone::SPtr zone)
    : zone_(std::move(zone))
{
}

void TickProcessor::RegistHandler()
{
    Regist(EZoneMsg::ZoneTick, &TickProcessor::OnZoneTick);
}

void TickProcessor::OnZoneTick(const Pipeline::OwnerId& /*owner*/, const ZoneTickBody& body)
{
    const UnitTickContext context{body.nowUt, body.deltaSeconds, zone_.get()};
    zone_->Tick(context);

    // 경계를 넘은 사람은 **순회가 끝난 뒤에** 처리한다. 핸드오프 요청은 소켓 전송을
    // 일으키는데, 병렬 순회 중에 그걸 하면 MoveModel 락을 쥔 채 post 하게 된다.
    for (const auto& crossing : zone_->TakeCrossings())
    {
        RequestZoneTransfer(crossing.unitId, crossing.x, crossing.y);
    }
}

void TickProcessor::RequestZoneTransfer(const Common::UnitId unitId, const float x, const float y) const
{
    const auto player = zone_->Units().FindPlayer(unitId);
    if (!player)
    {
        // 신고와 여기 사이에 퇴장했다. 존 이동 잠금도 그때 같이 풀렸다.
        return;
    }

    const auto worldSession = zone_->GetWorldLink().Get();
    if (!worldSession)
    {
        // 링크가 끊겨 요청을 못 보냈다. 잠금을 풀어 다음 틱에 다시 시도하게 한다 --
        // 안 풀면 이 사람은 링크가 돌아와도 영영 경계에 갇힌다.
        player->EndZoneCrossing();
        return;
    }

    Common::Z2WZoneTransfer packet{};
    packet.zoneId = zone_->GetZoneId();   // 보내는 쪽 존이다(목표가 아니다)
    packet.clientSessionId = static_cast<uint64_t>(player->GetClientSessionId());
    packet.playerId = player->GetPlayerId();
    packet.x = x;
    packet.y = y;

    Common::SendPacket(worldSession, packet);

    LOG.Debug(ELogCategory::Zone, "존 이동 요청")
        .KV("Zone", zone_->GetZoneId()).KV("PlayerId", player->GetPlayerId())
        .KV("X", x).KV("Y", y);
}
