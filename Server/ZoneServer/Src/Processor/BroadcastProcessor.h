#pragma once

#include "Processor/ZoneMsg.h"

#include "Server/Core/Src/Network/SessionHolder.h"
#include "Server/Core/Src/Pipeline/MessageProcessor.h"
#include "Server/Common/Src/Ids.h"

// **BROADCAST 레인**의 처리기. 존 하나의 팬아웃 전송을 맡는다 -- 담당 존마다 하나씩이다.
//
// **컨테이너를 보지 않는다.** 대상 목록은 보내는 쪽(BASIC/TICK)이 이미 만들어 넘겨준
// 사본이다. 여기서 로스터를 다시 훑으면 이 레인이 그 존의 락에 묶이고, 그러면 전송을
// 따로 떼어낸 의미가 없어진다.
//
// **모았다가 한꺼번에 보낸다.** 프레임을 만들어 함에 이어 붙여만 두고, 비우기 만기가 오면
// 그 덩어리를 한 번에 소켓에 넣는다. 이동 통지 하나가 존 전원에게 가는 구조라 건건이
// 보내면 같은 소켓에 초당 수십만 번 쓰게 된다.
//
// **owner 가 zoneId 다.** 같은 존의 편지는 보낸 순서대로 나가야 한다(채팅 두 줄이
// 뒤바뀌면 눈에 띈다). 존이 다르면 순서를 맞출 이유가 없어 병렬로 둔다.
// zoneId 는 값이 드문드문해서 **해시를 켠다** -- 끄면 나머지 연산이 한쪽 레인에 몰린다.
class BroadcastProcessor final : public Pipeline::MessageProcessor
{
public:
    BroadcastProcessor(const Common::ZoneId zoneId, Network::SessionHolder& worldLink);

    [[nodiscard]] std::string_view Name() const override { return "Broadcast"; }
    void RegistHandler() override;

private:
    void OnFanout(const Pipeline::OwnerId& owner, const FanoutBody& body);
    void OnFanoutFlush(const Pipeline::OwnerId& owner);

    void Flush();

    // 함이 이만큼 차면 만기를 기다리지 않고 바로 비운다. **지연보다 메모리가 먼저 걸리는
    // 것을 막는 안전장치**다 -- 만기가 밀리는 동안 계속 쌓이면 상한이 없다.
    static constexpr size_t kFlushThresholdBytes = 1 * 1024 * 1024;

    const Common::ZoneId zoneId_;
    Network::SessionHolder& worldLink_;

    // **이 레인 전용이라 락이 없다.** 이미 프레임으로 만들어진 바이트가 이어 붙는다 --
    // 받는 쪽은 스트림에서 헤더를 읽어 자르므로 몇 통이 한 번에 왔는지 알 필요가 없다.
    std::vector<byte> outbox_;
};
