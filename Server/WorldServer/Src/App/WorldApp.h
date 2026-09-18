#pragma once

#include "Server/Core/Src/Console/KeyBinder.h"
#include "Server/Core/Src/Network/IoContextPool.h"
#include "Server/Core/Src/Network/Listener.h"
#include "Server/Core/Src/Timer/RepeatingTimer.h"
#include "Server/Core/Src/Pipeline/ProducerHolder.h"
#include "Db/DbConnection.h"
#include "Handler/G2WHandler.h"
#include "Handler/T2WHandler.h"
#include "Handler/Z2WHandler.h"
#include "World/PlayerManager.h"
#include "World/ZoneLinkRegistry.h"

#include "App/WorldConfig.h"

#include <asio.hpp>

// 전체를 조립하는 곳: Gateway용/Zone용/운영툴용 accept 포트 셋, 클라이언트/Zone 라우팅 테이블,
// 그리고 이 프로세스의 레인 전부.
//
// **프로세서 객체는 여기가 소유하지 않는다.** InitProducers()에서 make_unique로 만들어
// MessageProducer::AddProcessor에 넘기고, 돌려받은 ProcessorId만 Ids()에 보관한다 --
// 보내는 쪽은 그 id만 알면 되고 객체를 몰라도 된다.
class WorldApp
{
public:
    explicit WorldApp(WorldConfig config);
    ~WorldApp();

    void Run();
    void Stop();

private:
    // 레인을 만들고 프로세서를 등록한다. **World의 레인 구성은 이 함수가 전부다.**
    void InitProducers();
    void SetupSignalHandling();

    WorldConfig config_;

    // **소켓 단계.** IOCP 완료와 프레임 조립을 맡고, 파이프라인 레인(MessageProducer)과는
    // 별개의 스레드 벌이다 -- 스레드 수를 셀 때 레인 스레드와 따로 세어야 한다.
    // 이 단계를 레인으로 만들지 않은 근거: docs/design/network-lane.md
    Network::IoContextPool ioPool_;

    // 둘 다 Mutexed다 -- 공지처럼 주인이 없는 경로가 있어 어피니티로는 지킬 수 없다.
    PlayerManager::Mutexed playerManager_;
    ZoneLinkRegistry::Mutexed zoneLinkRegistry_;

    // 커넥션은 DB 레인 스레드마다 thread_local로 하나씩 만들어진다. 그래서 여기서 만드는
    // 것은 "연결 문자열을 든 팩토리" 하나뿐이고, 실제 연결은 첫 쿼리에서 열린다 --
    // 기동 시점에 DB가 안 떠 있어도 서버는 뜨고, 로그인만 LoginDbFailure로 실패한다.
    DbConnectionPool dbPool_;

    // 포트마다 하나. 소켓 스레드에서 돌며 바이트를 레인으로 넘기기만 한다.
    G2WHandler gatewayLinkHandler_;
    Z2WHandler zoneLinkHandler_;
    T2WHandler toolLinkHandler_;

    // TIMER 레인의 주기 작업을 거는 쪽. 프로세서 객체 자체는 레인이 소유하고, 여기서는
    // Start/Stop을 부르려고 포인터만 들고 있다.
    class TimerProcessor* timerProcessor_{nullptr};

    // F키 입력 스레드. 콜백은 그 스레드에서 돌고, 서버 상태는 PushMsg로 레인에 넘긴다.
    Console::KeyBinder keyBinder_;
    std::shared_ptr<Network::Listener> gatewayListener_;
    std::shared_ptr<Network::Listener> zoneListener_;
    std::shared_ptr<Network::Listener> toolListener_;
    std::unique_ptr<Timer::RepeatingTimer> statsTimer_;
    asio::signal_set signals_;
};
