# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## 프로젝트 개요

| 항목 | 내용 |
|------|------|
| 종류 | C++20 / standalone ASIO(Boost 비의존) 기반 분산 게임 서버 포트폴리오 |
| 목표 | MMORPG 구조를 단순화해 **존(Zone) 단위 스레드 어피니티**로 락 없이 게임 상태를 처리 |
| 빌드 시스템 | 클래식 Visual Studio 프로젝트 파일(`.vcxproj` + `.slnx`). CMake 아님 — 되돌리지 말 것 |
| 컴파일러 옵션 | MSVC, `PlatformToolset=v143`, `x64` 전용, `/std:c++20 /utf-8`, `ASIO_STANDALONE`/`ASIO_NO_DEPRECATED` |
| 실행 파일 5개 | `GatewayServer`/`WorldServer`/`ZoneServer`/`TestClient`/`LoadTestClient` (`Core`는 정적 라이브러리라 실행 파일 없음) |
| 기동 순서 | `bat/server.bat`(World→Zone→Gateway) 또는 개별 실행, 자세한 건 README "빌드 & 실행" |
| 테스트 도구 | `TestClient/Src/main.cpp`(수동 확인용 REPL), `LoadTestClient/Src/main.cpp`(비동기 부하 테스트, 최대 1만 세션) — 둘 다 자동화 스위트 아님 |
| 배경 문서 | `README.md`(개요), `PROGRESS.md`(구현 이력·다음 할 일), `docs/load-test-fix-plan.md`(진행 중인 부하 병목 수정 계획) |

---

## 작업 전 필수 확인

1. **세부 C++ 규칙**(include 순서/캐스팅/pch/asio 예외 안전 등): `.claude/rules/cpp-patterns.md`
2. **`.claude/` 아래 새 md 파일명**(kebab-case, `SKILL.md`류 고정명은 예외): `.claude/rules/md-patterns.md`
3. **CLI 빌드/MSBuild 에러 진단**: `.claude/skills/build/SKILL.md`
4. **기존 코드 답습**: `Core/Src/`가 인프라, `WorldServer/Src/`·`ZoneServer/Src/`·
   `GatewayServer/Src/`가 각 서버 로직 — 새 코드는 같은 프로젝트의 유사 패턴부터 확인.

---

## 필수 규칙

- **언어**: 응답/코드 주석 전부 한글. 주석은 "무엇을"이 아니라 "왜"(스레드 안전성/객체 수명
  트릭 위주)를 설명한다.
- **네임스페이스**: PascalCase, 폴더 구조와 대응하되 **`Core::` 접두사는 붙이지 않는다**
  (`Core/Src/Network/` → `namespace Network`, 이하 `Packet`/`Thread`/`Timer`/`Common`/`Log`
  동일 — 계속 감싸면 시그니처 전체가 `Core::`로 시작해 잡음이 컸다. 근거:
  `cpp-patterns.md`의 "왜 `Core::` 접두사가 없는가"). `ZoneServer`는 원래 `Zone` 하나뿐이라
  변화 없음. 소문자(`core::net`)로 되돌리지 말 것. 카테고리 enum(`ELogCategory`)은 Core가
  콘텐츠를 몰라야 해서 Core/ZoneServer가 각자 따로 갖는다(같은 문서 참고).
- **멤버 변수**: trailing underscore + camelCase(`socket_`). 단 `PacketHeader`/`MovePacket`/
  `PlayerState`/`ZoneServerConfig` 같은 **POD 구조체의 public 필드**는 밑줄 없이 쓴다.
- **인코딩**: UTF-8 **without BOM** + 두 vcxproj의 `/utf-8` 플래그로 한글 주석 파싱 — 플래그가
  빠지면 CP949로 오인식돼 파싱 에러가 나니 새 vcxproj/`ItemDefinitionGroup` 수정 시 확인할 것.
- **include 경로는 솔루션 루트 기준**: `#include "Core/Src/Network/Session.h"`.
- **모던 C++20 적극 사용**: `std::span`/`std::byte`, concept, `[[nodiscard]]`, 템플릿화,
  `std::move`. 세부 규칙(const/sink/emplace/Get const 등)은 `cpp-patterns.md`.
