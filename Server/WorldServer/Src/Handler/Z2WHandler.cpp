#include "pch.h"
#include "Handler/Z2WHandler.h"

#include "Shared/Core/Src/Packet/OwnerIdPeek.h"
#include "Shared/Common/Src/Packet/ZoneLinkPackets.h"
#include "Processor/MainProcessor.h"

#include "Shared/Core/Src/Base/RUID.h"
#include "Shared/Core/Src/Network/Session.h"

Z2WHandler::Z2WHandler(Processor::Group<EWorldProcessorId>& basicGroup, MainProcessor& mainProcessor)
    : basicGroup_(basicGroup)
    , mainProcessor_(mainProcessor)
{
    Register();
}

void Z2WHandler::Register()
{
    // 이 링크가 받는 패킷 목록 + 그 주인이 페이로드 어디에 있나.
    ownerIds_.Register<uint32_t>(PacketId::Z2WZoneRegister);                              // zoneId (offset 0)
    ownerIds_.Register<Network::SessionId>(PacketId::Z2WRelay);                           // RelayEnvelope 맨 앞
    ownerIds_.Register<Network::SessionId>(PacketId::Z2WZoneTransfer, sizeof(uint32_t));  // zoneId 뒤

    // Task::UnitOfWork::Serialize 가 스트림 맨 앞에 넣어둔 ownerId(= clientSessionId).
    // 그 앞에 Zone 이 붙인 playerId(int64) + requestId(int64) 가 있다.
    ownerIds_.Register<uint64_t>(PacketId::Z2WUnitOfWorkStream,
                                 sizeof(Common::PlayerId) + sizeof(Base::RUID));
}

void Z2WHandler::OnSessionOpened(const Network::Session::SPtr& session)
{
    LOG.Info(ELogCategory::Zone, "Zone 연결 수락")
        .KV("SessionId", session->Id()).KV("Remote", session->RemoteAddress());
}

void Z2WHandler::OnPacket(const Network::Session::SPtr& session,
                               const Packet::Header& header,
                               const std::span<const byte> payload)
{
    // 여기는 이 연결의 I/O 스레드(Session의 strand)다. 라우팅에 필요한 정수 하나만 읽고
    // 바이트를 복사해 넘긴다.
    const auto packetId = static_cast<PacketId>(header.id);

    const auto ownerId = ownerIds_.Find(packetId, payload);
    if (!ownerId)
    {
        LOG.Warning(ELogCategory::Zone, "ownerId를 읽을 수 없는 패킷, 버림")
            .KV("PacketId", header.id).KV("PayloadSize", payload.size());
        return;
    }

    std::vector<byte> payloadCopy(payload.begin(), payload.end());

    basicGroup_.Post(EWorldProcessorId::Main, *ownerId,
        [this, session, packetId, payloadCopy = std::move(payloadCopy)]
        {
            mainProcessor_.DispatchFromZone(packetId, session, payloadCopy);
        });
}

void Z2WHandler::OnClosed(const Network::Session::SPtr& session, const std::error_code& /*reason*/)
{
    // 세션 종료 통지도 I/O 스레드에서 오므로, 여기서 레지스트리를 직접 건드리지 않고
    // BASIC 그룹으로 넘긴다. 주인은 끊긴 세션 자신이다 -- 등록/해제가 같은 스레드에서
    // 순서대로 처리되게 하려는 것이고, 레지스트리 자체는 Mutexed가 따로 지킨다.
    const auto sessionId = session->Id();
    basicGroup_.Post(EWorldProcessorId::Main, sessionId, [this, sessionId]
    {
        mainProcessor_.RemoveZoneLink(sessionId);
    });
}
