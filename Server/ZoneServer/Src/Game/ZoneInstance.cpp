#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Game/ZoneInstance.h"
#include "Server/ZoneServer/Src/Packet/ZonePackets.h"
#include "Server/ZoneServer/Src/World/WorldLink.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Server/WorldServer/Src/Packet/RelayEnvelope.h"
#include "Server/WorldServer/Src/Packet/ZoneLinkPackets.h"
#include "Shared/Protocol/Src/PacketId.h"

#include <utility>

namespace Zone
{
    ZoneInstance::ZoneInstance(const ZoneDef& def, WorldLink& worldLink)
        : def_(def)
        , worldLink_(worldLink)
    {
        // 처음부터 비어 있는 스냅샷을 하나 걸어둔다 -- 읽는 쪽이 null 검사를 하지 않아도 되게.
        broadcastTargets_.store(std::make_shared<const std::vector<Network::SessionId>>(),
                                std::memory_order_release);
    }

    void ZoneInstance::OnPlayerEnter(const std::shared_ptr<Player>& player)
    {
        const auto clientSessionId = player->GetSessionId();
        members_[clientSessionId] = player;
        PublishBroadcastTargets();

        LOG.Info(ELogCategory::Zone, "플레이어 입장")
            .KV("Zone", def_.zoneId).KV("ClientSessionId", clientSessionId)
            .KV("Population", members_.size());

        SendEnterZoneNotify(clientSessionId, player->GetPlayerId());
    }

    void ZoneInstance::OnPlayerLeave(const Network::SessionId clientSessionId)
    {
        if (members_.erase(clientSessionId) > 0)
        {
            PublishBroadcastTargets();
            LOG.Info(ELogCategory::Zone, "플레이어 퇴장")
                .KV("Zone", def_.zoneId).KV("ClientSessionId", clientSessionId)
                .KV("Population", members_.size());
        }
    }

    void ZoneInstance::PublishBroadcastTargets()
    {
        auto targets = std::make_shared<std::vector<Network::SessionId>>();
        targets->reserve(members_.size());
        for (const auto& [clientSessionId, player] : members_)
        {
            targets->push_back(clientSessionId);
        }

        broadcastTargets_.store(std::move(targets), std::memory_order_release);
    }

    void ZoneInstance::Tick(const float /*deltaSeconds*/)
    {
        // 경계를 넘은 사람은 순회가 끝난 뒤에 처리한다. 이유가 둘이다:
        //   1) 순회 중 members_에서 지우면 반복자가 깨진다.
        //   2) 핸드오프 요청은 소켓 전송(= asio post)을 일으키는데, 그때 MoveModel 락을 쥐고
        //      있으면 안 된다(cpp-patterns의 "락을 쥔 채 post 금지").
        std::vector<std::pair<Network::SessionId, MovePacket>> crossed;

        for (const auto& [clientSessionId, player] : members_)
        {
            auto move = player->Move().Write();
            if (!move->HasRequest())
            {
                continue;
            }

            const auto requestedX = move->GetRequestedX();
            const auto requestedY = move->GetRequestedY();

            if (!def_.Contains(requestedX, requestedY))
            {
                // 아직 위치를 확정하지 않는다 -- 대상 존을 찾지 못해 World가 되돌려 보낼 수도
                // 있으므로, 확정은 새 존의 EnterZoneRequest가 도착할 때 Teleport로 한다.
                move->CancelRequest();
                crossed.emplace_back(clientSessionId, MovePacket{requestedX, requestedY});
                continue;
            }

            move->ApplyRequest();
        }

        for (const auto& [clientSessionId, target] : crossed)
        {
            const auto it = members_.find(clientSessionId);
            if (it == members_.end())
            {
                continue;
            }

            const auto playerId = it->second->GetPlayerId();
            members_.erase(it);
            PublishBroadcastTargets();

            RequestZoneTransfer(clientSessionId, playerId, target.x, target.y);
        }
    }

    void ZoneInstance::SendEnterZoneNotify(const Network::SessionId clientSessionId, const uint32_t playerId) const
    {
        const auto worldSession = worldLink_.Get();
        if (!worldSession)
        {
            return;
        }

        EnterZoneNotifyPacket notify{};
        notify.playerId = playerId;
        notify.zoneId = def_.zoneId;

        World::ClientEnvelopeHeader header{};
        header.clientSessionId = clientSessionId;
        header.innerPacketId = static_cast<uint16_t>(PacketId::Z2CEnterZoneNotify);

        Packet::BinaryWriter writer;
        writer.Write(header);
        writer.Write(notify);
        worldSession->SendPacket(PacketId::Z2WRelay, writer.GetBuffer());
    }

    void ZoneInstance::RequestZoneTransfer(const Network::SessionId clientSessionId, const uint32_t playerId,
                                            const float x, const float y) const
    {
        const auto worldSession = worldLink_.Get();
        if (!worldSession)
        {
            return;
        }

        World::PlayerZoneStatePacket state{};
        state.zoneId = def_.zoneId;  // 보내는 쪽(현재) 존 -- World 쪽 로그용, 라우팅은 좌표로 결정됨
        state.clientSessionId = clientSessionId;
        state.playerId = playerId;
        state.x = x;
        state.y = y;
        worldSession->SendPacket(PacketId::Z2WZoneTransferRequest,
                                 std::as_bytes(std::span(&state, 1)));

        LOG.Info(ELogCategory::Zone, "존 경계 넘음, World에 핸드오프 요청")
            .KV("Zone", def_.zoneId).KV("ClientSessionId", clientSessionId).KV("X", x).KV("Y", y);
    }
}
