#pragma once

#include "Shared/Core/Src/Common/BasicTypes.h"
#include "Shared/Core/Src/Network/IPacketHandler.h"
#include "Shared/Core/Src/Processor/Group.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Worker/ProcessorId.h"

namespace World
{
    class MainProcessor;

    // Zone <-> World 연결의 IPacketHandler. Zone 서버 프로세스가 여러 개 연결해 오며, 각 연결이
    // 자기가 호스팅하는 zoneId/담당 사각형을 ZoneRegister로 알려온다.
    //
    // **여기 있는 것은 전부 I/O 스레드(Session의 strand)에서 돈다.** 하는 일은 **패킷마다 다른
    // 위치에 있는 ownerId를 훔쳐보는 것**(OwnerIdOf)과 바이트 복사뿐이고, 실제 처리는
    // MainProcessor가 그 ownerId가 배정한 레인에서 한다.
    //
    // **UnitOfWork 스트림도 다른 패킷과 같이 BASIC을 거친다**(owner = clientSessionId).
    // 거기서 World 캐시를 갱신하고, DB 작업만 playerId를 주인으로 DB 레인에 넘긴다 -- DB는
    // 블로킹 레인이라 CPU 레인과 섞으면 안 되고, 캐시 갱신은 라우팅 상태와 같은 strand에서
    // 일어나야 접속 종료와 순서가 어긋나지 않는다.
    class ZoneLinkHandler final : public Network::IPacketHandler
    {
    public:
        ZoneLinkHandler(Processor::Group<EProcessorId>& basicGroup, MainProcessor& mainProcessor);

        void OnSessionOpened(const Network::Session::SPtr& session) override;
        void OnPacket(const Network::Session::SPtr& session,
                      const Packet::Header& header,
                      const std::span<const byte> payload) override;
        void OnClosed(const Network::Session::SPtr& session, const std::error_code& reason) override;

    private:
        // I/O 스레드에서 "이 메시지의 주인"을 뽑는다. 패킷마다 주인도 위치도 다르다:
        //   ZoneRegister        -> zoneId          (ZoneLinkRegistry를 바꾸는 일)
        //   Relay               -> clientSessionId (그 클라이언트의 라우팅 항목을 읽는 일)
        //   ZoneTransferRequest -> clientSessionId (그 클라이언트의 존을 바꾸는 일)
        // 못 읽으면 nullopt -- 주인을 모르는 메시지는 어느 스레드로 보내도 틀리므로 버린다.
        [[nodiscard]] static std::optional<uint64_t> OwnerIdOf(const PacketId packetId,
                                                                const std::span<const byte> payload);

        Processor::Group<EProcessorId>& basicGroup_;
        MainProcessor& mainProcessor_;
    };
}