- **`3rd/asio` 수정 금지** — `.claude/settings.json` PreToolUse 훅으로도 자동 차단.
- **새 기능/패킷 흐름 추가 시 `docs/flowcharts/`에 HTML 다이어그램도 추가**(규칙:
  `docs/flowcharts/README.md`) — I/O ↔ 로직 스레드 경계를 스레드별 색으로 한눈에 보이게.

---

## 프로젝트 구조

```
C:\Work\asio-server\
├── asio-server.slnx                  솔루션 (더블클릭으로 VS에서 바로 열림)
├── Core/                             게임 로직을 전혀 모르는 재사용 가능 정적 라이브러리
│   └── Src/
│       ├── pch.h / pch.cpp           precompiled header (asio.hpp + 무거운 표준 헤더)
│       ├── Common/Types.h            SessionId 등 공용 별칭
│       ├── Packet/                   PacketHeader/Buffer/Framer, BinaryWriter/Reader,
│       │                             PacketDispatcher<TId,TContext>
│       ├── Network/                  IoContextPool, Listener(accept), Connector(outbound
│       │                             connect, Listener와 대칭), Session, SessionManager
│       ├── Thread/                   WorkerThread(SetThreadAffinityMask), AffinityWorkerPool<TWorker>
│       ├── Timer/                    RepeatingTimer
│       ├── Threading/Synchronized.h  shared_mutex 기반 `.Write()->`(쓰기)/`->`(읽기) 래퍼
│       └── Task/UnitOfWork.h          범용 Unit-of-Work(taskKind+직렬화 바이트만 다룸)
├── GatewayServer/                    클라이언트 accept + World로 순수 릴레이 (실행 파일)
├── WorldServer/                      WorldWorker(단일 처리 스레드) 라우팅 + DB 워커 풀 (실행 파일)
│   └── Src/World/                    ClientRegistry, ZoneLinkRegistry (WorldWorker 전용 접근)
├── ZoneServer/                       존 상태 + Mail 시스템 (실행 파일)
│   └── Src/
│       ├── Worker/                   TaskWorker(범용 실행기), ZoneWorkerManager
│       │                             (BASIC/TICK/BROADCAST 3개 풀 소유), BroadcastDispatcher
│       ├── Handler/WorldLinkHandler  World와의 연결의 IPacketHandler, 내부에 LB 풀
│       ├── Game/ZoneWorld            존별 권위 상태(BASIC 전용, 락-프리), PacketDispatcher로
│       │                             패킷별 핸들러 등록(Player 조회 → 핸들러 콜백)
│       └── Mail/                     MailModel/MailRegistry/MailExpiryService/MailUnitOfWork
├── TestClient/                       수동 테스트용 REPL (Core 참조, ZoneServer 헤더만 include)
├── LoadTestClient/                   비동기 멀티플렉싱 부하 테스트 도구(최대 1만 세션)
├── 3rd/asio/include/                 standalone ASIO 벤더 코드 (수정 금지)
├── docs/flowcharts/                  기능별 HTML 플로우차트 (index.html부터, 오프라인 열람용)
├── docs/load-test-fix-plan.md        진행 중인 부하 테스트 병목 수정 계획
├── bat/                              server.bat(전체 기동)/client.bat(TestClient 실행)
├── bin/x64/{Debug,Release}/          산출물 (gitignored)
├── obj/                              중간 산출물 (gitignored)
└── .claude/
    ├── settings.json                 PreToolUse/PostToolUse 훅 등록
    ├── rules/                        C++ / MD 코딩 규약
    ├── skills/build/SKILL.md         CLI 빌드 스킬
    └── hooks/                        벤더 코드 차단, 빌드 결과 알림 스크립트
```

---

## 아키텍처: 4계층 분산 + 존 내부 5-풀 분리

```
Client → GatewayServer(릴레이) → WorldServer(WorldWorker 단일 스레드, 라우팅) → ZoneServer
                                                                         NETWORK(I/O)
                                                                            ↓ 바이트만 복사
                                                                         LB(패킷 파싱, zoneId 판단)
                                                                            ↓ PostToBasic(zoneId, ...)
                                                                         BASIC(zoneId sticky, 게임 로직)
                                                                            ↓ (필요 시)
                                                                         BROADCAST(zoneId sticky, 팬아웃)
```

