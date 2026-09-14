#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Processor/Group.h"
#include "Shared/Protocol/Src/Ids.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Worker/ProcessorId.h"

namespace Zone
{
    class WorldLink;

    // 팬아웃 전송 전용 레인. 호출부(플레이어 레인)가 이미 만들어둔 **대상 목록 스냅샷만**
    // 넘겨받아 전송만 한다 -- 로스터(Instance::members_)를 직접 건드리지 않으므로 존
    // 레인과 동시에 돌아도 안전하다.
    //
    // ownerId를 zoneId로 두는 이유: 같은 존의 팬아웃끼리는 보낸 순서대로 나가야 한다
    // (채팅 두 줄이 뒤바뀌면 눈에 띈다). 서로 다른 존은 순서를 맞출 이유가 없으므로 병렬로 둔다.
    class BroadcastDispatcher
    {
    public:
        BroadcastDispatcher(Processor::Group<EProcessorId>& broadcastGroup, WorldLink& worldLink);

        void Broadcast(const Protocol::ZoneId zoneId, std::vector<Network::SessionId> targets,
                       const PacketId innerPacketId, std::vector<byte> payload);

    private:
        Processor::Group<EProcessorId>& broadcastGroup_;
        WorldLink& worldLink_;
    };
}
