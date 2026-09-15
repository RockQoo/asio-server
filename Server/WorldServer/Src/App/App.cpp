#include "pch.h"
#include "App/App.h"
#include "Packet/RelayEnvelope.h"
#include "Shared/Protocol/Src/PacketId.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"
#include "Test/TestKeys.h"
#include "Test/TestProcessor.h"

namespace World
{
    App::App(Config config)
        : config_(std::move(config))
        , ioPool_(config_.ioThreadCount)
        , basicGroup_("Basic", config_.basicThreadCount, config_.slowTaskWarnThreshold)
        , dbGroup_("Db", config_.dbThreadCount, config_.slowTaskWarnThreshold)
        , dbPool_(config_.dbConnectionString)
        , loginProcessor_(playerManager_, zoneLinkRegistry_, basicGroup_, dbGroup_, dbPool_)
        , gatewayLinkHandler_(playerManager_, zoneLinkRegistry_, basicGroup_, loginProcessor_)
        , zoneLinkHandler_(playerManager_, zoneLinkRegistry_, basicGroup_, dbGroup_, dbPool_)
        , toolProcessor_(playerManager_, zoneLinkRegistry_, basicGroup_, dbGroup_, config_.toolSharedSecret)
        , signals_(ioPool_.At(0), SIGINT, SIGTERM)
    {
        // **전역 핸들을 여기서 세운다.** 이 시점부터 PushMsg가 이 App의 라우터를 찾는다.
        MsgRouter::SetCurrent(&msgRouter_);
    }

    App::~App()
    {
        // **멤버(Group들)가 소멸되기 전에 지운다** -- 소멸자 본문이 멤버 소멸보다 먼저 도므로
        // 순서가 항상 맞는다. 이걸 빠뜨리면 죽은 Group을 가리키는 포인터가 남는다.
        MsgRouter::SetCurrent(nullptr);

    }
    void App::Run()
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
        msgRouter_.Register(EProcessorId::Test, basicGroup_);
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

    void App::Stop()
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

    void App::BroadcastToAll(const PacketId clientPacketId, const std::span<const byte> payload)
    {
        // 콘솔 REPL 스레드에서 호출되므로(I/O 스레드가 아닌 또 다른 생산자) 여기서 바로 돌지
        // 않고 BASIC 그룹으로 넘긴다. payload는 호출자의 지역 버퍼를 가리키므로, 비동기
        // 메시지로 넘어가기 전에 복사해서 소유권을 옮긴다.
        //
        // **한 메시지로 끝난다** -- 레지스트리가 Mutexed가 되면서 읽기 락 하나로 전체를 순회할
        // 수 있게 됐다. 샤딩이던 시절에는 "전부 순회할 수 있는 스레드"가 없어서 샤드마다
        // 메시지를 던지고 결과를 취합해야 했다.
        //
        // **ownerId를 주지 않는다** -- 대상이 전 클라이언트라 주인이 없고, 공지끼리 순서를
        // 맞출 필요도 없다. 주인을 억지로 0으로 주면 공지가 항상 0번 strand로만 가서, 전체
        // 순회라는 무거운 일이 레인 하나에 쌓인다. 남는 스레드가 집어가게 둔다.
        std::vector<byte> payloadCopy(payload.begin(), payload.end());

        basicGroup_.Post(EProcessorId::Main,
            [this, clientPacketId, payloadCopy = std::move(payloadCopy)]
            {
                ClientEnvelopeHeader header{};
                header.innerPacketId = static_cast<uint16_t>(clientPacketId);

                size_t sentCount = 0;
                size_t registeredCount = 0;

                playerManager_->ForEach(
                    [&](const Network::SessionId clientSessionId, const PlayerInfo& info)
                    {
                        ++registeredCount;
                        if (!info.gatewaySession)
                        {
                            return;
                        }

                        header.clientSessionId = clientSessionId;
                        Packet::BinaryWriter envelopeBinaryWriter;
                        envelopeBinaryWriter.Write(header);
                        envelopeBinaryWriter.WriteBytes(payloadCopy);
                        info.gatewaySession->SendPacket(PacketId::W2GRelay, envelopeBinaryWriter.GetBuffer());
                        ++sentCount;
                    });

                LOG.Info(ELogCategory::General, "전체 브로드캐스트 처리")
                    .KV("PacketId", static_cast<uint16_t>(clientPacketId))
                    .KV("RegisteredClients", registeredCount)
                    .KV("SentTo", sentCount);
            });
    }

    void App::SetupSignalHandling()
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
}
