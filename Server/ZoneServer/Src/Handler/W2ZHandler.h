#pragma once

#include "Shared/Core/Src/Base/Types.h"
#include "Shared/Core/Src/Network/IPacketHandler.h"
#include "Shared/Core/Src/Packet/OwnerIdTable.h"
#include "Shared/Core/Src/Processor/Group.h"
#include "App/ZoneDef.h"
#include "Processor/ProcessorId.h"
#include "Shared/Common/Src/PacketId.h"
#include "Shared/Core/Src/Network/SessionHolder.h"

class PlayerProcessor;

// World와의 연결 하나의 IPacketHandler.
//
// **여기 있는 것은 전부 I/O 스레드(Session의 strand)에서 돈다.** 하는 일은 둘이다 --
// 페이로드에서 ownerId(clientSessionId)를 훔쳐보고, 바이트를 복사해 플레이어 레인에 넣는다.
// 패킷 해석과 콘텐츠 처리는 전부 PlayerProcessor가 배정된 레인에서 한다.
//
// **예전에는 그 사이에 LB 레인이 하나 더 있었다.** 없앤 이유는 ProcessorId.h 참고 --
// 요약하면 앞뒤 단계의 ownerId가 같아서 홉만 늘고 있었다.
//
// World 링크는 이 프로세스에 **하나뿐**이고, 그 소켓의 수신은 strand 하나로 직렬화된다.
// 그래서 이 함수가 무거워지면 존 전체의 수신이 그만큼 좁아진다 -- 정수 하나 memcpy와
// 바이트 복사 이상을 여기에 두지 말 것.
class W2ZHandler final : public Network::IPacketHandler
{
public:
    W2ZHandler(PlayerProcessor& playerProcessor, Processor::Group<EZoneProcessorId>& playerGroup,
                     Network::SessionHolder& worldLink, std::vector<ZoneDef> zoneDefs);

    void OnSessionOpened(const Network::Session::SPtr& session) override;
    void OnPacket(const Network::Session::SPtr& session,
                  const Packet::Header& header,
                  const std::span<const byte> payload) override;
    void OnClosed(const Network::Session::SPtr& session, const std::error_code& reason) override;

private:
    // 이 링크가 받는 패킷 목록. 생성자 다음에 둔다.
    void Register();

    // 패킷 id -> 주인 오프셋. I/O 스레드에서 읽기만 한다(등록은 생성자에서 끝난다).
    Packet::OwnerIdTable<PacketId> ownerIds_;

    PlayerProcessor& playerProcessor_;
    Processor::Group<EZoneProcessorId>& playerGroup_;
    Network::SessionHolder& worldLink_;
    std::vector<ZoneDef> zoneDefs_;
};
