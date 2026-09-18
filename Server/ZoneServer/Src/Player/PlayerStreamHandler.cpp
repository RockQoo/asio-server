#include "pch.h"
#include "Player/PlayerStreamHandler.h"

#include "Player/Player.h"
#include "Player/PlayerMail.h"
#include "Zone/Zone.h"

#include "Server/Common/Src/Packet/Wire.h"
#include "Server/Common/Src/Packet/ZonePackets.h"

namespace
{
    PlayerPacketDispatcher& Table()
    {
        static PlayerPacketDispatcher table;
        return table;
    }
}

void PlayerStreamHandler::Init()
{
    Table().Register(&PlayerStreamHandler::HandleMove);
    Table().Register(&PlayerStreamHandler::HandleChat);

    // 콘텐츠 패킷은 콘텐츠가 스스로 등록한다 -- 우편 패킷을 하나 늘릴 때 이 파일을 고칠
    // 일이 없어야 콘텐츠마다 파일을 나눈 의미가 있다.
    PlayerMail::Register(Table());
}

void PlayerStreamHandler::Dispatch(const PlayerContext& context, const PacketId packetId,
                                   const std::span<const byte> payload)
{
    Table().Dispatch(packetId, context, payload);
}

void PlayerStreamHandler::HandleMove(const PlayerContext& context, const Common::C2ZMove& packet)
{
    // ① 검증. 지금은 형식 검사뿐이고(디스패치 앞에서 끝난다), 속도 클램프·어뷰즈 검사·
    //    사망/이동금지 상태 확인이 붙을 자리가 여기다 -- 직전 위치를 읽어야 하는데 그게
    //    이 레인에서 안전한 이유가 MoveModel 을 유닛 소유로 둔 이유다(MoveModel.h 주석).
    // ② 요청만 기록한다. 실제 위치 확정과 경계 판정은 TICK 이 다음 틱에 한다.
    context.player.Move().Write()->RequestMove(packet.move.x, packet.move.y);

    // ③ 브로드캐스트는 틱을 기다리지 않고 즉시. 요청(C2ZMove)과 통지(Z2CMoveNotify)는
    //    id도 본문도 다르다 -- 받는 쪽은 "누가" 움직였는지 알아야 하므로 sessionId를
    //    앞에 붙인다.
    Common::Z2CMoveNotify notify;
    notify.Set(static_cast<uint32_t>(context.player.GetClientSessionId()), packet.move);

    Common::AssertDirection<Common::Z2CMoveNotify, Common::EPacketDirection::Z2C>();
    context.zone.Fanout(Common::Z2CMoveNotify::kPacketId, Common::ToBytes(notify));
}

void PlayerStreamHandler::HandleChat(const PlayerContext& context, const Common::C2ZChat& packet)
{
    Common::Z2CChatNotify notify;
    notify.Set(static_cast<uint32_t>(context.player.GetClientSessionId()), packet.message);

    Common::AssertDirection<Common::Z2CChatNotify, Common::EPacketDirection::Z2C>();
    context.zone.Fanout(Common::Z2CChatNotify::kPacketId, Common::ToBytes(notify));
}
