#pragma once

#include "Server/Core/Src/Base/BasicTypes.h"
#include "Server/Core/Src/Network/Session.h"
#include "Server/Common/Src/PacketId.h"

// 레인 사이를 오가는 **내부 메시지 id**.
//
// 프로세서의 핸들러 표는 키가 `uint32` 하나뿐이고 **PacketId와 같은 공간을 쓴다**(한 프로세서가
// 내부 메시지와 클라 패킷을 같은 표로 받는다). PacketId는 1000 단위 대역이라 0x10000부터 쓰면
// 겹칠 일이 없다.
enum class EWorldMsg : uint32_t
{
    // ── 게이트웨이 링크 ───────────────────────────────────────────────────────
    // 프로액터(소켓 스레드) -> BASIC. **owner = 게이트웨이 세션 id.** 아직 껍질을 안 깠다.
    // 이 단계의 병렬도는 붙어 있는 게이트웨이 프로세스 수까지다.
    OnRecvStream = 0x10000,

    // BASIC -> BASIC(자기 자신). **owner = clientSessionId.** 껍질을 깐 뒤라 여기서 비로소
    // 접속자 수만큼 갈라진다. 프로세서를 바꾸는 게 아니라 **스레드를 바꾸려고** 다시 넣는다.
    FromClientStream,

    // ── 존 링크 ───────────────────────────────────────────────────────────────
    // 프로액터 -> BASIC. **owner = 존 세션 id.**
    OnRecvZoneStream,

    // BASIC -> BASIC(자기 자신). **owner = 패킷마다 다르다**(clientSessionId 또는 zoneId).
    FromZoneStream,

    // 링크가 끊겼다. **owner = 링크 세션 id.** body 없음 -- 주인이 곧 끊긴 세션이다.
    OnZoneLinkClosed,

    // 전체 공지. **owner = 고정 상수**(kGlobalQueryKey) -- 주인이 없는 일을 한 레인으로 몬다.
    BroadcastToAll,

    // ── DB 레인 ───────────────────────────────────────────────────────────────
    // **owner = playerId**(계정 단위 직렬화). 재접속으로 세션이 바뀌어도 한 계정의 쓰기가
    // 도착 순서대로 처리된다.
    DbExecute,   // SP 목록 실행 + 실패 정책
    DbInvoke,    // SP가 아닌 일을 블로킹 허용 레인에서 돌린다

    // ── 로그인 (DB 레인 -> BASIC 레인 되돌림) ─────────────────────────────────
    // **owner = clientSessionId.** 로그인의 모든 결말이 같은 레인에서 나가야 클라이언트가
    // 받는 순서가 흔들리지 않는다.
    LoginFailure,
    LoginSuccess,

    // ── 운영툴 ────────────────────────────────────────────────────────────────
    // **owner = 운영툴 세션 id.** 명령의 주인은 대상이 아니라 명령을 보낸 그 연결이다
    // (대상이 전역이거나 캠페인 코드라 하나로 못 정한다).
    OnRecvToolStream,
    OnToolLinkClosed,

    // ── TIMER 레인 ────────────────────────────────────────────────────────────
    // 만기 통지. **owner = 고정 상수** -- 만기에는 주인이 없다.
    TimerShortTick,
    TimerLongTick,

    // NETWORK 레인 송신. owner = 세션 id.
    TrySend,
};

// 주인이 없는 전역 작업을 한 스레드로 몰 때 쓰는 고정 키.
// **owner 없는 경로를 만들지 않는 것이 규약이다** -- 모든 메시지가 주인을 갖는다.
inline constexpr uint64_t kGlobalQueryKey = 1;

// `OnRecvStream` / `FromClientStream`의 본문.
//
// 직렬화하지 않고 통째로 싣는다 -- 같은 프로세스 안이라 Session::SPtr도 그대로 간다.
// 세션을 shared_ptr로 들고 있으므로 **메시지가 큐에 있는 동안 그 연결이 죽지 않는다.**
struct RecvStreamBody final
{
    Network::Session::SPtr session;
    PacketId packetId{};
    std::vector<byte> payload;
};
