# 진행 상황 정리 (다음 세션 이어하기용)

마지막 업데이트: 2026-09-18

`README.md`(소개용)와 별개로, 다음 세션에서 빠르게 컨텍스트를 복구하기 위한 문서.
**여기에는 "다음에 뭘 하면 되는가"만 둔다.**

---

## 1. 지금까지 된 것 — 여기에 적지 않는다

예전에는 이 자리에 날짜별 작업 일지가 쌓여 있었는데, 시간순 로그라 **앞 항목이 지금은
거짓**이었다(커밋 위치·레인 구성·주인 규칙이 뒤에서 다시 바뀐다). 순서대로 다 읽어야만
진실에 도달하는 문서는 이어하기용으로 쓸모가 없다. 그래서 원본이 있는 곳만 가리킨다.

| 알고 싶은 것 | 볼 곳 |
|---|---|
| 무엇이 완료됐나 | `README.md` 로드맵 표 |
| 지금 구조가 어떤가 | `CLAUDE.md` (아키텍처·타입 표·불변 규칙) |
| 왜 그렇게 했나 | `docs/design/` |
| 패킷이 어떻게 흐르나 | `docs/sequences/` |
| 성능 수치와 기준선 | `docs/performance.html` |
| 의도적으로 안 한 것 | `README.md` "의도적으로 범위 밖에 둔 것" |

## 2. 코딩 컨벤션

`.claude/rules/cpp-patterns.md` 하나가 원본이다. **여기에 요약을 두지 않는다** — 이 문서는
갱신이 늦어서, 요약본이 원문과 갈리면 거짓이 된다(실제로 그랬다).

## 3. 다음에 하면 좋을 일

### 1. 성능 개선 — 지금 하는 일

**백로그·순서·상태·측정 이력은 [`docs/performance.html`](docs/performance.html) 5·6절 하나에만 둔다.**
여기에 목록을 복제하지 않는다 — 세 문서에 따로 적혀 있다가 순서가 갈렸던 것을 고친 자리다.

규칙 셋:
- **한 번에 하나만** 넣고, 고정 시나리오 A·B·C 를 다시 돌려 6절에 한 행을 쌓은 뒤 다음으로
- **측정 시나리오는 바꾸지 않는다** — 바꾸면 이전 회차와 비교가 깨진다
- 항목의 근거 코드 위치와 "왜 이 순서인가"는 저장소 밖 검토 기록에 있다(로컬 전용)

> 성능 백로그와 아래 1-B 목록은 **2026-09-18 Claude Fable 5.1 이 소스 191개 파일을 읽고
> 뽑은 후보**다. 코드만 보고 판단한 것이라 각 항목은 재서 확정한다.

### 1-B. 코드 정리 — 두 번 이상 반복되는 것

성능과 무관하게 따로 간다. 템플릿으로 뽑을 가치가 있는 것은 **R1 · R3 · R4 · R8** 넷이고
나머지는 상수·함수를 한 곳으로 옮기는 일이다. 하나씩 하고 빌드 에러·경고 0을 확인한다.