핵심 불변식: **같은 zoneId의 패킷은 항상 같은 BASIC 스레드로만 라우팅된다**(`zoneId %
BASIC풀크기`) — 그 존 상태(`ZoneWorld`)는 항상 그 스레드에서만 접근되므로 락이 필요 없다.
NETWORK/LB 스레드는 게임 상태를 직접 안 건드리고 바이트만 복사해 넘긴다(예외: `Echo`는
공유 상태가 없어 LB 스레드에서 즉시 응답). TICK/BROADCAST는 BASIC과 "다른" 스레드이므로,
BASIC이 소유한 컨테이너(`players_` 등)를 직접 건드리면 안 되고 스냅샷을 넘겨야 한다
(`BroadcastDispatcher` 참고). **한 존에 인구가 과도하게 몰리면 이 불변식이 곧 "BASIC
스레드 1개로 사실상 직렬화"를 뜻하게 된다** — 인구는 여러 존에 분산하는 게 전제다.

존 경계를 넘는 이동(핸드오프)은 WorldServer가 라우팅 테이블(`ClientRegistry`)만 바꿔서
처리한다 — 클라이언트/Gateway는 이동 사실을 전혀 모르는 완전 투명 설계.

`Core/`는 **게임 로직을 전혀 모르는** 정적 라이브러리, 각 서버 프로젝트가 자기 콘텐츠(존/
라우팅/릴레이)를 담당한다.

### 계층별 핵심 타입

| 프로젝트 | 타입 | 역할 |
| --- | --- | --- |
| `Core` | `IoContextPool` | io_context N개 + 전용 I/O 스레드 N개 |
| | `Listener` / `Connector` | accept / outbound connect. 둘 다 성공 시 `IPacketHandler::OnSessionOpened` 호출 |
| | `Session` | 소켓 1개, `strand_`로 보호 — `SendPacket()`은 어느 스레드에서든 호출 가능 |
| | `PacketDispatcher<TId,TContext>` | 패킷 타입 → 핸들러 템플릿 라우터 (게임 무관) |
| | `WorkerThread` / `AffinityWorkerPool<TWorker>` | 작업 큐 1개 소비 스레드 / `key % N` 고정 라우팅 풀 |
| | `Threading::Synchronized<T>` | `.Write()->`(unique_lock)/`->`(shared_lock) — 교차 스레드 접근 예외 지점만 보호 |
| | `Task::UnitOfWork` | 범용 Unit-of-Work — taskKind+직렬화 바이트만 다룸, 콘텐츠 의미는 모름 |
| `WorldServer` | `WorldWorker` | 단일 처리 스레드. I/O는 여기 `PostTask`로만 넘김 |
| | `ClientRegistry` / `ZoneLinkRegistry` | WorldWorker 전용 접근 전제라 락 없음 |
| | `Db::DbWorker` | owner-hash 기반 DB 워커 풀(현재 로그만, 실제 쿼리는 TODO) |
| `ZoneServer` | `ZoneWorld` | 존 하나의 권위 상태. `PacketDispatcher`로 패킷별 핸들러 등록(Player 조회 후 콜백) |
| | `TaskWorker` | 특정 존을 소유하지 않는 범용 실행기(BASIC/TICK/BROADCAST 풀이 이걸 사용) |
| | `ZoneWorkerManager` | BASIC/TICK/BROADCAST 3개 풀 + 존별 tick 타이머 소유 |
| | `WorldLinkHandler` | World와의 연결의 `IPacketHandler`. 내부에 LB 풀 소유 |
| | `Mail::MailModel` 등 | 평소 BASIC 전용, 메일 만료만 별도 유지보수 타이머가 처리 — 그 교차 지점만 `Synchronized`로 보호, 변경분은 `Task::UnitOfWork`에 모았다가 한 번에 World로 전송 |
| `GatewayServer` | `ClientLinkHandler`/`WorldLinkHandler` | 클라이언트↔World 양방향 릴레이만, 게임 로직 없음 |

---

## 빌드 / 실행

1. `asio-server.slnx`를 Visual Studio 2022로 연다 (`PlatformToolset=v143`, `x64`만 지원).
2. 실행 파일이 5개(`GatewayServer`/`WorldServer`/`ZoneServer`/`TestClient`/`LoadTestClient`)라
   개별 F5보다 **`bat/server.bat`**(World→Zone→Gateway 순서로 새 창 3개)로 한 번에 띄우고
   `bat/client.bat`으로 `TestClient`를 붙이는 걸 권장.
