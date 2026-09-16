#pragma once

#include "Shared/Core/Src/Base/Types.h"
#include "Shared/Core/Src/Network/IPacketHandler.h"
#include "Shared/Core/Src/Processor/Group.h"
#include "Shared/Common/Src/PacketId.h"
#include "Worker/ProcessorId.h"

namespace World
{
    class MainProcessor;

    // Gateway <-> World 연결의 IPacketHandler.
    //
    // **여기 있는 것은 전부 I/O 스레드(Session의 strand)에서 돈다.** 하는 일은 딱 둘이다 --
    // 페이로드 앞에서 ownerId(=clientSessionId)를 훔쳐보고, 바이트를 복사해 BASIC 그룹에
    // 넣는다. 실제 처리는 MainProcessor가 그 ownerId가 배정한 레인에서 한다.
    //
    // **이 분리가 규약 자체다.** 여기서 레지스트리를 직접 만지면 어느 스레드에서 만지는지
    // 보장이 사라진다 -- 그래서 이 클래스는 PlayerManager도 ZoneLinkRegistry도 들고 있지 않다.
    //
    // 세 패킷 모두 페이로드 맨 앞이 clientSessionId라 훔쳐보기가 오프셋 0 하나로 끝난다.
    // 우연이 아니라 릴레이 봉투를 그렇게 설계했기 때문이다.
    class GatewayLinkHandler final : public Network::IPacketHandler
    {
    public:
        GatewayLinkHandler(Processor::Group<EProcessorId>& basicGroup, MainProcessor& mainProcessor);

        void OnSessionOpened(const Network::Session::SPtr& session) override;
        void OnPacket(const Network::Session::SPtr& session,
                      const Packet::Header& header,
                      const std::span<const byte> payload) override;
        void OnClosed(const Network::Session::SPtr& session, const std::error_code& reason) override;

    private:
        Processor::Group<EProcessorId>& basicGroup_;
        MainProcessor& mainProcessor_;
    };
}
