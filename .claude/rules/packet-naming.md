---
paths:
  - "**/Packet/*.h"
  - "**/Protocol/*.h"
  - "**/Protocol/*.cs"
---

# Packet Naming

## 하나의 enum

패킷 id는 **`Protocol::PacketId` 하나**로 관리한다. 링크별/방향별로 enum을 쪼개지 않는다.

```
Shared/Protocol/Src/PacketId.h  ->  namespace Protocol
```

`Common`이 아니라 `Protocol`인 이유: `Common`은 이미 `Shared/Core/Src/Common/`이 쓰고 있고,
Core는 게임 콘텐츠를 모르는 정적 라이브러리라 거기에 패킷 id를 넣으면 그 원칙이 깨진다.

`enum class`를 유지한다(암묵 정수 변환 차단). 호출부의 `static_cast<uint16_t>`는
`Session::SendPacket`에 enum 오버로드를 얹어 없앤다 — Core는 `std::is_enum_v` 제약만 걸어
어떤 enum이든 받으므로 여전히 콘텐츠를 모른다.

## 이름 형식

```
<보내는쪽><2><받는쪽><PacketName>
```

구분자 없이 붙여 쓴다: `C2ZMove`, `W2TToolCommandAck`. **앞 3글자는 예외 없이 방향**이다.

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

## 번호 대역

| 대역 | 방향 |
|------|------|
| 1 – 999 | `C2Z` |
| 1000 – 1999 | `Z2C` |
| 2000 – 2999 | `C2W` (로그인/인증 자리, 현재 비어 있음) |
| 3000 – 3999 | `W2C` |
| 4000 – 4999 | `G2W` |
| 5000 – 5999 | `W2G` |
| 6000 – 6999 | `W2Z` |
| 7000 – 7999 | `Z2W` |
| 8000 – 8999 | `T2W` |
| 9000 – 9999 | `W2T` |
| 10000 – | 예약 (새 방향이 생기면 1000 단위로 잘라 쓴다) |

대역 안에서는 **대역 시작 + 1**부터 센다(`C2ZEcho = 1`, `W2ZEnterZoneRequest = 6001`).
값은 한 번 정하면 재사용하지 않는다 -- 패킷을 지워도 그 번호는 비워둔다.

### 왜 대역을 자르는가

`PacketHeader::id`는 `uint16_t` 하나뿐이라 어느 링크에서 온 값인지 헤더만 봐서는 모른다.
대역을 나누기 전에는 네 개의 id 공간이 전부 1번부터 시작해서, 숫자 `3`이 링크마다 각각
`G2WRelay` / `C2ZMove` / `W2ZLeaveZoneNotify` / `T2WNoticeRequest`를 뜻했다. 소켓이
분리돼 있어 사고는 안 났지만, 잘못 흘러든 패킷이 **다른 뜻으로 조용히 해석될 수 있는**
상태였다. 대역이 겹치지 않으면 그런 패킷은 미등록 id로 즉시 튕긴다.

부수 효과로 `ClientEnvelopeHeader::innerPacketId`는 항상 클라이언트 대역(1~3999)이어야
한다는 불변식이 생겨서, 값 하나로 검증할 수 있다. 값에서 방향을 되뽑는 `constexpr` 함수를
두고 `PacketDispatcher::Register`에서 assert하면 잘못된 방향의 핸들러 등록도 잡힌다.

## 현재 전체 목록

