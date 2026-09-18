#include "pch.h"
#include "Handler/W2ZHandler.h"

#include "Processor/ProcessorIds.h"
#include "Processor/ZoneMsg.h"

#include "Server/Core/Src/Network/Session.h"
#include "Server/Core/Src/Pipeline/ProducerHolder.h"
#include "Server/Common/Src/Packet/Wire.h"
#include "Server/Common/Src/Packet/ZoneLinkPackets.h"

W2ZHandler::W2ZHandler(const ZoneDef& def, Network::SessionHolder& worldLink)
    : def_(def)
    , worldLink_(worldLink)
{
}

void W2ZHandler::OnSessionOpened(const Network::Session::SPtr& session)
{
    worldLink_.Set(session);

    // **이 연결은 이 존 하나만 등록한다.** World 는 담당 사각형만 보고 라우팅하므로 존
    // 배치 규칙(격자든 CSV든)을 알 필요가 없다.
    Common::Z2WZoneRegister registerPacket{};
    registerPacket.zoneId = def_.zoneId;
    registerPacket.xMin = def_.xMin;
    registerPacket.xMax = def_.xMax;
    registerPacket.yMin = def_.yMin;
    registerPacket.yMax = def_.yMax;
    Common::SendPacket(session, registerPacket);

    LOG.Info(ELogCategory::Zone, "World 연결 성공, 존 등록")
        .KV("ZoneId", def_.zoneId)
        .KV("XMin", def_.xMin).KV("XMax", def_.xMax)
        .KV("YMin", def_.yMin).KV("YMax", def_.yMax);
}

void W2ZHandler::OnPacket(const Network::Session::SPtr& session,
                          const Packet::Header& header,
                          const std::span<const byte> payload)
{
    // 여기는 I/O 스레드다. **payload 는 이 함수가 끝나면 재사용되는 버퍼**라 복사해서 넘긴다.
    const auto target = Ids().ZoneTarget(def_.zoneId);

    Pipeline::PushMsg<Pipeline::EProducerType::Lb>(
        EZoneMsg::OnRecvStream, target.lb, Pipeline::OwnerId{static_cast<int64_t>(session->Id())},
        RecvStreamBody{session, static_cast<PacketId>(header.id),
                       std::vector<byte>(payload.begin(), payload.end())});
}

void W2ZHandler::OnClosed(const Network::Session::SPtr& /*session*/, const std::error_code& reason)
{
    worldLink_.Clear();
    LOG.Warning(ELogCategory::Zone, "World 연결 끊김")
        .KV("ZoneId", def_.zoneId).KV("Message", reason.message());
}
