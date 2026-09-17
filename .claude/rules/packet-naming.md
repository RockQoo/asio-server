---
paths:
  - "**/Packet/*.h"
  - "**/Protocol/*.h"
  - "**/Protocol/*.cs"
---

# Packet Naming

## 하나의 enum

패킷 id는 **`Common::PacketId` 하나**로 관리한다. 링크별/방향별로 enum을 쪼개지 않는다.

```
Shared/Common/Src/PacketId.h  ->  namespace Common
```

예전에는 `Protocol` 이었다. `Common` 을 Core 가 쓰고 있어서 피한 이름인데, Core 쪽을
`Base` 로 개명하면서 자리가 났다 -- 지금은 `Common::` 이 **서버들이 공유하는 게임 계약**만
가리키고 `Base::` 가 Core 의 기반 타입을 가리킨다. Core 에 콘텐츠가 섞이면 안 된다는 규칙은
그대로고, 이름이 그 경계를 드러내게 됐다.

`enum class`를 유지한다(암묵 정수 변환 차단). 호출부의 `static_cast<uint16_t>`는
`Session::SendPacket`에 enum 오버로드를 얹어 없앤다 — Core는 `std::is_enum_v` 제약만 걸어
어떤 enum이든 받으므로 여전히 콘텐츠를 모른다.

## 이름 형식

```
<보내는쪽><2><받는쪽><PacketName>
```

구분자 없이 붙여 쓴다: `C2ZMove`, `W2TCommandResult`. **앞 3글자는 예외 없이 방향**이다.

| 약자 | 노드 |
|------|------|
| `C` | 클라이언트 |
| `T` | 운영툴(`Tool/GmTool`) |
| `W` | WorldServer |
| `G` | GatewayServer |
| `Z` | ZoneServer |

접두사는 **"실제로 그 패킷을 소켓에 쓰는 프로세스"** 기준이다. 논리적 주체가 아니다 --
`G2WClientConnected`는 클라이언트 접속을 알리는 내용이지만 보내는 건 GatewayServer
프로세스이므로 `C2W`가 아니라 `G2W`다. 이 규칙이 흔들리면 아래 대역 판정이 통째로 깨진다.

클라이언트 패킷의 상대는 **처리 주체** 기준으로 적는다. Gateway가 파싱하지 않는 순수
릴레이라 물리 연결은 클라↔Gateway지만, Move/Chat/Mail은 존이 처리하므로 `C2Z`,
Notice는 World가 직접 발신하므로 `W2C`다. id만 봐도 어디서 끝나는지 드러나게 하는 쪽을 택했다.

**한 id는 한 방향만 갖는다.** 요청과 응답이 같은 의미여도 id를 공유하지 않는다
(`C2ZChat` / `Z2CChatNotify`). 방향이 갈리면 본문 구조가 갈라지는 순간이 언젠가 오는데,
id를 공유하고 있으면 그때 "같은 id인데 본문이 다른" 상태가 조용히 만들어진다.

### `Req`/`Ack`는 붙이지 않는다

**방향이 이미 말하는 것을 접미사로 반복하지 않는다.** `C2W`면 클라가 보내는 것이니 `Req`는
같은 말을 두 번 하는 것이고, `W2C`면 서버가 주는 것이니 `Ack`도 마찬가지다.
`~Relay` 4개를 줄인 것(아래)과 같은 논리다.

```
W2ZEnterZoneRequest  ->  W2ZEnterZone
T2WMailSendRequest   ->  T2WMailSend
W2TClientListReply   ->  W2TClientList     (방향이 갈라주므로 T2W와 같은 이름이어도 된다)
T2WToolHello         ->  T2WHello          (Tool도 T2W가 이미 말한다)
```

**다만 접미사가 방향이 아니라 다른 것을 말하면 남긴다:**

| 접미사 | 무엇을 말하나 | 예 |
|---|---|---|
| `Result` | 본문 맨 앞에 **결과 코드**가 있다 | `W2CLogin`은 예외 — 이름만으로 결과임이 분명해 안 붙였다. `W2TCommandResult`, `Z2CTaskResult` |
| `Notify` | 요청한 사람이 아니라 **주변에 뿌린다**(브로드캐스트) | `Z2CChatNotify`, `Z2CMoveNotify` |

