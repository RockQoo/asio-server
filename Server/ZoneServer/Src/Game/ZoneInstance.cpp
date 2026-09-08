#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Game/ZoneInstance.h"
#include "Server/ZoneServer/Src/Packet/ZonePackets.h"
#include "Server/ZoneServer/Src/World/WorldLink.h"
#include "Server/ZoneServer/Src/Worker/BroadcastDispatcher.h"
#include "Server/ZoneServer/Src/Mail/MailModel.h"
#include "Server/ZoneServer/Src/Mail/MailRegistry.h"
#include "Server/ZoneServer/Src/Task/ZoneUnitOfWork.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryReader.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Server/WorldServer/Src/Packet/RelayEnvelope.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Shared/Protocol/Src/ErrorCode.h"
#include "Server/WorldServer/Src/Packet/ZoneLinkPackets.h"

#include <chrono>
#include <cstring>

namespace Zone
{
    ZoneInstance::ZoneInstance(const ZoneDef& def,
                         WorldLink& worldLink, BroadcastDispatcher& broadcastDispatcher, Mail::MailRegistry& mailRegistry)
        : def_(def)
        , zoneId_(def.zoneId)
        , worldLink_(worldLink)
        , broadcastDispatcher_(broadcastDispatcher)
        , mailRegistry_(mailRegistry)
    {
        RegisterPacketHandlers();
    }

    void ZoneInstance::RegisterPacketHandlers()
    {
        packetDispatcher_.Register(PacketId::C2ZMove, this, &ZoneInstance::HandleMove);
        packetDispatcher_.Register(PacketId::C2ZChat, this, &ZoneInstance::HandleChat);
        packetDispatcher_.Register(PacketId::C2ZMailAdd, this, &ZoneInstance::HandleMailAdd);
        packetDispatcher_.Register(PacketId::C2ZMailDel, this, &ZoneInstance::HandleMailDel);
        packetDispatcher_.Register(PacketId::C2ZMailBuy, this, &ZoneInstance::HandleMailBuy);
    }

    void ZoneInstance::OnPlayerEnter(const Network::SessionId clientSessionId, const uint32_t playerId,
                                  const float x, const float y)
    {
        // 우편함을 먼저 만들어야 그 핸들을 Player에 넣을 수 있다. 우편함이 레지스트리에도
        // 남는 이유는, 만료 스윕이 BASIC이 아닌 별도 스레드에서 돌아서 players_(락 없는 BASIC
        // 전용 컨테이너)를 순회할 수 없기 때문이다 -- 레지스트리가 그 스윕용 색인 역할을 한다.
        mailRegistry_.Add(clientSessionId);

        players_.try_emplace(clientSessionId, clientSessionId, playerId, x, y,
                             mailRegistry_.Find(clientSessionId));

        LOG.Info(ELogCategory::Zone, "플레이어 입장")
            .KV("Zone", zoneId_).KV("ClientSessionId", clientSessionId).KV("Population", players_.size());

        EnterZoneNotifyPacket notify{};
        notify.playerId = playerId;
        notify.zoneId = zoneId_;
        SendToPlayer(clientSessionId, PacketId::Z2CEnterZoneNotify,
                     std::as_bytes(std::span(&notify, 1)));
    }

    void ZoneInstance::OnPlayerLeave(const Network::SessionId clientSessionId)
    {
        if (players_.erase(clientSessionId) > 0)
        {
            mailRegistry_.Remove(clientSessionId);
            LOG.Info(ELogCategory::Zone, "플레이어 퇴장")
                .KV("Zone", zoneId_).KV("ClientSessionId", clientSessionId).KV("Population", players_.size());
        }
    }

    void ZoneInstance::HandleClientPacket(const Network::SessionId clientSessionId, const PacketId packetId,
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
        packetDispatcher_.Dispatch(packetId, &it->second, payload);
    }

