#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Network/IPacketHandler.h"
#include "Shared/Core/Src/Packet/PacketDispatcher.h"
#include "Shared/Core/Src/Processor/ProcessorGroup.h"
#include "Shared/Core/Src/Thread/Mutexed.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Server/WorldServer/Src/Packet/ToolResultCode.h"
#include "Server/WorldServer/Src/World/ZoneLinkRegistry.h"
#include "Server/WorldServer/Src/Worker/ProcessorId.h"

#include <cstddef>
#include <cstdint>
#include <functional>
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
    // 클라이언트 패킷(PacketId::C2ZMailAdd/MailDel)을 ClientEnvelopeHeader로 감싸 존에
    // 주입하는 방식이라(ForwardToZone), Zone/Mail/UnitOfWork/DbWorker 경로가 평소 클라이언트
    // 요청과 한 글자도 다르지 않게 흐른다. 운영툴이 게임 로직의 별도 우회로를 만들지 않는 게
    // 목적이다 -- 우회로를 만들면 "운영툴로 넣은 우편만 만료가 안 된다" 같은 사고가 난다.
    class ToolProcessor final : public Network::IPacketHandler
    {
    public:
        ToolProcessor(ClientRegistry& clientRegistry, ZoneLinkRegistry::Mutexed& zoneLinkRegistry,
                      Processor::ProcessorGroup<EProcessorId>& basicGroup,
                      Processor::ProcessorGroup<EProcessorId>& dbGroup,
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

        // 전체 클라이언트를 훑어야 하는 명령(공지, 접속 중 전체 우편, 목록 조회)의 팬아웃.
        //
        // **왜 이런 모양이 되는가**: ClientRegistry가 clientSessionId로 샤딩돼 있어서 전체를
        // 순회할 수 있는 스레드가 없다. 그래서 샤드마다 메시지를 하나씩(ownerId=샤드 인덱스)
        // 던져 각 스레드가 자기 몫만 처리하게 하고, 마지막으로 끝난 스레드가 합계를 모아
        // onComplete를 한 번 부른다. 어피니티로 락을 없앤 대가가 전역 작업의 이 팬아웃이다.
        //
        // perShard는 그 샤드를 소유한 스레드에서 실행되며 처리 건수를 반환한다.
        // onComplete는 마지막 샤드를 처리한 스레드에서 딱 한 번 실행된다(어느 스레드인지는
        // 정해지지 않으므로, 거기서 만지는 것은 전송뿐이어야 한다).
        void ScatterToShards(std::function<uint32_t(const size_t shardIndex)> perShard,
                             std::function<void(const uint32_t total)> onComplete);

        void SendCommandAck(const std::shared_ptr<Network::Session>& toolSession, const uint32_t requestId,
                            const EToolResultCode resultCode, const uint32_t affectedCount) const;

        // 클라이언트 한 명에게 "그 클라이언트가 보낸 것처럼" 원본 클라이언트 패킷을 존에
        // 주입한다. 성공(대상이 접속 중이고 그 존과 연결이 살아있음) 여부를 반환한다.
        [[nodiscard]] bool InjectClientPacket(const Network::SessionId clientSessionId, const PacketId innerPacketId,
                                              const std::span<const byte> innerPayload) const;

        // ClientListReply 한 패킷에 담을 항목 수 상한. PacketHeader::MaxBodySize()가 8192이고
        // 항목 하나가 12바이트라 여유를 둬서 500개로 잡았다(8 + 500*12 = 6008바이트).
        static constexpr size_t kMaxClientListEntries = 500;

        ClientRegistry& clientRegistry_;
        ZoneLinkRegistry::Mutexed& zoneLinkRegistry_;
        Processor::ProcessorGroup<EProcessorId>& basicGroup_;
        Processor::ProcessorGroup<EProcessorId>& dbGroup_;
        std::string sharedSecret_;

        // **왜 여기만 락인가**: 운영툴 명령은 대상이 전역이라 ownerId를 하나로 고정할 수 없고
        // (공지는 전 클라이언트, 쿠폰은 캠페인 코드), 그래서 이 집합은 BASIC의 여러 스레드에서
        // 보이게 된다. ownerId 어피니티로 못 막는 자리라 모델 단위 락으로 내려온 것이다.
        // 운영자가 손으로 누르는 명령이라 초당 수 건 수준이고, 샤딩까지 할 이유가 없다.
        Thread::Mutexed<std::unordered_set<Network::SessionId>> authenticatedSessions_;

        Packet::PacketDispatcher<PacketId, std::shared_ptr<Network::Session>> dispatcher_;
    };
}
