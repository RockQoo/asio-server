#pragma once

#include "Shared/Core/Src/Packet/Dispatcher.h"
#include "Shared/Common/Src/Ids.h"
#include "Shared/Common/Src/PacketId.h"
#include "Shared/Common/Src/Packet/Wire.h"
#include "Shared/Core/Src/Network/SessionHolder.h"

class Player;

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
    Network::SessionHolder& worldLink;
};

using PlayerPacketDispatcher = Packet::Dispatcher<PacketId, PlayerContext>;
