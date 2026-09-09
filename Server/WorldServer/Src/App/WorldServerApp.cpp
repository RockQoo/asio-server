#include "Server/WorldServer/Src/pch.h"
#include "Server/WorldServer/Src/App/WorldServerApp.h"
#include "Server/WorldServer/Src/Packet/RelayEnvelope.h"
#include "Shared/Protocol/Src/PacketId.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"

#include <csignal>
#include <utility>
#include <vector>

namespace World
{
    WorldServerApp::WorldServerApp(WorldServerConfig config)
        : config_(std::move(config))
        , ioPool_(config_.ioThreadCount)
        , basicGroup_("Basic", config_.basicThreadCount, config_.slowTaskWarnThreshold)
        , dbGroup_("Db", config_.dbThreadCount, config_.slowTaskWarnThreshold)
        // 샤드 개수를 BASIC 스레드 수와 맞춘다 -- 둘 다 `% N`으로 나누므로 같아야 "그 샤드를
        // 만지는 스레드가 항상 하나"가 성립한다(ClientRegistry.h 주석 참고).
        , clientRegistry_(basicGroup_.ThreadCount())
        , gatewayLinkHandler_(clientRegistry_, zoneLinkRegistry_, basicGroup_)
        , zoneLinkHandler_(clientRegistry_, zoneLinkRegistry_, basicGroup_, dbGroup_)
        , toolProcessor_(clientRegistry_, zoneLinkRegistry_, basicGroup_, dbGroup_, config_.toolSharedSecret)
        , signals_(ioPool_.At(0), SIGINT, SIGTERM)
    {
    }

    void WorldServerApp::Run()
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
        // (WorkerThread::Stop()은 큐에 남은 작업을 소진한 뒤 스레드를 join한다.)
        basicGroup_.Stop();
        dbGroup_.Stop();
        LOG.Info(ELogCategory::General, "WorldServer 종료 완료");
    }

    void WorldServerApp::Stop()
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

    void WorldServerApp::BroadcastToAll(const PacketId clientPacketId, const std::span<const byte> payload)
    {
        // 콘솔 REPL 스레드에서 호출되므로(I/O 스레드가 아닌 또 다른 생산자) clientRegistry_를
        // 직접 만지지 않고 BASIC 그룹으로 넘긴다. payload는 호출자의 지역 버퍼를 가리키므로,
        // 비동기 메시지로 넘어가기 전에 복사해서 소유권을 옮긴다.
        //
        // 전체 대상이라 **샤드마다 메시지를 하나씩** 던진다 -- clientRegistry_가
        // clientSessionId로 샤딩돼 있어 전부 순회할 수 있는 스레드가 없기 때문이다
        // (ToolProcessor::ScatterToShards와 같은 이유이고, 여기는 응답할 곳이 없어 취합이
        // 필요 없으므로 헬퍼 없이 단순 팬아웃으로 둔다).
        std::vector<byte> payloadCopy(payload.begin(), payload.end());

        for (size_t shardIndex = 0; shardIndex < clientRegistry_.ShardCount(); ++shardIndex)
        {
            basicGroup_.Post(EProcessorId::Main, shardIndex,
                [this, clientPacketId, shardIndex, payloadCopy]
                {
                    ClientEnvelopeHeader header{};
                    header.innerPacketId = static_cast<uint16_t>(clientPacketId);

                    size_t sentCount = 0;
                    clientRegistry_.ForEachInShard(shardIndex,
                        [&](const Network::SessionId clientSessionId, const ClientInfo& info)
                        {
                            if (!info.gatewaySession)
                            {
                                return;
                            }

                            header.clientSessionId = clientSessionId;
                            Packet::BinaryWriter envelopeWriter;
                            envelopeWriter.Write(header);
                            envelopeWriter.WriteBytes(payloadCopy);
                            info.gatewaySession->SendPacket(PacketId::W2GRelay, envelopeWriter.GetBuffer());
                            ++sentCount;
                        });

                    LOG.Info(ELogCategory::General, "전체 브로드캐스트 처리(샤드)")
                        .KV("PacketId", static_cast<uint16_t>(clientPacketId))
                        .KV("Shard", shardIndex)
                        .KV("RegisteredClients", clientRegistry_.CountInShard(shardIndex))
                        .KV("SentTo", sentCount);
                });
        }
    }

    void WorldServerApp::SetupSignalHandling()
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
