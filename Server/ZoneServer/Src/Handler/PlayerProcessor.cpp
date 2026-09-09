#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Handler/PlayerProcessor.h"
#include "Server/ZoneServer/Src/Game/ZoneInstance.h"
#include "Server/ZoneServer/Src/Mail/MailModel.h"
#include "Server/ZoneServer/Src/Mail/MailRegistry.h"
#include "Server/ZoneServer/Src/Packet/ZonePackets.h"
#include "Server/ZoneServer/Src/Task/ZoneUnitOfWork.h"
#include "Server/ZoneServer/Src/World/WorldLink.h"
#include "Server/ZoneServer/Src/Worker/BroadcastDispatcher.h"
#include "Server/ZoneServer/Src/Worker/ZoneWorkerManager.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryReader.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Server/WorldServer/Src/Packet/RelayEnvelope.h"
#include "Shared/Protocol/Src/ErrorCode.h"
#include "Shared/Protocol/Src/PacketId.h"

#include <chrono>
#include <cstring>
#include <utility>

namespace Zone
{
    PlayerProcessor::PlayerProcessor(PlayerRegistry& playerRegistry, ZoneWorkerManager& zoneWorkers,
                                     BroadcastDispatcher& broadcastDispatcher, WorldLink& worldLink,
                                     Mail::MailRegistry& mailRegistry)
        : playerRegistry_(playerRegistry)
        , zoneWorkers_(zoneWorkers)
        , broadcastDispatcher_(broadcastDispatcher)
        , worldLink_(worldLink)
        , mailRegistry_(mailRegistry)
    {
        RegisterPacketHandlers();
    }

    void PlayerProcessor::RegisterPacketHandlers()
    {
        packetDispatcher_.Register(PacketId::C2ZMove, this, &PlayerProcessor::HandleMove);
        packetDispatcher_.Register(PacketId::C2ZChat, this, &PlayerProcessor::HandleChat);
        packetDispatcher_.Register(PacketId::C2ZMailAdd, this, &PlayerProcessor::HandleMailAdd);
        packetDispatcher_.Register(PacketId::C2ZMailDel, this, &PlayerProcessor::HandleMailDel);
        packetDispatcher_.Register(PacketId::C2ZMailBuy, this, &PlayerProcessor::HandleMailBuy);
    }

    void PlayerProcessor::OnPlayerEnter(const Network::SessionId clientSessionId, const uint32_t playerId,
                                        const uint32_t zoneId, const float x, const float y)
    {
        // 핸드오프로 다시 들어온 경우 이미 이 프로세스에 Player가 있을 수 있다. 그때는 우편함을
        // 새로 만들지 않고 위치만 옮긴다 -- 우편함을 다시 만들면 그 사람의 우편이 사라진다.
        auto player = playerRegistry_.Find(clientSessionId);
        if (player)
        {
            // 같은 프로세스 안에서의 핸드오프(가로 이동)다. 우편함을 새로 만들면 그 사람의
            // 우편이 사라지므로, 이전 존의 로스터에서 빼고 위치와 소속만 옮긴다.
            const auto previousZoneId = player->GetZoneId();
            if (previousZoneId != zoneId)
            {
                zoneWorkers_.PostToZone(previousZoneId, [this, previousZoneId, clientSessionId]
                {
                    if (zoneWorkers_.HasZone(previousZoneId))
                    {
                        zoneWorkers_.GetZoneInstance(previousZoneId).OnPlayerLeave(clientSessionId);
                    }
                });
            }

            player->SetZoneId(zoneId);
            player->Move().Write()->Teleport(x, y);
        }
        else
        {
            mailRegistry_.Add(clientSessionId);
            player = std::make_shared<Player>(clientSessionId, playerId, zoneId, x, y,
                                              mailRegistry_.Find(clientSessionId));
            playerRegistry_.Add(player);
        }

        // 로스터는 존 레인 소유다 -- 여기서 직접 넣지 않고 그 존의 스레드로 넘긴다.
        zoneWorkers_.PostToZone(zoneId, [this, zoneId, player]
        {
            zoneWorkers_.GetZoneInstance(zoneId).OnPlayerEnter(player);
        });
    }

    void PlayerProcessor::OnPlayerLeave(const Network::SessionId clientSessionId)
    {
        const auto player = playerRegistry_.Find(clientSessionId);
        if (!player)
        {
            return;
        }

        const auto zoneId = player->GetZoneId();
        playerRegistry_.Remove(clientSessionId);
        mailRegistry_.Remove(clientSessionId);

        zoneWorkers_.PostToZone(zoneId, [this, zoneId, clientSessionId]
        {
            if (zoneWorkers_.HasZone(zoneId))
            {
                zoneWorkers_.GetZoneInstance(zoneId).OnPlayerLeave(clientSessionId);
            }
        });
    }

    void PlayerProcessor::HandleClientPacket(const Network::SessionId clientSessionId,
                                             const PacketId packetId, const std::span<const byte> payload)
    {
        const auto player = playerRegistry_.Find(clientSessionId);
        if (!player)
        {
            // 아직 입장하지 않았거나 이미 퇴장한 세션의 패킷은 여기서 버려진다.
            return;
        }

        // zoneId를 멤버가 아니라 스택 컨텍스트로 들고 간다 -- 이 객체는 플레이어 레인의 모든
        // 스레드가 공유하므로 멤버에 담으면 서로 덮어쓴다(PlayerContext 주석 참고).
        const PlayerContext context{*player, player->GetZoneId()};
        packetDispatcher_.Dispatch(packetId, context, payload);
    }

