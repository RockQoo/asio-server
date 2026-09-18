#include "pch.h"
#include "Processor/ZoneProcessor.h"

#include "Player/MailModel.h"
#include "Player/Player.h"

#include "Server/Core/Src/Network/Session.h"
#include "Server/Common/Src/Packet/Wire.h"
#include "Server/Common/Src/Packet/ZonePackets.h"

ZoneProcessor::ZoneProcessor(Zone::SPtr zone)
    : zone_(std::move(zone))
{
    worldDispatcher_.Register(this, &ZoneProcessor::HandleEnterZone);
    worldDispatcher_.Register(this, &ZoneProcessor::HandleLeaveZone);
}

void ZoneProcessor::RegistHandler()
{
    Regist(EZoneMsg::FromWorldStream, &ZoneProcessor::OnFromWorldStream);
}

void ZoneProcessor::OnFromWorldStream(const Pipeline::OwnerId& /*owner*/, const FromWorldStreamBody& body)
{
    // 클라이언트가 보낸 것이면 그 사람에게 내려보낸다. **여기서 Player 를 직접 찾지 않는
    // 이유**는 "누가 받는가"가 컨테이너의 일이고 "무엇을 하는가"가 그 유닛의 일이라서다.
    if (Common::DirectionOf(body.packetId) == Common::EPacketDirection::C2Z)
    {
        if (!zone_->Handle(body.playerId, body.packetId, body.payload))
        {
            // 아직 입장하지 않았거나 이미 퇴장한 사람의 패킷이다. 정상적으로 생기는
            // 경합이라 에러가 아니다 -- 다만 조용히 사라지면 원인을 못 찾으니 남긴다.
            LOG.Debug(ELogCategory::Zone, "존에 없는 사람의 패킷")
                .KV("Zone", zone_->GetZoneId()).KV("PlayerId", body.playerId)
                .KV("PacketId", static_cast<uint16_t>(body.packetId));
        }
        return;
    }

    // 존 자신에게 온 것(W2Z). 입장 패킷은 아직 Player 가 없는 쪽이라 여기서 갈린다.
    worldDispatcher_.Dispatch(body.packetId, body.playerId, body.payload);
}

void ZoneProcessor::HandleEnterZone(const Common::PlayerId& /*playerId*/,
                                    const Common::W2ZEnterZone& packet)
{
    const Network::SessionId clientSessionId = packet.head.clientSessionId;
    const auto playerId = packet.head.playerId;
    const Common::UnitId unitId{playerId.Value()};

    auto player = zone_->Units().FindPlayer(unitId);
    if (player)
    {
        // **이 존으로 되돌아왔다.** 경계를 넘었는데 World 가 갈 곳을 못 찾아 되돌려 보낸
        // 경우다. 살아 있는 Player 가 권위이므로 패킷에 실려 온 콘텐츠는 쓰지 않는다 --
        // 그것은 World 캐시의 사본이고, 아직 World 에 도달하지 않은 변경이 있으면 오히려
        // 과거로 되돌린다.
        player->Move().Write()->Teleport(packet.head.x, packet.head.y);
        player->EndZoneCrossing();
    }
    else
    {
        // 신규 입장이거나 다른 존에서 넘어온 것이다. **시작 상태는 전부 생성자로 들어간다** --
        // 존은 DB를 직접 읽지 않으므로 이 패킷(= World 캐시)이 유일한 출처다.
        // 패킷이 디스패처 소유라 move 하지 않는다. 입장 시 한 번이고 통 수가 바이트
        // 예산으로 잘려 있어(EnterZoneBody.h) 복사 비용이 크지 않다.
        auto mailBox = std::make_shared<MailModel::Mutexed>(packet.mails);

        player = std::make_shared<Player>(zone_->GetWorldLink(), clientSessionId, playerId,
                                          zone_->GetZoneId(), packet.head.x, packet.head.y,
                                          std::move(mailBox), packet.currencies);
        zone_->Units().AddUnit(player);
    }

    LOG.Info(ELogCategory::Zone, "플레이어 입장")
        .KV("Zone", zone_->GetZoneId()).KV("PlayerId", playerId)
        .KV("Population", zone_->Units().GetPlayerCount());

    SendEnterZoneNotify(clientSessionId, playerId);
}

void ZoneProcessor::HandleLeaveZone(const Common::PlayerId& playerId,
                                    const Common::W2ZLeaveZone& /*packet*/)
{
    const Common::UnitId unitId{playerId.Value()};

    const auto player = zone_->Units().FindPlayer(unitId);
    if (!player)
    {
        return;
    }

    // 존 이동 잠금을 먼저 푼다. 안 풀고 지우면, 같은 사람이 이 프로세스의 다른 존으로
    // 들어올 때 잠긴 유닛이 새로 만들어지는 게 아니라 **잠긴 채로 남은 것**을 찾게 된다.
    player->EndZoneCrossing();

    zone_->Units().RemoveUnit(unitId);

    LOG.Info(ELogCategory::Zone, "플레이어 퇴장")
        .KV("Zone", zone_->GetZoneId()).KV("PlayerId", playerId)
        .KV("Population", zone_->Units().GetPlayerCount());
}

void ZoneProcessor::SendEnterZoneNotify(const Network::SessionId clientSessionId,
                                        const Common::PlayerId playerId) const
{
    Common::Z2CEnterZoneNotify notify{};
    notify.playerId = playerId;
    notify.clientSessionId = clientSessionId;
    notify.zoneId = zone_->GetZoneId();

    Common::AssertDirection<Common::Z2CEnterZoneNotify, Common::EPacketDirection::Z2C>();
    zone_->SendToClient(clientSessionId, Common::Z2CEnterZoneNotify::kPacketId, Common::ToBytes(notify));
}
