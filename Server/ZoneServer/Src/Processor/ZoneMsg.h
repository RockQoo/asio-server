#pragma once

#include "Server/Core/Src/Base/BasicTypes.h"
#include "Server/Core/Src/Base/Types.h"
#include "Server/Core/Src/Network/Session.h"
#include "Server/Common/Src/Ids.h"
#include "Server/Common/Src/PacketId.h"

class Player;

// 레인 사이를 오가는 **내부 메시지 id**.
//
// 프로세서의 핸들러 표는 키가 `uint32` 하나뿐이고 **PacketId와 같은 공간을 쓴다**(한
// 프로세서가 내부 메시지와 클라 패킷을 같은 표로 받는다). PacketId는 1000 단위 대역이라
// 0x10000부터 쓰면 겹칠 일이 없다 -- World의 EWorldMsg와 같은 규칙이다.
enum class EZoneMsg : uint32_t
{
    // ── World 링크 수신 ───────────────────────────────────────────────────────
    // 소켓 스레드 -> LB. **owner = World 링크 세션 id.** 아직 껍질을 안 깠다.
    //
    // **World 링크가 이 프로세스에 하나뿐이라 이 단계의 병렬도가 1이다.** LB 레인 수를
    // 늘려도 주인 값이 하나라 같은 레인으로만 간다. 구조적으로 존에서 가장 좁은 목이고,
    // 재고 나서 줄이는 것이 측정 항목이다.
    OnRecvStream = 0x10000,

    // LB -> BASIC. **owner = playerId.** 껍질을 깐 뒤라 여기서 비로소 접속자 수만큼
    // 갈라진다. 어느 존의 ZoneProcessor 인지는 LB 가 targetProcessorId 로 정한다.
    FromWorldStream,

    // ── BASIC -> TICK ─────────────────────────────────────────────────────────
    // **owner = 존 서수.** 로스터는 TICK 소유라 BASIC 이 직접 넣지 않는다.
    ZoneEnter,
    ZoneLeave,

    // TIMER -> TICK. **owner = 존 서수.** 만기 판정만 TIMER 가 하고 적분·경계 판정은 여기서.
    ZoneTick,

    // ── BASIC/TICK -> BROADCAST ───────────────────────────────────────────────
    // 팬아웃 한 건을 함에 넣는다. **owner = 존 서수.** 같은 존의 편지는 보낸 순서대로
    // 나가야 한다(채팅 두 줄이 뒤바뀌면 눈에 띈다).
    Fanout,

    // 함을 비운다. TIMER 가 깨운다. **owner = 존 서수.**
    FanoutFlush,

    // ── TIMER 레인 ────────────────────────────────────────────────────────────
    // 만기 통지. **owner = 고정 상수** -- 만기에는 주인이 없다.
    TimerTick,        // 존 틱 만기 -> 담당 존마다 ZoneTick 을 TICK 레인에 넣는다
    TimerFanoutFlush, // 팬아웃 함 비우기 만기
    TimerMailSweep,   // 우편 만료 스윕 만기 -> BASIC 레인의 만료 처리기로 되돌린다
    TimerStatsDump,   // 레인 통계 덤프 만기 -- 값 읽고 로그만 남기므로 TIMER 에서 끝낸다

    // ── BASIC(우편 만료) ──────────────────────────────────────────────────────
    // **owner = 고정 상수.** 전원을 훑는 일이라 주인을 하나로 못 정한다.
    MailSweep,
};

// 주인이 없는 전역 작업을 한 스레드로 몰 때 쓰는 고정 키.
// **owner 없는 경로를 만들지 않는 것이 규약이다** -- 모든 메시지가 주인을 갖는다.
inline constexpr uint64_t kGlobalQueryKey = 1;

// `OnRecvStream` / `FromWorldStream`의 본문.
//
// 직렬화하지 않고 통째로 싣는다 -- 같은 프로세스 안이라 Session::SPtr도 그대로 간다.
// 세션을 shared_ptr로 들고 있으므로 **메시지가 큐에 있는 동안 그 연결이 죽지 않는다.**
struct RecvStreamBody final
{
    Network::Session::SPtr session;
    PacketId packetId{};
    std::vector<byte> payload;
};

// `ZoneEnter`의 본문. Player 는 BASIC 이 만들고 TICK 이 로스터에 넣는다 --
// **두 레인이 같은 객체를 가리키고**, 모델마다 어느 레인이 만질 수 있는지는 Player.h 규약.
struct ZoneEnterBody final
{
    std::shared_ptr<Player> player;
};

// `ZoneLeave`의 본문. 주인이 존 서수라 누구인지는 본문이 들고 있어야 한다.
struct ZoneLeaveBody final
{
    Network::SessionId clientSessionId{};
};

// `ZoneTick`의 본문.
struct ZoneTickBody final
{
    float deltaSeconds{};
};

// `Fanout`의 본문. 대상 목록은 **TICK 이 발행해둔 스냅샷을 걸러낸 사본**이라 로스터를
// 다시 순회하지 않는다.
struct FanoutBody final
{
    std::vector<Network::SessionId> targets;
    PacketId innerPacketId{};
    std::vector<byte> payload;
};
