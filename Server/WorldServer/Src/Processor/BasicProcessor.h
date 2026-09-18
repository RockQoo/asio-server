#pragma once

#include "Server/Core/Src/Base/BasicTypes.h"
#include "Server/Core/Src/Network/Session.h"
#include "Server/Core/Src/Packet/Dispatcher.h"
#include "Server/Core/Src/Packet/OwnerIdTable.h"
#include "Server/Core/Src/Pipeline/MessageProcessor.h"
#include "Server/Common/Src/PacketId.h"
#include "Server/Common/Src/Packet/LoginPackets.h"
#include "Server/Common/Src/Packet/RelayEnvelope.h"
#include "Server/Common/Src/Packet/Wire.h"
#include "Server/Common/Src/Packet/ZoneLinkPackets.h"
#include "Processor/DbProcessor.h"
#include "Processor/WorldMsg.h"
#include "World/PlayerManager.h"
#include "World/ZoneLinkRegistry.h"

// PlayerManager::Mutexed 를 쓰므로 전방 선언으로는 부족하다.
class LoginProcessor;

// BASIC 레인의 본체. 게이트웨이 수신구와 플레이어 콘텐츠 전부가 여기 있다.
//
// **수신이 두 단계로 나뉜다 -- 이게 이 클래스의 형태를 정한다.**
//
//   ① 프로액터(소켓 스레드)  IOCP 완료 + 프레임 조립까지. 껍질은 안 깐다.
//        │ PushMsg(OnRecvStream, Ids().main, OwnerId(링크 세션 id))
//        ▼
//   ② OnRecvStream           **owner = 링크 세션 id.** 릴레이 봉투를 열어 안의
//        │                    clientSessionId를 꺼낸다. 여기까지는 게이트웨이 하나당
//        │                    스레드 하나다.
//        │ PushMsg(FromClientStream, GetProcessorId(), OwnerId(clientSessionId))
//        ▼                    ↑ **자기 자신에게 다시 넣는다** -- 프로세서를 바꾸는 게 아니라
//   ③ OnRecvFromClientStream   스레드를 바꾸는 것이 목적이다.
//                             **owner = clientSessionId.** 여기서 비로소 접속자 수만큼
//                             갈라지고, 이후 콘텐츠 핸들러가 이 스레드에서 돈다.
//
// **②를 건너뛰고 소켓 스레드에서 껍질을 까면 안 된다.** 홉이 하나 줄지만 구조가 달라진다 --
// 소켓 스레드가 페이로드를 해석하기 시작하면 수신 자체가 그만큼 밀린다.
//
// **여기 있는 핸들러는 전부 BASIC 레인에서 불린다.** 그래서 PlayerManager/ZoneLinkRegistry를
// 직접 만진다 -- 둘의 Mutexed는 공지처럼 주인이 없는 경로 때문에 남아 있는 것이지, 여기가
// 아무 스레드에서나 불려서가 아니다.
class BasicProcessor final : public Pipeline::MessageProcessor
{
public:
    BasicProcessor(PlayerManager::Mutexed& playerManager, ZoneLinkRegistry::Mutexed& zoneLinkRegistry,
                   LoginProcessor& loginProcessor);

    [[nodiscard]] std::string_view Name() const override { return "Basic"; }
    void RegistHandler() override;

    // 접속 중인 모든 클라이언트에게 Zone을 거치지 않고 직접 보낸다 -- World가 전체
    // 클라이언트 레지스트리를 들고 있어서 가능하다. **이것만 호출 스레드를 가리지 않는다**
    // (콘솔 입력 스레드에서도 부를 수 있게 안에서 BASIC으로 던진다).
    void BroadcastToAll(const PacketId clientPacketId, const std::span<const byte> payload);

private:
    // ── 레인 사이 메시지 ──────────────────────────────────────────────────────
    // 게이트웨이 링크: ② 껍질 까기 -> ③ 콘텐츠
    void OnRecvStream(const Pipeline::OwnerId& owner, const RecvStreamBody& body);
    void OnRecvFromClientStream(const Pipeline::OwnerId& owner, const RecvStreamBody& body);

    // 존 링크: 같은 2단이다. **여기는 노션의 FZoneStreamProcessor 자리라 아직 어긋나 있다**
    // -- 다음 단계에서 ZoneStreamProcessor로 떼어낸다.
    void OnRecvZoneStream(const Pipeline::OwnerId& owner, const RecvStreamBody& body);
    void OnFromZoneStream(const Pipeline::OwnerId& owner, const RecvStreamBody& body);

