#pragma once

#include "Server/Core/Src/Base/BasicTypes.h"
#include "Server/Core/Src/Base/Types.h"
#include "Server/Core/Src/Network/Session.h"
#include "Server/Common/Src/Ids.h"
#include "Server/Common/Src/PacketId.h"

// 레인 사이를 오가는 **내부 메시지 id**.
//
// 프로세서의 핸들러 표는 키가 `uint32` 하나뿐이고 **PacketId와 같은 공간을 쓴다**(한
// 프로세서가 내부 메시지와 클라 패킷을 같은 표로 받는다). PacketId는 1000 단위 대역이라
// 0x10000부터 쓰면 겹칠 일이 없다.
enum class EZoneMsg : uint32_t
{
    // ── 소켓 -> LB ────────────────────────────────────────────────────────────
    // **owner = World 링크 세션 id.** 아직 봉투를 안 깠다.
    //
    // 링크가 **존마다 하나**라, 한 존의 수신은 이 단계에서 레인 하나로 직렬화된다.
    // 봉투를 까는 것 말고는 아무것도 안 하므로 여기 머무는 시간이 짧아야 한다.
    OnRecvStream = 0x10000,

    // ── LB -> BASIC ───────────────────────────────────────────────────────────
    // **owner = playerId.** 봉투를 깐 뒤라 여기서 비로소 접속자 수만큼 갈라진다.
    FromWorldStream,

    // ── TIMER -> TICK ─────────────────────────────────────────────────────────
    // **owner = 그 존의 틱 주인.** 만기 판정만 TIMER 가 하고 적분·경계 판정은 여기서.
    ZoneTick,

    // ── BASIC/TICK -> BROADCAST ───────────────────────────────────────────────
    // 팬아웃 한 건을 함에 넣는다. **owner = zoneId.** 같은 존의 편지는 보낸 순서대로
    // 나가야 한다(채팅 두 줄이 뒤바뀌면 눈에 띈다).
    Fanout,

    // 함을 비운다. TIMER 가 깨운다. **owner = zoneId.**
    //
    // 타이머를 BROADCAST 프로세서가 직접 들지 않고 TIMER 레인을 거치는 이유: 타이머
    // 콜백은 io 스레드에서 도는데, 거기서 바로 비우면 **함을 두 스레드가 만진다**. 만기만
    // TIMER 가 맡고 비우기는 원래 레인으로 되돌리면 함의 주인이 하나로 남는다.
    FanoutFlush,

    // ── TIMER 레인 ────────────────────────────────────────────────────────────
    // 만기 통지. **owner = 고정 상수** -- 만기에는 주인이 없다.
    TimerTick,        // 존 틱 만기 -> 담당 존마다 ZoneTick 을 TICK 레인에 넣는다
    TimerFanoutFlush, // 팬아웃 함 비우기 만기 -> 담당 존마다 FanoutFlush 를 BROADCAST 에 넣는다
    TimerStatsDump,  // 레인 통계 덤프 만기 -- 값 읽고 로그만 남기므로 TIMER 에서 끝낸다
};

// 주인이 없는 전역 작업을 한 스레드로 몰 때 쓰는 고정 키.
// **owner 없는 경로를 만들지 않는 것이 규약이다** -- 모든 메시지가 주인을 갖는다.
inline constexpr uint64_t kGlobalQueryKey = 1;

// `OnRecvStream`의 본문.
//
// 직렬화하지 않고 통째로 싣는다 -- 같은 프로세스 안이라 Session::SPtr도 그대로 간다.
// 세션을 shared_ptr로 들고 있으므로 **메시지가 큐에 있는 동안 그 연결이 죽지 않는다.**
struct RecvStreamBody final
{
    Network::Session::SPtr session;
    PacketId packetId{};
    std::vector<byte> payload;
};

// `FromWorldStream`의 본문. 봉투에서 꺼낸 값들이 여기 풀려 있다 -- BASIC 은 봉투를 모른다.
//
// **playerId 가 곧 주인이다.** 그래서 이 값이 봉투에 실려 와야 하고, 실려 오지 않으면
// LB 가 "이 패킷이 누구 것인지" 알 방법이 없다.
struct FromWorldStreamBody final
{
    PacketId packetId{};
    Network::SessionId clientSessionId{};
    Common::PlayerId playerId{};
    std::vector<byte> payload;
};

// `ZoneTick`의 본문.
struct ZoneTickBody final
{
    float deltaSeconds{};
    int64_t nowUt{};
};

// `Fanout`의 본문. 대상 목록은 **보내는 쪽이 이미 걸러낸 사본**이라 받는 쪽은 로스터를
// 보지 않는다 -- BROADCAST 레인이 존의 컨테이너 락에 묶이지 않는 것이 요점이다.
struct FanoutBody final
{
    std::vector<Network::SessionId> targets;
    PacketId innerPacketId{};
    std::vector<byte> payload;
};
