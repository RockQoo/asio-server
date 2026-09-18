#pragma once

#include "Server/Core/Src/Console/KeyBinder.h"
#include "Server/Core/Src/Network/Service.h"
#include "Server/Core/Src/Processor/Group.h"
#include "Player/PlayerRegistry.h"
#include "App/ZoneDef.h"
#include "Processor/PlayerProcessor.h"
#include "Handler/W2ZHandler.h"
#include "Mail/MailExpiryService.h"
#include "Mail/MailRegistry.h"
#include "Server/Core/Src/Network/SessionHolder.h"
#include "Processor/ProcessorId.h"
#include "Worker/WorkerManager.h"

#include "App/ZoneConfig.h"

#include <asio.hpp>

namespace Timer
{
    class RepeatingTimer;
}

// 전체를 조립하는 곳. 레인이 셋이고 **주인이 서로 다르다**는 것이 이 파일에서 읽혀야 한다:
//
//   Player (owner = clientSessionId)  수신 파싱 · 우편 · 재화 · ZoneUnitOfWork · 이동 검증
//   Zone   (owner = zoneId)           로스터 · 위치 적분 · 경계 판정 (틱)
//   Broadcast (owner = zoneId)        팬아웃 전송
//
// 그리고 이 레인들과 무관하게 도는 우편 만료 타이머가 하나 더 있다 -- 그게 Mutexed가
// 실제로 필요한 지점이다(Model).
class ZoneApp
{
public:
    explicit ZoneApp(ZoneConfig config);
    ~ZoneApp();

    void Run();
    void Stop();

private:
    void SetupSignalHandling();

    ZoneConfig config_;

    // World로 나가는 링크 하나. 존 서버는 클라이언트를 직접 accept하지 않아서 Listener가 없다.
    Network::Service network_;

    Network::SessionHolder worldLink_;

    // 선언 순서 = 생성 순서. 큐 그룹이 레지스트리보다 먼저 와야 한다 --
    // playerRegistry_가 playerGroup_.ThreadCount()를 받아 샤드를 나누기 때문이다.
    Processor::Group<EZoneProcessorId> playerGroup_;

    PlayerRegistry playerRegistry_;
    MailRegistry mailRegistry_;
    WorkerManager zoneWorkers_;
    PlayerProcessor playerProcessor_;
    W2ZHandler worldLinkHandler_;
    MailExpiryService mailExpiryService_;

    // F키 테스트 하네스. 콜백은 전용 입력 스레드에서 돈다(레인이 아니다).
    Console::KeyBinder keyBinder_;
    std::unique_ptr<Timer::RepeatingTimer> mailExpiryTimer_;
    std::unique_ptr<Timer::RepeatingTimer> statsTimer_;
    asio::signal_set signals_;
};
