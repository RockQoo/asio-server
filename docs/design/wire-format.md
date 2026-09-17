# 와이어 포맷 — 가변 길이 패킷의 바디 구조

대상 코드: `Shared/Common/Src/Packet/ToolLinkPackets.h`,
`Shared/Common/Src/Packet/ZoneLinkPackets.h`

전부 리틀엔디언. 문자열은 `BinaryWriter::WriteString` = **길이(uint16) + UTF-8 바이트**이고,
아래에서 `String(x)`로 표기한다. 고정 길이 패킷은 `#pragma pack(1)` 구조체를 그대로 보낸다.

## UnitOfWorkStream (Z2W)

가변 길이(문자열 포함)라 고정 구조체 대신 `BinaryWriter`/`BinaryReader`로 직접 쓰고 읽는다.

```
playerId(uint32)
+ ownerId(uint64, = clientSessionId)
+ taskCount(uint16)
+ taskCount개의 { kind(uint16) + payloadLen(uint32) + payload }
```

**Core는 `kind`/`payload`의 실제 의미를 모른다.** `kind`는 `Shared/Common/Src/TaskKind.h`가
정의하는 "상위 8비트 카테고리 + 하위 8비트 세부 동작"이다.

Mail 태스크는 카테고리 Mail + Added/Removed이고 **둘 다 payload 레이아웃이 같다**:

```
mailId(uint32) + String(title) + String(body) + sendUt(int64) + endUt(int64)
```

삭제 태스크가 지워진 내용을 통째로 싣는 이유는 그게 곧 롤백에 필요한 정보이기 때문이다
([UnitOfWork](unit-of-work.md) 참고).

| 쪽 | 파일 |
|----|------|
| 쓰기 | `Server/ZoneServer/Src/Player/MailModel.cpp` (payload) + `Task/ZoneUnitOfWork.cpp` (접두 + 전송) |
| 읽기 | `Server/WorldServer/Src/Handler/Z2WHandler.cpp` |

## 운영툴 링크 (T2W / W2T)

`kToolLinkProtocolVersion`은 와이어 포맷을 바꿀 때마다 올린다. GmTool 쪽
`ToolLinkProtocol.ProtocolVersion`과 값이 같아야 `ToolHello`가 통과한다 — 서버만 고쳐놓고
툴을 안 고쳤을 때 **"패킷이 이상하게 파싱되는" 대신 접속 단계에서 바로 걸러내기 위함**이다.

### ToolHello (T2W)
```
requestId(uint32) + protocolVersion(uint32) + String(sharedSecret) + String(operatorName)
```

### NoticeRequest (T2W)
```
requestId(uint32) + String(message)
```
World가 Zone을 거치지 않고 접속 중 전체 클라이언트에게 `W2CNotice`를 직접 보낸다.

### MailSendRequest (T2W)
```
requestId(uint32) + targetKind(uint8) + clientSessionId(uint64)
  + String(title) + String(body) + durationSec(int64)

targetKind: 0 = 접속 중 전체(clientSessionId 무시) / 1 = clientSessionId 한 명
```

존으로 `ForwardToZone(innerPacketId = C2ZMailAdd)` 형태로 **"그 클라이언트가 직접 보낸
것처럼" 주입한다.** 그래서 Zone/Mail 쪽은 코드를 한 줄도 고치지 않아도 되고, 응답과
UnitOfWork(DB 반영) 경로도 평소와 완전히 동일하게 흐른다.

> 운영 전용 우회로를 만들면 "운영툴 우편만 만료가 안 되는" 사고가 난다.

### CouponChunkPush (T2W)
```
requestId(uint32) + String(campaignCode) + chunkSeq(uint32) + couponCount(uint32)
  + couponCount개의 String(couponCode)
```

**주의**: `Header::MaxBodySize()`가 8192바이트라, 코드 하나가 27바이트(길이 2 + 25자)인
것을 감안하면 한 패킷에 **250개 정도가 상한**이다. 그래서 운영툴은 "DB 벌크 인서트
청크"(수천~수만 건)와 "World 전송 청크"(200건)를 서로 다른 크기로 나눠 쓴다.

### ClientListReply (W2T)
```
requestId(uint32) + count(uint32) + count개의 W2TClientListEntry
```
`count`는 `MaxBodySize`에 맞춰 잘라서 보낸다(상한 `kMaxClientListEntries`).

## 패킷 id 대역

`Shared/Common/Src/PacketId.h` 하나로 통합돼 있고, 이름 앞 3글자가 **발신 → 수신** 방향이다
(`C2ZMailAdd` = Client → Zone). 방향마다 1000 단위로 대역을 잘라서 **값 하나만 보면 어느
소켓의 패킷인지 판정**된다. 규약 원문은 `.claude/rules/packet-naming.md`.
