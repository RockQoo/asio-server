#include "pch.h"
#include "Processor/BroadcastProcessor.h"

#include "Server/Core/Src/Network/Session.h"
#include "Server/Core/Src/Packet/PacketFramer.h"
#include "Server/Common/Src/Packet/RelayEnvelope.h"

BroadcastProcessor::BroadcastProcessor(const Common::ZoneId zoneId, Network::SessionHolder& worldLink)
    : zoneId_(zoneId)
    , worldLink_(worldLink)
{
}

void BroadcastProcessor::RegistHandler()
{
    Regist(EZoneMsg::Fanout, &BroadcastProcessor::OnFanout);
    Regist(EZoneMsg::FanoutFlush, &BroadcastProcessor::OnFanoutFlush);
}

void BroadcastProcessor::OnFanout(const Pipeline::OwnerId& /*owner*/, const FanoutBody& body)
{
    for (const auto clientSessionId : body.targets)
    {
        // 대상마다 봉투가 다르므로(받는 사람이 다르다) 프레임도 대상 수만큼 만들어진다.
        // 줄일 수 있는 것은 **소켓에 쓰는 횟수**이고, 그걸 함이 맡는다.
        auto relay = Common::WrapRelay(clientSessionId, static_cast<uint16_t>(body.innerPacketId),
                                       body.payload);

        try
        {
            const auto frame = Packet::BuildFrame(PacketId::Z2WRelay, relay);
            outbox_.insert(outbox_.end(), frame.begin(), frame.end());
        }
        catch (const std::exception& ex)
        {
            // 상한을 넘은 본문 하나 때문에 이 존의 팬아웃 전체가 멈추지 않게 한다 --
            // 그 한 통만 버리고 나머지는 나간다.
            LOG.Error(ELogCategory::Zone, "팬아웃 프레임이 너무 커서 버린다")
                .KV("Zone", zoneId_).KV("InnerPacketId", static_cast<uint16_t>(body.innerPacketId))
                .KV("What", ex.what());
            return;
        }
    }

    if (outbox_.size() >= kFlushThresholdBytes)
    {
        Flush();
    }
}

void BroadcastProcessor::OnFanoutFlush(const Pipeline::OwnerId& /*owner*/)
{
    Flush();
}

void BroadcastProcessor::Flush()
{
    if (outbox_.empty())
    {
        return;
    }

    const auto worldSession = worldLink_.Get();
    if (!worldSession)
    {
        // 링크가 끊겼다. 모아둔 것은 보낼 곳이 없으므로 버린다 -- 들고 있어봐야 재연결
        // 뒤에는 이미 낡은 통지다.
        outbox_.clear();
        return;
    }

    worldSession->SendFrames(outbox_);
    outbox_.clear();
}
