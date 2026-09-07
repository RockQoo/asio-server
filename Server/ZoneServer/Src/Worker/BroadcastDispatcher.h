#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Thread/AffinityWorkerPool.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Server/ZoneServer/Src/Worker/TaskWorker.h"

#include <cstdint>
#include <vector>

namespace Zone
{
    class WorldLink;

    // 존마다 붙는 송신 전용 스레드 풀(BROADCAST). BASIC 스레드
    // (그 존의 players_를 소유한 유일한 스레드)가 이미 만들어둔 "보낼 대상 목록" 스냅샷만
    // 넘겨받아 실제 전송만 한다 -- 공유 컨테이너(ZoneInstance::players_)를 직접 건드리지 않으므로
    // BASIC과 BROADCAST가 서로 다른 스레드라도 안전하다. zoneId % 풀크기로 sticky 라우팅한다.
    class BroadcastDispatcher
    {
    public:
        BroadcastDispatcher(Thread::AffinityWorkerPool<TaskWorker>& broadcastPool, WorldLink& worldLink);

        void Broadcast(const uint32_t zoneId, std::vector<Network::SessionId> targets,
                       const PacketId innerPacketId, std::vector<byte> payload);

    private:
        Thread::AffinityWorkerPool<TaskWorker>& broadcastPool_;
        WorldLink& worldLink_;
    };
}
