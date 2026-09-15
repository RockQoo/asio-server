#include "pch.h"
#include "Game/Instance.h"
#include "Packet/ZonePackets.h"
#include "Worker/BroadcastDispatcher.h"
#include "World/WorldLink.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Server/WorldServer/Src/Packet/RelayEnvelope.h"
#include "Server/WorldServer/Src/Packet/ZoneLinkPackets.h"
#include "Shared/Protocol/Src/PacketId.h"

namespace Zone
{
    Instance::Instance(const Def& def, WorldLink& worldLink, BroadcastDispatcher& broadcastDispatcher)
        : def_(def)
        , worldLink_(worldLink)
        , broadcastDispatcher_(broadcastDispatcher)
        , combat_(def.zoneId, *this)
    {
        // 처음부터 비어 있는 스냅샷을 하나 걸어둔다 -- 읽는 쪽이 null 검사를 하지 않아도 되게.
        broadcastTargets_.store(std::make_shared<const std::vector<Network::SessionId>>(),
                                std::memory_order_release);

        // 몬스터는 존이 만들어질 때 한 번 세우고 그대로 둔다. 죽어도 유닛은 남아 리스폰을
        // 기다리므로(Service::UpdateRespawn) 여기 말고는 스폰 경로가 없다.
        combat_.SpawnMonsters(def_);
    }

    void Instance::OnPlayerEnter(const std::shared_ptr<Player>& player)
    {
        const auto clientSessionId = player->GetSessionId();
        members_[clientSessionId] = player;
        PublishBroadcastTargets();

        LOG.Info(ELogCategory::Zone, "플레이어 입장")
            .KV("Zone", def_.zoneId).KV("ClientSessionId", clientSessionId)
            .KV("Population", members_.size());

        SendEnterZoneNotify(clientSessionId, player->GetPlayerId());

        // 전투 유닛은 **로스터에 들어온 뒤에** 만든다 -- 등장 통지가 브로드캐스트를 타는데
        // 그 대상 목록이 방금 갱신한 members_이기 때문이다(순서를 바꾸면 자기 자신이 빠진다).
        const auto move = player->Move().Read();
        combat_.OnPlayerEnter(clientSessionId, move->GetX(), move->GetY());
    }

    void Instance::OnPlayerLeave(const Network::SessionId clientSessionId)
    {
        if (members_.erase(clientSessionId) > 0)
        {
            PublishBroadcastTargets();
            LOG.Info(ELogCategory::Zone, "플레이어 퇴장")
                .KV("Zone", def_.zoneId).KV("ClientSessionId", clientSessionId)
                .KV("Population", members_.size());
        }

        // 로스터에 없어도 한 번 부른다 -- 틱이 경계를 넘긴 사람을 먼저 지우고 나면 위
        // erase가 0이지만 유닛은 아직 남아 있을 수 있다(Service 쪽이 멱등하다).
        combat_.OnPlayerLeave(clientSessionId);
    }

    void Instance::HandleAttack(const Network::SessionId clientSessionId, const AttackPacket& packet)
    {
        // 사거리 판정 직전에 시전자 좌표를 한 번 맞춘다. 틱에서만 옮기면 최대 한 틱(100ms)
        // 낡은 좌표로 판정하게 되는데, 이동 속도가 3 unit/s라 그 사이 0.3 unit이 벌어진다 --
        // 근접 사거리 1.2의 4분의 1이라 경계에서 "붙었는데 사거리 밖"이 체감된다.
        if (const auto it = members_.find(clientSessionId); it != members_.end())
        {
            const auto move = it->second->Move().Read();
            combat_.UpdatePlayerPosition(clientSessionId, move->GetX(), move->GetY());
        }

        combat_.HandleAttack(clientSessionId, packet);
    }

    void Instance::PublishBroadcastTargets()
    {
        auto targets = std::make_shared<std::vector<Network::SessionId>>();
        targets->reserve(members_.size());
        for (const auto& [clientSessionId, player] : members_)
        {
            targets->push_back(clientSessionId);
        }

        broadcastTargets_.store(std::move(targets), std::memory_order_release);
    }