    void ZoneInstance::HandleMove(Player& player, const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(MovePacket))
        {
            return;
        }
        MovePacket move{};
        std::memcpy(&move, payload.data(), sizeof(MovePacket));

        if (!def_.Contains(move.x, move.y))
        {
            // 존 경계를 넘었다 -- 로컬 상태를 먼저 지우고 World에 핸드오프를 요청한다. World가
            // 라우팅 테이블만 바꾸므로 Gateway는 이 사실을 아예 모르고, 클라이언트도 재접속
            // 없이 대상 존의 EnterZoneNotify만 새로 받는다. player는
            // players_의 값이므로 erase 이전에 필요한 값을 전부 복사해둔다(erase 이후엔 댕글링).
            const auto clientSessionId = player.GetSessionId();
            const auto playerId = player.GetPlayerId();
            players_.erase(clientSessionId);
            mailRegistry_.Remove(clientSessionId);
            RequestZoneTransfer(clientSessionId, playerId, move.x, move.y);
            return;
        }

        player.SetPosition(move.x, move.y);

        // 요청(C2ZMove)과 브로드캐스트(Z2CMoveNotify)는 id도 본문도 다르다 -- 받는 쪽은
        // "누가" 움직였는지 알아야 하므로 sessionId를 앞에 붙인다(Z2CChatNotify와 같은 형태).
        Packet::BinaryWriter writer;
        writer.Write(static_cast<uint32_t>(player.GetSessionId()));
        writer.Write(move);
        BroadcastToZone(PacketId::Z2CMoveNotify, writer.GetBuffer());
    }

    void ZoneInstance::HandleChat(const Player& player, const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        std::string message;
        if (!reader.ReadString(message))
        {
            return;
        }

        Packet::BinaryWriter writer;
        writer.Write(static_cast<uint32_t>(player.GetSessionId()));
        writer.WriteString(message);

        BroadcastToZone(PacketId::Z2CChatNotify, writer.GetBuffer());
    }

    void ZoneInstance::HandleMailAdd(const Player& player, const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        std::string title;
        std::string body;
        int64_t durationSec{};

        // UnitOfWork를 먼저 열어두는 이유: 파싱 실패도 "이 요청의 결말"이라 클라이언트에는
        // 같은 경로(Z2CTaskResult)로 에러가 돌아가야 한다. 스코프를 벗어나는 순간 소멸자가
        // 성공이면 전송, 실패면 역순 롤백까지 끝낸다 -- 별도의 커밋 호출이 없다.
        ZoneUnitOfWork unitOfWork(worldLink_, player.GetSessionId(), player.GetPlayerId(),
                                  PacketId::C2ZMailAdd);

        if (!reader.ReadString(title) || !reader.ReadString(body) || !reader.Read(durationSec))
        {
            unitOfWork.SetError(EErrorCode::InvalidPayload);
            return;
        }

        const auto& mailBox = player.GetMailBox();
        if (!mailBox)
        {
            unitOfWork.SetError(EErrorCode::MailBoxNotFound);
            return;
        }

        const auto nowUt = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        Mail::MailInfo info{};
        info.title = std::move(title);
        info.body = std::move(body);
        info.sendUt = nowUt;
        info.endUt = nowUt + durationSec;

        if (const auto errorCode = mailBox->Write()->AddMail(std::move(info), unitOfWork);
            errorCode != EErrorCode::Success)
        {
            unitOfWork.SetError(errorCode);
            return;
        }
    }

    void ZoneInstance::HandleMailDel(const Player& player, const std::span<const byte> payload)
    {
        ZoneUnitOfWork unitOfWork(worldLink_, player.GetSessionId(), player.GetPlayerId(),
                                  PacketId::C2ZMailDel);

        uint32_t mailId{};
        if (payload.size() < sizeof(mailId))
        {
            unitOfWork.SetError(EErrorCode::InvalidPayload);
            return;
        }
        std::memcpy(&mailId, payload.data(), sizeof(mailId));

        const auto& mailBox = player.GetMailBox();
        if (!mailBox)
        {
            unitOfWork.SetError(EErrorCode::MailBoxNotFound);
            return;
        }

        if (const auto errorCode = mailBox->Write()->DelMail(mailId, unitOfWork, false);
            errorCode != EErrorCode::Success)
        {
            unitOfWork.SetError(errorCode);
            return;
        }
    }

    void ZoneInstance::HandleMailBuy(Player& player, const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        std::string title;
        std::string body;
        int64_t durationSec{};
        int64_t price{};

        ZoneUnitOfWork unitOfWork(worldLink_, player.GetSessionId(), player.GetPlayerId(),
                                  PacketId::C2ZMailBuy);

        if (!reader.ReadString(title) || !reader.ReadString(body) || !reader.Read(durationSec)
            || !reader.Read(price))
        {
            unitOfWork.SetError(EErrorCode::InvalidPayload);
            return;
        }

        const auto& mailBox = player.GetMailBox();
        if (!mailBox)
        {
            unitOfWork.SetError(EErrorCode::MailBoxNotFound);
            return;
        }

        const auto nowUt = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        Mail::MailInfo info{};
        info.title = std::move(title);
        info.body = std::move(body);
        info.sendUt = nowUt;
        info.endUt = nowUt + durationSec;

        // 우편을 **먼저** 넣고 골드를 나중에 깎는 순서가 중요하다 -- 잔액이 부족하면 이미
        // 들어간 우편을 되돌려야 하고, 그게 이 프로젝트에서 역순 롤백이 실제로 밟히는
        // 유일한 경로다(단일 모델 요청은 실패 시점에 되돌릴 것이 없다).
        if (const auto errorCode = mailBox->Write()->AddMail(std::move(info), unitOfWork);
            errorCode != EErrorCode::Success)
        {
            unitOfWork.SetError(errorCode);
            return;
        }

        if (const auto errorCode = player.GetWallet().DecCurrency(ECurrencyType::Gold, price, unitOfWork);
            errorCode != EErrorCode::Success)
        {
            unitOfWork.SetError(errorCode);
            return;
        }
    }

    void ZoneInstance::Tick(const float /*deltaSeconds*/)
    {
        // 존별 AI/물리/회복 등을 붙일 확장 지점. 지금은 의도적으로 아무 것도 하지 않는다.
        // 메일 만료 삭제는 이 tick이 아니라 별도 유지보수 타이머(Mail::MailExpiryService)가
        // 담당한다 -- ZoneServerApp 주석 참고.
    }

    void ZoneInstance::SendToPlayer(const Network::SessionId clientSessionId, const PacketId innerPacketId,
                                 const std::span<const byte> payload) const
    {
        const auto worldSession = worldLink_.Get();
        if (!worldSession)
        {
            return;
        }

        World::ClientEnvelopeHeader header{};
        header.clientSessionId = clientSessionId;
        header.innerPacketId = static_cast<uint16_t>(innerPacketId);

        Packet::BinaryWriter writer;
        writer.Write(header);
        writer.WriteBytes(payload);
        worldSession->SendPacket(PacketId::Z2WRelay, writer.GetBuffer());
    }

    void ZoneInstance::BroadcastToZone(const PacketId innerPacketId, const std::span<const byte> payload,
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

    void ZoneInstance::RequestZoneTransfer(const Network::SessionId clientSessionId, const uint32_t playerId,
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
        worldSession->SendPacket(PacketId::Z2WZoneTransferRequest,
                                 std::as_bytes(std::span(&state, 1)));

        LOG.Info(ELogCategory::Zone, "존 경계 넘음, World에 핸드오프 요청")
            .KV("Zone", zoneId_).KV("ClientSessionId", clientSessionId).KV("X", x).KV("Y", y);
    }
}