    void PlayerProcessor::HandleMove(const PlayerContext& context, const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(MovePacket))
        {
            return;
        }
        MovePacket move{};
        std::memcpy(&move, payload.data(), sizeof(MovePacket));

        // ① 검증. 지금은 형식 검사뿐이고, 속도 클램프·어뷰즈 검사·사망/이동금지 상태 확인이
        //    붙을 자리가 여기다 -- 직전 위치를 읽어야 하는데 그게 이 레인에서 안전한 이유가
        //    MoveModel을 플레이어 소유로 둔 이유다(MoveModel.h 주석 참고).
        // ② 요청만 기록한다. 실제 위치 확정과 경계 판정은 존 레인이 다음 틱에 한다.
        context.player.Move().Write()->RequestMove(move.x, move.y);

        // ③ 브로드캐스트는 틱을 기다리지 않고 즉시. 요청(C2ZMove)과 통지(Z2CMoveNotify)는
        //    id도 본문도 다르다 -- 받는 쪽은 "누가" 움직였는지 알아야 하므로 sessionId를
        //    앞에 붙인다.
        Packet::BinaryWriter writer;
        writer.Write(static_cast<uint32_t>(context.player.GetSessionId()));
        writer.Write(move);
        BroadcastToZone(context.zoneId, PacketId::Z2CMoveNotify, writer.GetBuffer());
    }

    void PlayerProcessor::HandleChat(const PlayerContext& context, const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        std::string message;
        if (!reader.ReadString(message))
        {
            return;
        }

        Packet::BinaryWriter writer;
        writer.Write(static_cast<uint32_t>(context.player.GetSessionId()));
        writer.WriteString(message);

        BroadcastToZone(context.zoneId, PacketId::Z2CChatNotify, writer.GetBuffer());
    }

    void PlayerProcessor::HandleMailAdd(const PlayerContext& context, const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        std::string title;
        std::string body;
        int64_t durationSec{};

        // UnitOfWork를 먼저 열어두는 이유: 파싱 실패도 "이 요청의 결말"이라 클라이언트에는
        // 같은 경로(Z2CTaskResult)로 에러가 돌아가야 한다. 스코프를 벗어나는 순간 소멸자가
        // 성공이면 전송, 실패면 역순 롤백까지 끝낸다 -- 별도의 커밋 호출이 없다.
        ZoneUnitOfWork unitOfWork(worldLink_, context.player.GetSessionId(), context.player.GetPlayerId(),
                                  PacketId::C2ZMailAdd);

        if (!reader.ReadString(title) || !reader.ReadString(body) || !reader.Read(durationSec))
        {
            unitOfWork.SetError(EErrorCode::InvalidPayload);
            return;
        }

        const auto& mailBox = context.player.GetMailBox();
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

    void PlayerProcessor::HandleMailDel(const PlayerContext& context, const std::span<const byte> payload)
    {
        ZoneUnitOfWork unitOfWork(worldLink_, context.player.GetSessionId(), context.player.GetPlayerId(),
                                  PacketId::C2ZMailDel);

        uint32_t mailId{};
        if (payload.size() < sizeof(mailId))
        {
            unitOfWork.SetError(EErrorCode::InvalidPayload);
            return;
        }
        std::memcpy(&mailId, payload.data(), sizeof(mailId));

        const auto& mailBox = context.player.GetMailBox();
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

    void PlayerProcessor::HandleMailBuy(const PlayerContext& context, const std::span<const byte> payload)
    {
        Packet::BinaryReader reader(payload);
        std::string title;
        std::string body;
        int64_t durationSec{};
        int64_t price{};

        ZoneUnitOfWork unitOfWork(worldLink_, context.player.GetSessionId(), context.player.GetPlayerId(),
                                  PacketId::C2ZMailBuy);

        if (!reader.ReadString(title) || !reader.ReadString(body) || !reader.Read(durationSec)
            || !reader.Read(price))
        {
            unitOfWork.SetError(EErrorCode::InvalidPayload);
            return;
        }

        const auto& mailBox = context.player.GetMailBox();
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

        if (const auto errorCode = context.player.GetWallet().DecCurrency(ECurrencyType::Gold, price, unitOfWork);
            errorCode != EErrorCode::Success)
        {
            unitOfWork.SetError(errorCode);
            return;
        }
    }

    void PlayerProcessor::SendToPlayer(const Network::SessionId clientSessionId, const PacketId innerPacketId,
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

    void PlayerProcessor::BroadcastToZone(const uint32_t zoneId, const PacketId innerPacketId,
                                          const std::span<const byte> payload,
                                          const Network::SessionId excludeClientSessionId) const
    {
        if (!zoneWorkers_.HasZone(zoneId))
        {
            return;
        }

        // 존 레인이 발행해둔 불변 스냅샷을 집어간다 -- 로스터를 직접 순회하지 않으므로
        // 존 레인과 동시에 돌아도 안전하다(ZoneInstance::BroadcastTargets 주석 참고).
        const auto targets = zoneWorkers_.GetZoneInstance(zoneId).BroadcastTargets();
        if (!targets || targets->empty())
        {
            return;
        }

        std::vector<Network::SessionId> filtered;
        filtered.reserve(targets->size());
        for (const auto clientSessionId : *targets)
        {
            if (clientSessionId != excludeClientSessionId)
            {
                filtered.push_back(clientSessionId);
            }
        }

        std::vector<byte> payloadCopy(payload.begin(), payload.end());
        broadcastDispatcher_.Broadcast(zoneId, std::move(filtered), innerPacketId, std::move(payloadCopy));
    }
}
