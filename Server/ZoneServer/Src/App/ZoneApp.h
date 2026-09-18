#pragma once

#include "App/ZoneConfig.h"
#include "Handler/W2ZHandler.h"
#include "Zone/Zone.h"

#include "Server/Core/Src/Console/KeyBinder.h"
#include "Server/Core/Src/Network/Service.h"
#include "Server/Core/Src/Network/SessionHolder.h"
#include "Server/Core/Src/Pipeline/ProducerHolder.h"

#include <asio.hpp>

namespace Timer
{
    class RepeatingTimer;
}

class TimerProcessor;

// 전체를 조립하는 곳. **레인이 다섯이고 주인이 서로 다르다**는 것이 이 파일에서 읽혀야 한다:
//
//   (소켓)     담당 존마다 World 링크 하나. 파이프라인 레인이 아니다
//   LB         owner = 링크 세션 id    봉투를 깐다
//   BASIC      owner = playerId        판단·예약·입퇴장·콘텐츠
//   TICK       owner = 존 틱 주인      적분·경계 판정 (해시를 끄는 유일한 레인)
//   BROADCAST  owner = zoneId          팬아웃 전송
//   TIMER      owner = 고정 상수       만기 판정만
class ZoneApp
{
public:
    explicit ZoneApp(ZoneConfig config);
    ~ZoneApp();

    void Run();
    void Stop();

private:
    void InitProducers();
    void SetupSignalHandling();

    // 담당 존 하나가 들고 있는 것. **연결이 존마다 하나라 링크도 여기 있다.**
    //
    // worldLink 를 unique_ptr 로 드는 이유: Zone 과 두 프로세서가 이 객체의 **참조**를
    // 들고 있어서 주소가 고정돼야 한다. vector 가 재할당되면 그 참조가 전부 매달린다.
    struct ZoneRuntime
    {
        std::unique_ptr<Network::SessionHolder> worldLink;
        Zone::SPtr zone;
        std::unique_ptr<W2ZHandler> handler;
    };

    ZoneConfig config_;

    // 존 서버는 클라이언트를 직접 accept 하지 않아서 Listener 가 없다 -- 나가는 연결뿐이다.
    Network::Service network_;

    std::vector<ZoneRuntime> zones_;

    // 소유권은 TIMER 레인에 있고 여기 남는 것은 포인터뿐이다. 타이머를 걸고 끄는 데 쓴다.
    TimerProcessor* timerProcessor_{nullptr};

    // F키 테스트 하네스. 콜백은 전용 입력 스레드에서 돈다(레인이 아니다).
    Console::KeyBinder keyBinder_;
    std::unique_ptr<Timer::RepeatingTimer> statsTimer_;
    asio::signal_set signals_;
};