판단 기준은 하나다 — **그 단어를 빼면 정보가 사라지는가.** 사라지면 남기고, 접두 3글자가
이미 말하고 있으면 뗀다.

## 구조체 이름도 패킷 id 그대로 쓴다

**패킷 하나를 나타내는 구조체의 이름은 그 패킷 id와 글자 그대로 같다.** 접미사 `Packet`을
붙이지 않는다 -- 타입이 이미 구조체다.

```cpp
struct C2ZMove   { ... };   // Zone  -- 클라 요청
struct W2ZEnterZone { ... };   // World -- 존으로 나가는 입장 요청
struct W2THelloResult { ... }; // World -- 운영툴로 나가는 응답
```

**왜**: 이름 앞 3글자가 곧 방향이라 **타입만 보고 "이게 어디로 가는 패킷인지" 읽힌다.**
`EnterZoneNotifyPacket`/`ToolHelloResultPacket` 같은 이름은 그 정보를 지운다.

같은 패킷을 보내는 쪽과 받는 쪽이 각자 구조체를 가져도 **둘 다 그 패킷 id 이름을 쓴다** --
네임스페이스가 어느 쪽인지 말해준다.

```cpp
World::W2ZEnterZone   // 보내는 쪽이 채우는 고정 머리
Zone::W2ZEnterZone    // 받는 쪽이 파싱한 전체(머리 + 가변 꼬리)
```

### 구조체는 자기 패킷 id 를 `static constexpr kPacketId` 로 들고 있는다

```cpp
struct C2WLogin
{
    static constexpr PacketId kPacketId = PacketId::C2WLogin;
    // 관련 데이터
    void Set(...);
};
```

**`static` 이어야 한다.** 비정적 멤버로 두면 두 가지가 생긴다:

1. **와이어에 실린다.** 구조체 자체가 곧 패킷 본문이라(`as_bytes(span(&packet,1))`),
   `uint16` 2바이트가 본문 앞에 박히고 프레임 헤더의 `Header::id` 와 같은 값이 두 번 나간다.
   C# 클라이언트/운영툴은 이 레이아웃을 손으로 미러링하므로 조용히 어긋난다.
2. **`const` 멤버면 복사 대입이 삭제된다.** `packet.x = ...` 도, `std::vector` 의
   `erase`/`sort` 도 막힌다.

`static` 은 객체에 안 들어가서 `sizeof` 도 `is_trivially_copyable` 도 그대로다 --
`BinaryWriter::Write` 의 concept 을 계속 통과하고, 집합 초기화 `T{a, b, c}` 도 그대로 된다.

이 값이 있어야 보내는 쪽이 id 를 따로 받지 않고 `packet` 하나만 받을 수 있고,
`DirectionOf(TPacket::kPacketId)` 로 **방향을 컴파일 타임에 검증**할 수 있다.

### 레이아웃이 같아도 뜻이 다르면 구조체를 나눈다

`W2ZEnterZone`과 `Z2WZoneTransfer`는 필드가 완전히 같지만 `zoneId`의 뜻이 반대다(목표 존 /
보낸 존). 하나로 합치면 **같은 값을 반대로 읽는 버그가 조용히 생긴다.** 그래서 따로 두고,
사이에 명시적 변환을 둔다(`World::ToEnterZone`) -- 뜻이 바뀌는 지점이 코드에 드러난다.

### 패킷이 아닌 것에는 방향 접두사를 붙이지 않는다

| 종류 | 예 | 이름 짓는 법 |
|---|---|---|
| 여러 패킷이 공유하는 봉투 | `RelayEnvelope` | 역할 이름. `G2WRelay`/`W2GRelay`/`W2ZRelay`/`Z2WRelay` **넷이 공유**하므로 어느 하나의 이름을 쓰면 거짓말이 된다 |
| 패킷 본문의 부품 | `Position`(x,y) | 역할 이름. `C2ZMove` 안에도 들어가고 `Z2CMoveNotify` 본문에도 쓰인다 |
| 본문에 반복되는 항목 | `W2TClientListEntry` | 그 패킷 이름 + `Entry` |