    void Instance::Tick(const float deltaSeconds)
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

            // 이 존에서는 더 이상 보이지 않아야 한다. 새 존이 자기 유닛을 새로 만든다
            // (그래서 경계를 넘으면 HP가 최대치로 돌아간다 -- UnitRegistry::SpawnPlayer 주석).
            combat_.OnPlayerLeave(clientSessionId);

            RequestZoneTransfer(clientSessionId, playerId, target.x, target.y);
        }

        // 남아 있는 사람들의 확정 좌표를 유닛에 옮긴 뒤 전투를 굴린다. **순서가 중요하다** --
        // 먼저 굴리면 이번 틱에 이동한 결과가 사거리 판정에 한 틱 늦게 반영된다.
        for (const auto& [clientSessionId, player] : members_)
        {
            const auto move = player->Move().Read();
            combat_.UpdatePlayerPosition(clientSessionId, move->GetX(), move->GetY());
        }

        combat_.Tick(std::chrono::steady_clock::now(), deltaSeconds);
    }

    void Instance::SendToClient(const Network::SessionId clientSessionId, const PacketId innerPacketId,
                                const std::span<const byte> payload)
    {
        const auto worldSession = worldLink_.Get();
        if (!worldSession)
        {
            return;
        }

        World::ClientEnvelopeHeader header{};
        header.clientSessionId = clientSessionId;
        header.innerPacketId = static_cast<uint16_t>(innerPacketId);

        Packet::BinaryWriter binaryWriter;
        binaryWriter.Write(header);
        binaryWriter.WriteBytes(payload);
        worldSession->SendPacket(PacketId::Z2WRelay, binaryWriter.GetBuffer());
    }

    void Instance::BroadcastToZone(const PacketId innerPacketId, const std::span<const byte> payload,
                                   const Network::SessionId excludeClientSessionId)
    {
        if (members_.empty())
        {
            return;
        }

        std::vector<Network::SessionId> targets;
        targets.reserve(members_.size());
        for (const auto& [clientSessionId, player] : members_)
        {
            if (clientSessionId != excludeClientSessionId)
            {
                targets.push_back(clientSessionId);
            }
        }

        if (targets.empty())
        {
            return;
        }

        // 전송 자체는 브로드캐스트 레인이 한다 -- 인구가 많은 존의 팬아웃(대상 수만큼 프레임을
        // 쓴다)이 이 레인의 틱 주기를 붙잡으면 전투가 통째로 밀린다.
        broadcastDispatcher_.Broadcast(def_.zoneId, std::move(targets), innerPacketId,
                                       std::vector<byte>(payload.begin(), payload.end()));
    }

    void Instance::SendEnterZoneNotify(const Network::SessionId clientSessionId, const Protocol::PlayerId playerId) const
    {
        const auto worldSession = worldLink_.Get();
        if (!worldSession)
        {
            return;
        }

        EnterZoneNotifyPacket notify{};
        notify.playerId = playerId;
        notify.clientSessionId = clientSessionId;
        notify.zoneId = def_.zoneId;

        World::ClientEnvelopeHeader header{};
        header.clientSessionId = clientSessionId;
        header.innerPacketId = static_cast<uint16_t>(PacketId::Z2CEnterZoneNotify);

        Packet::BinaryWriter binaryWriter;
        binaryWriter.Write(header);
        binaryWriter.Write(notify);
        worldSession->SendPacket(PacketId::Z2WRelay, binaryWriter.GetBuffer());
    }

    void Instance::RequestZoneTransfer(const Network::SessionId clientSessionId, const Protocol::PlayerId playerId,
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
        worldSession->SendPacket(PacketId::Z2WZoneTransfer,
                                 std::as_bytes(std::span(&state, 1)));

        LOG.Info(ELogCategory::Zone, "존 경계 넘음, World에 핸드오프 요청")
            .KV("Zone", def_.zoneId).KV("ClientSessionId", clientSessionId).KV("X", x).KV("Y", y);
    }
}
