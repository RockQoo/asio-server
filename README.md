# asio-server

C++20 / standalone ASIO(Boost 비의존) 기반 **분산 게임 서버** 포트폴리오.

MMORPG 서버 구조를 4개 프로세스로 단순화해, **"게임 상태에 락을 쓰지 않고 어떻게
멀티스레드로 처리할 것인가"** 한 가지 질문에 집중해 만들었습니다.

| | |
|---|---|
| **핵심 주장** | 락을 없애는 게 목적이 아니라, **락이 필요한 지점을 세어서 줄이는 것**이 목적 — 상황별로 4가지 전략(스레드 어피니티 / 단일 스레드 / 스냅샷 전달 / 명시적 락)을 나눠 적용했고, 서버 3종에 남은 mutex는 **전부 8곳**으로 셀 수 있습니다 |
| **규모** | C++ 소스 113개 파일 / 프로젝트 6개(정적 라이브러리 1 + 실행 파일 5) |
| **환경** | MSVC v143, x64 전용, `/std:c++20`, 외부 의존성은 벤더링한 standalone ASIO 하나뿐 |
| **검증** | 자동화 스위트 없음. 프로토콜 REPL 클라이언트와 **최대 1만 세션 부하 도구를 직접 만들어** 정확성·처리량·지연을 측정 |

---

## 1. 아키텍처 한눈에 보기

