#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Network/IPacketHandler.h"
#include "Shared/Core/Src/Packet/PacketDispatcher.h"
#include "Shared/Core/Src/Thread/AffinityWorkerPool.h"
#include "Server/WorldServer/Src/Db/DbWorker.h"
#include "Server/WorldServer/Src/Packet/ToolLinkPacketId.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_set>

namespace World
{
    class ClientRegistry;
    class ZoneLinkRegistry;
    class WorldWorker;

    // 운영툴(Tool/GmTool) <-> World 연결의 IPacketHandler. GatewayLinkHandler/ZoneLinkHandler와
    // 같은 자리에 있는 세 번째 링크 핸들러이며, 전용 accept 포트(WorldServerConfig::toolPort)
    // 하나를 담당한다.
    //
    // 스레드 규약은 다른 두 핸들러와 완전히 동일하다: OnPacket은 이 연결의 I/O 스레드(Session의
    // strand)에서 실행되므로 바이트만 복사해 WorldWorker로 넘기고, ClientRegistry/
    // ZoneLinkRegistry를 실제로 만지는 Handle* 메서드들은 전부 그 단일 스레드에서 돈다.
    // authenticatedSessions_도 그래서 락이 없다 -- WorldWorker 스레드만 이 집합을 건드린다.
    //
    // 설계상 중요한 점: 이 클래스는 우편/공지의 "내용"을 새로 정의하지 않는다. 우편은 기존
    // 클라이언트 패킷(Zone::PacketId::MailAdd/MailDel)을 ClientEnvelopeHeader로 감싸 존에
    // 주입하는 방식이라(ForwardToZone), Zone/Mail/UnitOfWork/DbWorker 경로가 평소 클라이언트
    // 요청과 한 글자도 다르지 않게 흐른다. 운영툴이 게임 로직의 별도 우회로를 만들지 않는 게
    // 목적이다 -- 우회로를 만들면 "운영툴로 넣은 우편만 만료가 안 된다" 같은 사고가 난다.
    class ToolProcessor final : public Network::IPacketHandler
    {
    public:
        ToolProcessor(ClientRegistry& clientRegistry, ZoneLinkRegistry& zoneLinkRegistry,
                      Thread::AffinityWorkerPool<Db::DbWorker>& dbWorkers, WorldWorker& worldWorker,
                      std::string sharedSecret);

        void OnSessionOpened(const std::shared_ptr<Network::Session>& session) override;
        void OnPacket(const std::shared_ptr<Network::Session>& session,
                      const Packet::PacketHeader& header,
                      const std::span<const byte> payload) override;
        void OnClosed(const std::shared_ptr<Network::Session>& session, const std::error_code& reason) override;

    private:
        void RegisterHandlers();

        void HandleToolHello(const std::shared_ptr<Network::Session>& toolSession, const std::span<const byte> payload);
        void HandleNoticeRequest(const std::shared_ptr<Network::Session>& toolSession, const std::span<const byte> payload);
        void HandleMailSendRequest(const std::shared_ptr<Network::Session>& toolSession, const std::span<const byte> payload);
        void HandleMailDeleteRequest(const std::shared_ptr<Network::Session>& toolSession, const std::span<const byte> payload);
        void HandleCouponChunkPush(const std::shared_ptr<Network::Session>& toolSession, const std::span<const byte> payload);
        void HandleClientListRequest(const std::shared_ptr<Network::Session>& toolSession, const std::span<const byte> payload);

        // ToolHello를 통과하지 않은 세션의 요청은 전부 NotAuthenticated로 거절한다. 인증 자체는
        // 공유 시크릿 비교 한 번뿐이지만, 운영툴 링크는 "이 포트에 붙을 수 있는 프로세스"를
        // 방화벽/루프백으로 제한하는 것이 실질적인 1차 방어선이고 이건 그 위의 최소 확인이다.
        [[nodiscard]] bool IsAuthenticated(const Network::SessionId toolSessionId) const;

        void SendCommandAck(const std::shared_ptr<Network::Session>& toolSession, const uint32_t requestId,
                            const EToolResultCode resultCode, const uint32_t affectedCount) const;

        // 클라이언트 한 명에게 "그 클라이언트가 보낸 것처럼" 원본 클라이언트 패킷을 존에
        // 주입한다. 성공(대상이 접속 중이고 그 존과 연결이 살아있음) 여부를 반환한다.
        [[nodiscard]] bool InjectClientPacket(const Network::SessionId clientSessionId, const uint16_t innerPacketId,
                                              const std::span<const byte> innerPayload) const;

        // ClientListReply 한 패킷에 담을 항목 수 상한. PacketHeader::MaxBodySize()가 8192이고
        // 항목 하나가 12바이트라 여유를 둬서 500개로 잡았다(8 + 500*12 = 6008바이트).
        static constexpr size_t kMaxClientListEntries = 500;

        ClientRegistry& clientRegistry_;
        ZoneLinkRegistry& zoneLinkRegistry_;
        Thread::AffinityWorkerPool<Db::DbWorker>& dbWorkers_;
        WorldWorker& worldWorker_;
        std::string sharedSecret_;

        // WorldWorker 스레드 전용 -- 그래서 락이 없다(클래스 주석 참고).
        std::unordered_set<Network::SessionId> authenticatedSessions_;

        Packet::PacketDispatcher<ToolLinkPacketId, std::shared_ptr<Network::Session>> dispatcher_;
    };
}
