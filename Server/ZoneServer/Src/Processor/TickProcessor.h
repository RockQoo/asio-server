#pragma once

#include "Processor/ZoneMsg.h"
#include "Zone/Zone.h"

#include "Server/Core/Src/Network/SessionHolder.h"
#include "Server/Core/Src/Pipeline/MessageProcessor.h"

// **TICK 레인**의 처리기. 존 하나의 박자를 맡는다 -- 담당 존마다 하나씩 만들어진다.
//
// **이 레인만 해시를 끈다.** 주인 값이 `1..2N` 로 촘촘하고 레인이 `2N+1` 개라 나머지
// 연산이 항등이 된다(ProcessorIds.h). 그래서 "N번 존의 틱은 반드시 N번 스레드"가 되고,
// 한 존이 무거워도 다른 존의 박자가 안 밀린다.
//
// **상태는 여기 없다.** 로스터도 좌표도 Zone 이 들고 있고, 이 객체는 "틱 때 뭘 할지"만
// 안다. 그래서 같은 Zone 을 BASIC 레인의 ZoneProcessor 와 나눠 볼 수 있다.
//
// **로스터를 바꾸지 않는다.** 경계를 넘은 사람은 World 에 이동을 요청만 하고, 실제로
// 빠지는 것은 World 가 되돌려 보내는 퇴장이 BASIC 에 도착했을 때다 -- 같은 프로세스
// 안이라고 질러가면 이동 경로가 두 벌이 된다.
class TickProcessor final : public Pipeline::MessageProcessor
{
public:
    explicit TickProcessor(Zone::SPtr zone);

    [[nodiscard]] std::string_view Name() const override { return "Tick"; }
    void RegistHandler() override;

private:
    void OnZoneTick(const Pipeline::OwnerId& owner, const ZoneTickBody& body);

    void RequestZoneTransfer(const Common::UnitId unitId, const float x, const float y) const;

    // BASIC 레인과 같은 것을 가리킨다. 바뀌는 상태는 전부 여기 있다.
    const Zone::SPtr zone_;
};
