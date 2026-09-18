#include "pch.h"
#include "Zone/Zone.h"

#include "Player/Player.h"
#include "Processor/ProcessorIds.h"
#include "Processor/ZoneMsg.h"

#include "Server/Core/Src/Network/Session.h"
#include "Server/Core/Src/Pipeline/ProducerHolder.h"
#include "Server/Common/Src/Packet/Wire.h"

Zone::Zone(const ZoneDef& def, Network::SessionHolder& worldLink)
    : def_(def)
    , worldLink_(worldLink)
{
}

void Zone::Tick(const UnitTickContext& context)
{
    unitContainer_.Tick(context);

    // 순회가 끝난 **뒤에** 확정한다 -- 이 줄이 위로 올라가면 병렬 순회 중에 대상 목록이
    // 바뀌고, 그 순간 raw 포인터가 매달린다.
    unitContainer_.OnEndTick();
}

void Zone::ReportCrossing(const Common::UnitId unitId, const float x, const float y)
{
    std::scoped_lock lock(crossingsMutex_);
    crossings_.push_back(ZoneCrossing{unitId, x, y});
}

std::vector<ZoneCrossing> Zone::TakeCrossings()
{
    std::vector<ZoneCrossing> taken;

    std::scoped_lock lock(crossingsMutex_);
    taken.swap(crossings_);

    return taken;
}

bool Zone::Handle(const Common::PlayerId playerId, const PacketId packetId,
                  const std::span<const byte> payload)
{
    const auto player = unitContainer_.FindPlayer(Common::UnitId{playerId.Value()});
    if (!player)
    {
        return false;
    }

    player->Handle(*this, packetId, payload);
    return true;
}

void Zone::Fanout(const PacketId innerPacketId, const std::span<const byte> payload,
                  const Network::SessionId excludeClientSessionId) const
{
    const auto target = Ids().ZoneTarget(def_.zoneId);
    if (!target.broadcast.IsValid())
    {
        return;
    }

    auto targets = unitContainer_.CollectClientSessionIds();
    std::erase(targets, excludeClientSessionId);
    if (targets.empty())
    {
        return;
    }

    Pipeline::PushMsg<Pipeline::EProducerType::Broadcast>(
        EZoneMsg::Fanout, target.broadcast, target.broadcastOwner,
        FanoutBody{std::move(targets), innerPacketId,
                   std::vector<byte>(payload.begin(), payload.end())});
}

void Zone::SendToClient(const Network::SessionId clientSessionId, const PacketId innerPacketId,
                        const std::span<const byte> payload) const
{
    const auto worldSession = worldLink_.Get();
    if (!worldSession)
    {
        return;
    }

    Common::SendRelay(worldSession, PacketId::Z2WRelay, clientSessionId, innerPacketId, payload);
}
