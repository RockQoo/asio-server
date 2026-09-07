#include "Server/ZoneServer/Src/pch.h"
#include "Server/ZoneServer/Src/Handler/WorldLinkHandler.h"
#include "Shared/Protocol/Src/PacketId.h"
#include "Server/ZoneServer/Src/Worker/ZoneWorkerManager.h"
#include "Server/ZoneServer/Src/World/WorldLink.h"
#include "Server/WorldServer/Src/Packet/ZoneLinkPackets.h"

#include "Shared/Core/Src/Network/Session.h"
#include "Shared/Core/Src/Packet/BinaryWriter.h"

#include <cstring>
#include <utility>

namespace Zone
{
    WorldLinkHandler::WorldLinkHandler(ZoneWorkerManager& zoneWorkers, WorldLink& worldLink, std::vector<ZoneDef> zoneDefs,
                                       const size_t lbThreadCount)
        : zoneWorkers_(zoneWorkers)
        , worldLink_(worldLink)
        , zoneDefs_(std::move(zoneDefs))
        , lbPool_(lbThreadCount)
    {
    }

    void WorldLinkHandler::Start()
    {
        lbPool_.Start();
    }

    void WorldLinkHandler::Stop()
    {
        lbPool_.Stop();
    }

    void WorldLinkHandler::OnSessionOpened(const std::shared_ptr<Network::Session>& session)
    {
        worldLink_.Set(session);

        // 이 프로세스가 담당하는 존마다 한 번씩 등록한다 -- 여러 zoneId가 이 연결 하나를 같이
        // 쓴다(프로세스 하나가 존 여러 개를 동시에 호스팅할 수 있으므로).
        for (const auto& def : zoneDefs_)
        {
            World::ZoneRegisterPacket registerPacket{};
            registerPacket.zoneId = def.zoneId;
            registerPacket.xMin = def.xMin;
            registerPacket.xMax = def.xMax;
            session->SendPacket(PacketId::Z2WZoneRegister,
                                 std::as_bytes(std::span(&registerPacket, 1)));

            LOG.Info(ELogCategory::Zone, "World 연결 성공, 존 등록")
                .KV("ZoneId", def.zoneId).KV("XMin", def.xMin).KV("XMax", def.xMax);
        }
    }

    void WorldLinkHandler::OnPacket(const std::shared_ptr<Network::Session>& /*session*/,
                                    const Packet::PacketHeader& header,
                                    const std::span<const byte> payload)
    {
        // 여기는 NETWORK 스레드(Session의 strand)다. 파싱/분기는 하지 않고 바이트만 복사해서
        // LB 풀로 넘긴다 -- payload는 이 함수가 끝나면 I/O 스레드가 재사용할 버퍼를 가리키므로
        // 복사가 필요하다.
        const auto packetId = header.id;
        std::vector<byte> payloadCopy(payload.begin(), payload.end());

        lbPool_.GetWorker(lbRoundRobin_.fetch_add(1, std::memory_order_relaxed))
            .PostTask([this, packetId, payloadCopy = std::move(payloadCopy)]
            {
                DecodeAndDispatch(packetId, payloadCopy);
            });
    }

    void WorldLinkHandler::OnClosed(const std::shared_ptr<Network::Session>& /*session*/, const std::error_code& reason)
    {
        worldLink_.Clear();
        LOG.Warning(ELogCategory::Zone, "World 연결 끊김").KV("Message", reason.message());
    }

    void WorldLinkHandler::DecodeAndDispatch(const uint16_t packetId, const std::span<const byte> payload)
    {
        // 여기부터는 LB 스레드. "recv 처리"(패킷 타입 파싱 + 1차 분기)가 여기서 일어난다.
        switch (static_cast<PacketId>(packetId))
        {
        case PacketId::W2ZEnterZoneRequest:
            HandleEnterZoneRequest(payload);
            break;
        case PacketId::W2ZLeaveZoneNotify:
            HandleLeaveZoneNotify(payload);
            break;
        case PacketId::W2ZRelay:
            HandleForwardToZone(payload);
            break;
        default:
            break;
        }
    }

    void WorldLinkHandler::HandleEnterZoneRequest(const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(World::PlayerZoneStatePacket))
        {
            return;
        }

        World::PlayerZoneStatePacket state{};
        std::memcpy(&state, payload.data(), sizeof(World::PlayerZoneStatePacket));

        if (!zoneWorkers_.HasZone(state.zoneId))
        {
            LOG.Warning(ELogCategory::Zone, "이 프로세스가 담당하지 않는 zoneId로 EnterZoneRequest 수신")
                .KV("ZoneId", state.zoneId).KV("ClientSessionId", state.clientSessionId);
            return;
        }

