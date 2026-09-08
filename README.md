# asio-server

C++20 / standalone ASIO(Boost 비의존) 기반 **분산 게임 서버** 포트폴리오.

MMORPG 서버 구조를 4개 프로세스로 단순화해, **"게임 상태에 락을 쓰지 않고 어떻게
멀티스레드로 처리할 것인가"** 한 가지 질문에 집중해 만들었습니다.

| | |
|---|---|
| **핵심 주장** | 락을 없애는 게 목적이 아니라, **락이 필요한 지점을 세어서 줄이는 것**이 목적 — 상황별로 4가지 전략(스레드 어피니티 / 단일 스레드 / 스냅샷 전달 / 명시적 락)을 나눠 적용했고, 서버 3종에 남은 mutex **선언 지점은 전부 8곳**으로 셀 수 있습니다 |
| **규모** | C++ 소스 115개 파일 / 프로젝트 6개(정적 라이브러리 1 + 실행 파일 5) + C# 4개 프로젝트(운영툴 3 + MonoGame 시각 클라이언트 1) |
| **환경** | MSVC v143, x64 전용, `/std:c++20`, 외부 의존성은 벤더링한 standalone ASIO 하나뿐 (운영툴만 .NET 10 / SQL Server 별도) |
| **검증** | 서버는 자동화 스위트 없음 — 프로토콜 REPL 클라이언트와 **1만 세션까지 실측한 부하 도구를 직접 만들어** 정확성·처리량·지연을 측정. 운영툴은 xUnit 77개 |

---

## 1. 아키텍처 한눈에 보기

```
Client ──► GatewayServer ──► WorldServer ──► ZoneServer #0, #1, ...
           (순수 릴레이)      (라우팅 두뇌)     (존 권위 상태)
           게임 로직 0        단일 스레드       내부 5-풀 분리
                                  ▲
                             GmTool (검증용 운영툴)
                             전용 포트 9300
```

ZoneServer 내부는 역할별로 스레드 풀이 나뉘고, 패킷은 항상 한 방향으로만 흐릅니다.

```
NETWORK ──바이트만 복사──► LB ──PostToBasic(zoneId)──► BASIC ──스냅샷──► BROADCAST
(소켓 I/O)               (파싱,        (zoneId sticky,          (팬아웃 전송)
                        zoneId 판단)   게임 로직·락 없음)
                                          ▲
                                       TICK (존별 주기 처리)
```

**핵심 불변식**: 같은 `zoneId`의 패킷은 항상 같은 BASIC 스레드로만 라우팅된다
(`zoneId % BASIC풀크기`). 그 존의 상태(`ZoneInstance::players_`)는 그 스레드에서만 접근되므로
mutex가 아예 없습니다.

