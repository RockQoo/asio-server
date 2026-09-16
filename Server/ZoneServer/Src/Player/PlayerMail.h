#pragma once

#include "Player/PlayerContext.h"
#include "Shared/Common/Src/Packet/ClientPackets.h"

// **우편 콘텐츠의 요청 처리기.** 콘텐츠 하나 = 파일 하나이고, 여기에 그 콘텐츠의 패킷
// 등록과 핸들러가 전부 모인다(인벤토리가 붙으면 PlayerInventory가 같은 모양으로 생긴다).
//
// 전부 static인 이유: 핸들러가 필요로 하는 것은 전부 PlayerContext로 들어온다. 상태를
// 가질 이유가 없고, 가지면 플레이어 레인의 여러 스레드가 공유하는 멤버가 되어 위험해진다.
//
// **롤백을 여기서 부르지 않는다.** 실패하면 ZoneUnitOfWork 소멸자가 기록된 태스크를 역순으로
// 훑으며 각 태스크의 역연산(MailTask::Rollback 등)을 부른다 -- 콘텐츠마다 "무엇을
// 되돌려야 하는가"를 다시 적으면 추가한 곳과 되돌리는 곳이 갈린다.
class PlayerMail final
{
public:
    PlayerMail() = delete;

    static void Register(PlayerPacketDispatcher& packetDispatcher);

private:
    static void HandleMailAdd(const PlayerContext& context, const Common::C2ZMailAdd& packet);
    static void HandleMailDel(const PlayerContext& context, const Common::C2ZMailDel& packet);

    // 우편 지급 + 골드 차감을 한 트랜잭션으로 처리한다 -- 모델 두 개에 걸친 변경이라
    // 뒤(골드)에서 실패하면 앞(우편)이 역순으로 되돌아가는 걸 실제로 밟는 경로다.
    static void HandleMailBuy(const PlayerContext& context, const Common::C2ZMailBuy& packet);
};
