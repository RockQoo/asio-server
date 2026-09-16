#include "pch.h"
#include "App/WorldApp.h"
#include "Shared/Common/Src/PacketId.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Test/TestKeys.h"
#include "Processor/TestProcessor.h"

WorldApp::WorldApp(WorldConfig config)
    : config_(std::move(config))
    , ioPool_(config_.ioThreadCount)
    , basicGroup_("Basic", config_.basicThreadCount, config_.slowTaskWarnThreshold)
    , dbGroup_("Db", config_.dbThreadCount, config_.slowTaskWarnThreshold)
    , dbPool_(config_.dbConnectionString)
    , dbProcessor_(dbPool_, dbGroup_)
    , loginProcessor_(playerManager_, zoneLinkRegistry_, basicGroup_, dbProcessor_)
    , mainProcessor_(playerManager_, zoneLinkRegistry_, basicGroup_, dbProcessor_, loginProcessor_)
    , gatewayLinkHandler_(basicGroup_, mainProcessor_)
    , zoneLinkHandler_(basicGroup_, mainProcessor_)
    , toolProcessor_(playerManager_, zoneLinkRegistry_, basicGroup_, dbProcessor_, config_.toolSharedSecret)
    , signals_(ioPool_.At(0), SIGINT, SIGTERM)
{
    // **전역 핸들을 여기서 세운다.** 이 시점부터 PushMsg가 이 App의 라우터를 찾는다.
    MsgRouter::SetCurrent(&msgRouter_);
}

WorldApp::~WorldApp()
{
    // **멤버(Group들)가 소멸되기 전에 지운다** -- 소멸자 본문이 멤버 소멸보다 먼저 도므로
    // 순서가 항상 맞는다. 이걸 빠뜨리면 죽은 Group을 가리키는 포인터가 남는다.
    MsgRouter::SetCurrent(nullptr);

}
void WorldApp::Run()
{
    basicGroup_.Start();
    dbGroup_.Start();

    gatewayListener_ = std::make_shared<Network::Listener>(ioPool_.At(0), ioPool_, config_.gatewayPort, gatewayLinkHandler_);
    gatewayListener_->Start();

    zoneListener_ = std::make_shared<Network::Listener>(ioPool_.At(0), ioPool_, config_.zonePort, zoneLinkHandler_);
    zoneListener_->Start();

    toolListener_ = std::make_shared<Network::Listener>(ioPool_.At(0), ioPool_, config_.toolPort, toolProcessor_);
    toolListener_->Start();

    // 레인별 대기/처리 시간을 주기적으로 남긴다 -- 부하 상황에서 "어느 레인이 밀렸나"를
    // 판정할 유일한 수단이다(처리량 수치만으로는 느리다는 것까지만 알 수 있다).
    statsTimer_ = std::make_unique<Timer::RepeatingTimer>(ioPool_.Next());
    statsTimer_->Start(config_.statsDumpInterval, [this]
    {
        basicGroup_.LogStats();
        dbGroup_.LogStats();
    });

    SetupSignalHandling();

    // 테스트 하네스. 묶인 키가 없으면 Start()가 스레드를 만들지 않는다.
    // 어느 프로세서 메시지를 어느 그룹에서 돌릴지 먼저 등록하고, 그다음 핸들러를 묶는다.
    // **둘 다 KeyBinder::Start()보다 앞이다** -- 레인 스레드가 락 없이 읽는 표라 도는
    // 중에 바꾸면 경합한다.
    msgRouter_.Register(EWorldProcessorId::Test, basicGroup_);
    TestProcessor::Register(msgRouter_);
    RegisterTestKeys(keyBinder_);
    keyBinder_.Start();

    LOG.Info(ELogCategory::General, "WorldServer 대기 시작")
        .KV("GatewayPort", config_.gatewayPort).KV("ZonePort", config_.zonePort)
        .KV("ToolPort", config_.toolPort)
        .KV("IoThreads", config_.ioThreadCount)
        .KV("BasicThreads", basicGroup_.ThreadCount())
        .KV("DbThreads", dbGroup_.ThreadCount());

    ioPool_.Run();
    ioPool_.Join();

    // **종속 관계의 역순으로 내린다**: BASIC이 DB에 일을 던지므로 BASIC을 먼저 세워야
    // DB로 새 일이 더 들어오지 않는다. 그리고 **DB 그룹이 마지막까지 남아 밀린 저장을
    // 소진**해야 한다 -- 급하게 내리면 몇 초 분량의 플레이 결과가 사라진다.
    // (Group::Stop()은 work_guard를 놓아 남은 작업을 소진시킨 뒤 스레드를 join한다.)
    // **BASIC을 세우기 전에 입력 스레드를 멈춘다** -- 안 그러면 F키 한 번이 이미 닫히는
    // 중인 레인으로 메시지를 밀어 넣는다.
    keyBinder_.Stop();

    basicGroup_.Stop();
    dbGroup_.Stop();
    LOG.Info(ELogCategory::General, "WorldServer 종료 완료");
}

void WorldApp::Stop()
{
    if (gatewayListener_)
    {
        gatewayListener_->Stop();
    }
    if (zoneListener_)
    {
        zoneListener_->Stop();
    }
    if (toolListener_)
    {
        toolListener_->Stop();
    }
    ioPool_.Stop();
}

void WorldApp::SetupSignalHandling()
{
    signals_.async_wait([this](const std::error_code ec, const int signalNumber)
    {
        if (!ec)
        {
            LOG.Info(ELogCategory::General, "시그널 수신, 종료 절차 시작").KV("Signal", signalNumber);
            Stop();
        }
    });
}
