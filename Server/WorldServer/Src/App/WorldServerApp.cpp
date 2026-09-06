#include "Server/WorldServer/Src/pch.h"
#include "Server/WorldServer/Src/App/WorldServerApp.h"
#include "Server/WorldServer/Src/Packet/RelayEnvelope.h"
#include "Server/WorldServer/Src/Packet/GatewayLinkPacketId.h"

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
        , dbWorkers_(config_.dbWorkerCount)
        , gatewayLinkHandler_(clientRegistry_, zoneLinkRegistry_, worldWorker_)
        , zoneLinkHandler_(clientRegistry_, zoneLinkRegistry_, dbWorkers_, worldWorker_)
        , signals_(ioPool_.At(0), SIGINT, SIGTERM)
    {
    }

    void WorldServerApp::Run()
    {
        worldWorker_.Start();
        dbWorkers_.Start();

        gatewayListener_ = std::make_shared<Network::Listener>(ioPool_.At(0), ioPool_, config_.gatewayPort, gatewayLinkHandler_);
        gatewayListener_->Start();

        zoneListener_ = std::make_shared<Network::Listener>(ioPool_.At(0), ioPool_, config_.zonePort, zoneLinkHandler_);
        zoneListener_->Start();

        SetupSignalHandling();

        LOG.Info(ELogCategory::General, "WorldServer 대기 시작")
            .KV("GatewayPort", config_.gatewayPort).KV("ZonePort", config_.zonePort)
            .KV("DbWorkers", config_.dbWorkerCount);

        ioPool_.Run();
        ioPool_.Join();

        dbWorkers_.Stop();
        worldWorker_.Stop();
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
        ioPool_.Stop();
    }

    void WorldServerApp::BroadcastToAll(const uint16_t clientPacketId, const std::span<const byte> payload)
    {
        // 콘솔 REPL 스레드에서 호출되므로(I/O 스레드가 아닌 또 다른 생산자) clientRegistry_를
        // 직접 순회하지 않고 WorldWorker로 넘긴다. payload는 호출자의 지역 버퍼를 가리키므로,
        // 비동기 태스크로 넘어가기 전에 복사해서 소유권을 옮긴다.
        std::vector<byte> payloadCopy(payload.begin(), payload.end());

        worldWorker_.PostTask([this, clientPacketId, payloadCopy = std::move(payloadCopy)]
        {
            ClientEnvelopeHeader header{};
            header.innerPacketId = clientPacketId;

            size_t sentCount = 0;
            clientRegistry_.ForEach([&](const Network::SessionId clientSessionId, const ClientInfo& info)
            {
                if (!info.gatewaySession)
                {
                    return;
                }

                header.clientSessionId = clientSessionId;
                Packet::BinaryWriter envelopeWriter;
                envelopeWriter.Write(header);
                envelopeWriter.WriteBytes(payloadCopy);
                info.gatewaySession->SendPacket(static_cast<uint16_t>(GatewayLinkPacketId::ToClient), envelopeWriter.GetBuffer());
                ++sentCount;
            });

            LOG.Info(ELogCategory::General, "전체 브로드캐스트 처리").KV("PacketId", clientPacketId)
                .KV("RegisteredClients", clientRegistry_.Count()).KV("SentTo", sentCount);
        });
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
