#include "pch.h"
#include "Processor/PlayerProcessor.h"
#include "Processor/ZoneProcessor.h"
#include "Player/PlayerMail.h"
#include "Player/MailModel.h"
#include "Mail/MailRegistry.h"
#include "Shared/Common/Src/Packet/ZonePackets.h"
#include "Shared/Core/Src/Network/SessionHolder.h"
#include "Processor/BroadcastProcessor.h"
#include "Worker/WorkerManager.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Shared/Common/Src/Packet/RelayEnvelope.h"
#include "Shared/Common/Src/Packet/ZoneLinkPackets.h"
#include "Shared/Common/Src/Enum.h"
#include "Shared/Common/Src/PacketId.h"

PlayerProcessor::PlayerProcessor(PlayerRegistry& playerRegistry, WorkerManager& zoneWorkers,
                                 BroadcastProcessor& broadcastProcessor, Network::SessionHolder& worldLink,
                                 MailRegistry& mailRegistry)
    : playerRegistry_(playerRegistry)
    , zoneWorkers_(zoneWorkers)
    , broadcastProcessor_(broadcastProcessor)
    , worldLink_(worldLink)
    , mailRegistry_(mailRegistry)
{
    Register();
}

void PlayerProcessor::Register()
{
    RegisterPacketHandler<Common::C2ZMove>(packetDispatcher_, PacketId::C2ZMove,
        [this](const PlayerContext& context, const Common::C2ZMove& packet) { HandleMove(context, packet); });
    RegisterPacketHandler<Common::C2ZChat>(packetDispatcher_, PacketId::C2ZChat,
        [this](const PlayerContext& context, const Common::C2ZChat& packet) { HandleChat(context, packet); });

    // 콘텐츠 패킷은 콘텐츠가 스스로 등록한다 -- 우편 패킷을 하나 늘릴 때 이 파일을 고칠
    // 일이 없어야 콘텐츠마다 파일을 나눈 의미가 있다.
    PlayerMail::Register(packetDispatcher_);
}

void PlayerProcessor::DispatchFromWorld(const PacketId packetId, const Network::SessionId clientSessionId,
                                        const std::span<const byte> payload)
{
    // **여기부터 플레이어 레인이다**(owner = clientSessionId). 와이어 해석도 여기서 한다 --
    // I/O 스레드는 주인만 뽑아 넘겼다.
    switch (packetId)
    {
    case PacketId::W2ZEnterZone:
        {
            // 파싱 실패는 버린다. 포맷의 유일한 계약은 World의 Packet/ZoneLinkPackets.h 표이고,
            // 쓰는 쪽은 Packet/EnterZoneBody.h다.
            Common::W2ZEnterZone packet;
            if (!packet.Parse(payload))
            {
                LOG.Warning(ELogCategory::Zone, "EnterZone 본문이 잘렸거나 형식이 맞지 않아 버린다")
                    .KV("PayloadBytes", payload.size());
                return;
            }
            OnPlayerEnter(std::move(packet));
        }
        break;

    case PacketId::W2ZLeaveZone:
        {
            if (payload.size() < sizeof(Common::W2ZLeaveZone))
            {
                return;
            }
            Common::W2ZLeaveZone leave{};
            std::memcpy(&leave, payload.data(), sizeof(leave));
            OnPlayerLeave(leave.clientSessionId);
        }
        break;

    case PacketId::W2ZRelay:
        HandleForwardToZone(clientSessionId, payload);
        break;

    default:
        break;
    }
}

void PlayerProcessor::HandleForwardToZone(const Network::SessionId clientSessionId,
                                          const std::span<const byte> payload)
{
    const auto relay = Common::UnwrapRelay(payload);
    if (!relay)
    {
        return;
    }

    const auto innerPacketId = static_cast<PacketId>(relay->envelope.innerPacketId);
    if (innerPacketId == PacketId::C2ZEcho)
    {
        ReplyEcho(relay->envelope, relay->innerPayload);
        return;
    }

    HandleClientPacket(clientSessionId, innerPacketId, relay->innerPayload);
}

