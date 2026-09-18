#pragma once

#include "Processor/ZoneMsg.h"
#include "Zone/Zone.h"

#include "Server/Core/Src/Network/SessionHolder.h"
#include "Server/Core/Src/Pipeline/MessageProcessor.h"

// **TICK 레인**의 처리기. 존 하나의 박자를 맡는다 -- 담당 존마다 하나씩 만들어진다.
//
// **owner 가 존 서수이고, 이 레인만 해시를 끈다.** 그래야 "N번 존의 틱은 반드시 N번
// 스레드"가 되고, 한 존이 무거워도 다른 존의 박자가 안 밀린다(근거: Pipeline::LaneIndexOf).
// 순서만 지키면 되는 다른 레인들과 여기가 갈리는 지점이다.
//
// **상태는 여기 없다.** 로스터와 좌표는 Zone 이 들고 있고, 이 객체는 "틱 때 뭘 할지"만 안다.
// 그래서 같은 Zone 을 BASIC 레인의 ZoneProcessor 와 나눠 볼 수 있다.
//
// **이동은 이 레인이 틱에서만 만진다.** 이동 패킷 자체는 BASIC 이 받아 검증하고 MoveModel 에
// 요청만 기록하므로, 패킷이 폭주해도 여기 박자가 흔들리지 않는다.
class TickProcessor final : public Pipeline::MessageProcessor
{
public:
    TickProcessor(Zone::SPtr zone, Network::SessionHolder& worldLink);

    [[nodiscard]] std::string_view Name() const override { return "Tick"; }
    void RegistHandler() override;

private:
    // ── 레인 메시지 ───────────────────────────────────────────────────────────
    void OnZoneEnter(const Pipeline::OwnerId& owner, const ZoneEnterBody& body);
    void OnZoneLeave(const Pipeline::OwnerId& owner, const ZoneLeaveBody& body);
    void OnZoneTick(const Pipeline::OwnerId& owner, const ZoneTickBody& body);

    void SendEnterZoneNotify(const Network::SessionId clientSessionId, const Common::PlayerId playerId) const;
    void RequestZoneTransfer(const Network::SessionId clientSessionId, const Common::PlayerId playerId,
                             const float x, const float y) const;

    // BASIC 레인과 같은 것을 가리킨다. 바뀌는 상태는 전부 여기 있다.
    const Zone::SPtr zone_;
    Network::SessionHolder& worldLink_;
};