판단 기준: **그 타입 하나가 패킷 하나와 1:1인가.** 아니면 방향 접두사를 붙이지 않는다 --
붙이면 그 이름이 곧 거짓이 된다.

### 모든 패킷은 자기 구조체를 갖는다

**예외가 없다.** 예전에는 `~Relay` 넷과 `Z2WUnitOfWorkStream`/`Z2CTaskResult`, 운영툴 대역의
가변 길이 패킷을 빼놨었는데, 그 판단 근거가 **보내는 쪽 기준**(`Set()` 과 모양이 안 맞는다)
이었고 받는 쪽은 따져보지 않은 것이었다.

**고정 머리 + 가변 꼬리**인 패킷은 꼬리를 `std::span` 으로 들고, 순회는 받는 쪽이 한다.

```cpp
struct Z2WUnitOfWorkStream
{
    static constexpr PacketId kPacketId = PacketId::Z2WUnitOfWorkStream;
    PlayerId playerId{};
    int64_t requestId{};
    uint64_t ownerId{};
    std::vector<TaskRecord> tasks;   // TaskRecord::payload 는 수신 버퍼를 가리키는 span

    [[nodiscard]] bool Parse(std::span<const byte> payload);
};
```

중계 넷은 봉투(`RelayEnvelope`)를 **멤버로 재사용**하고 구조체만 id 마다 하나씩 둔다
(`RelayPacket<TPacketId>` 템플릿 + `using G2WRelay = ...`). 봉투 자체는 여전히 넷이
공유하므로 `kPacketId` 를 갖지 않는다 -- "패킷이 아닌 것에는 방향 접두사를 붙이지 않는다"
규칙 그대로다.

**왜 예외를 없앴나**: 예외가 하나라도 있으면 받는 쪽 등록이 두 형태로 갈린다. 그리고 실제로
그 예외 때문에 버그가 하나 있었다 -- `Z2WUnitOfWorkStream` 을 핸들러가 직접 순회했는데, 그
루프가 이미 열린 트랜잭션(`AutoSpCommands`) 안이라 **스트림이 잘리면 앞쪽 태스크만
커밋**됐다. 파싱을 구조체로 빼면 그런 형태가 만들어지지 않는다.

### `span` 멤버를 가진 패킷 구조체는 핸들러 스코프를 넘기지 않는다

`RelayPacket::innerPayload`/`raw`, `TaskRecord::payload` 는 복사가 아니라 **수신 버퍼를
가리키는 subspan** 이다. 핸들러가 끝나면 그 버퍼는 사라진다 -- 들고 나가야 하면 복사한다.

### `Parse()` 는 가변 길이 패킷만 쓴다

`Common::FromBytes(packet, payload)` 가 `Parse()` 유무를 concept 으로 보고 갈라준다.
**고정 레이아웃 패킷은 아무것도 안 써도 된다** -- 크기 검사 + memcpy 를 `FromBytes` 가 한다.
`Common::ToBytes` 가 `Serialize()` 유무로 갈라주는 것과 정확히 대칭이고, 둘 다
`Shared/Common/Src/Packet/Wire.h` 에 있다.

## 번호 대역

| 대역 | 방향 |
|------|------|
| 1 – 999 | `C2Z` |
| 1000 – 1999 | `Z2C` |
| 2000 – 2999 | `C2W` (로그인/인증. **이 대역만 World가 끝점**이다) |
| 3000 – 3999 | `W2C` |
| 4000 – 4999 | `G2W` |
| 5000 – 5999 | `W2G` |
| 6000 – 6999 | `W2Z` |
| 7000 – 7999 | `Z2W` |
| 8000 – 8999 | `T2W` |
| 9000 – 9999 | `W2T` |
| 10000 – | 예약 (새 방향이 생기면 1000 단위로 잘라 쓴다) |

대역 안에서는 **대역 시작 + 1**부터 센다(`C2ZEcho = 1`, `W2ZEnterZone = 6001`).
값은 한 번 정하면 재사용하지 않는다 -- 패킷을 지워도 그 번호는 비워둔다.

### 왜 대역을 자르는가

