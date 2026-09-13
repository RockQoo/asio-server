#pragma once

#include "Shared/Core/Src/Common/BasicTypes.h"
#include "Shared/Core/Src/Network/IPacketHandler.h"
#include "Shared/Core/Src/Packet/Dispatcher.h"
#include "Shared/Core/Src/Processor/Group.h"
#include "Packet/ZoneLinkPackets.h"
#include "World/PlayerManager.h"
#include "World/ZoneLinkRegistry.h"
#include "Worker/ProcessorId.h"
#include "Shared/Protocol/Src/PacketId.h"

namespace World
{
    // PlayerManager::Mutexed 를 쓰므로 전방 선언으로는 부족하다.

    // Zone <-> World 연결의 IPacketHandler. Zone 서버 프로세스가 여러 개 연결해 오며, 각 연결이
    // 자기가 호스팅하는 zoneId/담당 사각형을 ZoneRegister로 알려온다.
    //
    // 스레드 규약: OnPacket은 이 연결의 I/O 스레드에서 실행되고, 거기서 하는 일은 **패킷마다
    // 다른 위치에 있는 ownerId를 훔쳐보는 것**과 바이트 복사뿐이다(OwnerIdOf 참고). 실제
    // 처리는 그 ownerId가 배정한 스레드에서 일어난다.
    //
    // **UnitOfWork 스트림만 BASIC이 아니라 DB 그룹으로 직행한다.** 라우팅 상태를 전혀 건드리지
    // 않고 영속화만 하는 일이고, 무엇보다 DB는 블로킹 레인이라 CPU 레인과 섞으면 안 되기
    // 때문이다. 메시지가 목적 프로세서를 스스로 지정한다는 게 이 구조의 요점이다 --
    // processorId는 스레드를 고르지 않지만 "어느 그룹의 누구에게 가는가"는 호출부가 정한다.
    class ZoneLinkHandler final : public Network::IPacketHandler
    {
    public:
        ZoneLinkHandler(PlayerManager::Mutexed& playerManager, ZoneLinkRegistry::Mutexed& zoneLinkRegistry,
                         Processor::Group<EProcessorId>& basicGroup,
                         Processor::Group<EProcessorId>& dbGroup);

        void OnSessionOpened(const std::shared_ptr<Network::Session>& session) override;
        void OnPacket(const std::shared_ptr<Network::Session>& session,
                      const Packet::Header& header,
                      const std::span<const byte> payload) override;
        void OnClosed(const std::shared_ptr<Network::Session>& session, const std::error_code& reason) override;

    private:
        void RegisterHandlers();

        // I/O 스레드에서 "이 메시지의 주인"을 뽑는다. 패킷마다 주인도 위치도 다르다:
        //   ZoneRegister        -> zoneId          (ZoneLinkRegistry를 바꾸는 일)
        //   Relay               -> clientSessionId (그 클라이언트의 라우팅 항목을 읽는 일)
        //   ZoneTransferRequest -> clientSessionId (그 클라이언트의 존을 바꾸는 일)
        // 못 읽으면 nullopt -- 주인을 모르는 메시지는 어느 스레드로 보내도 틀리므로 버린다.
        [[nodiscard]] static std::optional<uint64_t> OwnerIdOf(const PacketId packetId,
                                                                const std::span<const byte> payload);

        void HandleZoneRegister(const std::shared_ptr<Network::Session>& zoneSession, const std::span<const byte> payload);
        void HandleForwardToWorld(const std::shared_ptr<Network::Session>& zoneSession, const std::span<const byte> payload);
        void HandleZoneTransfer(const std::shared_ptr<Network::Session>& zoneSession, const std::span<const byte> payload);

        // W2ZEnterZone 본문을 캐시의 콘텐츠와 함께 만든다(포맷: Packet/ZoneLinkPackets.h).
        // 핸드오프와 되돌림이 같은 바이트를 보내야 존 쪽 파서가 하나로 끝난다.
        [[nodiscard]] std::vector<byte> EnterZoneBodyFor(const PlayerZoneStatePacket& state) const;

        // I/O 스레드에서 스트림 앞부분(playerId/requestId/ownerId)만 읽어 DB 그룹으로 넘긴다.
        // 같은 ownerId의 스트림은 항상 같은 DB 스레드로 가므로, 한 플레이어의 변경이 도착
        // 순서대로 적재되고 락이 필요 없다.
        void PostUnitOfWorkStream(const std::span<const byte> payload);

        // 이동 대상 존을 못 찾았을 때 플레이어를 원래 존으로 되돌린다. 보낸 존이 이미 자기
        // 상태에서 지운 뒤라, 되돌리지 않으면 그 플레이어는 어느 존에도 없는 상태가 된다.
        // state를 값으로 받는 이유: 좌표를 원래 존 안쪽으로 보정해서 그대로 다시 보낸다.
        void ReturnToSourceZone(PlayerZoneStatePacket state) const;

        PlayerManager::Mutexed& playerManager_;
        ZoneLinkRegistry::Mutexed& zoneLinkRegistry_;
        Processor::Group<EProcessorId>& basicGroup_;
        Processor::Group<EProcessorId>& dbGroup_;
        Packet::Dispatcher<PacketId, std::shared_ptr<Network::Session>> dispatcher_;
    };
}
