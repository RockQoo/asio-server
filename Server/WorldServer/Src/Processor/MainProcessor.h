#pragma once

#include "Shared/Core/Src/Base/BasicTypes.h"
#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/Dispatcher.h"
#include "Shared/Core/Src/Processor/Group.h"
#include "Shared/Common/Src/PacketId.h"
#include "Packet/ZoneLinkPackets.h"
#include "Processor/DbProcessor.h"
#include "World/PlayerManager.h"
#include "World/ZoneLinkRegistry.h"
#include "Worker/ProcessorId.h"

namespace World
{
    // PlayerManager::Mutexed 를 쓰므로 전방 선언으로는 부족하다.
    class LoginProcessor;

    // BASIC 레인(EProcessorId::Main)에서 도는 라우팅 처리기.
    //
    // **링크 핸들러와 역할이 갈린다.** GatewayLinkHandler/ZoneLinkHandler는 I/O 스레드에서
    // 주인만 뽑아 던지는 데까지고, 던져진 일이 실제로 도는 곳이 여기다. 예전에는 둘이 한
    // 클래스에 있어서 "이 메서드가 I/O 스레드인가 레인인가"를 주석으로만 구분했다.
    //
    // **여기 있는 public 메서드는 전부 BASIC 레인에서 불린다**(owner = clientSessionId 또는
    // zoneId). 그래서 PlayerManager/ZoneLinkRegistry를 직접 만진다 -- 둘의 Mutexed는 공지처럼
    // 주인이 없는 경로 때문에 남아 있는 것이지, 여기가 아무 스레드에서나 불려서가 아니다.
    class MainProcessor
    {
    public:
        MainProcessor(PlayerManager::Mutexed& playerManager, ZoneLinkRegistry::Mutexed& zoneLinkRegistry,
                      Processor::Group<EProcessorId>& basicGroup, DbProcessor& dbProcessor,
                      LoginProcessor& loginProcessor);

        // Gateway 링크에서 온 패킷 / Zone 링크에서 온 패킷. 표를 둘로 나눠 둔 이유는 **어느
        // 링크로 들어올 수 있는 패킷인지가 표에 드러나게** 하려는 것이다(id는 겹치지 않아서
        // 하나로 합쳐도 돌기는 한다).
        void DispatchFromGateway(const PacketId packetId, const Network::Session::SPtr& gatewaySession,
                                 const std::span<const byte> payload);
        void DispatchFromZone(const PacketId packetId, const Network::Session::SPtr& zoneSession,
                              const std::span<const byte> payload);

        // Zone 링크가 끊겼다. 한 Zone 프로세스가 존 여러 개를 호스팅하므로 그 세션으로 등록된
        // zoneId를 **전부** 지워야 끊긴 세션으로 계속 라우팅되는 걸 막는다.
        void RemoveZoneLink(const Network::SessionId zoneSessionId);

        // 접속 중인 모든 클라이언트에게 Zone을 거치지 않고 직접 보낸다 -- World가 전체
        // 클라이언트 레지스트리를 들고 있어서 가능하다. **이것만 호출 스레드를 가리지 않는다**
        // (콘솔 입력 스레드에서도 부를 수 있게 안에서 BASIC으로 던진다).
        void BroadcastToAll(const PacketId clientPacketId, const std::span<const byte> payload);

    private:
        void Register();

        // --- Gateway 링크 ---
        void HandleClientConnected(const Network::Session::SPtr& gatewaySession, const std::span<const byte> payload);
        void HandleClientDisconnected(const Network::Session::SPtr& gatewaySession, const std::span<const byte> payload);
        void HandleFromClient(const Network::Session::SPtr& gatewaySession, const std::span<const byte> payload);

        // --- Zone 링크 ---
        void HandleZoneRegister(const Network::Session::SPtr& zoneSession, const std::span<const byte> payload);
        void HandleForwardToWorld(const Network::Session::SPtr& zoneSession, const std::span<const byte> payload);
        void HandleZoneTransfer(const Network::Session::SPtr& zoneSession, const std::span<const byte> payload);

        // 존이 올린 태스크 목록을 World 캐시에 반영하고, 같은 내용을 DB에 쓴다.
        // DB 작업은 AutoSpCommands가 모았다가 스코프 끝에서 playerId를 주인으로 DB 레인에 한
        // 번에 넘긴다 -- UnitOfWork 하나가 트랜잭션 하나다.
        void HandleUnitOfWorkStream(const Network::Session::SPtr& zoneSession,
                                    const std::span<const byte> payload);

        // W2ZEnterZone 본문을 캐시의 콘텐츠와 함께 만든다(포맷: Packet/ZoneLinkPackets.h).
        // 핸드오프와 되돌림이 같은 바이트를 보내야 존 쪽 파서가 하나로 끝난다.
        [[nodiscard]] std::vector<byte> EnterZoneBodyFor(const PlayerZoneStatePacket& state) const;

        // 이동 대상 존을 못 찾았을 때 플레이어를 원래 존으로 되돌린다. 보낸 존이 이미 자기
        // 상태에서 지운 뒤라, 되돌리지 않으면 그 플레이어는 어느 존에도 없는 상태가 된다.
        // state를 값으로 받는 이유: 좌표를 원래 존 안쪽으로 보정해서 그대로 다시 보낸다.
        void ReturnToSourceZone(PlayerZoneStatePacket state) const;

        PlayerManager::Mutexed& playerManager_;
        ZoneLinkRegistry::Mutexed& zoneLinkRegistry_;
        Processor::Group<EProcessorId>& basicGroup_;
        DbProcessor& dbProcessor_;
        LoginProcessor& loginProcessor_;

        Packet::Dispatcher<PacketId, Network::Session::SPtr> gatewayDispatcher_;
        Packet::Dispatcher<PacketId, Network::Session::SPtr> zoneDispatcher_;
    };
}
