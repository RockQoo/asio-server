#pragma once

#include "Player/PlayerContext.h"

#include "Server/Common/Src/Packet/ClientPackets.h"

// 플레이어에게 내려온 패킷(C2Z)의 표.
//
// **표를 프로세스에 한 벌만 둔다.** 등록 내용이 사람마다 다르지 않은데 개체마다 표를
// 들면 접속 수만큼 통째로 복제된다 -- 항목 하나가 수십 바이트라도 만 단위 동접이면
// 무시할 수 없다. 존마다 두지 않는 이유도 같다.
//
// **등록은 Init() 한 번뿐이고 그 뒤로는 읽기 전용**이라, BASIC 레인의 여러 스레드가
// 동시에 Dispatch 해도 안전하다. 첫 호출 때 자동으로 채우는 방식을 쓰지 않은 것은
// 등록 시점이 코드에 안 보이면 "왜 이 패킷만 안 먹나"를 추적할 자리가 없어서다.
class PlayerStreamHandler final
{
public:
    PlayerStreamHandler() = delete;

    // 기동 때 한 번. 두 번 불러도 같은 표를 다시 채울 뿐이라 해는 없지만, 부르지 않으면
    // 모든 패킷이 미등록으로 버려진다.
    static void Init();

    static void Dispatch(const PlayerContext& context, const PacketId packetId,
                         const std::span<const byte> payload);

private:
    // 모델 변경도 DB 저장도 없는 것만 여기 남는다 -- 브로드캐스트가 전부라 UnitOfWork 를
    // 열지 않으므로 콘텐츠 파일로 뺄 것이 없다. UoW 를 여는 콘텐츠는 자기 파일을 갖는다
    // (PlayerMail 이 그 예).
    static void HandleMove(const PlayerContext& context, const Common::C2ZMove& packet);
    static void HandleChat(const PlayerContext& context, const Common::C2ZChat& packet);
};
