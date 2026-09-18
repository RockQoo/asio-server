#pragma once

#include "Server/Core/Src/Base/Types.h"
#include "Server/Core/Src/Network/IPacketHandler.h"
#include "Server/Common/Src/Packet/ToolLinkPackets.h"
#include "Server/Common/Src/Packet/Wire.h"
#include "Server/Core/Src/Packet/Dispatcher.h"
#include "Server/Core/Src/Pipeline/MessageProcessor.h"
#include "Processor/WorldMsg.h"
#include "Server/Core/Src/Thread/Mutexed.h"
#include "Server/Common/Src/PacketId.h"
#include "Server/Common/Src/Packet/ToolResultCode.h"
#include "Processor/DbProcessor.h"
#include "World/PlayerManager.h"
#include "World/ZoneLinkRegistry.h"
#include "Processor/ProcessorId.h"

// PlayerManager::Mutexed 를 쓰므로 전방 선언으로는 부족하다.
class ZoneLinkRegistry;
class WorldWorker;

// 운영툴 전용 accept 포트(toolPort)의 IPacketHandler. **스레드 규약은 다른 두 링크
// 핸들러와 동일하다** -- I/O 스레드는 바이트만 복사해 넘기고, 레지스트리를 만지는 Handle*
// 는 전부 배정된 레인에서 돈다. authenticatedSessions_에 락이 없는 것도 그래서다.
//
// **우편/공지의 "내용"을 새로 정의하지 않는다** -- 기존 클라이언트 패킷(C2ZMailAdd 등)을
// RelayEnvelope로 감싸 존에 주입하므로 Zone/Mail/UnitOfWork 경로가 평소와 한 글자도
// 다르지 않게 흐른다. 우회로를 만들면 "운영툴로 넣은 우편만 만료가 안 된다"는 사고가 난다.
//
// 와이어 포맷: docs/design/wire-format.md
class ToolProcessor final : public Pipeline::MessageProcessor
{
public:
    ToolProcessor(PlayerManager::Mutexed& playerManager, ZoneLinkRegistry::Mutexed& zoneLinkRegistry,
                  std::string sharedSecret);

    [[nodiscard]] std::string_view Name() const override { return "Tool"; }
    void RegistHandler() override;

private:
    // 레인 사이 메시지. **owner = 운영툴 세션 id** -- 명령의 주인은 대상이 아니라 명령을 보낸
    // 그 연결이다(대상이 전역이거나 캠페인 코드라 하나로 못 정한다). 이렇게 두면 한 운영툴
    // 연결이 보낸 명령들끼리는 보낸 순서대로 처리된다.
    void OnRecvToolStream(const Pipeline::OwnerId& owner, const RecvStreamBody& body);
    void OnToolLinkClosed(const Pipeline::OwnerId& owner);

    // 이 처리기가 받는 T2W 패킷 목록. 생성자 다음에 둔다.
    void Register();

    // 아래 여섯은 **해석이 끝난 패킷 구조체**를 받는다 -- 역직렬화와 형식 검증은
    // 디스패치 앞(Packet::Dispatcher)에서 끝난다.
    void HandleHello(const Network::Session::SPtr& toolSession, const Common::T2WHello& packet);
    void HandleNotice(const Network::Session::SPtr& toolSession, const Common::T2WNotice& packet);
    void HandleMailSend(const Network::Session::SPtr& toolSession, const Common::T2WMailSend& packet);
    void HandleMailDelete(const Network::Session::SPtr& toolSession, const Common::T2WMailDelete& request);
    void HandleCouponChunkPush(const Network::Session::SPtr& toolSession, const Common::T2WCouponChunkPush& packet);
    void HandleClientList(const Network::Session::SPtr& toolSession, const Common::T2WClientList& request);

    // ToolHello를 통과하지 않은 세션의 요청은 전부 NotAuthenticated로 거절한다. 인증 자체는
    // 공유 시크릿 비교 한 번뿐이지만, 운영툴 링크는 "이 포트에 붙을 수 있는 프로세스"를
    // 방화벽/루프백으로 제한하는 것이 실질적인 1차 방어선이고 이건 그 위의 최소 확인이다.
    [[nodiscard]] bool IsAuthenticated(const Network::SessionId toolSessionId) const;

    // 접속 중인 클라이언트 id를 값으로 떠온다.
    //
    // **왜 순회하면서 바로 처리하지 않는가**: PlayerManager::ForEach는 읽기 락을 잡은 채로
    // 돈다. 그 안에서 InjectClientPacket을 부르면 매니저를 다시 조회하므로 같은 스레드가
    // 락을 재진입한다. 목록만 떠서 락을 벗어난 뒤에 처리한다.
    [[nodiscard]] std::vector<Network::SessionId> SnapshotOnlineClients() const;

    void SendCommandResult(const Network::Session::SPtr& toolSession, const uint32_t requestId,
                        const Common::EToolResultCode resultCode, const uint32_t affectedCount) const;

    // 클라이언트 한 명에게 "그 클라이언트가 보낸 것처럼" 원본 클라이언트 패킷을 존에
    // 주입한다. 성공(대상이 접속 중이고 그 존과 연결이 살아있음) 여부를 반환한다.
    [[nodiscard]] bool InjectClientPacket(const Network::SessionId clientSessionId, const PacketId innerPacketId,
                                          const std::span<const byte> innerPayload) const;

    // ClientListReply 한 패킷에 담을 항목 수 상한. Header::MaxBodySize()가 8192이고
    // 항목 하나가 12바이트라 여유를 둬서 500개로 잡았다(8 + 500*12 = 6008바이트).
    static constexpr size_t kMaxClientListEntries = 500;

    PlayerManager::Mutexed& playerManager_;
    ZoneLinkRegistry::Mutexed& zoneLinkRegistry_;

    std::string sharedSecret_;

    // **왜 여기만 락인가**: 운영툴 명령은 대상이 전역이라 ownerId를 하나로 고정할 수 없고
    // (공지는 전 클라이언트, 쿠폰은 캠페인 코드), 그래서 이 집합은 BASIC의 여러 스레드에서
    // 보이게 된다. ownerId 어피니티로 못 막는 자리라 모델 단위 락으로 내려온 것이다.
    // 운영자가 손으로 누르는 명령이라 초당 수 건 수준이고, 샤딩까지 할 이유가 없다.
    Thread::Mutexed<std::unordered_set<Network::SessionId>> authenticatedSessions_;

    Packet::Dispatcher<PacketId, Network::Session::SPtr> dispatcher_;
};