| # | 무엇 | 어디 | 어떻게 |
|---|---|---|---|
| R1 | 링크 핸들러 `OnPacket` 본문 — 수신 본문 조립 + `PushMsg` | `G2WHandler` `Z2WHandler` `T2WHandler` `W2ZHandler` 넷이 msgId·프로세서 id만 다르다 | 템플릿 헬퍼 하나 `PushRecvStream<EProducerType>(msgId, target, session, header, payload)` |
| R2 | World/Zone 이 같은 것을 따로 정의 — `RecvStreamBody` · `kGlobalQueryKey` · `NowUt()` · `kTimerLaneCount` | `WorldMsg.h`/`ZoneMsg.h` 등 양쪽 | 서버가 공유하는 것은 `Server/Common`, `NowUt()` 는 Core `Base` |
| R3 | `PushMsg` 오버로드 둘의 본문 중복 | `Server/Core/Src/Pipeline/ProducerHolder.h` | body 없는 쪽이 있는 쪽을 부르거나 내부 함수 하나로 |
| R4 | 태스크 스트림 파서 3벌 | `ProtocolClient/main.cpp` · `StressClient/Session.cpp` · C# `WorldModel.cs` | C++ 둘은 `Common::Z2CTaskResult::Parse` + `TaskRecord` 순회로. 스트림 walker 를 `Common` 에 하나 두면 `Z2WUnitOfWorkStream::Parse` 와도 공유 |
| R5 | `Z2WUnitOfWorkStream` 이 반쪽 — `Parse` 만 있고 보내는 쪽은 손으로 조립 | `Server/ZoneServer/Src/Task/ZoneUnitOfWork.cpp` `SendToWorld` | `Serialize()` 를 붙이고 `Common::SendPacket(session, packet)` 으로. `Z2CTaskResult` 와 같은 모양 |
| R6 | `SetupSignalHandling()` 3벌 동일 | `GatewayApp` `WorldApp` `ZoneApp` | `Network::Service` 에 `OnSignal(callback)` 하나 |
| R7 | `lane_backend` 파싱 블록 동일 | `WorldConfig.cpp` `ZoneConfig.cpp` | `ConfigFile::GetLaneBackend()` 또는 `Pipeline::LoadLaneBackend(file, fallback)` |
| R8 | `ConfigFile` getter 5개가 같은 골격 — 키 찾고 없으면 fallback, 틀리면 경고 | `Server/Core/Src/Base/ConfigFile.h` | `template <typename T> T Get(key, fallback)` + 타입별 파서 |
| R9 | `worldLink_.Get()` → 널체크 → return 8곳 | Zone/Gateway 전반 | 작다. `SessionHolder::IfConnected(func)` 정도. 안 해도 무방 |
| R10 | `Ids().ZoneTarget(zoneId_)` 를 메시지마다 해시 조회 | `Zone::Fanout` · `ZoneNetworkProcessor::PushToBasic`/`ReplyEcho` | 생성 때 `ZoneLaneTarget` 을 한 번 받아 멤버로. **성능에도 걸리는 항목**이라 재면 6절에 행을 남긴다 |

### 2. 검증이 비어 있는 자리 두 개

- **중복 로그인 차단.** `playerId -> clientSessionId` 색인이 없다. 검사와 삽입이 원자적이어야
  한다.
- **UnitOfWork 위조 검사.** 존이 올린 태스크를 World 캐시와 대조하지 않는다 — 캐시에 없는
  우편을 지웠다는 태스크가 그대로 통과한다.

### 3. 존 이동 보류 큐

핸드오프 중(존A가 지웠고 존B가 아직 안 받은 구간)에 온 패킷이 버려진다. 지금은 이동이
빨라서 드러나지 않을 뿐이다.

### 4. AOI / 몬스터 — 한 세트로 묶인 것

존 내부를 그리드로 나눠 "가까운 사람에게만" 보내기 + 몬스터(NPC)와 간단한 FSM.
**같이 해야 하는 것이 셋 붙어 있다.**

- **존 입장 스냅샷과 퇴장 통지.** 입장할 때 존 안의 사람 목록을 주는 패킷도, 누가 나갔는지
  알리는 패킷도 없다. 지금은 클라이언트가 좌표 하트비트(1초)와 타임아웃(6초)으로 메운다.
  AOI를 넣으면 "누가 내 시야에 들어왔나/나갔나"를 어차피 서버가 판단해야 한다.
- **존 배치를 CSV 데이터로.** 지금은 `ParseZoneList`가 균일 격자를 계산하므로 존 크기가
  전부 같아야 한다. `zoneId, xMin, xMax, yMin, yMax`로 읽으면 불규칙 배치가 된다.
  **고칠 자리는 `ParseZoneList` 하나**다 — 아래 계층은 이미 담당 사각형만 들고 다닌다.
