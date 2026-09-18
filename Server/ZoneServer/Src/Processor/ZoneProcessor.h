#pragma once

#include "Processor/ZoneMsg.h"
#include "Zone/Zone.h"

#include "Server/Core/Src/Packet/Dispatcher.h"
#include "Server/Core/Src/Pipeline/MessageProcessor.h"
#include "Server/Common/Src/Packet/WorldPackets.h"
#include "Server/Common/Src/Packet/ZoneLinkPackets.h"

// **BASIC 레인**의 처리기. 존 하나의 판단과 예약을 맡는다 -- 담당 존마다 하나씩이다.
//
// **owner 가 playerId 라 같은 존의 서로 다른 사람이 서로 다른 스레드에서 동시에 처리된다.**
// 그래서 이 객체에 사람별 상태를 멤버로 두면 안 된다(핸들러가 받는 것은 전부 스택이다).
//
// 하는 일은 셋이다:
//   입·퇴장   유닛을 만들어 컨테이너에 넣고 빼는 **유일한 자리**
//   내려보내기 그 사람에게 온 패킷을 존 -> 컨테이너 -> 유닛으로 넘긴다
//   존 자신에게 온 것 World 링크가 존에게 직접 보내는 패킷(W2Z)
//
// **틱은 여기서 돌지 않는다.** 적분과 경계 판정은 TICK 레인이 하고, 여기는 그 결과를
// 되돌려 받는 쪽이다 -- 이동 패킷이 아무리 몰려도 박자가 안 흔들리는 이유가 이 분리다.
class ZoneProcessor final : public Pipeline::MessageProcessor
{
public:
    explicit ZoneProcessor(Zone::SPtr zone);

    [[nodiscard]] std::string_view Name() const override { return "Zone"; }
    void RegistHandler() override;

private:
    void OnFromWorldStream(const Pipeline::OwnerId& owner, const FromWorldStreamBody& body);

    // ── World 링크가 존에게 직접 보내는 것(W2Z) ───────────────────────────────
    // 컨텍스트가 playerId 인 이유: 컨테이너의 키가 그것이고, 퇴장 패킷은 본문에 그 값이
    // 없어서(clientSessionId 뿐이다) 봉투에서 온 값을 그대로 물려받아야 한다.
    void HandleEnterZone(const Common::PlayerId& playerId, const Common::W2ZEnterZone& packet);
    void HandleLeaveZone(const Common::PlayerId& playerId, const Common::W2ZLeaveZone& packet);

    void SendEnterZoneNotify(const Network::SessionId clientSessionId,
                             const Common::PlayerId playerId) const;

    // TICK 레인과 같은 것을 가리킨다.
    const Zone::SPtr zone_;

    // 등록은 생성자에서 끝나고 이후로는 읽기 전용이라, 여러 스레드가 동시에 Dispatch 해도
    // 안전하다.
    Packet::Dispatcher<PacketId, Common::PlayerId> worldDispatcher_;
};
