#pragma once

#include "Shared/Core/Src/Common/Types.h"
#include "Game/Player.h"
#include "Game/PlayerRegistry.h"
#include "Handler/PlayerContext.h"
#include "Packet/ClientPackets.h"
#include "Shared/Protocol/Src/PacketId.h"

namespace Zone
{
    class WorldLink;
    class WorkerManager;
    class BroadcastDispatcher;
}

namespace Mail
{
    class Registry;
}

namespace Zone
{
    // **플레이어 레인(owner = clientSessionId)**의 진입점 -- 입장/퇴장과 패킷 라우팅을 맡고,
    // 콘텐츠 처리 자체는 콘텐츠별 파일(PlayerMail 등)로 내보낸다. 존이 아니라 플레이어가
    // 주인이므로 **같은 존의 서로 다른 플레이어가 서로 다른 스레드에서 동시에 처리된다.**
    //
    // 여기 남아 있는 Move/Chat은 모델 변경도 DB 저장도 없어서(브로드캐스트가 전부라
    // UnitOfWork를 열지 않는다) 콘텐츠 파일로 뺄 것이 없다.
    //
    // 이동은 네 단계로 나뉘고, 요점은 **이동 패킷이 존 레인을 건드리지 않는다**는 것이다:
    //   ① 디스패치 앞에서 파싱  ② MoveModel에 요청만 기록
    //   ③ 브로드캐스트는 즉시(틱을 기다리면 체감 지연이 커진다)
    //   ④ 적분·경계 판정은 존 레인이 다음 틱에 (Instance::Tick)
    //
    // 흐름 전체: docs/sequences/zone-handoff.html
    class PlayerProcessor
    {
    public:
        PlayerProcessor(PlayerRegistry& playerRegistry, WorkerManager& zoneWorkers,
                        BroadcastDispatcher& broadcastDispatcher, WorldLink& worldLink,
                        Mail::Registry& mailRegistry);

        // 아래 셋은 전부 그 clientSessionId를 담당하는 플레이어 레인 스레드에서 호출된다.
        //
        // mails/currencies는 World가 DB에서 읽어 W2ZEnterZone에 실어 보낸 시작 상태다. 존이
        // DB를 직접 읽지 않는 이유는 ZoneLinkPackets.h의 표 주석 참고.
        void OnPlayerEnter(const Network::SessionId clientSessionId, const uint32_t playerId,
                           const uint32_t zoneId, const float x, const float y,
                           std::vector<Mail::Info> mails,
                           std::vector<std::pair<uint8_t, int64_t>> currencies);
        void OnPlayerLeave(const Network::SessionId clientSessionId);

        // 콘텐츠 패킷 진입점. 어느 존인지는 Player가 들고 있으므로 LB가 알려줄 필요가 없다.
        void HandleClientPacket(const Network::SessionId clientSessionId,
                                const PacketId packetId, const std::span<const byte> payload);

    private:
        // 자기 패킷은 여기서, 콘텐츠 패킷은 각 콘텐츠의 Register가 등록한다.
        void RegisterPacketHandlers();

        // HandleClientPacket이 이미 Player를 찾아 넘겨주므로 여기서 다시 "이 사람이 존재하는가"를
        // 확인할 필요가 없고, 페이로드도 이미 해석돼 들어온다(RegisterPacketHandler 주석 참고).
        void HandleMove(const PlayerContext& context, const C2ZMove& packet);
        void HandleChat(const PlayerContext& context, const C2ZChat& packet);

        void SendToPlayer(const Network::SessionId clientSessionId, const PacketId innerPacketId,
                          const std::span<const byte> payload) const;

        // 존 레인이 발행해둔 대상 스냅샷을 읽어 BROADCAST 그룹으로 넘긴다.
        // 로스터를 직접 순회하지 않으므로 존 레인과 겹치지 않는다.
        void BroadcastToZone(const uint32_t zoneId, const PacketId innerPacketId,
                             const std::span<const byte> payload,
                             const Network::SessionId excludeClientSessionId = 0) const;

        PlayerRegistry& playerRegistry_;
        WorkerManager& zoneWorkers_;
        BroadcastDispatcher& broadcastDispatcher_;
        WorldLink& worldLink_;
        Mail::Registry& mailRegistry_;

        // 패킷 타입 -> 핸들러. **존마다도 플레이어마다도 아니고 이 처리기에 하나만** 둔다 --
        // 개체마다 테이블을 갖는 구조는 등록 내용이 개체별로 다를 때만 의미가 있고, 그렇지
        // 않으면 접속 수만큼 테이블이 통째로 복제된다(항목 하나가 수십 바이트라도 만 단위
        // 동접이면 무시할 수 없는 양이 된다).
        // 등록은 생성자에서 끝나고 이후로는 읽기 전용이라, 여러 스레드가 동시에 Dispatch해도
        // 안전하다.
        PlayerPacketDispatcher packetDispatcher_;
    };
}
