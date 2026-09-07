#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Worker/ZoneWorkerManager.h"

#include "Shared/Core/Src/Network/IoContextPool.h"
#include "Shared/Core/Src/Timer/RepeatingTimer.h"

namespace Zone
{
    ZoneWorkerManager::ZoneWorkerManager(std::vector<ZoneDef> zoneDefs, const PoolSizes& poolSizes,
                                         WorldLink& worldLink, Mail::MailRegistry& mailRegistry)
        : zoneDefs_(std::move(zoneDefs))
        , basicPool_(poolSizes.basicThreadCount)
        , tickPool_(poolSizes.tickThreadCount)
        , broadcastPool_(poolSizes.broadcastThreadCount)
        , broadcastDispatcher_(broadcastPool_, worldLink)
    {
        for (const auto& def : zoneDefs_)
        {
            zoneInstances_.emplace(def.zoneId,
                std::make_unique<ZoneInstance>(def.zoneId, def.xMin, def.xMax, worldLink, broadcastDispatcher_, mailRegistry));
        }
    }

    ZoneWorkerManager::~ZoneWorkerManager()
    {
        Stop();
    }

    void ZoneWorkerManager::Start(Network::IoContextPool& ioPool, const std::chrono::milliseconds tickInterval)
    {
        basicPool_.Start();
        tickPool_.Start();
        broadcastPool_.Start();

        const auto deltaSeconds = static_cast<float>(tickInterval.count()) / 1000.0f;

        tickTimers_.reserve(zoneDefs_.size());
        for (const auto& def : zoneDefs_)
        {
            auto timer = std::make_unique<Timer::RepeatingTimer>(ioPool.Next());

            // world/tickPool_ 참조는 이 매니저보다 먼저 파괴되지 않는다(Stop()이 타이머부터
            // 먼저 취소한 뒤 풀을 정지시키므로). TICK 풀로 태스크를 맡긴다 -- BASIC과는 다른
            // 스레드라는 점을 ZoneInstance.h 주석에 적어뒀다.
            auto& world = *zoneInstances_.at(def.zoneId);
            auto& tickPool = tickPool_;
            const auto zoneId = def.zoneId;
            timer->Start(tickInterval, [&tickPool, &world, zoneId, deltaSeconds]
            {
                tickPool.GetWorker(zoneId).PostTask([&world, deltaSeconds] { world.Tick(deltaSeconds); });
            });

            tickTimers_.push_back(std::move(timer));
        }
    }

    void ZoneWorkerManager::Stop()
    {
        for (const auto& timer : tickTimers_)
        {
            timer->Stop();
        }
        tickTimers_.clear();

        broadcastPool_.Stop();
        tickPool_.Stop();
        basicPool_.Stop();
    }
}