`Header::id`는 `uint16_t` 하나뿐이라 어느 링크에서 온 값인지 헤더만 봐서는 모른다.
대역을 나누기 전에는 네 개의 id 공간이 전부 1번부터 시작해서, 숫자 `3`이 링크마다 각각
`G2WRelay` / `C2ZMove` / `W2ZLeaveZone` / `T2WNotice`를 뜻했다. 소켓이
분리돼 있어 사고는 안 났지만, 잘못 흘러든 패킷이 **다른 뜻으로 조용히 해석될 수 있는**
상태였다. 대역이 겹치지 않으면 그런 패킷은 미등록 id로 즉시 튕긴다.

부수 효과로 `RelayEnvelope::innerPacketId`는 항상 클라이언트 대역(1~3999)이어야
한다는 불변식이 생겨서, 값 하나로 검증할 수 있다. 값에서 방향을 되뽑는 `constexpr` 함수를
두고 `Dispatcher::Register`에서 assert하면 잘못된 방향의 핸들러 등록도 잡힌다.

## 현재 전체 목록

| 값 | 이름 | 기존 이름 |
|----|------|-----------|
| 1 | `C2ZEcho` | `Zone::PacketId::Echo` |
| 2 | `C2ZChat` | `Zone::PacketId::Chat` |
| 3 | `C2ZMove` | `Zone::PacketId::Move` |
| 4 | `C2ZMailAdd` | `Zone::PacketId::MailAdd` |
| 5 | `C2ZMailDel` | `Zone::PacketId::MailDel` |
| 6 | `C2ZMailBuy` | 신규 — 우편 지급 + 골드 차감(모델 두 개에 걸친 트랜잭션) |
| 1001 | `Z2CEchoAck` | `Echo` 재사용이었음 |
| 1002 | `Z2CChatNotify` | `Chat` 재사용이었음 |
| 1003 | `Z2CMoveNotify` | `Move` 재사용이었음, **본문에 sessionId 추가** |
| 1004 | `Z2CEnterZoneNotify` | `Zone::PacketId::EnterZoneNotify`. **본문에 clientSessionId 추가** — playerId가 int64 계정 키가 되면서, 브로드캐스트 발신자 키(세션 id)와 갈라졌다 |
| ~~1005~~ | ~~`Z2CMailAddAck`~~ | `Z2CTaskResult`로 통합되며 폐기(번호 재사용 안 함) |
| ~~1006~~ | ~~`Z2CMailDelAck`~~ | 〃 |
| 1007 | `Z2CTaskResult` | 신규 — UnitOfWork 결과 공통 응답 |
| 2001 | `C2WLogin` | 신규 — playerName + password. 계정이 없으면 그 자리에서 만든다 |
| 3001 | `W2CNotice` | `Zone::PacketId::Notice` |
| 3002 | `W2CLogin` | 신규 — errorCode + playerId + playerName |
| 4001 | `G2WClientConnected` | `GatewayLinkPacketId::ClientConnected` |
| 4002 | `G2WClientDisconnected` | `GatewayLinkPacketId::ClientDisconnected` |
| 4003 | `G2WRelay` | `GatewayLinkPacketId::FromClient` |
| 5001 | `W2GRelay` | `GatewayLinkPacketId::ToClient` |
| 6001 | `W2ZEnterZone` | `ZoneLinkPacketId::EnterZoneRequest` |
| 6002 | `W2ZLeaveZone` | `ZoneLinkPacketId::LeaveZoneNotify` |
| 6003 | `W2ZRelay` | `ZoneLinkPacketId::ForwardToZone` |
| 7001 | `Z2WZoneRegister` | `ZoneLinkPacketId::ZoneRegister` |
| 7002 | `Z2WRelay` | `ZoneLinkPacketId::ForwardToWorld` |
| 7003 | `Z2WZoneTransfer` | `ZoneLinkPacketId::ZoneTransferRequest` |
| 7004 | `Z2WUnitOfWorkStream` | `ZoneLinkPacketId::UnitOfWorkStream` |
| 8001 | `T2WHello` | `ToolLinkPacketId::ToolHello` |
| 8002 | `T2WNotice` | `ToolLinkPacketId::NoticeRequest` |
| 8003 | `T2WMailSend` | `ToolLinkPacketId::MailSendRequest` |
| 8004 | `T2WMailDelete` | `ToolLinkPacketId::MailDeleteRequest` |
| 8005 | `T2WCouponChunkPush` | `ToolLinkPacketId::CouponChunkPush` |
| 8006 | `T2WClientList` | `ToolLinkPacketId::ClientListRequest` |
| 9001 | `W2THelloResult` | `ToolLinkPacketId::ToolHelloAck` |
| 9002 | `W2TClientList` | `ToolLinkPacketId::ClientListReply` |
| 9003 | `W2TCommandResult` | `ToolLinkPacketId::ToolCommandAck` |

