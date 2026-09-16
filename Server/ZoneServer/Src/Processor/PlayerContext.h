#pragma once

#include "Shared/Core/Src/Packet/Dispatcher.h"
#include "Shared/Common/Src/Ids.h"
#include "Shared/Common/Src/PacketId.h"

namespace Zone
{
    class Player;
    class WorldLink;

    // 핸들러 하나가 처리하는 동안 필요한 것들. **어느 것도 처리기의 멤버로 두면 안 된다** --
    // 처리기 객체 하나를 플레이어 레인의 모든 스레드가 공유하므로, 멤버에 쓰는 순간 서로 다른
    // 플레이어를 처리하던 스레드들이 같은 변수를 덮어쓴다. 그래서 호출마다 스택으로 들고 다닌다.
    //
    // worldLink가 여기 있는 이유: 콘텐츠 핸들러는 UnitOfWork를 열어야 하고 UnitOfWork는
    // World로 나가는 링크를 필요로 한다. 이걸 넣어두면 핸들러가 처리기의 멤버를 볼 일이 없어져
    // **자유 함수(static)로 떨어져 나올 수 있다** -- PlayerMail이 그렇게 분리됐다.
    struct PlayerContext
    {
        Player& player;
        Common::ZoneId zoneId;
        WorldLink& worldLink;
    };

    using PlayerPacketDispatcher = Packet::Dispatcher<PacketId, PlayerContext>;

    // **파싱을 디스패치 앞으로 당기는 등록 도우미.** 핸들러는 이미 해석된 TPacket을 받는다.
    //
    // 형식이 깨진 페이로드는 여기서 끝난다 -- 정상 클라이언트는 자기가 만든 구조체를 그대로
    // 보내므로 실패할 수 없고, 실패했다면 조작이거나 프로토콜 버전이 어긋난 것이다. 둘 다
    // 콘텐츠가 답할 내용이 아니라서 UnitOfWork를 열지 않고 로그만 남기고 버린다(예전에는
    // 핸들러마다 InvalidPayload를 UnitOfWork에 넣어 클라이언트에 돌려줬다).
    template <typename TPacket, typename THandler>
    void RegisterPacketHandler(PlayerPacketDispatcher& packetDispatcher, const PacketId packetId,
                               THandler handler)
    {
        packetDispatcher.Register(packetId,
            [packetId, handler](const PlayerContext& context, const std::span<const byte> payload)
            {
                TPacket packet{};
                if (!packet.Parse(payload))
                {
                    LOG.Warning(ELogCategory::Zone, "페이로드 형식이 맞지 않아 버린다")
                        .KV("PacketId", static_cast<uint16_t>(packetId))
                        .KV("PayloadBytes", payload.size());
                    return;
                }
                handler(context, packet);
            });
    }
}