void PlayerProcessor::ReplyEcho(const Common::RelayEnvelope& header,
                                const std::span<const byte> innerPayload) const
{
    const auto worldSession = worldLink_.Get();
    if (!worldSession)
    {
        return;
    }

    // 받은 envelope을 그대로 쓰되 innerPacketId만 응답 방향으로 바꾼다 -- 요청과 응답이
    // 같은 id를 공유하지 않는 것이 패킷 id 규약이다(본문은 받은 것 그대로).
    Common::Z2CEchoAck packet;
    packet.Set(innerPayload);

    worldSession->SendPacket(
        PacketId::Z2WRelay,
        Common::WrapRelay(header.clientSessionId, static_cast<uint16_t>(packet.kPacketId),
                          Common::ToBytes(packet)));
}
void PlayerProcessor::OnPlayerEnter(Common::W2ZEnterZone packet)
{
    const auto clientSessionId = packet.head.clientSessionId;
    const auto zoneId = packet.head.zoneId;

    // 핸드오프로 다시 들어온 경우 이미 이 프로세스에 Player가 있을 수 있다. 그때는 우편함을
    // 새로 만들지 않고 위치만 옮긴다 -- 우편함을 다시 만들면 그 사람의 우편이 사라진다.
    auto player = playerRegistry_.Find(clientSessionId);
    if (player)
    {
        // **같은 프로세스 안에서의 핸드오프(가로 이동)다.** 살아 있는 Player가 권위이므로
        // 패킷에 실려 온 콘텐츠(packet.mails/currencies)는 쓰지 않는다 -- 그것은 World
        // 캐시의 사본이고, 아직 World에 도달하지 않은 변경이 있으면 오히려 과거로 되돌린다.
        // 프로세스를 넘는 이동에서는 World가 W2ZLeaveZone으로 이쪽 Player를 먼저 지우므로
        // 여기로 오지 않는다.
        const auto previousZoneId = player->GetZoneId();
        if (previousZoneId != zoneId)
        {
            zoneWorkers_.PostToZone(previousZoneId, [this, previousZoneId, clientSessionId]
            {
                if (zoneWorkers_.HasZone(previousZoneId))
                {
                    zoneWorkers_.GetZoneProcessor(previousZoneId).OnPlayerLeave(clientSessionId);
                }
            });
        }

        player->SetZoneId(zoneId);
        player->Move().Write()->Teleport(packet.head.x, packet.head.y);
    }
    else
    {
        // 신규 입장이거나 **프로세스를 넘는 핸드오프**(세로 이동)다. 둘 다 이 프로세스에는
        // 그 사람이 없으므로 모델을 새로 만든다 -- **시작 상태는 전부 생성자로 들어간다.**
        // 존은 DB를 직접 읽지 않으므로 이 패킷(=World 캐시)이 유일한 출처다.
        auto mailBox = mailRegistry_.Add(clientSessionId, packet.head.playerId, std::move(packet.mails));

        player = std::make_shared<Player>(clientSessionId, packet.head.playerId, zoneId,
                                          packet.head.x, packet.head.y,
                                          std::move(mailBox), packet.currencies);
        playerRegistry_.Add(player);
    }

    // 로스터는 존 레인 소유다 -- 여기서 직접 넣지 않고 그 존의 스레드로 넘긴다.
    zoneWorkers_.PostToZone(zoneId, [this, zoneId, player]
    {
        zoneWorkers_.GetZoneProcessor(zoneId).OnPlayerEnter(player);
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
            zoneWorkers_.GetZoneProcessor(zoneId).OnPlayerLeave(clientSessionId);
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

    // 컨텍스트를 멤버가 아니라 스택으로 들고 간다 -- 이 객체는 플레이어 레인의 모든
    // 스레드가 공유하므로 멤버에 담으면 서로 덮어쓴다(PlayerContext 주석 참고).
    const PlayerContext context{*player, player->GetZoneId(), worldLink_};
    packetDispatcher_.Dispatch(packetId, context, payload);
}

void PlayerProcessor::HandleMove(const PlayerContext& context, const Common::C2ZMove& packet)
{
    // ① 검증. 지금은 형식 검사뿐이고(디스패치 앞에서 끝난다), 속도 클램프·어뷰즈 검사·
    //    사망/이동금지 상태 확인이 붙을 자리가 여기다 -- 직전 위치를 읽어야 하는데 그게
    //    이 레인에서 안전한 이유가 MoveModel을 플레이어 소유로 둔 이유다(MoveModel.h 주석).
    // ② 요청만 기록한다. 실제 위치 확정과 경계 판정은 존 레인이 다음 틱에 한다.
    context.player.Move().Write()->RequestMove(packet.move.x, packet.move.y);

    // ③ 브로드캐스트는 틱을 기다리지 않고 즉시. 요청(C2ZMove)과 통지(Z2CMoveNotify)는
    //    id도 본문도 다르다 -- 받는 쪽은 "누가" 움직였는지 알아야 하므로 sessionId를
    //    앞에 붙인다.
    Common::Z2CMoveNotify notify;
    notify.Set(static_cast<uint32_t>(context.player.GetSessionId()), packet.move);
    BroadcastToZone(context.zoneId, notify);
}

void PlayerProcessor::HandleChat(const PlayerContext& context, const Common::C2ZChat& packet)
{
    Common::Z2CChatNotify notify;
    notify.Set(static_cast<uint32_t>(context.player.GetSessionId()), packet.message);
    BroadcastToZone(context.zoneId, notify);
}

void PlayerProcessor::SendToClientBytes(const Network::SessionId clientSessionId, const PacketId innerPacketId,
                                        const std::span<const byte> payload) const
{
    const auto worldSession = worldLink_.Get();
    if (!worldSession)
    {
        return;
    }

    worldSession->SendPacket(
        PacketId::Z2WRelay,
        Common::WrapRelay(clientSessionId, static_cast<uint16_t>(innerPacketId), payload));
}

void PlayerProcessor::BroadcastToZoneBytes(const Common::ZoneId zoneId, const PacketId innerPacketId,
                                           const std::span<const byte> payload,
                                           const Network::SessionId excludeClientSessionId) const
{
    if (!zoneWorkers_.HasZone(zoneId))
    {
        return;
    }

    // 존 레인이 발행해둔 불변 스냅샷을 집어간다 -- 로스터를 직접 순회하지 않으므로
    // 존 레인과 동시에 돌아도 안전하다(ZoneProcessor::BroadcastTargets 주석 참고).
    const auto targets = zoneWorkers_.GetZoneProcessor(zoneId).BroadcastTargets();
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
    broadcastProcessor_.Broadcast(zoneId, std::move(filtered), innerPacketId, std::move(payloadCopy));
}
