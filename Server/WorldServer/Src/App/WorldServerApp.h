#pragma once

#include "Shared/Core/Src/Network/IoContextPool.h"
#include "Shared/Core/Src/Network/Listener.h"
#include "Shared/Core/Src/Thread/AffinityWorkerPool.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Server/WorldServer/Src/Db/DbWorker.h"
#include "Server/WorldServer/Src/Handler/GatewayLinkHandler.h"
#include "Server/WorldServer/Src/Handler/ZoneLinkHandler.h"
#include "Server/WorldServer/Src/Tool/ToolProcessor.h"
#include "Server/WorldServer/Src/World/ClientRegistry.h"
#include "Server/WorldServer/Src/World/ZoneLinkRegistry.h"
#include "Server/WorldServer/Src/Worker/WorldWorker.h"

#include <asio.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace World
{
    struct WorldServerConfig
    {
        uint16_t gatewayPort{9100};
        uint16_t zonePort{9200};
        // 운영툴(Tool/GmTool) 전용 accept 포트. 클라이언트 트래픽이 오는 게이트웨이 포트와
        // 분리해둬야 "운영 권한 패킷은 이 포트에서만 온다"가 성립한다(ToolProcessor 주석 참고).
        uint16_t toolPort{9300};
        size_t ioThreadCount{2};
        size_t dbWorkerCount{2};
        // 운영툴 링크의 공유 시크릿. 개발 기본값이며 실제 운영에서는 환경 변수
        // ASIO_SERVER_TOOL_SECRET로 덮어쓴다(main.cpp 참고).
        std::string toolSharedSecret{"dev-only-gmtool-secret"};
    };

    // 전체를 조립하는 곳: Gateway용/Zone용 accept 포트 두 개, 클라이언트/Zone 라우팅 테이블,
    // DB 워커 풀을 한데 묶는다. ZoneServerApp과 구조는 같지만 "게임 로직 스레드" 대신
    // WorldWorker(단일 처리 스레드) + DB 워커 풀이 로직 스레드 역할을 한다.
    class WorldServerApp
    {
    public:
        explicit WorldServerApp(WorldServerConfig config);

        void Run();
        void Stop();

        // 접속 중인 모든 클라이언트에게 Zone을 거치지 않고 직접 브로드캐스트한다 -- World가
        // 전체 클라이언트 레지스트리를 들고 있기 때문에 가능하다. 콘솔 REPL 스레드에서
        // 호출되므로(I/O 스레드가 아닌 또 다른 생산자) 이 역시 clientRegistry_를 직접 만지지
        // 않고 WorldWorker로 넘긴다.
        void BroadcastToAll(const PacketId clientPacketId, const std::span<const byte> payload);

    private:
        void SetupSignalHandling();

        WorldServerConfig config_;
        Network::IoContextPool ioPool_;
        ClientRegistry clientRegistry_;
        ZoneLinkRegistry zoneLinkRegistry_;
        Thread::AffinityWorkerPool<Db::DbWorker> dbWorkers_;
        WorldWorker worldWorker_;
        GatewayLinkHandler gatewayLinkHandler_;
        ZoneLinkHandler zoneLinkHandler_;
        ToolProcessor toolProcessor_;
        std::shared_ptr<Network::Listener> gatewayListener_;
        std::shared_ptr<Network::Listener> zoneListener_;
        std::shared_ptr<Network::Listener> toolListener_;
        asio::signal_set signals_;
    };
}