        SetLocalZone(state.clientSessionId, state.zoneId);

        zoneWorkers_.PostToBasic(state.zoneId, [&zone = zoneWorkers_.GetZoneInstance(state.zoneId), state]
        {
            zone.OnPlayerEnter(state.clientSessionId, state.playerId, state.x, state.y);
        });
    }

    void WorldLinkHandler::HandleLeaveZoneNotify(const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(World::LeaveZoneNotifyPacket))
        {
            return;
        }

        World::LeaveZoneNotifyPacket leave{};
        std::memcpy(&leave, payload.data(), sizeof(World::LeaveZoneNotifyPacket));

        const auto zoneIdOpt = FindLocalZone(leave.clientSessionId);
        if (!zoneIdOpt)
        {
            return;
        }

        const auto zoneId = *zoneIdOpt;
        RemoveLocalZone(leave.clientSessionId);

        zoneWorkers_.PostToBasic(zoneId, [&zone = zoneWorkers_.GetZoneInstance(zoneId), clientSessionId = leave.clientSessionId]
        {
            zone.OnPlayerLeave(clientSessionId);
        });
    }

    void WorldLinkHandler::HandleForwardToZone(const std::span<const byte> payload)
    {
        if (payload.size() < sizeof(World::ClientEnvelopeHeader))
        {
            return;
        }

        World::ClientEnvelopeHeader header{};
        std::memcpy(&header, payload.data(), sizeof(World::ClientEnvelopeHeader));
        const auto innerPayload = payload.subspan(sizeof(World::ClientEnvelopeHeader));

        const auto zoneIdOpt = FindLocalZone(header.clientSessionId);
        if (!zoneIdOpt)
        {
            return;
        }

        const auto innerPacketId = static_cast<PacketId>(header.innerPacketId);
        if (innerPacketId == PacketId::C2ZEcho)
        {
            // 공유 게임 상태가 필요 없으니 BASIC까지 안 가고 이 LB 스레드에서 바로 되돌려
            // 보낸다 -- ZoneInstance::HandleClientPacket으로 넘기지 않는 유일한 예외.
            if (const auto worldSession = worldLink_.Get())
            {
                // 받은 envelope을 그대로 쓰되 innerPacketId만 응답 방향으로 바꾼다 -- 요청과
                // 응답이 같은 id를 공유하지 않는 것이 패킷 id 규약이다(본문은 받은 것 그대로).
                World::ClientEnvelopeHeader replyHeader = header;
                replyHeader.innerPacketId = static_cast<uint16_t>(PacketId::Z2CEchoAck);

                Packet::BinaryWriter writer;
                writer.Write(replyHeader);
                writer.WriteBytes(innerPayload);
                worldSession->SendPacket(PacketId::Z2WRelay, writer.GetBuffer());
            }
            return;
        }

        // Echo를 제외한 나머지는 패킷 내용을 전혀 들여다보지 않고 그대로 BASIC(ZoneInstance)에
        // 넘긴다 -- 와이어 포맷 파싱은 ZoneInstance::HandleClientPacket 쪽 몫이다.
        const auto zoneId = *zoneIdOpt;
        const auto clientSessionId = header.clientSessionId;
        std::vector<byte> innerPayloadCopy(innerPayload.begin(), innerPayload.end());
        zoneWorkers_.PostToBasic(zoneId, [&zone = zoneWorkers_.GetZoneInstance(zoneId), clientSessionId,
                                          innerPacketId, innerPayloadCopy = std::move(innerPayloadCopy)]
        {
            zone.HandleClientPacket(clientSessionId, innerPacketId, innerPayloadCopy);
        });
    }

    std::optional<uint32_t> WorldLinkHandler::FindLocalZone(const Network::SessionId clientSessionId) const
    {
        std::shared_lock lock(clientLocalZoneMutex_);
        const auto it = clientLocalZone_.find(clientSessionId);
        if (it == clientLocalZone_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    void WorldLinkHandler::SetLocalZone(const Network::SessionId clientSessionId, const uint32_t zoneId)
    {
        std::unique_lock lock(clientLocalZoneMutex_);
        clientLocalZone_[clientSessionId] = zoneId;
    }

    void WorldLinkHandler::RemoveLocalZone(const Network::SessionId clientSessionId)
    {
        std::unique_lock lock(clientLocalZoneMutex_);
        clientLocalZone_.erase(clientSessionId);
    }
}