- **월드에 구멍이 생기는 경우.** 배치가 불규칙해지면 어느 존도 담당하지 않는 좌표가 나온다.
  지금은 World가 원래 존 안쪽으로 좌표를 보정해 되돌려 넣는데(균일 격자에서는 월드 밖으로
  나가려는 경우뿐이라 충분했다), 그때는 "이동 실패"를 클라이언트에 알려주는 쪽이 맞다.

### 5. 프로토콜 코드젠 (yml -> Gen 파일) — 방향만 정해둠

패킷 id·에러 코드·존 배치 상수가 **C++ 헤더와 C# 두 곳에 손으로 복제**돼 있고, 지키는
수단이 주석뿐이다. 이미 어긋나 있다 — `PacketId.h`의 `C2ZMailBuy`가 `Client`에도
`GmTool.Core`에도 없다(아직 안 써서 안 드러났을 뿐이다).

**정한 방향**: 정의 파일(yml)을 원본으로 두고 **C++ 헤더까지 포함해 전부 생성물로** 만든다.
C++을 원본으로 두고 C#만 생성하면 언어 하나가 특별해지고 파서가 헤더 문법에 얽매인다.

```
Server/Common/Def/*.yml   (packet_id / error_code / task_kind / currency_type / zone_layout)
          |
          v   bat\gen_protocol.bat
Server/Common/Src/*.g.h  +  Client/Src/Protocol/*.g.cs  +  GmTool.Core/Protocol/*.g.cs
```

- **범위는 enum·상수까지**다. 패킷 **본문**(필드 레이아웃과 Read/Write)은 계속 손으로 쓴다 —
  직렬화까지 뽑으려면 `ZonePackets.h`/`ClientPackets.h`를 통째로 옮겨야 해서 비용이 몇 배다.
- 생성기는 **.NET 10 파일 기반 앱**(csproj 없이 `dotnet run Foo.cs`)으로 둔다. 새 솔루션도
  NuGet 복원도 안 생겨서 "C++ 솔루션에 NuGet을 끌어들이지 않는다"를 안 건드린다.
- **생성 결과물은 커밋한다.** clone한 사람이 bat을 돌리지 않아도 빌드되고, diff에서 패킷
  변경이 눈에 보인다.
- **같이 옮겨야 하는 것**: `PacketId.h`가 생성물이 되면 거기 있는 규약 주석이 yml 맨 위로
  가야 하고, `.claude/rules/packet-naming.md`와 `CLAUDE.md`가 그 파일을 가리키므로 같은
  커밋에서 고친다.

**순서**: 성능 개선(1번)보다 뒤, **4번(AOI/몬스터)보다 앞**이다. 거기서 패킷이 여러 개
늘어나므로 그 전에 하는 게 이사 비용이 싸다. 4번의 "존 배치를 CSV로"와도 자리가 겹친다
(`zone_layout.yml`이 그 입구다).

### 6. 자동화 테스트

C++ 쪽은 `ProtocolClient`/`StressClient` 수동·부하 확인뿐이라 회귀 방지 스위트가 없다.
운영툴은 xUnit 77개가 붙어 있으니(`cd Tool\GmTool && dotnet test`), 같은 방식으로 최소한
패킷 코덱/프레이밍 단위 테스트부터 붙이는 게 다음 후보다.

### 7. 쿠폰 청크 DB 적재

`ToolProcessor::HandleCouponChunkPush`가 청크를 캠페인별 DB 레인까지 순서대로 가져다 놓고
**로그만 남긴다.** 쿠폰의 권위 저장소가 아직 운영툴 쪽이라, 벌크 INSERT만 채우면 된다.

### 8. 운영툴 기능 추가 — 지금은 보류

`Tool/GmTool`은 서버 기능을 검증하기 위한 도구이고 기능 개발은 중단한 상태다(코드 정리와
문서 정합성만 손댄다). 역할 검사, 운영자 계정 관리 화면, 비밀 관리, 쿠폰 발급 재개가 후보로
남아 있지만 지금 범위 밖이다 — 근거는 `Tool/GmTool/README.md`의 "지금 범위 밖으로 둔 것".