```
Client ──► GatewayServer ──► WorldServer ──► ZoneServer #0, #1, ...
           (순수 릴레이)      (라우팅 두뇌)     (존 권위 상태)
           게임 로직 0        단일 스레드       내부 5-풀 분리
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
(`zoneId % BASIC풀크기`). 그 존의 상태(`ZoneWorld::players_`)는 그 스레드에서만 접근되므로
mutex가 아예 없습니다.

이 불변식은 **인구가 여러 존에 분산돼 있을 때만** 성립합니다. 한 존에 몰아넣으면 BASIC
스레드 1개로 직렬화되고, 실제로 부하 테스트에서 그 현상을 재현했습니다([5절](#5-성능-지표)).

---

## 2. 서버별 핵심 기술과 설계 의도

### GatewayServer — 접속 종단, 게임 로직 0

| | |
|---|---|
| **역할** | 클라이언트 TCP를 accept해 `clientSessionId`만 붙여 World로 넘기는 무상태 릴레이 |
| **핵심 기술** | ① `ClientEnvelopeHeader{clientSessionId, innerPacketId}`를 원본 패킷 앞에 붙여 **소켓 1개로 N명 다중화** ② 로직 스레드가 아예 없음 — 모든 처리가 Session strand(I/O 스레드) 위에서 끝남 ③ `Listener`(inbound)와 `Connector`(outbound)를 같은 `IPacketHandler`로 대칭 처리 |
| **왜 이렇게** | 인증·게임 로직을 여기에 두지 않아야 접속 종단을 수평 확장할 수 있습니다. 실제로 클래스 4개, 200줄 미만으로 유지됩니다 |
| **대표 코드** | [ClientLinkHandler.cpp:49](GatewayServer/Src/Handler/ClientLinkHandler.cpp#L49) (envelope 씌우기) |

### WorldServer — 라우팅 두뇌, 단일 스레드

| | |
|---|---|
| **역할** | 클라이언트↔존 매핑, 존 등록, 존 핸드오프, DB 태스크 분배 |
| **핵심 기술** | ① **단일 처리 스레드(`WorldWorker`)로 락 제거** — 샤딩 키가 없는 전역 라우팅 테이블이라 "풀 대신 스레드 1개"를 선택, 두 레지스트리에 mutex가 0개 ② **투명 존 핸드오프** — 라우팅 테이블만 교체하므로 클라이언트도 Gateway도 이동을 모름 ③ **owner-hash DB 워커 풀** — `ownerId % N`으로 "같은 플레이어 = 같은 스레드 = 순서 보장" |
| **왜 이렇게** | 라우팅 상태와 영속화는 성격이 다릅니다. 전자는 전역이라 직렬화(스레드 1개)가 맞고, 후자는 owner 단위로 독립이라 샤딩이 맞습니다. I/O 스레드는 페이로드만 복사해 `PostTask`할 뿐 상태를 만지지 않습니다 |
| **대표 코드** | [WorldWorker.h:9](WorldServer/Src/Worker/WorldWorker.h#L9) (선택 근거 주석), [ZoneLinkHandler.cpp:114](WorldServer/Src/Handler/ZoneLinkHandler.cpp#L114) (핸드오프) |

### ZoneServer — 존 권위 상태, 5-풀 분리

| | |
|---|---|
| **역할** | 존 하나(또는 여럿)의 게임 상태를 소유하고 Mail 시스템을 운영 |
| **핵심 기술** | ① **zoneId sticky 라우팅으로 락-프리 게임 상태** — `players_`가 순수 `unordered_map` ② **스냅샷 핸드오프** — BASIC이 대상 목록을 복사해 BROADCAST로 넘기고, BROADCAST는 공유 컨테이너를 절대 읽지 않음 ③ **`Synchronized` + Unit-of-Work** — 만료 스윕과 BASIC이 겹치는 유일한 지점만 `shared_mutex`로 보호 |
| **왜 이렇게** | 스레드를 나누면 상태 공유 지점이 생깁니다. 그래서 각 풀이 "무엇을 소유하고 무엇을 넘겨받는지"를 코드 주석으로 못박았습니다. [ZoneWorld.h:32](ZoneServer/Src/Game/ZoneWorld.h#L32)는 **TICK 풀이 실제 상태를 만지는 순간 이 설계가 깨진다**는 미래의 파손 조건까지 명시합니다 |
| **대표 코드** | [ZoneWorkerManager.h:67](ZoneServer/Src/Worker/ZoneWorkerManager.h#L67) (sticky 라우팅), [ZoneWorld.cpp:233](ZoneServer/Src/Game/ZoneWorld.cpp#L233) (스냅샷 브로드캐스트) |

### Core — 게임 로직을 전혀 모르는 재사용 라이브러리

| 타입 | 어떤 문제를 푸는가 |
|---|---|
| `Network::Session` | strand로 소켓·송신 큐를 보호 → **어느 스레드에서든 `SendPacket()` 호출 가능**. `closed_.exchange()`로 `OnClosed` 정확히 1회 보장 |
| `Network::IoContextPool` | io_context 1개의 완료 핸들러 직렬화 병목 회피. io_context N개 + 전용 스레드 N개 |
| `Network::Connector` | outbound connect + 재시도 → **기동 순서 의존 제거**(Zone이 World보다 먼저 떠도 됨) |
| `Packet::PacketBuffer` | 경계 없는 TCP 스트림 → 프레임 재조립. 크기 초과 시 예외로 악성 스트림 차단 |
| `Packet::PacketDispatcher<TId,TContext>` | switch 증식 방지. Zone은 `TContext`를 `PlayerState*`로 써서 "플레이어 조회 후 콜백"까지 흡수 |
| `Thread::AffinityWorkerPool<T>` | `key % N` 고정 매핑 → **키별 상태의 스레드 전용성 보장** = 락 제거의 근거 |
| `Threading::Synchronized<T>` | `ref->`(읽기) / `ref.Write()->`(쓰기) 프록시. thread_local 재진입 가드로 shared_mutex 재귀 데드락 차단, **shared→unique 승급은 `abort()`로 즉시 노출** |
| `Task::UnitOfWork` | 변경 N건 → 패킷 1개 배칭. `kind + payloadLen + payload` 포맷이라 **모르는 kind를 만나도 스킵 가능**(Core 재컴파일 없이 콘텐츠 확장) |
| `Timer::RepeatingTimer` | 드리프트 보정. 5주기 이상 밀리면 catch-up 폭주 대신 리베이스 |

---

## 3. 락 전략 — 이 프로젝트가 실제로 증명하려는 것

상태마다 다른 전략을 씁니다. **"락이 없다"가 아니라 "락이 여기 8곳에만 있다"**가 요점입니다.

| 전략 | 적용 대상 | 락 |
|---|---|---|
| 스레드 어피니티(`key % N`) | `ZoneWorld::players_` (BASIC 풀) | **없음** |
| 단일 처리 스레드 | `ClientRegistry` / `ZoneLinkRegistry` (WorldWorker) | **없음** |
| 스냅샷 전달 | BROADCAST 풀이 받는 대상 목록 | **없음** |
| strand 직렬화 | `Session`의 소켓·송신 큐 | strand 1곳 |
| 명시적 mutex | 위 전략이 성립하지 않는 교차 지점만 | **8곳** |

8곳의 내역: 로거(`Logger`), 워커 큐(`WorkerThread`), Gateway 세션 레지스트리
(`SessionManager`), Gateway·Zone의 World 링크 홀더 2곳, Zone LB 풀의 로컬 존 맵
(`WorldLinkHandler`), 메일 레지스트리(`MailRegistry`), `Synchronized`가 감싸는
`MailModel` 1개.

### Mail이 그 예제입니다

`MailModel`(플레이어 우편함)은 평소 그 존의 BASIC 스레드에서만 바뀝니다. 그런데 **메일 만료만
별도 유지보수 타이머가 처리**하므로, 여기 한 곳에서만 불변식이 깨집니다. 그래서:

- 그 교차 지점만 `Threading::Synchronized`로 보호 (`ref->`=읽기 / `ref.Write()->`=쓰기)
- 상태 변경은 즉시 전송하지 않고 `Task::UnitOfWork`에 기록했다가 **스코프 종료 시 한 번에**
  World로 flush
- 클라이언트는 `MailAdd`/`MailDel`을 보내고, 서버가 실제 배정한 `mailId`를
  `MailAddAck`/`MailDelAck`으로 돌려받아 왕복을 확인

---

## 4. 존 핸드오프 — 클라이언트가 모르는 이동

존 경계(x=10)를 넘는 이동은 **WorldServer가 라우팅 테이블만 바꿔서** 처리합니다.

1. Zone A가 좌표를 보고 자기 담당이 아님을 판단 → 로컬 상태를 먼저 정리하고 World에
   `ZoneTransferRequest`
2. World가 x좌표로 대상 존을 찾아 `ClientRegistry::SetZone` + Zone B에 `EnterZoneRequest`
3. 이후 그 클라이언트의 패킷은 Zone B로 흐름

클라이언트도 Gateway도 이동이 일어났다는 사실 자체를 모릅니다. 전체 흐름은
[`docs/flowcharts/zone-handoff-and-mail.html`](docs/flowcharts/zone-handoff-and-mail.html)에
스레드별 색으로 정리돼 있습니다.

---

## 5. 성능 지표

`LoadTestClient`가 세션당 스레드 없이 io_context 풀 하나로 최대 1만 소켓을 비동기
멀티플렉싱하며, **정확성(mailId 왕복 일치) · 처리량 · 지연을 동시에** 측정합니다.

측정 방식:

- **지연**: `MailAdd` 송신 → `MailAddAck` 수신까지의 **클라이언트 체감 왕복 시간**. 서버 내부
  처리 시간만 재면 큐에서 밀린 시간이 빠져 실제보다 낙관적인 값이 나오므로, 소켓에 얹은
  순간부터 잽니다
- 원시 샘플을 다 들고 있지 않고 **551개 로그스케일 버킷 히스토그램**에 relaxed atomic으로
  기록합니다 — 수백만 샘플에도 상수 메모리·O(1)이라 측정이 실험 자체를 방해하지 않습니다
  ([LatencyHistogram.h](LoadTestClient/Src/Stats/LatencyHistogram.h))
- 추적 상한은 100초입니다. 처음엔 10초까지만 뒀는데 과부하 실험에서 P95·P99가 전부 최상위
  버킷에 몰려 "10초"로만 보고돼(실제 최대는 131초) **얼마나 나쁜지를 구분할 수 없었습니다.**
  상한에 걸린 값은 `≥100초`로 구분해 표기합니다

### 측정 결과

측정 환경: AMD Ryzen 7 9800X3D(8코어 16스레드), Windows 11, **Release 빌드**. 서버 3개와
부하 클라이언트가 **같은 머신**에서 CPU를 나눠 쓰므로, 지연 값에는 클라이언트 측 경합도
포함돼 있습니다. 1 사이클 = `MailAdd → Ack → MailDel → Ack` 왕복 2회.

| 시나리오 | 처리량 | P50 | P95 | P99 | 최대 | 정확성 | 브로드캐스트 수신율 |
|---|---|---|---|---|---|---|---|
| **1,000 세션 × 200 사이클** | **18,337 사이클/초** (200,000 사이클 / 10.9초) | 16 ms | 40 ms | 180 ms | 359 ms | 불일치 0 / 스톨 0 | **100%** (200,000/200,000) |
| **10,000 세션 × 100 사이클** (전원 존 1개) | 471 사이클/초 (300초에 목표의 14.2%) | 73 ms | 62 초 | ≥100 초 | 148 초 | **불일치 0** / 스톨 10,000 | 49.5% |

지연은 `MailAdd → MailAddAck` 왕복 기준이고, 두 시나리오 모두 2회 측정해 재현을
확인했습니다(1,000세션: 18,350 / 18,337 사이클/초).

### 1만 세션에서 무슨 일이 있었나

**정확성은 완전히 유지됐습니다** — 14만 건의 Mail 왕복에서 mailId 불일치 0건, 데드락 0건,
서버 WARN/ERROR 0건, 종료 시 정상 정리. 즉 **깨진 게 아니라 느려진 것**입니다. 대신 처리량이
1,000세션 대비 1/39로 무너졌고, 전 세션이 스톨 판정(20초 무응답)을 받았으며 브로드캐스트도
절반만 도달했습니다.

**원인은 테스트 설계 쪽이 컸습니다** — 이 테스트가 존 1개에 1만 명을 몰아넣어, 락-프리의
전제인 zoneId 분산이 성립하지 않았습니다. 존을 2개 띄워도 신규 접속이 전부 존 0으로
배정되기 때문입니다(아래 1번 항목).

진단한 병목 3가지와 수정 계획은 [`docs/load-test-fix-plan.md`](docs/load-test-fix-plan.md)에
정리돼 있습니다:

1. **인구가 존 0으로만 배정됨** — 신규 접속을 여러 존에 라운드로빈 분산 (근본 원인)
2. **WorldWorker가 라우팅과 브로드캐스트 릴레이를 같은 큐에서 처리** — 릴레이 큐 분리
3. **브로드캐스트가 대상 1명당 프레임 1개 전송** — 다중 대상 배칭

---

## 6. 빌드 & 실행

**Visual Studio**: `asio-server.slnx` 열기 → `Ctrl+F5`. `PlatformToolset=v143`, x64 전용.

**CLI**:
```bash
MSBuild.exe asio-server.slnx -p:Configuration=Release -p:Platform=x64 -m
```

**전체 기동**(실행 파일이 5개라 개별 F5보다 권장):
```bat
bat\server.bat    :: WorldServer → ZoneServer(0,1) → GatewayServer 순서로 새 창 3개
bat\client.bat    :: TestClient 실행 (127.0.0.1:9000)
```

모든 실행 파일이 `Core.vcxproj`를 프로젝트 참조로 물고 있어 `Core` → 나머지 순서로 자동
빌드됩니다. 산출물은 `bin/x64/{Debug,Release}/`.

---

## 7. 테스트

자동화 스위트는 없습니다. 바이너리 프로토콜이라 telnet 검증이 안 돼서 클라이언트 2개를
직접 만들었습니다.

**`TestClient`** — 프로토콜 왕복을 눈으로 확인하는 REPL:
```
echo hello-asio
move 12 4            # 존 경계(x=10)를 넘으면 서버가 조용히 다음 존으로 핸드오프
chat hi
mail add 제목 본문 5   # 5초 뒤 자동 만료, MailAddAck으로 실제 mailId 확인
mail del <id>
quit
```

**`LoadTestClient`** — 대규모 동시 접속·Mail 멱등성·브로드캐스트 부하:
```bat
LoadTestClient.exe <host> <port> <세션수> <세션당사이클수> [램프업/초] [스톨판정초] [최대초]
LoadTestClient.exe 127.0.0.1 9000 1000 200
```
종료 시 처리량·지연 백분위(P50/P95/P99/P99.9)·불일치·스톨·브로드캐스트 수신율을 요약합니다.

---

## 8. 프로젝트 구조

```
asio-server/
├─ Core/            정적 라이브러리 — 게임 로직을 전혀 모름
│   └─ Src/         Network(Session/Listener/Connector/IoContextPool), Packet(직렬화/디스패처),
│                   Thread(WorkerThread/AffinityWorkerPool), Timer, Threading(Synchronized),
│                   Task(UnitOfWork), Log
├─ GatewayServer/   클라이언트 accept + World 릴레이
├─ WorldServer/     라우팅(WorldWorker) + DB 워커 풀
├─ ZoneServer/      존 상태(NETWORK/LB/BASIC/TICK/BROADCAST) + Mail
├─ TestClient/      프로토콜 확인용 REPL
├─ LoadTestClient/  부하 테스트 도구 (최대 1만 세션)
├─ 3rd/asio/        standalone ASIO 벤더 코드 (수정하지 않음)
├─ docs/flowcharts/ 기능별 HTML 다이어그램 (오프라인 열람)
└─ bat/             서버·클라이언트 기동 스크립트
```

include 경로는 솔루션 루트 기준(`#include "Core/Src/Network/Session.h"`)이고, 클라이언트
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
| 코드 리뷰 파이프라인 | [`docs/code-review/README.md`](docs/code-review/README.md) | Codex 리뷰 → Claude 재검증 → HTML 보고서 |
| 플로우차트 규칙 | [`docs/flowcharts/README.md`](docs/flowcharts/README.md) | 새 기능 추가 시 다이어그램도 같이 갱신 |