| 값 | 이름 | 기존 이름 |
|----|------|-----------|
| 1 | `C2ZEcho` | `Zone::PacketId::Echo` |
| 2 | `C2ZChat` | `Zone::PacketId::Chat` |
| 3 | `C2ZMove` | `Zone::PacketId::Move` |
| 4 | `C2ZMailAdd` | `Zone::PacketId::MailAdd` |
| 5 | `C2ZMailDel` | `Zone::PacketId::MailDel` |
| 1001 | `Z2CEchoAck` | `Echo` 재사용이었음 |
| 1002 | `Z2CChatNotify` | `Chat` 재사용이었음 |
| 1003 | `Z2CMoveNotify` | `Move` 재사용이었음, **본문에 sessionId 추가** |
| 1004 | `Z2CEnterZoneNotify` | `Zone::PacketId::EnterZoneNotify` |
| 1005 | `Z2CMailAddAck` | `Zone::PacketId::MailAddAck` |
| 1006 | `Z2CMailDelAck` | `Zone::PacketId::MailDelAck` |
| 3001 | `W2CNotice` | `Zone::PacketId::Notice` |
| 4001 | `G2WClientConnected` | `GatewayLinkPacketId::ClientConnected` |
| 4002 | `G2WClientDisconnected` | `GatewayLinkPacketId::ClientDisconnected` |
| 4003 | `G2WRelay` | `GatewayLinkPacketId::FromClient` |
| 5001 | `W2GRelay` | `GatewayLinkPacketId::ToClient` |
| 6001 | `W2ZEnterZoneRequest` | `ZoneLinkPacketId::EnterZoneRequest` |
| 6002 | `W2ZLeaveZoneNotify` | `ZoneLinkPacketId::LeaveZoneNotify` |
| 6003 | `W2ZRelay` | `ZoneLinkPacketId::ForwardToZone` |
| 7001 | `Z2WZoneRegister` | `ZoneLinkPacketId::ZoneRegister` |
| 7002 | `Z2WRelay` | `ZoneLinkPacketId::ForwardToWorld` |
| 7003 | `Z2WZoneTransferRequest` | `ZoneLinkPacketId::ZoneTransferRequest` |
| 7004 | `Z2WUnitOfWorkStream` | `ZoneLinkPacketId::UnitOfWorkStream` |
| 8001 | `T2WToolHello` | `ToolLinkPacketId::ToolHello` |
| 8002 | `T2WNoticeRequest` | `ToolLinkPacketId::NoticeRequest` |
| 8003 | `T2WMailSendRequest` | `ToolLinkPacketId::MailSendRequest` |
| 8004 | `T2WMailDeleteRequest` | `ToolLinkPacketId::MailDeleteRequest` |
| 8005 | `T2WCouponChunkPush` | `ToolLinkPacketId::CouponChunkPush` |
| 8006 | `T2WClientListRequest` | `ToolLinkPacketId::ClientListRequest` |
| 9001 | `W2TToolHelloAck` | `ToolLinkPacketId::ToolHelloAck` |
| 9002 | `W2TClientListReply` | `ToolLinkPacketId::ClientListReply` |
| 9003 | `W2TToolCommandAck` | `ToolLinkPacketId::ToolCommandAck` |

### `~Relay` 4개

`FromClient`/`ToClient`/`ForwardToZone`/`ForwardToWorld`는 전부 `~Relay`로 줄였다 --
방향은 이미 접두 3글자가 말하고, 네 패킷에 남는 뜻은 "중계"뿐이다. 넷 다 하는 일이 같다:
`ClientEnvelopeHeader`(`Packet/RelayEnvelope.h`)를 앞에 붙이고 클라이언트 원본 패킷을
**해석하지 않은 채 그대로** 다음 홉으로 넘긴다. 소켓 하나에 여러 플레이어의 패킷이 섞여
흐르므로 `clientSessionId` 태그가 필요하고, 중계 서버(Gateway/World)는 그 태그만 보고
어디로 넘길지 정한다 -- Gateway에 게임 로직이 한 줄도 없는 이유다.

**겉봉투의 방향과 안쪽 내용물의 방향은 별개다.** 존이 클라이언트에게 보내는 패킷도 World를
거쳐야 하므로 `Z2WRelay`(홉 방향)에 `innerPacketId = Z2CMoveNotify`(내용물 방향)가 실린다.
접두 3글자 규칙이 두 층에 각각 따로 적용되고, 안쪽은 항상 클라이언트 대역(1~3999)이다.

## 가시성

