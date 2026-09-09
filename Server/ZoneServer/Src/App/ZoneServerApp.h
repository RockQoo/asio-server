#pragma once

#include "Shared/Core/Src/Network/Connector.h"
#include "Shared/Core/Src/Network/IoContextPool.h"
#include "Shared/Core/Src/Processor/ProcessorGroup.h"
#include "Server/ZoneServer/Src/Game/PlayerRegistry.h"
#include "Server/ZoneServer/Src/Game/ZoneDef.h"
#include "Server/ZoneServer/Src/Handler/PlayerProcessor.h"
#include "Server/ZoneServer/Src/Handler/WorldLinkHandler.h"
#include "Server/ZoneServer/Src/Mail/MailExpiryService.h"
#include "Server/ZoneServer/Src/Mail/MailRegistry.h"
#include "Server/ZoneServer/Src/World/WorldLink.h"
#include "Server/ZoneServer/Src/Worker/ProcessorId.h"
#include "Server/ZoneServer/Src/Worker/ZoneWorkerManager.h"

#include <asio.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Timer
{
    class RepeatingTimer;
}

namespace Zone
{
    struct ZoneServerConfig
    {
        // 이 프로세스가 담당하는 존 목록 -- 리스트 길이만큼 이 프로세스가 존을 동시에 호스팅한다.
        std::vector<ZoneDef> zones;

        // 수신(LB) 레인 크기. 존 개수와 무관하다.
        size_t lbThreadCount{4};

        // 플레이어 레인 / 존 레인 / 브로드캐스트 레인 크기.
        PoolSizes poolSizes;

        std::string worldHost{"127.0.0.1"};
        uint16_t worldPort{9200};
        size_t ioThreadCount{2};
        std::chrono::milliseconds tickInterval{100};
        std::chrono::milliseconds mailSweepInterval{1000};

        // 이 시간을 넘긴 작업은 경고 로그를 남긴다. 어느 프로세서가 레인을 태우는지 찾는 용도.
        std::chrono::microseconds slowTaskWarnThreshold{50000};  // 50ms

        // 레인 통계를 로그로 남기는 주기. 0으로 두면 끈다.
        std::chrono::milliseconds statsDumpInterval{10000};
    };

    // 전체를 조립하는 곳. 레인이 넷이고 **주인이 서로 다르다**는 것이 이 파일에서 읽혀야 한다:
    //
    //   LB     (owner = clientSessionId)  수신 파싱 · 1차 분기
    //   Player (owner = clientSessionId)  우편 · 재화 · UnitOfWork · 이동 검증
    //   Zone   (owner = zoneId)           로스터 · 위치 적분 · 경계 판정 (틱)
    //   Broadcast (owner = zoneId)        팬아웃 전송
    //
    // 그리고 이 레인들과 무관하게 도는 우편 만료 타이머가 하나 더 있다 -- 그게 Mutexed가
    // 실제로 필요한 지점이다(MailModel).
    class ZoneServerApp
    {
    public:
        explicit ZoneServerApp(ZoneServerConfig config);
        ~ZoneServerApp();

        void Run();
        void Stop();

    private:
        void SetupSignalHandling();

        ZoneServerConfig config_;
        Network::IoContextPool ioPool_;
        WorldLink worldLink_;

        // 선언 순서 = 생성 순서. 큐 그룹이 레지스트리보다 먼저 와야 한다 --
        // playerRegistry_가 playerGroup_.ThreadCount()를 받아 샤드를 나누기 때문이다.
        Processor::ProcessorGroup<EProcessorId> lbGroup_;
        Processor::ProcessorGroup<EProcessorId> playerGroup_;

        PlayerRegistry playerRegistry_;
        Mail::MailRegistry mailRegistry_;
        ZoneWorkerManager zoneWorkers_;
        PlayerProcessor playerProcessor_;
        WorldLinkHandler worldLinkHandler_;
        Mail::MailExpiryService mailExpiryService_;

        std::shared_ptr<Network::Connector> worldConnector_;
        std::unique_ptr<Timer::RepeatingTimer> mailExpiryTimer_;
        std::unique_ptr<Timer::RepeatingTimer> statsTimer_;
        asio::signal_set signals_;
    };
}
