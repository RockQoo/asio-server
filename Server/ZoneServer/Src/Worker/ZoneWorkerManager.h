#pragma once

#include "Shared/Core/Src/Thread/AffinityWorkerPool.h"
#include "Server/ZoneServer/Src/Game/ZoneDef.h"
#include "Server/ZoneServer/Src/Game/ZoneWorld.h"
#include "Server/ZoneServer/Src/Worker/BroadcastDispatcher.h"
#include "Server/ZoneServer/Src/Worker/TaskWorker.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Network
{
    class IoContextPool;
}

namespace Zone
{
    class WorldLink;
}

namespace Mail
{
    class MailRegistry;
}

namespace Timer
{
    class RepeatingTimer;
}

namespace Zone
{
    // 존 로직을 BASIC/TICK/BROADCAST 세 개의 풀로 분리한다(스레드 개수는 학습용으로 작게
    // 잡음). 셋 다 zoneId % 풀크기로 sticky 라우팅하지만 서로 "다른 풀"이다 -- 같은 zoneId라도 BASIC
    // 스레드와 TICK 스레드는 실제로 다른 OS 스레드라는 뜻이다(ZoneWorld.h 상단 주석의
    // "지금은 안전하지만 나중에..." 설명 참고). BROADCAST는 스냅샷 핸드오프 방식으로 이
    // 문제를 애초에 피한다(BroadcastDispatcher 참고).
    struct PoolSizes
    {
        size_t basicThreadCount{2};
        size_t tickThreadCount{2};
        size_t broadcastThreadCount{2};
    };

    class ZoneWorkerManager
    {
    public:
        ZoneWorkerManager(std::vector<ZoneDef> zoneDefs, const PoolSizes& poolSizes,
                          WorldLink& worldLink, Mail::MailRegistry& mailRegistry);
        ~ZoneWorkerManager();

        ZoneWorkerManager(const ZoneWorkerManager&) = delete;
        ZoneWorkerManager& operator=(const ZoneWorkerManager&) = delete;

        void Start(Network::IoContextPool& ioPool, const std::chrono::milliseconds tickInterval);
        void Stop();

        // hash(zoneId) % consumerCount와 동일한 원리로 BASIC 풀에 태스크를 맡긴다
        // (zoneId가 이미 작은 정수라 modulo만 쓴다).
        template <typename F>
        void PostToBasic(const uint32_t zoneId, F&& task)
        {
            basicPool_.GetWorker(zoneId).PostTask(std::forward<F>(task));
        }

        [[nodiscard]] bool HasZone(const uint32_t zoneId) const { return zoneWorlds_.contains(zoneId); }
        [[nodiscard]] ZoneWorld& GetZoneWorld(const uint32_t zoneId) { return *zoneWorlds_.at(zoneId); }
        [[nodiscard]] const std::vector<ZoneDef>& ZoneDefs() const noexcept { return zoneDefs_; }

    private:
        std::vector<ZoneDef> zoneDefs_;
        Thread::AffinityWorkerPool<TaskWorker> basicPool_;
        Thread::AffinityWorkerPool<TaskWorker> tickPool_;
        Thread::AffinityWorkerPool<TaskWorker> broadcastPool_;
        BroadcastDispatcher broadcastDispatcher_;
        std::unordered_map<uint32_t, std::unique_ptr<ZoneWorld>> zoneWorlds_;
        std::vector<std::unique_ptr<Timer::RepeatingTimer>> tickTimers_;
    };
}
