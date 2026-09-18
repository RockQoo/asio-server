#pragma once

#include "Server/Core/Src/Packet/Dispatcher.h"
#include "Server/Common/Src/PacketId.h"

class Player;
class Zone;

// 핸들러 하나가 처리하는 동안 필요한 것들. **어느 것도 처리기나 표의 멤버로 두면 안 된다** --
// 표는 프로세스에 한 벌이고 BASIC 레인의 모든 스레드가 공유하므로, 멤버에 쓰는 순간 서로
// 다른 플레이어를 처리하던 스레드들이 같은 변수를 덮어쓴다. 그래서 호출마다 스택으로 든다.
//
// **zone 이 있으면 나머지가 다 나온다** -- zoneId, World 링크, 브로드캐스트. 그래서 그것들을
// 따로 들고 다니지 않는다. 핸들러가 "그 사람"과 "그 세계" 둘만 알면 된다는 뜻이기도 하다.
struct PlayerContext
{
    Player& player;
    Zone& zone;
};

using PlayerPacketDispatcher = Packet::Dispatcher<PacketId, PlayerContext>;