### `~Relay` 4개

`FromClient`/`ToClient`/`ForwardToZone`/`ForwardToWorld`는 전부 `~Relay`로 줄였다 --
방향은 이미 접두 3글자가 말하고, 네 패킷에 남는 뜻은 "중계"뿐이다. 넷 다 하는 일이 같다:
`RelayEnvelope`(`Packet/RelayEnvelope.h`)를 앞에 붙이고 클라이언트 원본 패킷을
**해석하지 않은 채 그대로** 다음 홉으로 넘긴다. 소켓 하나에 여러 플레이어의 패킷이 섞여
흐르므로 `clientSessionId` 태그가 필요하고, 중계 서버(Gateway/World)는 그 태그만 보고
어디로 넘길지 정한다 -- Gateway에 게임 로직이 한 줄도 없는 이유다.

**겉봉투의 방향과 안쪽 내용물의 방향은 별개다.** 존이 클라이언트에게 보내는 패킷도 World를
거쳐야 하므로 `Z2WRelay`(홉 방향)에 `innerPacketId = Z2CMoveNotify`(내용물 방향)가 실린다.
접두 3글자 규칙이 두 층에 각각 따로 적용되고, 안쪽은 항상 클라이언트 대역(1~3999)이다.

### 콘텐츠마다 Ack를 새로 만들지 않는다

상태를 바꾸는 요청(우편/인벤/친구 등)의 응답은 콘텐츠별 Ack 패킷이 아니라 **`Z2CTaskResult`
하나**로 돌아간다. 본문은 `errorCode(4) + requestPacketId(2) + requestId(8) + UnitOfWork 태스크
스트림`이고,
클라이언트는 그 태스크 목록을 자기 메모리에 그대로 적용해서 서버와 동기화한다 -- 서버가 DB에
남기는 변경과 클라이언트가 적용하는 변경이 같은 목록이라 한쪽만 빠뜨릴 여지가 없다.

- `requestPacketId`: 어느 요청의 결과인지 짝짓는 키. **0이면 요청 없이 서버가 만든 변경**
  (메일 자동 만료 등)이라 클라이언트는 통지로 받아 적용만 한다.
- 실패하면 태스크 스트림 없이 `errorCode`(`Common::EErrorCode`)만 온다 -- 서버는 이미
  메모리를 되돌린 뒤다.

그래서 새 콘텐츠를 추가할 때 Z2C 응답 id를 새로 딸 필요가 없다. 필요한 건 태스크 종류
(`Common::ETaskCategory` + 세부 동작)와 에러 코드뿐이다.

## 가시성

C++ enum은 여러 헤더로 나눠 정의할 수 없으므로, `PacketId.h`를 include하는 쪽은 전체 목록을
보게 된다. 이건 단일 enum을 택한 대가로 감수한다.

