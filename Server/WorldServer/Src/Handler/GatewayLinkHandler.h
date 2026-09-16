#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Network/IPacketHandler.h"
#include "Shared/Core/Src/Packet/Dispatcher.h"
#include "Shared/Core/Src/Processor/Group.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "World/PlayerManager.h"
#include "World/ZoneLinkRegistry.h"
#include "Worker/ProcessorId.h"

namespace World
{
    // PlayerManager::Mutexed 를 쓰므로 전방 선언으로는 부족하다.
    class LoginProcessor;

    // Gateway <-> World 연결의 IPacketHandler. 클라이언트 등록/해제를 통지받고, 클라이언트
    // 패킷을 그 사람이 현재 있는 존으로 릴레이한다.
    //
    // **여기가 World가 클라이언트 프로토콜을 유일하게 들여다보는 자리다.** 봉투 안 id의 방향이
    // C2W면(= World가 끝점) 존으로 넘기지 않고 LoginProcessor로 보낸다. 나머지는 예전처럼
    // 해석하지 않고 그대로 재전송한다.
    //
    // **스레드 규약**: OnPacket은 이 연결의 I/O 스레드에서 돈다. 거기서는 ownerId
    // (=clientSessionId)만 훔쳐보고 바이트를 복사해 큐에 넣을 뿐이고, 레지스트리 접근은 전부
    // 그 ownerId가 배정한 레인에서 일어난다 -- 그래서 락이 없다.
    //
    // 세 패킷 모두 페이로드 맨 앞이 clientSessionId라 훔쳐보기가 오프셋 0 하나로 끝난다.
    // 우연이 아니라 릴레이 봉투를 그렇게 설계했기 때문이다.
    class GatewayLinkHandler final : public Network::IPacketHandler
    {
    public:
        GatewayLinkHandler(PlayerManager::Mutexed& playerManager, ZoneLinkRegistry::Mutexed& zoneLinkRegistry,
                           Processor::Group<EProcessorId>& basicGroup, LoginProcessor& loginProcessor);

        void OnSessionOpened(const Network::Session::SPtr& session) override;
        void OnPacket(const Network::Session::SPtr& session,
                      const Packet::Header& header,
                      const std::span<const byte> payload) override;
        void OnClosed(const Network::Session::SPtr& session, const std::error_code& reason) override;

    private:
        void RegisterHandlers();

        void HandleClientConnected(const Network::Session::SPtr& gatewaySession, const std::span<const byte> payload);
        void HandleClientDisconnected(const Network::Session::SPtr& gatewaySession, const std::span<const byte> payload);
        void HandleFromClient(const Network::Session::SPtr& gatewaySession, const std::span<const byte> payload);

        PlayerManager::Mutexed& playerManager_;
        ZoneLinkRegistry::Mutexed& zoneLinkRegistry_;
        Processor::Group<EProcessorId>& basicGroup_;
        LoginProcessor& loginProcessor_;
        Packet::Dispatcher<PacketId, Network::Session::SPtr> dispatcher_;
    };
}
