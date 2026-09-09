#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Network/IPacketHandler.h"
#include "Shared/Core/Src/Packet/PacketDispatcher.h"
#include "Shared/Core/Src/Processor/ProcessorGroup.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Server/WorldServer/Src/World/ZoneLinkRegistry.h"
#include "Server/WorldServer/Src/Worker/ProcessorId.h"

#include <cstdint>

namespace World
{
    class ClientRegistry;

    // Gateway <-> World 연결의 IPacketHandler. Gateway가 accept한 각 클라이언트를 대신 통지받아
    // ClientRegistry에 등록/해제하고, 클라이언트 패킷(FromClient)을 그 클라이언트가 현재 있는
    // 존으로 릴레이한다.
    //
    // 스레드 규약: OnPacket은 이 연결의 I/O 스레드(Session의 strand)에서 실행된다. 거기서는
    // **ownerId(=clientSessionId)만 훔쳐보고** 바이트를 복사해 BASIC 큐 그룹에 넣을 뿐이고,
    // ClientRegistry/ZoneLinkRegistry 접근은 전부 그 ownerId가 배정한 스레드에서 일어난다.
    // 같은 클라이언트의 메시지는 언제나 같은 스레드로 가므로 그 클라이언트의 레지스트리
    // 항목에는 락이 필요 없다(ClientRegistry 샤딩 주석 참고).
    //
    // 이 핸들러가 다루는 세 패킷은 전부 페이로드 맨 앞이 clientSessionId(uint64)라 훔쳐보기가
    // 오프셋 0 하나로 끝난다 -- 우연이 아니라, 릴레이 봉투(ClientEnvelopeHeader)를 그렇게
    // 설계했기 때문이다.
    class GatewayLinkHandler final : public Network::IPacketHandler
    {
    public:
        GatewayLinkHandler(ClientRegistry& clientRegistry, ZoneLinkRegistry::Mutexed& zoneLinkRegistry,
                           Processor::ProcessorGroup<EProcessorId>& basicGroup);

        void OnSessionOpened(const std::shared_ptr<Network::Session>& session) override;
        void OnPacket(const std::shared_ptr<Network::Session>& session,
                      const Packet::PacketHeader& header,
                      const std::span<const byte> payload) override;
        void OnClosed(const std::shared_ptr<Network::Session>& session, const std::error_code& reason) override;

    private:
        void RegisterHandlers();

        void HandleClientConnected(const std::shared_ptr<Network::Session>& gatewaySession, const std::span<const byte> payload);
        void HandleClientDisconnected(const std::shared_ptr<Network::Session>& gatewaySession, const std::span<const byte> payload);
        void HandleFromClient(const std::shared_ptr<Network::Session>& gatewaySession, const std::span<const byte> payload);

        ClientRegistry& clientRegistry_;
        ZoneLinkRegistry::Mutexed& zoneLinkRegistry_;
        Processor::ProcessorGroup<EProcessorId>& basicGroup_;
        Packet::PacketDispatcher<PacketId, std::shared_ptr<Network::Session>> dispatcher_;
    };
}