3. `F7`(빌드만) 또는 `F5`/`Ctrl+F5`(빌드 후 실행) — 특정 프로젝트만 빌드하려면 솔루션
   탐색기에서 우클릭 → 빌드.
4. 산출물: `bin/x64/Debug/{Core.lib, GatewayServer, WorldServer, ZoneServer, TestClient,
   LoadTestClient}.exe` (`obj/`, `bin/`은 `.gitignore`에 포함됨).

모든 실행 파일 프로젝트가 `Core.vcxproj`를 프로젝트 참조로 물고 있어 `Core` → 나머지 순서로
자동 빌드된다.

**CLI 빌드**(VS GUI 없이): `.claude/skills/build/SKILL.md` — MSBuild 경로 탐색, Git Bash
`/p:` 치환 우회법 정리.

**테스트**: 자동화 스위트 없음. `TestClient.exe`가 실제 프로토콜(Echo/Move/Chat/Mail/
EnterZoneNotify)을 왕복시키는 REPL 더미 클라이언트, `LoadTestClient.exe`가 최대 1만 세션
동시 접속 부하 테스트 도구 — 바이너리 프로토콜이라 telnet 검증 불가라 둘 다 직접 만들었다.
사용법은 `README.md` "수동 테스트" 절 참고.

---

## 코드 작성 규칙 인덱스

| 항목 | 위치 |
|------|------|
| C++ 세부 패턴(include 순서/캐스팅/pch/const/emplace_back/asio 예외 안전) | `.claude/rules/cpp-patterns.md` |
| MD 파일명 규약 | `.claude/rules/md-patterns.md` |
| CLI 빌드 절차 | `.claude/skills/build/SKILL.md` |
| 기능별 HTML 플로우차트 | `docs/flowcharts/` (규칙: `docs/flowcharts/README.md`) |

---

## 주의사항

- **개발 환경(Windows/NTFS)은 대소문자를 구분하지 않는다.** 새 폴더를 만들 때 기존 폴더와
  대소문자만 다른 이름(`core` vs `Core`처럼)을 쓰면 같은 폴더로 병합돼버린다. 실제로 이 문제로
  한 번 정리한 이력이 있음.
- `ZoneWorkerManager::Start()`는 `ZoneWorld&` 참조를 캡처하는 람다를 타이머 콜백으로 쓴다.
  `Stop()`은 반드시 **타이머를 먼저 취소한 뒤** 워커를 정지시키는 순서를 지켜야 안전하다
  (순서를 바꾸면 안 됨).
- `PlatformToolset`은 `v143`으로 맞춰져 있다(설치된 VS 2022 Professional 기준). 다른 머신에서
  vcxproj를 열었는데 `MSB8020` 툴셋 오류가 나면, 그 머신에 맞는 툴셋으로 다시 맞출 것.
- `3rd/asio` 수정 금지는 `.claude/settings.json`의 PreToolUse 훅으로도 강제된다(Edit/Write가
  해당 경로를 건드리면 자동 차단).

---

## 로드맵 상태

Gateway/World/Zone 4계층 분리, 존 핸드오프(투명), ZoneServer 5-풀 분리, WorldServer
WorldWorker, Mail(Synchronized/UnitOfWork) 시스템, 부하 테스트 도구(LoadTestClient)까지 완료.
부하 테스트로 발견된 처리량 병목 수정이 진행 중(`docs/load-test-fix-plan.md`). 남은 것:
실제 DB 연동(`Db::DbWorker`는 현재 로그만 남김), Actor/Monster/AOI, 클라이언트. 자세한 표는
`README.md` "로드맵", 다음 할 일은 `PROGRESS.md` 3절 참고.

---

## 버전 관리

- **Git** 사용.
- `bin/`, `obj/`는 `.gitignore`에 포함.
- **커밋 전 사용자 확인 필수** — 자동 커밋 금지.

---

## Plan 모드 규칙

계획 파일 작성 시 다음 항목을 반드시 포함:

- 사용자 원본 요청 (수정 없이 그대로 인용)
- 영향받는 범위 (파일/디렉토리, 어떤 vcxproj/필터를 건드리는지)
- 검색 가능한 핵심 키워드 3~5개
- 요구사항을 분해한 상세 목록
- 검증 방법 (빌드 성공 여부, `TestClient`로 확인 가능하면 어떤 명령으로 확인하는지)
