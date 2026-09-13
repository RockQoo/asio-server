#pragma once

#include "Shared/Core/Src/Network/IoContextPool.h"
#include "Shared/Core/Src/Network/Listener.h"
#include "Shared/Core/Src/Timer/RepeatingTimer.h"
#include "Shared/Core/Src/Processor/Group.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Handler/GatewayLinkHandler.h"
#include "Handler/ZoneLinkHandler.h"
#include "Tool/ToolProcessor.h"
#include "World/ClientRegistry.h"
#include "World/ZoneLinkRegistry.h"
#include "Worker/ProcessorId.h"

#include "App/Config.h"

#include <asio.hpp>

namespace World
{
    // 전체를 조립하는 곳: Gateway용/Zone용 accept 포트 두 개, 클라이언트/Zone 라우팅 테이블,
    // DB 워커 풀을 한데 묶는다. App과 구조는 같지만 "게임 로직 스레드" 대신
    // WorldWorker(단일 처리 스레드) + DB 워커 풀이 로직 스레드 역할을 한다.
    class App
    {
    public:
        explicit App(Config config);

        void Run();
        void Stop();

        // 접속 중인 모든 클라이언트에게 Zone을 거치지 않고 직접 브로드캐스트한다 -- World가
        // 전체 클라이언트 레지스트리를 들고 있기 때문에 가능하다. 콘솔 REPL 스레드에서
        // 호출되므로(I/O 스레드가 아닌 또 다른 생산자) 이 역시 clientRegistry_를 직접 만지지
        // 않고 WorldWorker로 넘긴다.
        void BroadcastToAll(const PacketId clientPacketId, const std::span<const byte> payload);

    private:
        void SetupSignalHandling();

        Config config_;
        Network::IoContextPool ioPool_;

        // 선언 순서 = 생성 순서다. 큐 그룹이 레지스트리보다 **먼저** 와야 한다 --
        // clientRegistry_가 basicGroup_.ThreadCount()를 받아 샤드를 나누기 때문이다.
        Processor::Group<EProcessorId> basicGroup_;
        Processor::Group<EProcessorId> dbGroup_;

        ClientRegistry clientRegistry_;
        ZoneLinkRegistry::Mutexed zoneLinkRegistry_;
        GatewayLinkHandler gatewayLinkHandler_;
        ZoneLinkHandler zoneLinkHandler_;
        ToolProcessor toolProcessor_;
        std::shared_ptr<Network::Listener> gatewayListener_;
        std::shared_ptr<Network::Listener> zoneListener_;
        std::shared_ptr<Network::Listener> toolListener_;
        std::unique_ptr<Timer::RepeatingTimer> statsTimer_;
        asio::signal_set signals_;
    };
}
