#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Shared/Core/Src/Network/IPacketHandler.h"
#include "Shared/Core/Src/Processor/ProcessorGroup.h"
#include "Server/WorldServer/Src/Packet/RelayEnvelope.h"
#include "Server/ZoneServer/Src/Game/ZoneDef.h"
#include "Server/ZoneServer/Src/Worker/ProcessorId.h"
#include "Shared/Protocol/Src/PacketId.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace Zone
{
    class WorldLink;
    class PlayerProcessor;

    // World와의 연결 하나의 IPacketHandler. **수신(LB) 레인**을 안에 둔다.
    //
    // 세 단계로 나뉜다:
    //   I/O 스레드   -- 바이트 복사 + ownerId(clientSessionId) 훔쳐보기만
    //   LB 레인      -- 패킷 id 파싱과 1차 분기(입장/퇴장/릴레이). Echo는 여기서 즉답
    //   플레이어 레인 -- 실제 콘텐츠 처리(PlayerProcessor)
    //
    // **세 단계 모두 ownerId가 clientSessionId로 같다.** 그래서 한 클라이언트의 패킷은 LB에서도
    // 플레이어 레인에서도 도착 순서를 유지한다 -- 라운드로빈으로 흩뿌리면 같은 사람의 이동
    // 두 개가 뒤바뀔 수 있는데, 그러면 위치가 튄다.
    //
    // 예전에 있던 "clientSessionId -> zoneId" 공유 맵(+ shared_mutex)은 사라졌다. 그 값은
    // 이제 Player가 들고 있고, Player는 플레이어 레인 전용이라 락이 필요 없다.
    class WorldLinkHandler final : public Network::IPacketHandler
    {
    public:
        WorldLinkHandler(PlayerProcessor& playerProcessor,
                         Processor::ProcessorGroup<EProcessorId>& lbGroup,
                         Processor::ProcessorGroup<EProcessorId>& playerGroup,
                         WorldLink& worldLink, std::vector<ZoneDef> zoneDefs);

        void OnSessionOpened(const std::shared_ptr<Network::Session>& session) override;
        void OnPacket(const std::shared_ptr<Network::Session>& session,
                      const Packet::PacketHeader& header,
                      const std::span<const byte> payload) override;
        void OnClosed(const std::shared_ptr<Network::Session>& session, const std::error_code& reason) override;

    private:
        // I/O 스레드에서 부른다. 세 패킷 모두 주인이 클라이언트 세션이고, 그 값이 페이로드
        // 앞쪽에 있다(입장 요청만 zoneId 뒤라 offset 4).
        [[nodiscard]] static std::optional<uint64_t> OwnerIdOf(const PacketId packetId,
                                                                const std::span<const byte> payload);

        void DecodeAndDispatch(const PacketId packetId, const Network::SessionId ownerId,
                               const std::vector<byte>& payload);

        void HandleEnterZoneRequest(const std::span<const byte> payload);
        void HandleLeaveZoneNotify(const std::span<const byte> payload);
        void HandleForwardToZone(const Network::SessionId ownerId, const std::span<const byte> payload);

        // 공유 게임 상태가 필요 없어 플레이어 레인까지 갈 이유가 없다 -- LB에서 바로 돌려보낸다.
        void ReplyEcho(const World::ClientEnvelopeHeader& header, const std::span<const byte> innerPayload) const;

        PlayerProcessor& playerProcessor_;
        Processor::ProcessorGroup<EProcessorId>& lbGroup_;
        Processor::ProcessorGroup<EProcessorId>& playerGroup_;
        WorldLink& worldLink_;
        std::vector<ZoneDef> zoneDefs_;
    };
}
