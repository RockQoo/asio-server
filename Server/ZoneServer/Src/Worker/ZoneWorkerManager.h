#pragma once

#include "Shared/Core/Src/Processor/ProcessorGroup.h"
#include "Server/ZoneServer/Src/Game/ZoneDef.h"
#include "Server/ZoneServer/Src/Game/ZoneInstance.h"
#include "Server/ZoneServer/Src/Worker/BroadcastDispatcher.h"
#include "Server/ZoneServer/Src/Worker/ProcessorId.h"

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

namespace Timer
{
    class RepeatingTimer;
}

namespace Zone
{
    class WorldLink;

    struct PoolSizes
    {
        // 플레이어 레인(BASIC). owner = clientSessionId라 실질 병렬도가 접속자 수만큼이다.
        size_t playerThreadCount{8};

        // 존 레인(TICK). **담당 존 수만큼**이 원칙이다 -- 존 하나의 공간 상태는 직렬 단위가
        // 하나여야 하고(여럿이 만지면 락이 필요한데 그 락이 이동 경로 전체에 걸려 결국 다시
        // 직렬이 된다), 그래서 스레드를 늘려도 존 하나는 빨라지지 않는다. 존이 버거우면
        // 스레드가 아니라 존을 쪼갠다.
        size_t zoneThreadCount{2};

        // 팬아웃 전송(BROADCAST). World 링크가 하나라 어차피 그 소켓에서 직렬화되므로 크게
        // 잡을 이유가 없다. 그래도 별도로 두는 이유는, 인구가 많은 존의 팬아웃(대상 수만큼
        // 프레임을 쓴다)이 플레이어 레인을 붙잡지 않게 하려는 것이다.
        size_t broadcastThreadCount{1};
    };

    // 존 레인 · 브로드캐스트 레인을 소유하고, 존별 tick 타이머를 건다.
    // 플레이어 레인(BASIC)은 여기가 아니라 ZoneServerApp이 소유한다 -- 존과 무관한
    // 레인이라 존 매니저에 두면 소유 관계가 거꾸로 된다.
    class ZoneWorkerManager
    {
    public:
        ZoneWorkerManager(std::vector<ZoneDef> zoneDefs, const PoolSizes& poolSizes,
                          const std::chrono::microseconds slowWarnThreshold, WorldLink& worldLink);
        ~ZoneWorkerManager();

        ZoneWorkerManager(const ZoneWorkerManager&) = delete;
        ZoneWorkerManager& operator=(const ZoneWorkerManager&) = delete;

        void Start(Network::IoContextPool& ioPool, const std::chrono::milliseconds tickInterval);
        void Stop();

        // 존 레인으로 메시지를 보낸다. ownerId가 zoneId이므로 같은 존의 일은 항상 같은
        // 스레드에서 순서대로 처리되고, 그래서 ZoneInstance에 락이 없다.
        template <typename F>
        void PostToZone(const uint32_t zoneId, F&& work)
        {
            zoneGroup_.Post(EProcessorId::ZoneSpace, zoneId, std::forward<F>(work));
        }

        // zoneInstances_는 생성자에서 다 만들어지고 이후 구조가 바뀌지 않으므로, 조회 자체는
        // 어느 레인에서 해도 안전하다. 다만 **돌려받은 ZoneInstance의 메서드는 존 레인에서만**
        // 불러야 한다(예외: BroadcastTargets()는 락 없이 읽는 스냅샷이라 어디서든 가능).
        [[nodiscard]] bool HasZone(const uint32_t zoneId) const { return zoneInstances_.contains(zoneId); }
        [[nodiscard]] ZoneInstance& GetZoneInstance(const uint32_t zoneId) { return *zoneInstances_.at(zoneId); }

        void LogStats() const
        {
            zoneGroup_.LogStats();
            broadcastGroup_.LogStats();
        }

        [[nodiscard]] BroadcastDispatcher& Broadcaster() noexcept { return broadcastDispatcher_; }
        [[nodiscard]] const std::vector<ZoneDef>& ZoneDefs() const noexcept { return zoneDefs_; }

    private:
        std::vector<ZoneDef> zoneDefs_;
        Processor::ProcessorGroup<EProcessorId> zoneGroup_;
        Processor::ProcessorGroup<EProcessorId> broadcastGroup_;
        BroadcastDispatcher broadcastDispatcher_;
        std::unordered_map<uint32_t, std::unique_ptr<ZoneInstance>> zoneInstances_;
        std::vector<std::unique_ptr<Timer::RepeatingTimer>> tickTimers_;
    };
}
