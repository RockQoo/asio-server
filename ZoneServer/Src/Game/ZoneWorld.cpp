#include "ZoneServer/Src/pch.h"
#include "ZoneServer/Src/Game/ZoneWorld.h"
#include "ZoneServer/Src/Packet/ZonePackets.h"
#include "ZoneServer/Src/World/WorldLink.h"
#include "ZoneServer/Src/Worker/BroadcastDispatcher.h"
#include "ZoneServer/Src/Mail/MailModel.h"
#include "ZoneServer/Src/Mail/MailRegistry.h"
#include "ZoneServer/Src/Mail/MailUnitOfWork.h"

#include "Core/Src/Network/Session.h"
#include "Core/Src/Packet/BinaryReader.h"
#include "Core/Src/Packet/BinaryWriter.h"
#include "WorldServer/Src/Packet/RelayEnvelope.h"
#include "WorldServer/Src/Packet/ZoneLinkPacketId.h"
#include "WorldServer/Src/Packet/ZoneLinkPackets.h"

#include <chrono>
#include <cstring>

namespace Zone
{
    ZoneWorld::ZoneWorld(const uint32_t zoneId, const float xMin, const float xMax,
                         WorldLink& worldLink, BroadcastDispatcher& broadcastDispatcher, Mail::MailRegistry& mailRegistry)
        : zoneId_(zoneId)
        , xMin_(xMin)
        , xMax_(xMax)
        , worldLink_(worldLink)
        , broadcastDispatcher_(broadcastDispatcher)
        , mailRegistry_(mailRegistry)
    {
        RegisterPacketHandlers();
    }

    void ZoneWorld::RegisterPacketHandlers()
    {
        packetDispatcher_.Register(PacketId::Move, [this](PlayerState* const& player, const std::span<const byte> payload)
        {
            HandleMove(*player, payload);
        });
        packetDispatcher_.Register(PacketId::Chat, [this](PlayerState* const& player, const std::span<const byte> payload)
        {
            HandleChat(*player, payload);
        });
        packetDispatcher_.Register(PacketId::MailAdd, [this](PlayerState* const& player, const std::span<const byte> payload)
        {
            HandleMailAdd(*player, payload);
        });
        packetDispatcher_.Register(PacketId::MailDel, [this](PlayerState* const& player, const std::span<const byte> payload)
        {
            HandleMailDel(*player, payload);
        });
    }

    void ZoneWorld::OnPlayerEnter(const Network::SessionId clientSessionId, const uint32_t playerId,
                                  const float x, const float y)
    {
        PlayerState state{};
        state.sessionId = clientSessionId;
        state.playerId = playerId;
        state.x = x;
        state.y = y;
        players_[clientSessionId] = state;

        mailRegistry_.Add(clientSessionId);

        LOG.Info(ELogCategory::Zone, "플레이어 입장")
            .KV("Zone", zoneId_).KV("ClientSessionId", clientSessionId).KV("Population", players_.size());

        EnterZoneNotifyPacket notify{};
        notify.playerId = playerId;
        notify.zoneId = zoneId_;
        SendToPlayer(clientSessionId, static_cast<uint16_t>(PacketId::EnterZoneNotify),
                     std::as_bytes(std::span(&notify, 1)));
    }

    void ZoneWorld::OnPlayerLeave(const Network::SessionId clientSessionId)
    {
        if (players_.erase(clientSessionId) > 0)
        {
            mailRegistry_.Remove(clientSessionId);
            LOG.Info(ELogCategory::Zone, "플레이어 퇴장")
                .KV("Zone", zoneId_).KV("ClientSessionId", clientSessionId).KV("Population", players_.size());
        }
    }

    void ZoneWorld::HandleClientPacket(const Network::SessionId clientSessionId, const uint16_t packetId,
                                        const std::span<const byte> payload)
    {
        // 1) Player를 먼저 찾는다 -- 아직 입장하지 않았거나 이미 퇴장한 세션의 패킷은 여기서
        // 버려진다.
        const auto it = players_.find(clientSessionId);
        if (it == players_.end())
        {
            return;
        }

        // 2) 패킷 종류별로 등록해둔 핸들러를 찾아 콜백한다.
        packetDispatcher_.Dispatch(static_cast<PacketId>(packetId), &it->second, payload);
    }

    void ZoneWorld::HandleMove(PlayerState& player, const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(MovePacket))
        {
            return;
        }
        MovePacket move{};
        std::memcpy(&move, payload.data(), sizeof(MovePacket));

        if (move.x < xMin_ || move.x >= xMax_)
        {
            // 존 경계를 넘었다 -- 로컬 상태를 먼저 지우고 World에 핸드오프를 요청한다. World가
            // 라우팅 테이블만 바꾸므로 Gateway는 이 사실을 아예 모르고, 클라이언트도 재접속
            // 없이 대상 존의 EnterZoneNotify만 새로 받는다. player는
            // players_의 값이므로 erase 이전에 필요한 값을 전부 복사해둔다(erase 이후엔 댕글링).
            const auto clientSessionId = player.sessionId;
            const auto playerId = player.playerId;
            players_.erase(clientSessionId);
            mailRegistry_.Remove(clientSessionId);
            RequestZoneTransfer(clientSessionId, playerId, move.x, move.y);
            return;
        }

        player.x = move.x;
        player.y = move.y;