C++ enum은 여러 헤더로 나눠 정의할 수 없으므로, `PacketId.h`를 include하는 쪽은 전체 목록을
보게 된다. 이건 단일 enum을 택한 대가로 감수한다.

다만 **별도 코드베이스는 자기 대역만 선언한다.** `Tool/GmTool`(C#)은 `T2W`/`W2T` 값만 갖는
enum을 직접 선언하고, 클라이언트 패킷 정의를 "참고용"으로 미러링해두지 않는다 --
운영툴의 공지/우편은 클라이언트 패킷을 존에 주입하는 방식이지만 그 변환은 전적으로 World의
`Tool::ToolProcessor`가 하므로, 운영툴은 어떤 클라이언트 패킷으로 바뀌는지 알 필요가 없다.

## 호출부 표기: `PacketId::C2ZMove`

정의는 `namespace Protocol` 안이지만, `PacketId.h` 맨 끝의 `using Protocol::PacketId;`로
전역에 노출해서 **호출부는 `Protocol::`을 붙이지 않는다**. `Protocol::PacketId::C2ZMove`는
같은 얘기를 두 번 하는 셈이라(`Protocol` + `PacketId`) 시그니처가 길어질 뿐이었다.

```cpp
dispatcher_.Register(PacketId::T2WToolHello, this, &ToolProcessor::HandleToolHello);
void HandleClientPacket(const Network::SessionId clientSessionId, const PacketId packetId, ...);
```

`ELogCategory`(`Shared/Core/Src/Log/LogCategory.h`)와 같은 방식이다. 다만 `ELogCategory`는
프로젝트마다 자기 것을 같은 이름으로 노출하는 반면 `PacketId`는 저장소 전체에 하나뿐이라
이름이 겹칠 여지도 없다.

**`using enum`은 쓰지 않는다.** `using enum Protocol::PacketId;`까지 가면 `T2WToolHello`처럼
한정자 없이 쓸 수 있지만, 열거자 30여 개가 전역 이름이 되고 이 헤더를 include한 모든 TU가
그걸 떠안는다. `PacketId::` 한 겹은 "이 값이 패킷 id"라는 표시로 남겨둔다. 타입 안전성은
어느 쪽이든 그대로다(`enum class`이므로 정수로의 암묵 변환은 계속 막힌다).

## 캐스팅을 없애는 오버로드

`enum class`라 그대로는 `uint16_t` 자리에 못 넣는데, 호출부마다 `static_cast`를 쓰면 잡음이
크다(치환 전 38곳). 그래서 Core에 enum을 받는 오버로드를 하나씩 얹어뒀다 --
`Session::SendPacket`과 `Packet::BuildFrame`이고, 둘 다 `std::is_enum_v` 제약만 걸어서
**Core는 여전히 어떤 enum인지 모른다**(콘텐츠를 모르는 라이브러리라는 원칙 유지).

콘텐츠를 아는 쪽(`ZoneWorld::SendToPlayer`/`BroadcastToZone`, `BroadcastDispatcher::Broadcast`,
`ToolProcessor::InjectClientPacket`, `WorldServerApp::BroadcastToAll`,
`ZoneWorld::HandleClientPacket`)은 아예 매개변수 타입을 `PacketId`로 바꿨다.
남은 `static_cast`는 `ClientEnvelopeHeader::innerPacketId`(POD 필드가 `uint16_t`)에 넣는
두 곳과 로그 출력 한 곳뿐이다.

## 적용 상태

**적용 완료.** 링크별 4개 enum(`Zone::PacketId`, `World::GatewayLinkPacketId`,
`World::ZoneLinkPacketId`, `World::ToolLinkPacketId`)은 삭제됐고 전부 `Protocol::PacketId`로
합쳐졌다. `World::EToolResultCode`만 `Server/WorldServer/Src/Packet/ToolResultCode.h`로
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
- 새 `Shared/Protocol/` 프로젝트(또는 헤더 전용 폴더)를 6개 vcxproj가 참조하도록 추가한다.
