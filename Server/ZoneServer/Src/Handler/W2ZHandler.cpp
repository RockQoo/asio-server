#include "pch.h"
#include "Handler/W2ZHandler.h"
#include "Processor/PlayerProcessor.h"
#include "Server/Core/Src/Network/SessionHolder.h"
#include "Server/Core/Src/Packet/OwnerIdPeek.h"
#include "Server/Common/Src/Packet/ZoneLinkPackets.h"
#include "Server/Common/Src/PacketId.h"

#include "Server/Core/Src/Network/Session.h"

W2ZHandler::W2ZHandler(PlayerProcessor& playerProcessor,
                                   Processor::Group<EZoneProcessorId>& playerGroup,
                                   Network::SessionHolder& worldLink, std::vector<ZoneDef> zoneDefs)
    : playerProcessor_(playerProcessor)
    , playerGroup_(playerGroup)
    , worldLink_(worldLink)
    , zoneDefs_(std::move(zoneDefs))
{
    Register();
}

void W2ZHandler::Register()
{
    // 이 링크가 받는 패킷 목록 + 그 주인이 페이로드 어디에 있나.
    ownerIds_.Register<Network::SessionId>(PacketId::W2ZEnterZone, sizeof(uint32_t));  // zoneId 뒤
    ownerIds_.Register<Network::SessionId>(PacketId::W2ZLeaveZone);
    ownerIds_.Register<Network::SessionId>(PacketId::W2ZRelay);                        // RelayEnvelope 맨 앞
}

void W2ZHandler::OnSessionOpened(const Network::Session::SPtr& session)
{
    worldLink_.Set(session);

    // 이 프로세스가 담당하는 존마다 한 번씩 등록한다 -- 여러 zoneId가 이 연결 하나를 같이
    // 쓴다(프로세스 하나가 존 여러 개를 동시에 호스팅할 수 있으므로).
    for (const auto& def : zoneDefs_)
    {
        Common::Z2WZoneRegister registerPacket{};
        registerPacket.zoneId = def.zoneId;
        registerPacket.xMin = def.xMin;
        registerPacket.xMax = def.xMax;
        registerPacket.yMin = def.yMin;
        registerPacket.yMax = def.yMax;
        Common::SendPacket(session, registerPacket);

        LOG.Info(ELogCategory::Zone, "World 연결 성공, 존 등록")
            .KV("ZoneId", def.zoneId)
            .KV("XMin", def.xMin).KV("XMax", def.xMax)
            .KV("YMin", def.yMin).KV("YMax", def.yMax);
    }
}


void W2ZHandler::OnPacket(const Network::Session::SPtr& /*session*/,
                                const Packet::Header& header,
                                const std::span<const byte> payload)
{
    // 여기는 I/O 스레드(Session의 strand)다. ownerId만 훔쳐보고 바이트를 복사해 플레이어
    // 레인에 넘긴다 -- payload는 이 함수가 끝나면 I/O 스레드가 재사용할 버퍼를 가리킨다.
    const auto packetId = static_cast<PacketId>(header.id);

    const auto ownerId = ownerIds_.Find(packetId, payload);
    if (!ownerId)
    {
        LOG.Warning(ELogCategory::Zone, "ownerId를 읽을 수 없는 패킷, 버림")
            .KV("PacketId", header.id).KV("PayloadSize", payload.size());
        return;
    }

    std::vector<byte> payloadCopy(payload.begin(), payload.end());

    playerGroup_.Post(EZoneProcessorId::Player, *ownerId,
        [this, clientSessionId = static_cast<Network::SessionId>(*ownerId),
         packetId, payloadCopy = std::move(payloadCopy)]
        {
            playerProcessor_.DispatchFromWorld(packetId, clientSessionId, payloadCopy);
        });
}

void W2ZHandler::OnClosed(const Network::Session::SPtr& /*session*/, const std::error_code& reason)
{
    worldLink_.Clear();
    LOG.Warning(ELogCategory::Zone, "World 연결 끊김").KV("Message", reason.message());
}
