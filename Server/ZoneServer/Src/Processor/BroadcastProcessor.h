#pragma once

#include "Server/Core/Src/Base/Types.h"
#include "Server/Core/Src/Processor/Group.h"
#include "Server/Common/Src/Ids.h"
#include "Server/Common/Src/PacketId.h"
#include "Processor/ProcessorId.h"
#include "Server/Core/Src/Network/SessionHolder.h"


// 팬아웃 전송 전용 레인. 호출부(플레이어 레인)가 이미 만들어둔 **대상 목록 스냅샷만**
// 넘겨받아 전송만 한다 -- 로스터(ZoneProcessor::members_)를 직접 건드리지 않으므로 존
// 레인과 동시에 돌아도 안전하다.
//
// ownerId를 zoneId로 두는 이유: 같은 존의 팬아웃끼리는 보낸 순서대로 나가야 한다
// (채팅 두 줄이 뒤바뀌면 눈에 띈다). 서로 다른 존은 순서를 맞출 이유가 없으므로 병렬로 둔다.
class BroadcastProcessor
{
public:
    BroadcastProcessor(Processor::Group<EZoneProcessorId>& broadcastGroup, Network::SessionHolder& worldLink);

    void Broadcast(const Common::ZoneId zoneId, std::vector<Network::SessionId> targets,
                   const PacketId innerPacketId, std::vector<byte> payload);

private:
    Processor::Group<EZoneProcessorId>& broadcastGroup_;
    Network::SessionHolder& worldLink_;
};