        Packet::BinaryWriter writer;
        writer.Write(move);
        BroadcastToZone(static_cast<uint16_t>(PacketId::Move), writer.GetBuffer());
    }

    void ZoneWorld::HandleChat(const PlayerState& player, const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        std::string message;
        if (!reader.ReadString(message))
        {
            return;
        }

        Packet::BinaryWriter writer;
        writer.Write(static_cast<uint32_t>(player.sessionId));
        writer.WriteString(message);

        BroadcastToZone(static_cast<uint16_t>(PacketId::Chat), writer.GetBuffer());
    }

    void ZoneWorld::HandleMailAdd(const PlayerState& player, const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        std::string title;
        std::string body;
        int64_t durationSec{};
        if (!reader.ReadString(title) || !reader.ReadString(body) || !reader.Read(durationSec))
        {
            return;
        }

        const auto mailModel = mailRegistry_.Find(player.sessionId);
        if (!mailModel)
        {
            return;
        }

        const auto nowUt = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        Mail::MailInfo info{};
        info.title = std::move(title);
        info.body = std::move(body);
        info.sendUt = nowUt;
        info.endUt = nowUt + durationSec;

        auto unitOfWork = Mail::MakeMailUnitOfWork(worldLink_, player.sessionId, player.playerId);
        const auto assignedMailId = mailModel->Write()->AddMail(std::move(info), unitOfWork);

        MailAddAckPacket ack{};
        ack.mailId = assignedMailId;
        SendToPlayer(player.sessionId, static_cast<uint16_t>(PacketId::MailAddAck),
                     std::as_bytes(std::span(&ack, 1)));
    }

    void ZoneWorld::HandleMailDel(const PlayerState& player, const std::span<const byte> payload)
    {
        uint32_t mailId{};
        if (payload.size() < sizeof(mailId))
        {
            return;
        }
        std::memcpy(&mailId, payload.data(), sizeof(mailId));

        const auto mailModel = mailRegistry_.Find(player.sessionId);
        if (!mailModel)
        {
            return;
        }

        auto unitOfWork = Mail::MakeMailUnitOfWork(worldLink_, player.sessionId, player.playerId);
        const auto success = mailModel->Write()->DelMail(mailId, unitOfWork, false);

        MailDelAckPacket ack{};
        ack.mailId = mailId;
        ack.success = success ? 1 : 0;
        SendToPlayer(player.sessionId, static_cast<uint16_t>(PacketId::MailDelAck),
                     std::as_bytes(std::span(&ack, 1)));
    }

    void ZoneWorld::Tick(const float /*deltaSeconds*/)
    {
        // 존별 AI/물리/회복 등을 붙일 확장 지점. 지금은 의도적으로 아무 것도 하지 않는다.
        // 메일 만료 삭제는 이 tick이 아니라 별도 유지보수 타이머(Mail::MailExpiryService)가
        // 담당한다 -- ZoneServerApp 주석 참고.
    }

    void ZoneWorld::SendToPlayer(const Network::SessionId clientSessionId, const uint16_t innerPacketId,
                                 const std::span<const byte> payload) const
    {
        const auto worldSession = worldLink_.Get();
        if (!worldSession)
        {
            return;
        }

        World::ClientEnvelopeHeader header{};
        header.clientSessionId = clientSessionId;
        header.innerPacketId = innerPacketId;

        Packet::BinaryWriter writer;
        writer.Write(header);
        writer.WriteBytes(payload);
        worldSession->SendPacket(static_cast<uint16_t>(World::ZoneLinkPacketId::ForwardToWorld), writer.GetBuffer());
    }

    void ZoneWorld::BroadcastToZone(const uint16_t innerPacketId, const std::span<const byte> payload,
                                    const Network::SessionId excludeClientSessionId) const
    {
        // 대상 목록은 지금(players_를 소유한 유일한 스레드인 BASIC) 스냅샷으로 복사해서
        // BROADCAST 풀로 넘긴다 -- BROADCAST 스레드는 이 복사본만 갖고 전송만 할 뿐, players_
        // 맵을 직접 읽지 않는다(BASIC과 BROADCAST는 서로 다른 스레드라 공유 컨테이너를 같이
        // 만지면 안전하지 않다 -- BroadcastDispatcher 주석 참고).
        std::vector<Network::SessionId> targets;
        targets.reserve(players_.size());
        for (const auto& [clientSessionId, state] : players_)
        {
            if (clientSessionId != excludeClientSessionId)
            {
                targets.push_back(clientSessionId);
            }
        }

        std::vector<byte> payloadCopy(payload.begin(), payload.end());
        broadcastDispatcher_.Broadcast(zoneId_, std::move(targets), innerPacketId, std::move(payloadCopy));
    }

    void ZoneWorld::RequestZoneTransfer(const Network::SessionId clientSessionId, const uint32_t playerId,
                                        const float x, const float y) const
    {
        const auto worldSession = worldLink_.Get();
        if (!worldSession)
        {
            return;
        }

        World::PlayerZoneStatePacket state{};
        state.zoneId = zoneId_;  // 보내는 쪽(현재) 존 -- World 쪽 로그용, 라우팅은 x로 결정됨
        state.clientSessionId = clientSessionId;
        state.playerId = playerId;
        state.x = x;
        state.y = y;
        worldSession->SendPacket(static_cast<uint16_t>(World::ZoneLinkPacketId::ZoneTransferRequest),
                                 std::as_bytes(std::span(&state, 1)));

        LOG.Info(ELogCategory::Zone, "존 경계 넘음, World에 핸드오프 요청")
            .KV("Zone", zoneId_).KV("ClientSessionId", clientSessionId).KV("X", x).KV("Y", y);
    }
}