이 불변식은 **인구가 여러 존에 분산돼 있을 때만** 성립합니다. 한 존에 몰아넣으면 BASIC
스레드 1개로 직렬화되고, 실제로 부하 테스트에서 그 현상을 재현했습니다([5절](#5-성능-지표)).

---

## 2. 서버별 핵심 기술과 설계 의도

### GatewayServer — 접속 종단, 게임 로직 0

| | |
|---|---|
| **역할** | 클라이언트 TCP를 accept해 `clientSessionId`만 붙여 World로 넘기는 무상태 릴레이 |
| **핵심 기술** | ① `ClientEnvelopeHeader{clientSessionId, innerPacketId}`를 원본 패킷 앞에 붙여 **소켓 1개로 N명 다중화** ② 로직 스레드가 아예 없음 — 모든 처리가 Session strand(I/O 스레드) 위에서 끝남 ③ `Listener`(inbound)와 `Connector`(outbound)를 같은 `IPacketHandler` 인터페이스로 대칭 처리 |
| **왜 이렇게** | 인증·게임 로직을 여기에 두지 않아야 접속 종단을 수평 확장할 수 있습니다. 실제로 클래스 4개, 450줄 남짓으로 유지됩니다 |
| **대표 코드** | [ClientLinkHandler.cpp:49](Server/GatewayServer/Src/Handler/ClientLinkHandler.cpp#L49) (envelope 씌우기) |

### WorldServer — 라우팅 두뇌, 단일 스레드

| | |
|---|---|
| **역할** | 클라이언트↔존 매핑, 존 등록, 존 핸드오프, DB 태스크 분배, 운영툴 명령 수신 |
| **핵심 기술** | ① **단일 처리 스레드(`WorldWorker`)로 락 제거** — 샤딩 키가 없는 전역 라우팅 테이블이라 "풀 대신 스레드 1개"를 선택, 두 레지스트리에 mutex가 0개 ② **재접속 없는 존 핸드오프** — 라우팅 테이블만 교체하므로 Gateway는 이동 자체를 모르고, 클라이언트는 재접속·재인증 없이 같은 연결을 유지 ③ **owner-hash DB 워커 풀** — `ownerId % N`으로 "같은 플레이어 = 같은 스레드 = 순서 보장" |
| **왜 이렇게** | 라우팅 상태와 영속화는 성격이 다릅니다. 전자는 전역이라 직렬화(스레드 1개)가 맞고, 후자는 owner 단위로 독립이라 샤딩이 맞습니다. I/O 스레드는 페이로드만 복사해 `PostTask`할 뿐 상태를 만지지 않습니다 |
| **대표 코드** | [WorldWorker.h:9](Server/WorldServer/Src/Worker/WorldWorker.h#L9) (선택 근거 주석), [ZoneLinkHandler.cpp:114](Server/WorldServer/Src/Handler/ZoneLinkHandler.cpp#L114) (핸드오프) |

운영툴 링크는 세 번째 accept 포트(9300)로 분리돼 있고, `ToolProcessor`가 위 두 링크 핸들러와
**같은 스레드 규약**(I/O 스레드는 바이트 복사만 → `WorldWorker::PostTask`)을 그대로 따릅니다.
운영 우편은 새 패킷을 만들지 않고 기존 클라이언트 패킷(`C2ZMailAdd`/`C2ZMailDel`)을 봉투에 싸서
존에 주입하므로, 존·우편·`UnitOfWork`·DB 경로가 평소 클라이언트 요청과 동일하게 흐릅니다 —
운영 전용 우회로를 만들면 "운영툴로 넣은 우편만 만료가 안 된다" 같은 사고가 나기 때문입니다.
자세한 흐름은 [운영툴 플로우차트](docs/flowcharts/gmtool-operations.html) 참고.

### ZoneServer — 존 권위 상태, 5-풀 분리

| | |
|---|---|
| **역할** | 존 하나(또는 여럿)의 게임 상태를 소유하고 Mail 시스템을 운영 |
| **핵심 기술** | ① **zoneId sticky 라우팅으로 락 없는 게임 상태** — `players_`가 순수 `unordered_map` ② **스냅샷 핸드오프** — BASIC이 대상 목록을 복사해 BROADCAST로 넘기고, BROADCAST는 공유 컨테이너를 절대 읽지 않음 ③ **`Synchronized` + Unit-of-Work** — 만료 스윕과 BASIC이 겹치는 유일한 지점만 `shared_mutex`로 보호 |
| **왜 이렇게** | 스레드를 나누면 상태 공유 지점이 생깁니다. 그래서 각 풀이 "무엇을 소유하고 무엇을 넘겨받는지"를 코드 주석으로 못박았습니다. [ZoneInstance.h:32](Server/ZoneServer/Src/Game/ZoneInstance.h#L32)는 **TICK 풀이 실제 상태를 만지는 순간 이 설계가 깨진다**는 미래의 파손 조건까지 명시합니다 |
| **대표 코드** | [ZoneWorkerManager.h:67](Server/ZoneServer/Src/Worker/ZoneWorkerManager.h#L67) (sticky 라우팅), [ZoneInstance.cpp:234](Server/ZoneServer/Src/Game/ZoneInstance.cpp#L234) (스냅샷 브로드캐스트) |

### Core — 게임 로직을 전혀 모르는 재사용 라이브러리

| 타입 | 어떤 문제를 푸는가 |
|---|---|
| `Network::Session` | strand로 소켓·송신 큐를 보호 → **어느 스레드에서든 `SendPacket()` 호출 가능**. `closed_.exchange()`로 `OnClosed` 최대 1회 보장(중복 호출 방지) |
| `Network::IoContextPool` | io_context 1개의 완료 핸들러 직렬화 병목 회피. io_context N개 + 전용 스레드 N개 |
| `Network::Connector` | outbound connect + 재시도 → **기동 순서 의존 제거**(Zone이 World보다 먼저 떠도 됨) |
| `Packet::PacketBuffer` | 경계 없는 TCP 스트림 → 프레임 재조립. 크기 초과 시 예외로 악성 스트림 차단 |
| `Packet::PacketDispatcher<TId,TContext>` | switch 증식 방지. Zone은 `TContext`를 `PlayerState*`로 써서 "플레이어 조회 후 콜백"까지 흡수 |
| `Thread::AffinityWorkerPool<T>` | `key % N` 고정 매핑 → **키별 상태의 스레드 전용성 보장** = 락 제거의 근거 |
| `Threading::Synchronized<T>` | `ref->`(읽기) / `ref.Write()->`(쓰기) 프록시. thread_local 재진입 가드로 shared_mutex 재귀 데드락 차단, **shared→unique 승급은 `abort()`로 즉시 노출** |
| `Task::UnitOfWork` | 변경 N건 → 패킷 1개 배칭. `kind + payloadLen + payload` 포맷이라 **모르는 kind를 만나도 길이만큼 건너뛸 수 있음**(kind별 분기는 아직 Mail 하나뿐) |
| `Timer::RepeatingTimer` | 드리프트 보정. 5주기 이상 밀리면 catch-up 폭주 대신 리베이스 |

---

## 3. 락 전략 — 이 프로젝트가 실제로 증명하려는 것

상태마다 다른 전략을 씁니다. **"락이 없다"가 아니라 "락 선언이 여기 8곳뿐"**이라는 게 요점입니다(런타임 인스턴스 수는 다르다 — `Synchronized`는 플레이어 1인당 1개, `WorkerThread`는 워커당 1개).

| 전략 | 적용 대상 | 락 |
|---|---|---|
| 스레드 어피니티(`key % N`) | `ZoneInstance::players_` (BASIC 풀) | **없음** |
| 단일 처리 스레드 | `ClientRegistry` / `ZoneLinkRegistry` (WorldWorker) | **없음** |
| 스냅샷 전달 | BROADCAST 풀이 받는 대상 목록 | **없음** |
| strand 직렬화 | `Session`의 소켓·송신 큐 | strand 1곳 |
| 명시적 mutex | 위 전략이 성립하지 않는 교차 지점만 | **8곳** |

8곳의 내역: 로거(`Logger`), 워커 큐(`WorkerThread`), Gateway 세션 레지스트리
(`SessionManager`), Gateway·Zone의 World 링크 홀더 2곳, Zone LB 풀의 로컬 존 맵
(`WorldLinkHandler`), 메일 레지스트리(`MailRegistry`), `Synchronized`가 감싸는
`MailModel` 1개.

> **"lock-free"가 아닙니다.** 이 프로젝트에 lock-free 자료구조는 하나도 없습니다 — 워커
> 태스크 큐부터가 `std::queue` + `mutex` + `condition_variable`이고, `atomic`은 전부 단순
> 플래그·카운터·라운드로빈 인덱스입니다. 여기서 락이 없는 이유는 **CAS로 경합을 이겨내서가
> 아니라 애초에 경합이 생기지 않게 상태를 한 스레드에 가뒀기 때문**입니다(thread
> confinement / shared-nothing). 진짜 lock-free는 여러 스레드가 같은 데이터를 동시에
> 만지면서도 진행을 보장하는 것이고, 그건 이 프로젝트가 푼 문제가 아닙니다.

### Mail이 그 예제입니다

`MailModel`(플레이어 우편함)은 평소 그 존의 BASIC 스레드에서만 바뀝니다. 그런데 **메일 만료만
별도 유지보수 타이머(io 스레드를 빌려 도는)가 처리**하므로, 우편함 **내용**은 여기 한 곳에서만 불변식이 깨집니다. 그래서:

- 그 교차 지점만 `Threading::Synchronized`로 보호 (`ref->`=읽기 / `ref.Write()->`=쓰기)
- 상태 변경은 즉시 전송하지 않고 `Task::UnitOfWork`에 기록했다가 **Commit() 시점에 한 번에**
  내보냅니다. 실패하면 기록해둔 태스크를 역순으로 되짚어 **메모리를 롤백**하고 DB로는 아무것도
  나가지 않습니다
- 성공한 변경은 같은 태스크 목록이 World(DB)와 클라이언트 양쪽으로 갑니다 -- 클라이언트는
  `Z2CTaskResult`로 받은 태스크를 자기 메모리에 그대로 적용해 서버와 동기화합니다
  (콘텐츠마다 Ack 패킷을 새로 만들지 않는 이유)

---

## 4. 존 핸드오프 — 재접속 없이 존을 넘어간다

존 경계(x=10)를 넘는 이동은 **WorldServer가 라우팅 테이블만 바꿔서** 처리합니다.

1. Zone A가 좌표를 보고 자기 담당이 아님을 판단 → 로컬 상태를 먼저 정리하고 World에
   `Z2WZoneTransferRequest`
2. World가 x좌표로 대상 존을 찾아 `ClientRegistry::SetZone` + Zone B에 `W2ZEnterZoneRequest`
3. 이후 그 클라이언트의 패킷은 Zone B로 흐름

**Gateway는 이동이 일어났다는 사실 자체를 모릅니다** — 라우팅 테이블은 World만 소유하고
Gateway는 envelope 릴레이만 하기 때문입니다. **클라이언트는** 새 존에서
`Z2CEnterZoneNotify{playerId, zoneId}`를 받으므로 자기 존이 바뀐 것 자체는 알 수 있지만,
**재접속도 재인증도 새 주소로의 연결도 필요 없고** 핸드오프를 요청하거나 처리할 일도
없습니다 — 같은 TCP 연결을 그대로 쓰면서 통지 한 장만 더 받는 셈입니다. 전체 흐름은
[`docs/flowcharts/zone-handoff-and-mail.html`](docs/flowcharts/zone-handoff-and-mail.html)에
스레드별 색으로 정리돼 있습니다.

---

## 5. 성능 지표

`StressClient`가 세션당 스레드 없이 io_context 풀 하나로 1만 소켓까지 비동기
멀티플렉싱하며, **정확성(mailId 왕복 일치) · 처리량 · 지연을 동시에** 측정합니다.

측정 방식:

- **지연**: `C2ZMailAdd` 송신 → `Z2CTaskResult` 수신까지의 **클라이언트 체감 왕복 시간**. 서버 내부
  처리 시간만 재면 큐에서 밀린 시간이 빠져 실제보다 낙관적인 값이 나오므로, 소켓에 얹은
  순간부터 잽니다
- 원시 샘플을 다 들고 있지 않고 **551개 로그스케일 버킷 히스토그램**에 relaxed atomic으로
  기록합니다 — 수백만 샘플에도 상수 메모리·O(1)이라 측정이 실험 자체를 방해하지 않습니다
  ([LatencyHistogram.h](Tool/StressClient/Src/Stats/LatencyHistogram.h))
- 추적 상한은 100초입니다. 처음엔 10초까지만 뒀는데 과부하 실험에서 P95·P99가 전부 최상위
  버킷에 몰려 "10초"로만 보고돼(실제 최대는 131초) **얼마나 나쁜지를 구분할 수 없었습니다.**
  상한에 걸린 값은 `≥100초`로 구분해 표기합니다

### 측정 결과

측정 환경: AMD Ryzen 7 9800X3D(8코어 16스레드), Windows 11, **Release 빌드**. 서버 3개와
부하 클라이언트가 **같은 머신**에서 CPU를 나눠 쓰므로, 지연 값에는 클라이언트 측 경합도
포함돼 있습니다. 1 사이클 = `C2ZMailAdd → Ack → C2ZMailDel → Ack` 왕복 2회.

| 시나리오 | 처리량 | P50 | P95 | P99 | 최대 | 정확성 | 브로드캐스트 수신율 |
|---|---|---|---|---|---|---|---|
| **1,000 세션 × 200 사이클** | **18,337 사이클/초** (200,000 사이클 / 10.9초) | 16 ms | 40 ms | 180 ms | 359 ms | 불일치 0 / 스톨 0 | **100%** (200,000/200,000) |
| **10,000 세션 × 100 사이클** (전원 존 1개) | 471 사이클/초 (300초에 목표의 14.2%) | 73 ms | 62 초 | ≥100 초 | 148 초 | **불일치 0** / 스톨 10,000 | 49.5% |

지연은 `C2ZMailAdd → Z2CTaskResult`(당시 이름은 `Z2CMailAddAck`) 왕복 기준이고, 두 시나리오 모두 2회 측정해 재현을
확인했습니다(1,000세션: 18,350 / 18,337 사이클/초).

### 운영툴 대량 쿠폰 발급 (같은 머신, SQL Server 2022 컨테이너)

100만 장 발급, 청크 1만 건 × 100회. 세 번 모두 **로컬 메모리 중복 재시도 0건, DB 적재
100만 건 전부 고유**했습니다.

| 조건 | 소요 | 처리량 |
|---|---|---|
| Release, World 전송 포함 | 11.5 초 | **87,123 장/초** |
| Release, World 전송 제외 | 10.5 초 | **95,066 장/초** |
| Debug, World 전송 포함 | 12.3 초 | 81,453 장/초 |

**Debug와 Release 차이가 15%도 안 된다는 점이 이 표의 핵심**입니다 — 병목이 코드 생성
속도가 아니라 I/O(SqlBulkCopy 적재 + 스풀 `fsync`)라는 뜻입니다. World 전송(200건 × 5,000회
왕복)이 차지하는 몫도 약 1초, 전체의 8% 남짓입니다. 더 줄이려면 CSPRNG를 최적화할 게 아니라
청크 크기나 인서트 방식(`LOAD DATA INFILE` 등)을 손봐야 합니다.

마지막 측정은 **WorldServer를 일부러 죽인 채** 돌렸는데 발급이 정상 완료됐습니다 — 쿠폰의
권위 저장소는 운영툴 SQL Server이고 World 전송은 부가 경로라는 설계가 의도대로 동작한 것입니다.
CSV 다운로드(100만 행, 31MB)는 스트리밍으로 약 1초입니다.

### 1만 세션에서 무슨 일이 있었나

**정확성은 완전히 유지됐습니다** — 14만 사이클(= Mail 왕복 28만 회)에서 mailId 불일치 0건, 데드락 0건,
서버 WARN/ERROR 0건, 종료 시 정상 정리. 즉 **깨진 게 아니라 느려진 것**입니다. 대신 처리량이
1,000세션 대비 1/39로 무너졌고, 전 세션이 스톨 판정(20초 무응답)을 받았으며 브로드캐스트도
절반만 도달했습니다.

**원인은 테스트 설계 쪽이 컸습니다** — 이 테스트가 존 1개에 1만 명을 몰아넣어, "공유하지
않으니 락이 없다"는 전제인 zoneId 분산이 성립하지 않았습니다. 존을 2개 띄워도 신규 접속이 전부 존 0으로
배정되기 때문입니다(아래 1번 항목).

진단한 병목 3가지와 수정 계획은 [`docs/load-test-fix-plan.md`](docs/load-test-fix-plan.md)에
정리돼 있습니다:

1. **인구가 존 0으로만 배정됨** — 신규 접속을 여러 존에 라운드로빈 분산 (근본 원인)
2. **WorldWorker가 라우팅과 브로드캐스트 릴레이를 같은 큐에서 처리** — 릴레이 큐 분리
3. **브로드캐스트가 대상 1명당 프레임 1개 전송** — 다중 대상 배칭

---

## 6. 빌드 & 실행

**Visual Studio**: `asio-server.slnx` 열기 → `Ctrl+F5`. `PlatformToolset=v143`, x64 전용.
VS 2022 이상이면 열립니다 — VS 2026에서도 v143 빌드 도구만 설치돼 있으면 그대로 빌드되고,
"v145로 업그레이드" 안내가 뜨면 무시하세요. v143을 유지하는 이유는 VS 2022 사용자도
clone 후 바로 빌드할 수 있게 하기 위해서입니다.

**CLI**:
```bash
MSBuild.exe asio-server.slnx -p:Configuration=Release -p:Platform=x64 -m
```

**전체 기동**(실행 파일이 5개라 개별 F5보다 권장):
```bat
bat\start_server_all.bat          :: World → Zone(1,2) → Zone(3,4) → Gateway, 탭 4개 (Debug)
bat\start_server_all.bat Release  :: 5절 성능 수치를 재현하려면 이쪽 — Debug는 약 5배 느립니다
bat\start_protocol_client.bat         :: ProtocolClient 실행 (127.0.0.1:9000)
bat\start_visual_client.bat 2      :: VisualClient 창 2개 (MonoGame, 별도 .NET 솔루션)
bat\stop_server_all.bat           :: 서버 프로세스 종료 (-keep 을 주면 콘솔 창은 남김)
```

**프로세스가 4개**입니다 — `ZoneServer`를 두 개 띄워 존을 **2×2 격자**로 나눕니다:

```
 y:[10,20)   존 1   존 2      ← ZoneServer.exe 1,2   (프로세스 #1)
 y:[0,10)    존 3   존 4      ← ZoneServer.exe 3,4   (프로세스 #2)
             x:[0,10)  x:[10,20)
```

그래서 **가로 이동(1↔2, 3↔4)은 같은 프로세스 안의 BASIC 스레드 간 이동**이고, **세로
이동(1↔3, 2↔4)이 프로세스(TCP 링크)를 넘는 핸드오프**입니다. 두 경로는 화면에서 구분되지
않지만 World가 다른 링크로 라우팅하므로 실제 경로가 다릅니다. **zoneId는 1부터 시작합니다**
(0은 "존 없음/미배정" 예약값 — `ParseZoneList`가 0을 거부합니다). 배치 규칙은
`ParseZoneList`의 `kZoneSize`/`kZonesPerRow`/`kZoneRows` 세 상수뿐이라, 나중에 CSV에서 읽도록
바꿀 자리도 이 함수 하나입니다.

`start_server_all.bat`은 Windows Terminal이 있으면 **창 하나에 탭 4개**로 띄우고, 없으면
콘솔 창을 따로 띄웁니다(`cmd.exe` 자체에는 탭이 없습니다). 기동 후 네 프로세스의 PID를
출력하므로 Visual Studio의 **디버그 → 프로세스에 연결**(`Ctrl+Alt+P`)에서 Ctrl로 다중
선택하면 한 번에 붙을 수 있습니다. 중단점이 정확히 걸리려면 Debug 빌드를 쓰세요. 클라이언트
쪽에 중단점을 걸어야 하면 `start_protocol_client.bat -attach`로 별도 창에 띄웁니다.

모든 실행 파일이 `Core.vcxproj`를 프로젝트 참조로 물고 있어 `Core` → 나머지 순서로 자동
빌드됩니다. 산출물은 `bin/x64/{Debug,Release}/`.

**운영툴(GmTool)** — C# 프로젝트라 `asio-server.slnx`와 분리된 별도 솔루션입니다
(`Tool/GmTool/GmTool.slnx`). C++ 솔루션에 섞으면 MSBuild 전체 빌드가 NuGet 복원까지
끌고 들어가서 서버만 빌드하려는 흐름이 느려집니다.

```bat
bat\start_gmtool_mssql.bat   :: SQL Server 2022 컨테이너 기동 (Docker Desktop 필요, 최초 1회는 이미지 받느라 오래 걸림)
bat\start_gmtool.bat         :: http://127.0.0.1:5080  (초기 계정 admin / admin1234!)
```

스키마(`Tool/GmTool/Sql/schema.sql`)는 기동 시 자동 적용되고, 운영자 계정이 하나도 없을 때만
초기 계정을 만듭니다. WorldServer가 안 떠 있어도 웹은 뜨며 화면 우상단에 "World 연결 끊김"으로
표시됩니다. 공유 시크릿과 토큰 서명 키는 개발 기본값이 소스에 있으니 실제로 쓸 때는 환경
변수(`ASIO_SERVER_TOOL_SECRET`, `WorldLink__SharedSecret`, `Auth__TokenSigningKey`)로 덮어쓰세요.

---

## 7. 테스트

**서버(C++)** 는 자동화 스위트가 없습니다. 바이너리 프로토콜이라 telnet 검증이 안 돼서
클라이언트 3개를 직접 만들었습니다 — 프로토콜 확인용 REPL, 부하 테스트 도구, 그리고 눈으로
보는 창 클라이언트입니다.

**`ProtocolClient`** — 프로토콜 왕복을 눈으로 확인하는 REPL:
```
echo hello-asio
move 12 4            # 존 경계를 넘으면 재접속 없이 다음 존으로 핸드오프(Z2CEnterZoneNotify 한 장을 받는다)
chat hi
mail add 제목 본문 5   # 5초 뒤 자동 만료, Z2CTaskResult의 Added 태스크로 실제 mailId 확인
mail del <id>
quit
```

**`StressClient`** — 대규모 동시 접속·Mail 멱등성·브로드캐스트 부하:
```bat
StressClient.exe <host> <port> <세션수> <세션당사이클수> [램프업/초] [스톨판정초] [최대초]
StressClient.exe 127.0.0.1 9000 1000 200
```
종료 시 처리량·지연 백분위(P50/P95/P99/P99.9)·불일치·스톨·브로드캐스트 수신율을 요약합니다.

**`VisualClient`** — 존 이동·채팅·우편·쿠폰을 한 화면에서 눈으로 확인하는 MonoGame 클라이언트
(C# / .NET 10, `Tool/VisualClient/VisualClient.slnx` — GmTool과 같은 이유로 별도 솔루션):

```bat
bat\start_visual_client.bat 2     :: 창 2개. 브로드캐스트 확인에는 최소 2개가 필요합니다
```

**자동 순회(`--auto` / `--auto-rev`, 실행 중에는 `F2`)** — 월드 가운데를 중심으로 원을 돌며
네 존을 순서대로 지납니다. 한 바퀴(22초)에 **가로 경계와 세로 경계를 각각 두 번씩** 넘으므로
"스레드만 넘는 핸드오프"와 "프로세스를 넘는 핸드오프"를 한 번에 확인할 수 있고, 지켜보지
않아도 계속 반복됩니다. 상태줄에 `자동 순회 시계/반시계  존 전환 N회`로 방향과 누적 횟수가
표시됩니다.

`--auto-rev`는 반대 방향으로 돕니다. 창 두 개를 서로 반대로 돌리면 한 바퀴에 두 번 만나고
갈라져서, **같은 존에 있을 때 보이고 다른 존으로 나가면 사라지는 것**을 규칙적으로 볼 수
있습니다(같은 방향으로만 돌리면 위상 차이가 유지돼 계속 안 마주칠 수도 있습니다).

```bat
bat\start_visual_client.bat 2 Debug auto   :: 창 2개, 방향을 서로 반대로
dotnet run --project Tool\VisualClient\VisualClient -- --auto-rev
```

REPL로는 잘 안 보이는 것들을 화면이 대신 보여주는 게 목적입니다:

| 화면에 보이는 것 | 그게 왜 유용한가 |
|---|---|
| 존 격자 + 배정된 존의 배경색 | 경계를 넘는 순간 배경색이 옆 칸으로 옮겨가면 그게 핸드오프 성공입니다. 재접속도 재인증도 없습니다 |
| 목표 좌표(주황 점)와 서버 확정 좌표(플레이어 원)를 동시에 | 둘 사이 간격이 곧 왕복 지연이고, 핸드오프가 도는 동안 눈에 띄게 벌어집니다 |
| 창 2개를 나란히 | 한쪽 이동/채팅이 다른 쪽에 보이면 존 브로드캐스트가 실제로 팬아웃된 것입니다 |
| 우편함 + 남은 시간 카운트다운 | 만료 시각이 지나면 서버(`MailExpiryService`)가 지우고, `Z2CTaskResult`가 요청 없이 도착해 목록에서 사라집니다 |
| 쿠폰 등록 문구와 보상 우편의 시차 | 등록은 HTTP 응답, 보상은 소켓으로 옵니다 — 두 사건이 따로 보이는 게 정상입니다 |

방향(dir)은 서버에 없는 값이라 좌표 변화량으로 클라이언트가 만들어 그립니다. 존 경계 좌표도
서버가 알려주지 않아 `ParseZoneList` 규칙("각 존은 10칸 폭, zoneId는 1부터")을 클라이언트가
복제한 값입니다(`Src/Protocol/ZoneLayout.cs` — 한쪽만 고치면 어긋납니다). 자세한 경로는
[VisualClient 다이어그램](docs/flowcharts/visualclient-screen-and-coupon.html) 참고.

**클라이언트가 메우고 있는 프로토콜 공백 두 가지** — 만들면서 드러난 부분이라 적어둡니다:

- **입장 시 플레이어 스냅샷이 없습니다.** 존 브로드캐스트는 그 순간 존에 있는 사람에게만
  가는데 "입장 시 기존 플레이어 목록"을 주는 패킷이 없어서, 나중에 들어온 창은 이미 있던
  플레이어가 *움직일 때까지* 그 존재를 알 수 없습니다. 그래서 이동이 없어도 1초마다 좌표를
  다시 보냅니다(`PositionHeartbeatInterval`). 제대로 하려면 입장 응답에 스냅샷이 실려야 합니다
- **퇴장 통지가 없습니다.** 서버는 `W2ZLeaveZoneNotify`로 자기 상태에서만 지우고 남은 사람들에게
  알리지 않으므로, 위 하트비트가 끊긴 것으로 퇴장을 추정해 6초 뒤 목록에서 지웁니다

**그 밖의 한계 두 가지**: 한글 IME 입력이 안 됩니다(MonoGame이 OS 조합 문자를 받아오지 않아
입력칸은 ASCII만 받고, 한글 왕복 확인은 채팅 패널의 프리셋 버튼으로 합니다 — 수신 표시는
정상입니다). 그리고 존을 넘어가면 서버가 우편함을 버리므로 클라이언트도 목록을 비웁니다
(존 간 우편 이관은 미구현 — 10절 참고).

**운영툴(GmTool)** 은 xUnit 77개가 붙어 있습니다:

```bat
cd Tool\GmTool && dotnet test
```

무엇을 고정하려고 만들었는지가 더 중요합니다:

| 대상 | 왜 테스트가 필요한가 |
|---|---|
| 쿠폰 번호 체계 (문자셋 / 자릿수 / 체크 문자) | 발급된 쿠폰은 되돌릴 수 없습니다. 규칙이 한 번 바뀌면 이미 나간 수백만 장이 전부 검증에 실패합니다 |
| 체크 문자의 필터율 | "DB 앞단 1차 필터"라는 존재 이유를 수치로 못 박습니다 — 무작위 5만 건 중 통과율 0.5% 미만을 강제 |
| 난수 분포 | CSPRNG 문자 분포가 기대치의 ±15% 안에 드는지. 문자셋 크기를 바꿀 때 modulo bias가 조용히 생기는 걸 막습니다 |
| 스풀 파일 + 오프셋 | 유실 대비 장치인데 **실패해도 조용합니다**. 평소엔 증상이 없고 정작 필요한 순간에야 없다는 걸 압니다 |
| C++ ↔ C# 와이어 호환성 | 리틀엔디언, 문자열 길이가 바이트 수(문자 수 아님), 패킷 id 값 일치. 한쪽만 바뀌면 컴파일은 되고 런타임에 깨집니다 |

---

## 8. 프로젝트 구조

```
asio-server/
├─ Shared/          서버와 도구가 함께 쓰는 모듈
│   └─ Core/        정적 라이브러리 — 게임 로직을 전혀 모름
│       └─ Src/     Network(Session/Listener/Connector/IoContextPool), Packet(직렬화/디스패처),
│                   Thread(WorkerThread/AffinityWorkerPool), Timer, Threading(Synchronized),
│                   Task(UnitOfWork), Log
├─ Server/
│   ├─ GatewayServer/  클라이언트 accept + World 릴레이
│   ├─ WorldServer/    라우팅(WorldWorker) + DB 워커 풀
│   └─ ZoneServer/     존 상태(NETWORK/LB/BASIC/TICK/BROADCAST) + Mail
├─ Tool/
│   ├─ ProtocolClient/     프로토콜 확인용 REPL
│   ├─ StressClient/ 부하 테스트 도구 (1만 세션까지 실측)
│   ├─ VisualClient/   존/채팅/우편/쿠폰을 눈으로 보는 창 클라이언트 (C# / MonoGame) — 별도 솔루션
│   │   └─ VisualClient/Src/  Protocol(코덱·PacketId), Net(GameLink·CouponClient),
│   │                         Model(WorldModel), Text(GlyphAtlas), Ui(패널·위젯)
│   └─ GmTool/         검증용 운영툴 (C# / .NET 10 / SQL Server) — 별도 솔루션
│       ├─ GmTool.Core/  프로토콜 코덱 + 쿠폰 생성 엔진 (의존성 없음)
│       ├─ GmTool.Web/   Blazor Web App + Minimal API + SqlKata 리포지토리
│       ├─ GmTool.Tests/ xUnit 77개 (쿠폰 체계 / 대량 발급 / 와이어 호환성)
│       └─ Sql/schema.sql
├─ 3rd/asio/        standalone ASIO 벤더 코드 (수정하지 않음)
├─ docs/flowcharts/ 기능별 HTML 다이어그램 (오프라인 열람)
└─ bat/             서버·클라이언트 기동 스크립트
```

include 경로는 솔루션 루트 기준(`#include "Shared/Core/Src/Network/Session.h"`)이고, 클라이언트
프로토콜 헤더(POD/enum)는 링크 없이 서로 직접 include합니다.

---

## 9. 개발 워크플로

AI 코딩 도구(Claude Code)를 **규칙과 훅으로 통제해서** 사용했고, 그 설정을 저장소에
남겨뒀습니다. 문서화된 규약이 실제로 지켜지는지를 자동으로 강제하는 쪽에 관심이 있어서입니다.

| 항목 | 위치 | 무엇을 하는가 |
|---|---|---|
| 아키텍처·컨벤션 | [`CLAUDE.md`](CLAUDE.md) | 네임스페이스/명명/인코딩 규칙과 그 **근거** |
| C++ 세부 규약 | `.claude/rules/cpp-patterns.md` | include 순서, sink 매개변수의 const, asio 핸들러 예외 안전 등 |
| 벤더 코드 보호 | `.claude/hooks/block_vendor_edit.py` | `3rd/asio` 수정 시도를 **PreToolUse 훅으로 차단** |
| 빌드 스킬 | `.claude/skills/build/SKILL.md` | MSBuild 경로 탐색, Git Bash `/p:` 치환 우회 |
| 코드 리뷰 파이프라인 | [`docs/code-review/README.md`](docs/code-review/README.md) | Codex 리뷰 → Claude 재검증 → HTML 보고서(도구만 커밋, 실행 결과물은 로컬 산출물이라 제외) |
| 플로우차트 규칙 | [`docs/flowcharts/README.md`](docs/flowcharts/README.md) | 새 기능 추가 시 다이어그램도 같이 갱신 |

진행 이력과 다음 할 일은 [`PROGRESS.md`](PROGRESS.md) 참고.

---

## 10. 로드맵과 알려진 한계

| 단계 | 내용 | 상태 |
|---|---|---|
| 0~3 | 프로젝트 셋업, echo 서버, 패킷 프레이밍, 존 어피니티 라우팅 | 완료 |
| 4 | Gateway/World/Zone 계층 분리, 재접속 없는 존 핸드오프, Mail(Synchronized/UnitOfWork) | 완료 |
| 5 | ZoneServer 5-풀 분리, WorldServer WorldWorker | 완료 |
| 6 | 부하 도구(StressClient), 대규모 세션 검증, 지연 백분위 계측 | 완료 (병목 진단됨) |
| 7 | 부하 테스트 병목 수정 (`docs/load-test-fix-plan.md`) | 진행 중 |
| 8 | 운영툴(GmTool): 운영자 로그인, 우편 발송/삭제, 전체 공지, 대량 쿠폰 발급·등록 | 완료 |
| 9 | 실제 DB 연동 (게임 서버 쪽) | 예정 |
| 10 | Actor/Monster, AOI(시야 동기화) | 예정 |
| 11 | C# MonoGame 클라이언트(VisualClient): 존 격자/핸드오프, 채팅, 우편, 쿠폰 | 완료 |

**의도적으로 범위 밖에 둔 것** (물어보시면 설명드릴 수 있습니다):

- **영속화**: `Db::DbWorker`는 owner-hash 분배 **구조만** 있고 실제 쿼리는 로그만 남깁니다.
  프로세스 재시작 시 Mail은 소실됩니다. 운영툴이 보내는 쿠폰 청크도 같은 워커로 들어가지만
  아직 적재하지 않습니다 — 쿠폰의 권위 저장소는 운영툴 쪽 SQL Server입니다
- **운영툴의 우편 삭제 결과**: `Z2CTaskResult`가 클라이언트에게만 가므로, 운영툴은 "존까지
  전달됨"까지만 알 수 있고 실제로 그 `mailId`가 있었는지는 모릅니다
- **운영툴 인증**: 공유 시크릿 한 줄이 전부입니다. 실질적인 방어선은 툴 포트를
  루프백/내부 네트워크로 제한하는 것이고, mTLS나 IP 화이트리스트는 넣지 않았습니다
- **운영툴의 중복 로그인 차단**: 새로 로그인하면 이전 API 토큰이 전부 회수됩니다. 웹 UI와
  스크립트를 같은 계정으로 병행하면 서로를 로그아웃시키므로, 실제로는 용도별 계정 분리가 필요합니다
- **인증/신원**: `playerId`를 `clientSessionId`에서 그대로 파생합니다. 인증 계층 없음
- **핸드오프 시 우편함 초기화**: `MailModel`이 zone-local이라 존을 넘어가면 빈 우편함으로
  시작합니다. 원래는 위 DB 계층이 소유해야 할 상태를 존이 들고 있어서 생기는 한계입니다
- **존 입장 스냅샷 / 퇴장 통지 없음**: 브로드캐스트는 그 순간 존에 있는 사람에게만 가고,
  입장할 때 기존 플레이어 목록을 주는 패킷도 누가 나갔는지 알리는 패킷도 없습니다.
  `VisualClient`는 좌표 하트비트와 타임아웃으로 이걸 메우고 있는데, 원래는 입장 응답에
  스냅샷을 싣고 퇴장을 브로드캐스트해야 할 부분입니다 — AOI를 넣을 때 같이 정리할 자리입니다
- **수평 확장**: `Listener`의 세션 id가 프로세스별 1부터 시작하므로 Gateway를 2대 띄우면
  World의 라우팅 키가 충돌합니다. Gateway↔World 연결도 끊기면 재연결하지 않습니다
- **TICK 풀**: 현재 `Tick()`이 비어 있어 실질적으로 4-풀입니다. 풀 분리 구조를 먼저 만들고
  콘텐츠를 나중에 채우는 순서를 택했습니다
- **자동화 테스트**: 회귀 방지 스위트가 없습니다. 수동 REPL과 부하 도구로만 검증합니다

---

## 라이선스

학습 및 포트폴리오 용도. ASIO는 Boost Software License를 따릅니다.