다만 **별도 코드베이스는 자기 대역만 선언한다.** `Tool/GmTool`(C#)은 `T2W`/`W2T` 값만 갖는
enum을 직접 선언하고, 클라이언트 패킷 정의를 "참고용"으로 미러링해두지 않는다 --
운영툴의 공지/우편은 클라이언트 패킷을 존에 주입하는 방식이지만 그 변환은 전적으로 World의
`World::ToolProcessor`가 하므로, 운영툴은 어떤 클라이언트 패킷으로 바뀌는지 알 필요가 없다.

## 호출부 표기: `PacketId::C2ZMove`

정의는 `namespace Common` 안이지만, `PacketId.h` 맨 끝의 `using Common::PacketId;`로
전역에 노출해서 **호출부는 `Common::`을 붙이지 않는다**. `Common::PacketId::C2ZMove`는
시그니처가 길어질 뿐이었다.

```cpp
dispatcher_.Register(this, &ToolProcessor::HandleHello);   // id 는 TPacket::kPacketId 에서
void HandleClientPacket(const Network::SessionId clientSessionId, const PacketId packetId, ...);
```

`ELogCategory`(`Shared/Core/Src/Log/LogCategory.h`)와 같은 방식이다. 다만 `ELogCategory`는
프로젝트마다 자기 것을 같은 이름으로 노출하는 반면 `PacketId`는 저장소 전체에 하나뿐이라
이름이 겹칠 여지도 없다.

**`using enum`은 쓰지 않는다.** `using enum Common::PacketId;`까지 가면 `T2WHello`처럼
한정자 없이 쓸 수 있지만, 열거자 30여 개가 전역 이름이 되고 이 헤더를 include한 모든 TU가
그걸 떠안는다. `PacketId::` 한 겹은 "이 값이 패킷 id"라는 표시로 남겨둔다. 타입 안전성은
어느 쪽이든 그대로다(`enum class`이므로 정수로의 암묵 변환은 계속 막힌다).

## 캐스팅을 없애는 오버로드

`enum class`라 그대로는 `uint16_t` 자리에 못 넣는데, 호출부마다 `static_cast`를 쓰면 잡음이
크다(치환 전 38곳). 그래서 Core에 enum을 받는 오버로드를 하나씩 얹어뒀다 --
`Session::SendPacket`과 `Packet::BuildFrame`이고, 둘 다 `std::is_enum_v` 제약만 걸어서
**Core는 여전히 어떤 enum인지 모른다**(콘텐츠를 모르는 라이브러리라는 원칙 유지).

콘텐츠를 아는 쪽(`ZoneProcessor::SendToPlayer`/`BroadcastToZone`, `BroadcastProcessor::Broadcast`,
`ToolProcessor::InjectClientPacket`, `App::BroadcastToAll`,
`Instance::HandleClientPacket`)은 아예 매개변수 타입을 `PacketId`로 바꿨다.
남은 `static_cast`는 `RelayEnvelope::innerPacketId`(POD 필드가 `uint16_t`)에 넣는
두 곳과 로그 출력 한 곳뿐이다.

## 적용 상태

**적용 완료.** 링크별 4개 enum(`Zone::PacketId`, `World::GatewayLinkPacketId`,
`World::ZoneLinkPacketId`, `World::ToolLinkPacketId`)은 삭제됐고 전부 `Common::PacketId`로
합쳐졌다. `World::EToolResultCode`만 `Shared/Common/Src/Packet/ToolResultCode.h`로
따로 남았다(패킷 id가 아니라 결과 코드라 대역과 무관).

같이 처리한 것:

- `Echo`/`Move`/`Chat`은 요청과 응답이 같은 id를 쓰고 있었다 -> 방향별로 갈라졌다
  (`C2ZEcho`/`Z2CEchoAck`, `C2ZMove`/`Z2CMoveNotify`, `C2ZChat`/`Z2CChatNotify`).
- `Z2CMoveNotify` 본문 앞에 `sessionId(uint32)`를 추가했다 -- 그전에는 브로드캐스트에 누가
  움직였는지가 없어서 `Z2CChatNotify`와 형태가 어긋나 있었다. **와이어 포맷 변경이다.**
- `WorldLinkHandler`의 Echo 즉시 응답은 받은 envelope을 그대로 되돌려 보내던 것을,
  `innerPacketId`만 `Z2CEchoAck`로 바꿔 쓰도록 고쳤다.
- `Tool/GmTool`의 `ZoneClientPacketId.cs`와 그 값을 고정하던 테스트 3건은 위 가시성 규칙에
  따라 삭제했다(xUnit 80 -> 77개). `ToolLinkPacketId.cs`는 `PacketId.cs`로 바뀌었다.
- 새 `Shared/Common/` 프로젝트(또는 헤더 전용 폴더)를 6개 vcxproj가 참조하도록 추가한다.
