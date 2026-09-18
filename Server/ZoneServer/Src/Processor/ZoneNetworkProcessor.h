#pragma once

#include "Processor/ZoneMsg.h"

#include "Server/Core/Src/Network/SessionHolder.h"
#include "Server/Core/Src/Pipeline/MessageProcessor.h"
#include "Server/Common/Src/Ids.h"

// **LB 레인**의 처리기. 존 하나의 World 링크 수신구다 -- 연결이 zoneId 하나당 하나라
// 이 처리기도 담당 존마다 하나씩이다.
//
// **하는 일은 봉투를 까는 것뿐이다.** 상태가 없고, 유닛도 로스터도 보지 않는다.
// 깐 뒤에는 주인(playerId)을 붙여 BASIC 레인으로 넘긴다 -- 그 한 줄이 이 레인의 존재
// 이유다. 소켓 스레드에서 봉투를 까지 않는 것은 그쪽이 그 연결의 수신 전부를 직렬화하는
// 자리라 무거워지면 안 되기 때문이고, BASIC 에서 까지 않는 것은 **까기 전에는 주인을
// 모르기 때문**이다(주인이 봉투 안에 있다).
//
// **owner 가 링크 세션 id 라 이 레인의 병렬도는 그 존의 연결 수, 곧 1이다.** 레인 수를
// 늘려도 한 존의 수신은 한 레인으로만 간다. 그래서 여기 머무는 시간이 짧아야 하고,
// 구조적으로 존에서 가장 좁은 목이라 측정 대상이다.
class ZoneNetworkProcessor final : public Pipeline::MessageProcessor
{
public:
    ZoneNetworkProcessor(const Common::ZoneId zoneId, Network::SessionHolder& worldLink);

    [[nodiscard]] std::string_view Name() const override { return "ZoneNetwork"; }
    void RegistHandler() override;

private:
    void OnRecvStream(const Pipeline::OwnerId& owner, const RecvStreamBody& body);

    // 봉투를 깐 결과를 그 사람의 BASIC 레인으로 넘긴다.
    void PushToBasic(const PacketId packetId, const Network::SessionId clientSessionId,
                     const Common::PlayerId playerId, const std::span<const byte> payload) const;

    // Echo 는 공유 상태가 필요 없어서 여기서 바로 되돌린다 -- BASIC 까지 보내면 왕복만
    // 늘고 답은 같다.
    void ReplyEcho(const Network::SessionId clientSessionId,
                   const std::span<const byte> innerPayload) const;

    const Common::ZoneId zoneId_;
    Network::SessionHolder& worldLink_;
};
