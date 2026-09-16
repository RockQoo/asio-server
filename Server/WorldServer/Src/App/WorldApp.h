#pragma once

#include "Shared/Core/Src/Console/KeyBinder.h"
#include "Shared/Core/Src/Network/IoContextPool.h"
#include "Shared/Core/Src/Network/Listener.h"
#include "Shared/Core/Src/Timer/RepeatingTimer.h"
#include "Shared/Core/Src/Processor/Group.h"
#include "Shared/Common/Src/PacketId.h"
#include "Db/DbConnection.h"
#include "Handler/G2WHandler.h"
#include "Handler/Z2WHandler.h"
#include "Processor/DbProcessor.h"
#include "Processor/LoginProcessor.h"
#include "Processor/MainProcessor.h"
#include "Test/MsgId.h"
#include "Processor/ToolProcessor.h"
#include "World/PlayerManager.h"
#include "World/ZoneLinkRegistry.h"
#include "Processor/ProcessorId.h"

#include "App/WorldConfig.h"

#include <asio.hpp>

// 전체를 조립하는 곳: Gateway용/Zone용 accept 포트 두 개, 클라이언트/Zone 라우팅 테이블,
// DB 워커 풀을 한데 묶는다. App과 구조는 같지만 "게임 로직 스레드" 대신
// WorldWorker(단일 처리 스레드) + DB 워커 풀이 로직 스레드 역할을 한다.
class WorldApp
{
public:
    explicit WorldApp(WorldConfig config);
    ~WorldApp();

    void Run();
    void Stop();


private:
    void SetupSignalHandling();

    WorldConfig config_;
    Network::IoContextPool ioPool_;

    // 선언 순서 = 생성 순서다.
    Processor::Group<EWorldProcessorId> basicGroup_;
    Processor::Group<EWorldProcessorId> dbGroup_;

    // 둘 다 Mutexed다 -- World는 기본이 "남는 스레드"라 어느 레인에서든 읽힐 수 있고,
    // 어피니티로는 지킬 수 없는 자리다(각 클래스 주석 참고).
    PlayerManager::Mutexed playerManager_;
    ZoneLinkRegistry::Mutexed zoneLinkRegistry_;

    // 커넥션은 DB 레인 스레드마다 thread_local로 하나씩 만들어진다. 그래서 여기서 만드는
    // 것은 "연결 문자열을 든 팩토리" 하나뿐이고, 실제 연결은 첫 쿼리에서 열린다 --
    // 기동 시점에 DB가 안 떠 있어도 서버는 뜨고, 로그인만 LoginDbFailure로 실패한다.
    DbConnectionPool dbPool_;

    // **선언 순서가 곧 의존 순서다**(멤버 초기화 순서 = 선언 순서). 링크 핸들러 둘은
    // mainProcessor_를, mainProcessor_는 loginProcessor_와 dbProcessor_를 참조로 물고 있다.
    DbProcessor dbProcessor_;
    LoginProcessor loginProcessor_;
    MainProcessor mainProcessor_;
    G2WHandler gatewayLinkHandler_;
    Z2WHandler zoneLinkHandler_;
    ToolProcessor toolProcessor_;

    // 테스트 하네스. **msgRouter_는 App이 소유한다**(싱글턴이 아니다) -- 전역에는 이걸
    // 가리키는 포인터만 두고 생성자/소멸자에서 세우고 지운다. 그래야 라우터가 참조하는
    // Group들보다 오래 살지 않는다(Router.h 참고).
    MsgRouter msgRouter_;

    // F키 입력 스레드. 콜백은 그 스레드에서 돌고, 서버 상태는 PushMsg로 레인에 넘긴다.
    Console::KeyBinder keyBinder_;
    std::shared_ptr<Network::Listener> gatewayListener_;
    std::shared_ptr<Network::Listener> zoneListener_;
    std::shared_ptr<Network::Listener> toolListener_;
    std::unique_ptr<Timer::RepeatingTimer> statsTimer_;
    asio::signal_set signals_;
};
