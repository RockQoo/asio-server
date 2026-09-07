#pragma once

#include "Shared/Core/Src/Network/Connector.h"
#include "Shared/Core/Src/Network/IoContextPool.h"
#include "Server/ZoneServer/Src/Game/ZoneDef.h"
#include "Server/ZoneServer/Src/Handler/WorldLinkHandler.h"
#include "Server/ZoneServer/Src/Mail/MailExpiryService.h"
#include "Server/ZoneServer/Src/Mail/MailRegistry.h"
#include "Server/ZoneServer/Src/World/WorldLink.h"
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
        // 이 프로세스가 담당하는 존 목록(config로 조절) -- 리스트 길이만큼 이 프로세스가 존을
        // 동시에 호스팅한다.
        std::vector<ZoneDef> zones;
        // recv 처리 전용 LB 풀 크기 -- 존 개수와 무관하게 설정 가능.
        size_t lbThreadCount{2};
        // BASIC/TICK/BROADCAST 풀 크기(각각 존 개수와 무관하게 설정 가능).
        PoolSizes poolSizes;
        std::string worldHost{"127.0.0.1"};
        uint16_t worldPort{9200};
        size_t ioThreadCount{2};
        std::chrono::milliseconds tickInterval{100};
        std::chrono::milliseconds mailSweepInterval{1000};
    };

    // 전체를 조립하는 곳: World로 나가는 연결(Connector) 하나, 그 연결의 LB 풀(WorldLinkHandler
    // 내부), 이 프로세스가 호스팅하는 존들의 BASIC/TICK/BROADCAST 풀(ZoneWorkerManager),
    // 그리고 그 풀들과 무관하게 도는 메일 만료 유지보수 타이머(Mail::MailExpiryService)를
    // 한데 묶는다 -- 후자가 Synchronized가 실제로 필요한 유일한 지점이다(ZoneInstance.h 상단 주석
    // 참고).
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
        Mail::MailRegistry mailRegistry_;
        ZoneWorkerManager zoneWorkers_;
        WorldLinkHandler worldLinkHandler_;
        Mail::MailExpiryService mailExpiryService_;
        std::shared_ptr<Network::Connector> worldConnector_;
        std::unique_ptr<Timer::RepeatingTimer> mailExpiryTimer_;
        asio::signal_set signals_;
    };
}