    // 존 링크 종료. body가 없다 -- 주인이 곧 끊긴 세션이다.
    void OnZoneLinkClosed(const Pipeline::OwnerId& owner);

    // 전체 공지. owner가 고정 상수라 이 일은 한 레인에 직렬화된다.
    void OnBroadcastToAll(const Pipeline::OwnerId& owner, const RecvStreamBody& body);

    void RegisterPackets();

    // 존 링크는 패킷마다 주인도 위치도 다르다. 기동 때 한 번 채우고 이후 읽기만 한다.
    void RegisterZoneOwnerIds();
    Packet::OwnerIdTable<PacketId> zoneOwnerIds_;

    // Gateway 링크에서 온 패킷 / Zone 링크에서 온 패킷. 표를 둘로 나눠 둔 이유는 **어느
    // 링크로 들어올 수 있는 패킷인지가 표에 드러나게** 하려는 것이다(id는 겹치지 않아서
    // 하나로 합쳐도 돌기는 한다).
    void DispatchFromGateway(const PacketId packetId, const Network::Session::SPtr& gatewaySession,
                             const std::span<const byte> payload);
    void DispatchFromZone(const PacketId packetId, const Network::Session::SPtr& zoneSession,
                          const std::span<const byte> payload);

    // Zone 링크가 끊겼다. 한 Zone 프로세스가 존 여러 개를 호스팅하므로 그 세션으로 등록된
    // zoneId를 **전부** 지워야 끊긴 세션으로 계속 라우팅되는 걸 막는다.
    void RemoveZoneLink(const Network::SessionId zoneSessionId);

    // 아래 핸들러는 전부 **해석이 끝난 패킷 구조체**를 받는다 -- 역직렬화와 형식 검증은
    // 디스패치 앞(Packet::Dispatcher)에서 끝난다.

    // --- Gateway 링크 ---
    void HandleClientConnected(const Network::Session::SPtr& gatewaySession, const Common::G2WClientConnected& packet);
    void HandleClientDisconnected(const Network::Session::SPtr& gatewaySession, const Common::G2WClientDisconnected& packet);
    void HandleFromClient(const Network::Session::SPtr& gatewaySession, const Common::G2WRelay& packet);

    // --- Zone 링크 ---
    void HandleZoneRegister(const Network::Session::SPtr& zoneSession, const Common::Z2WZoneRegister& registerPacket);
    void HandleForwardToWorld(const Network::Session::SPtr& zoneSession, const Common::Z2WRelay& packet);
    void HandleZoneTransfer(const Network::Session::SPtr& zoneSession, const Common::Z2WZoneTransfer& transfer);

    // 존이 올린 태스크 목록을 World 캐시에 반영하고, 같은 내용을 DB에 쓴다.
    // DB 작업은 AutoSpCommands가 모았다가 스코프 끝에서 playerId를 주인으로 DB 레인에 한
    // 번에 넘긴다 -- UnitOfWork 하나가 트랜잭션 하나다.
    void HandleUnitOfWorkStream(const Network::Session::SPtr& zoneSession,
                                const Common::Z2WUnitOfWorkStream& packet);

    // W2ZEnterZone 본문을 캐시의 콘텐츠와 함께 만든다(포맷: Packet/ZoneLinkPackets.h).
    // 핸드오프와 되돌림이 같은 바이트를 보내야 존 쪽 파서가 하나로 끝난다.
    [[nodiscard]] std::vector<byte> EnterZoneBodyFor(const Common::W2ZEnterZoneHead& enterZone) const;

    // 이동 대상 존을 못 찾았을 때 플레이어를 원래 존으로 되돌린다. 보낸 존이 이미 자기
    // 상태에서 지운 뒤라, 되돌리지 않으면 그 플레이어는 어느 존에도 없는 상태가 된다.
    // state를 값으로 받는 이유: 좌표를 원래 존 안쪽으로 보정해서 그대로 다시 보낸다.
    void ReturnToSourceZone(Common::Z2WZoneTransfer transfer) const;

    PlayerManager::Mutexed& playerManager_;
    ZoneLinkRegistry::Mutexed& zoneLinkRegistry_;

    LoginProcessor& loginProcessor_;

    Packet::Dispatcher<PacketId, Network::Session::SPtr> gatewayDispatcher_;
    Packet::Dispatcher<PacketId, Network::Session::SPtr> zoneDispatcher_;
};