진행 이력과 다음 할 일은 [`PROGRESS.md`](PROGRESS.md) 참고.

---

## 10. 로드맵과 알려진 한계

| 단계 | 내용 | 상태 |
|---|---|---|
| 0~3 | 프로젝트 셋업, echo 서버, 패킷 프레이밍, 존 어피니티 라우팅 | 완료 |
| 4 | Gateway/World/Zone 계층 분리, 투명 존 핸드오프, Mail(Synchronized/UnitOfWork) | 완료 |
| 5 | ZoneServer 5-풀 분리, WorldServer WorldWorker | 완료 |
| 6 | 부하 도구(LoadTestClient), 대규모 세션 검증, 지연 백분위 계측 | 완료 (병목 진단됨) |
| 7 | 부하 테스트 병목 수정 (`docs/load-test-fix-plan.md`) | 진행 중 |
| 8 | 실제 DB 연동 (PostgreSQL) | 예정 |
| 9 | Actor/Monster, AOI(시야 동기화) | 예정 |
| 10 | C# MonoGame 클라이언트 | 예정 |

**의도적으로 범위 밖에 둔 것** (물어보시면 설명드릴 수 있습니다):

- **영속화**: `Db::DbWorker`는 owner-hash 분배 **구조만** 있고 실제 쿼리는 로그만 남깁니다.
  프로세스 재시작 시 Mail은 소실됩니다
- **인증/신원**: `playerId`를 `clientSessionId`에서 그대로 파생합니다. 인증 계층 없음
- **수평 확장**: `Listener`의 세션 id가 프로세스별 1부터 시작하므로 Gateway를 2대 띄우면
  World의 라우팅 키가 충돌합니다. Gateway↔World 연결도 끊기면 재연결하지 않습니다
- **TICK 풀**: 현재 `Tick()`이 비어 있어 실질적으로 4-풀입니다. 풀 분리 구조를 먼저 만들고
  콘텐츠를 나중에 채우는 순서를 택했습니다
- **자동화 테스트**: 회귀 방지 스위트가 없습니다. 수동 REPL과 부하 도구로만 검증합니다

---

## 라이선스

학습 및 포트폴리오 용도. ASIO는 Boost Software License를 따릅니다.
