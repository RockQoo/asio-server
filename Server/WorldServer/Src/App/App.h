#pragma once

#include "Shared/Core/Src/Network/IoContextPool.h"
#include "Shared/Core/Src/Network/Listener.h"
#include "Shared/Core/Src/Timer/RepeatingTimer.h"
#include "Shared/Core/Src/Processor/Group.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Db/DbConnection.h"
#include "Handler/GatewayLinkHandler.h"
#include "Handler/ZoneLinkHandler.h"
#include "Login/LoginProcessor.h"
#include "Tool/ToolProcessor.h"
#include "World/PlayerManager.h"
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
        // 호출되므로(I/O 스레드가 아닌 또 다른 생산자) 이 역시 playerManager_를 직접 만지지
        // 않고 WorldWorker로 넘긴다.
        void BroadcastToAll(const PacketId clientPacketId, const std::span<const byte> payload);

    private:
        void SetupSignalHandling();

        Config config_;
        Network::IoContextPool ioPool_;

        // 선언 순서 = 생성 순서다.
        Processor::Group<EProcessorId> basicGroup_;
        Processor::Group<EProcessorId> dbGroup_;

        // 둘 다 Mutexed다 -- World는 기본이 "남는 스레드"라 어느 레인에서든 읽힐 수 있고,
        // 어피니티로는 지킬 수 없는 자리다(각 클래스 주석 참고).
        PlayerManager::Mutexed playerManager_;
        ZoneLinkRegistry::Mutexed zoneLinkRegistry_;

        // 커넥션은 DB 레인 스레드마다 thread_local로 하나씩 만들어진다. 그래서 여기서 만드는
        // 것은 "연결 문자열을 든 팩토리" 하나뿐이고, 실제 연결은 첫 쿼리에서 열린다 --
        // 기동 시점에 DB가 안 떠 있어도 서버는 뜨고, 로그인만 LoginDbFailure로 실패한다.
        DbConnectionPool dbPool_;

        // gatewayLinkHandler_가 참조로 물고 있으므로 **반드시 그보다 먼저 선언한다**
        // (멤버 초기화 순서 = 선언 순서).
        LoginProcessor loginProcessor_;
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
