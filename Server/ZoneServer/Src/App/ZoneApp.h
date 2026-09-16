#pragma once

#include "Shared/Core/Src/Console/KeyBinder.h"
#include "Shared/Core/Src/Network/Connector.h"
#include "Shared/Core/Src/Network/IoContextPool.h"
#include "Shared/Core/Src/Processor/Group.h"
#include "Game/PlayerRegistry.h"
#include "App/ZoneDef.h"
#include "Processor/PlayerProcessor.h"
#include "Handler/W2ZHandler.h"
#include "Mail/ExpiryService.h"
#include "Mail/Registry.h"
#include "Shared/Core/Src/Network/SessionHolder.h"
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
    Network::IoContextPool ioPool_;
    Network::SessionHolder worldLink_;

    // 선언 순서 = 생성 순서. 큐 그룹이 레지스트리보다 먼저 와야 한다 --
    // playerRegistry_가 playerGroup_.ThreadCount()를 받아 샤드를 나누기 때문이다.
    Processor::Group<EZoneProcessorId> playerGroup_;

    PlayerRegistry playerRegistry_;
    Mail::Registry mailRegistry_;
    WorkerManager zoneWorkers_;
    PlayerProcessor playerProcessor_;
    W2ZHandler worldLinkHandler_;
    Mail::ExpiryService mailExpiryService_;

    std::shared_ptr<Network::Connector> worldConnector_;

    // F키 테스트 하네스. 콜백은 전용 입력 스레드에서 돈다(레인이 아니다).
    Console::KeyBinder keyBinder_;
    std::unique_ptr<Timer::RepeatingTimer> mailExpiryTimer_;
    std::unique_ptr<Timer::RepeatingTimer> statsTimer_;
    asio::signal_set signals_;
};
