#pragma once

#include "Shared/Core/Src/Network/Connector.h"
#include "Shared/Core/Src/Network/IoContextPool.h"
#include "Shared/Core/Src/Processor/Group.h"
#include "Game/PlayerRegistry.h"
#include "Game/Def.h"
#include "Handler/PlayerProcessor.h"
#include "Handler/WorldLinkHandler.h"
#include "Mail/ExpiryService.h"
#include "Mail/Registry.h"
#include "World/WorldLink.h"
#include "Worker/ProcessorId.h"
#include "Worker/WorkerManager.h"

#include "App/Config.h"

#include <asio.hpp>

namespace Timer
{
    class RepeatingTimer;
}

namespace Zone
{
    // 전체를 조립하는 곳. 레인이 넷이고 **주인이 서로 다르다**는 것이 이 파일에서 읽혀야 한다:
    //
    //   LB     (owner = clientSessionId)  수신 파싱 · 1차 분기
    //   Player (owner = clientSessionId)  우편 · 재화 · UnitOfWork · 이동 검증
    //   Zone   (owner = zoneId)           로스터 · 위치 적분 · 경계 판정 (틱)
    //   Broadcast (owner = zoneId)        팬아웃 전송
    //
    // 그리고 이 레인들과 무관하게 도는 우편 만료 타이머가 하나 더 있다 -- 그게 Mutexed가
    // 실제로 필요한 지점이다(Model).
    class App
    {
    public:
        explicit App(Config config);
        ~App();

        void Run();
        void Stop();

    private:
        void SetupSignalHandling();

        Config config_;
        Network::IoContextPool ioPool_;
        WorldLink worldLink_;

        // 선언 순서 = 생성 순서. 큐 그룹이 레지스트리보다 먼저 와야 한다 --
        // playerRegistry_가 playerGroup_.ThreadCount()를 받아 샤드를 나누기 때문이다.
        Processor::Group<EProcessorId> lbGroup_;
        Processor::Group<EProcessorId> playerGroup_;

        PlayerRegistry playerRegistry_;
        Mail::Registry mailRegistry_;
        WorkerManager zoneWorkers_;
        PlayerProcessor playerProcessor_;
        WorldLinkHandler worldLinkHandler_;
        Mail::ExpiryService mailExpiryService_;

        std::shared_ptr<Network::Connector> worldConnector_;
        std::unique_ptr<Timer::RepeatingTimer> mailExpiryTimer_;
        std::unique_ptr<Timer::RepeatingTimer> statsTimer_;
        asio::signal_set signals_;
    };
}
