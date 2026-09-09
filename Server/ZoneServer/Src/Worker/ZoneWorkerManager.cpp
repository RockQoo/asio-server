#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Worker/ZoneWorkerManager.h"

#include "Shared/Core/Src/Network/IoContextPool.h"
#include "Shared/Core/Src/Timer/RepeatingTimer.h"

namespace Zone
{
    ZoneWorkerManager::ZoneWorkerManager(std::vector<ZoneDef> zoneDefs, const PoolSizes& poolSizes,
                                         const std::chrono::microseconds slowWarnThreshold,
                                         WorldLink& worldLink)
        : zoneDefs_(std::move(zoneDefs))
        , zoneGroup_("Zone", poolSizes.zoneThreadCount, slowWarnThreshold)
        , broadcastGroup_("Broadcast", poolSizes.broadcastThreadCount, slowWarnThreshold)
        , broadcastDispatcher_(broadcastGroup_, worldLink)
    {
        for (const auto& def : zoneDefs_)
        {
            zoneInstances_.emplace(def.zoneId, std::make_unique<ZoneInstance>(def, worldLink));
        }
    }

    ZoneWorkerManager::~ZoneWorkerManager()
    {
        Stop();
    }

    void ZoneWorkerManager::Start(Network::IoContextPool& ioPool, const std::chrono::milliseconds tickInterval)
    {
        zoneGroup_.Start();
        broadcastGroup_.Start();

        const auto deltaSeconds = static_cast<float>(tickInterval.count()) / 1000.0f;

        tickTimers_.reserve(zoneDefs_.size());
        for (const auto& def : zoneDefs_)
        {
            auto timer = std::make_unique<Timer::RepeatingTimer>(ioPool.Next());

            // 타이머는 I/O 스레드에서 만기되지만, tick 자체는 그 존의 레인으로 넘겨서 실행한다
            // -- 존 상태를 만지는 건 언제나 존 레인이어야 하기 때문이다.
            // this 캡처는 안전하다: Stop()이 타이머부터 취소한 뒤 그룹을 정지시키므로, 이
            // 매니저가 죽는 시점에는 걸려 있는 타이머가 없다(그 순서를 바꾸면 안 된다).
            const auto zoneId = def.zoneId;
            timer->Start(tickInterval, [this, zoneId, deltaSeconds]
            {
                PostToZone(zoneId, [this, zoneId, deltaSeconds]
                {
                    GetZoneInstance(zoneId).Tick(deltaSeconds);
                });
            });

            tickTimers_.push_back(std::move(timer));
        }
    }

    void ZoneWorkerManager::Stop()
    {
        // 타이머를 **먼저** 취소해야 안전하다 -- 그룹을 먼저 세우면 그 사이 만기된 타이머가
        // 이미 정지한 그룹에 tick을 밀어넣는다.
        for (const auto& timer : tickTimers_)
        {
            timer->Stop();
        }
        tickTimers_.clear();

        // 종속 관계의 역순: 존 레인이 브로드캐스트 레인에 일을 던지므로 존을 먼저 세운다.
        zoneGroup_.Stop();
        broadcastGroup_.Stop();
    }
}
