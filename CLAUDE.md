# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## 프로젝트 개요

| 항목 | 내용 |
|------|------|
| 종류 | C++23 / standalone ASIO(Boost 비의존) 기반 분산 게임 서버 포트폴리오 |
| 목표 | MMORPG 구조를 단순화해 **존(Zone) 단위 스레드 어피니티**로 락 없이 게임 상태를 처리 |
| 빌드 시스템 | 클래식 Visual Studio 프로젝트 파일(`.vcxproj` + `.slnx`). CMake 아님 — 되돌리지 말 것 |
| 컴파일러 옵션 | MSVC, `PlatformToolset=v145`, `x64` 전용, `/std:c++23 /utf-8`, `ASIO_STANDALONE`/`ASIO_NO_DEPRECATED` |
| 실행 파일 5개 | `GatewayServer`/`WorldServer`/`ZoneServer`/`ProtocolClient`/`StressClient` (`Core`는 정적 라이브러리라 실행 파일 없음) |
| 운영툴 | `Tool/GmTool` — C#/.NET 10 + SQL Server. **별도 솔루션**(`Tool/GmTool/GmTool.slnx`)이며 C++ 솔루션에 넣지 않는다. WorldServer의 전용 포트(9300)로만 붙는다. **서버 기능 검증용 도구이고 기능 개발은 현재 중단** — 지금은 코드 정리/문서 정합성만 손댄다(역할 검사·비밀 관리 미비는 지금 범위 밖이지 미완성이 아니다). 영구 동결은 아니므로 사용자가 요청하면 기능 추가는 정상 진행 |
| 로그 파일 | `logs/` 아래 snake_case(`world_server.log`, `zone_server_1_2.log`) -- 존 서버는 담당 zoneId를 파일명에 넣어 프로세스를 가른다 |
| 기동 순서 | `bat/start_server_all.bat`(World→Zone(1,2)→Zone(3,4)→Gateway, Windows Terminal 탭 4개) 또는 개별 실행, 자세한 건 README "빌드 & 실행" |
| 존 배치 | **2×2 격자**(`1 2` / `3 4`, 각 존 10×10, 월드 x:[0,20) y:[0,20)). 규칙은 `ParseZoneList`의 `kZoneSize`/`kZonesPerRow`/`kZoneRows`뿐이고 클라이언트 `ZoneLayout.cs`가 같은 값을 복제한다 — **한쪽만 고치면 화면 경계와 실제 핸드오프 지점이 어긋난다** |
| 존 번호 | **zoneId는 1부터.** 0은 "존 없음/미배정" 예약값이고 `ParseZoneList`가 거부한다 |
| 핸드오프 경로 | 가로(1↔2, 3↔4)는 같은 프로세스의 레인 간, **세로(1↔3, 2↔4)는 프로세스(TCP 링크)를 넘는다**(`docs/sequences/zone-handoff.html`) |
| 테스트 도구 | `Tool/ProtocolClient/Src/main.cpp`(수동 확인용 REPL), `Tool/StressClient/Src/main.cpp`(비동기 부하 테스트, 1만 세션까지 실측), `Client`(C#/MonoGame 시각 클라이언트 — **별도 솔루션**) — 셋 다 자동화 스위트 아님 |
| 시각 클라이언트 | `Client` — C#/MonoGame, 별도 솔루션(`Client/Client.slnx`). 존 격자/핸드오프·채팅·우편·쿠폰을 한 창에서 눈으로 확인. **서버 C++을 고치지 않는 것이 전제** — 기존 프로토콜과 이미 있는 쿠폰 API만 쓴다. 쿠폰 등록만 소켓이 아니라 GmTool.Web HTTP로 나가고 보상은 우편으로 소켓으로 돌아온다 |
| 설정 | 스레드 수·포트·주기는 `config/*.cfg`에서 읽고, 로딩은 각 서버의 `Src/App/<서버>Config.{h,cpp}`(`GatewayConfig`/`WorldConfig`/`ZoneConfig`)의 `LoadConfig`가 맡는다(`main`은 호출만). **기본값은 그 `Config` 구조체에만 적고** 읽는 쪽이 그 값을 fallback으로 넘긴다(두 군데 적으면 갈린다). **싱글턴으로 만들지 않는다** -- 근거는 `docs/design/config-file.md`. 담당 존 목록만 실행 인자 |
| 배경 문서 | `README.md`(개요), `PROGRESS.md`(구현 이력·다음 할 일). 진행 중인 부하 병목 수정 계획과 측정 수치는 `docs/local/`에 있다(gitignore 대상이라 경로를 여기 적지 않는다 — 필수 규칙 참고) |

---

## 작업 전 필수 확인

1. **세부 C++ 규칙**(include 순서/캐스팅/pch/상수 네이밍/스마트 포인터 별칭 `SPtr`/asio 예외 안전 등): `.claude/rules/cpp-patterns.md`
2. **`.claude/` 아래 새 md 파일명**(kebab-case, `SKILL.md`류 고정명은 예외): `.claude/rules/md-patterns.md`
3. **패킷 id 네이밍/번호 대역**(`C2ZMove` 형식, 방향별 1000 단위 대역): `.claude/rules/packet-naming.md`
4. **SQL 네이밍/SP 작성**(테이블은 복수형 snake_case, 조회는 기본 `WITH(NOLOCK)` 등): `.claude/rules/sql-patterns.md`
5. **CLI 빌드/MSBuild 에러 진단**: `.claude/skills/build/SKILL.md`
6. **기존 코드 답습**: `Shared/Core/Src/`가 인프라, `Server/WorldServer/Src/`·`Server/ZoneServer/Src/`·
   `Server/GatewayServer/Src/`가 각 서버 로직 — 새 코드는 같은 프로젝트의 유사 패턴부터 확인.

---

## 필수 규칙

### 불변 규칙 두 개 (다른 모든 판단보다 우선한다)

1. **`Shared/Core` 에 콘텐츠가 종속되면 안 된다.** Core 는 이 게임을 몰라야 다른 서버에
   그대로 가져다 쓸 수 있는 라이브러리다. 콘텐츠를 아는 타입·분기·상수는 `Shared/Common`
   (서버들이 공유하는 계약) 또는 각 서버 프로젝트에 둔다. 그래서 Core 의 기반 네임스페이스는
   `Base::` 이고 `Common::` 은 `Shared/Common` 이 쓴다 -- 이름만 봐도 층이 갈린다.

2. **모델 메모리에 적용한 변경은 반드시 UnitOfWork 에 Task 로 Add 되어야 한다.**
   태스크 목록이 곧 "실제로 적용된 변경"이라, 빠뜨리면 (1) 실패 시 되돌릴 수 없고
   (2) 클라이언트가 서버와 다른 상태로 남는다. 모델 함수가 상태를 바꾸고 태스크를 남기지
   않는 경로는 만들지 않는다.

- **언어**: 응답/코드 주석 전부 한글. **C# 운영툴(`Tool/GmTool`)도 동일** — XML 문서
  주석(`///`)과 일반 주석 모두 한글.
- **주석의 분량**: 주석은 "무엇을"이 아니라 "왜"를 설명하되, **소스에는 "고칠 때 모르면
  사고 나는 것"만 남긴다** — 스레드 규약, 호출 순서 의존, 객체 수명 트릭, 한두 줄짜리 경고.
  설계 배경("왜 이 대안을 버렸나", 측정 결과, 대안 검토)은 `docs/design/`에 문서로 옮기고
  소스에는 `// 설계 근거: docs/design/xxx.md` 한 줄만 남긴다(규칙: `docs/design/README.md`).
  **한 선언 위에 8줄이 넘는 주석 블록이 생기면 옮길 때가 된 것이다.**
- **C# 코드 규약**(`Tool/GmTool`): private 필드는 C++와 맞춰 trailing underscore
  (`factory_`), 그 외에는 표준 .NET 컨벤션(PascalCase 메서드/프로퍼티)을 따른다.
  `Nullable`/`ImplicitUsings` 활성 상태를 유지하고, 경고 0개로 빌드되게 한다.
- **참고 출처를 커밋되는 파일에 적지 않는다** — 외부 자료/도서/영상을 참고했더라도
  `README.md`/`PROGRESS.md`/소스 주석/`docs/`에는 출처를 남기지 않는다.
  기록이 필요하면 `docs/local/`(gitignore 대상)에만 둔다.
- **새 문서의 기본 위치는 `docs/local/`이다.** 파일명은 `<이름>.local.<확장자>`(kebab-case).
  공개 `docs/`에 두는 것은 "저장소를 읽는 사람에게 보여줄 것"으로 한정하고 그때만 따로 정한다
  — 확정 전 수치나 진행 중인 계획을 공개 문서에 두면 틀린 정보를 보여주게 된다.
  **거꾸로, 커밋되는 파일에서 `docs/local/` 아래의 개별 파일을 경로로 가리키지 않는다.**
  clone한 사람에게는 없는 파일이라 깨진 링크가 된다(그래서 아래 구조 트리에도 없다).
- **네임스페이스는 `Shared/Core`와 `Shared/Common`만 쓴다.** 실행 파일 프로젝트
  (Gateway/World/Zone)는 네임스페이스를 두지 않고, 이름이 겹치거나 전역에서 뜻이 모호해지면
  **서버 이름을 접두사로** 붙인다 -- `WorldApp`/`ZoneConfig`/`EZoneProcessorId`/`ZoneDef`/
  `ZoneUnitOfWork`. 서버끼리 헤더를 안 보므로 겹쳐도 무해하고, 겹쳐서 터지더라도 조용한
  버그가 아니라 컴파일 에러다. 링크 핸들러는 패킷 이름과 같은 방향 표기를 쓴다
  (`C2GHandler`/`W2GHandler`/`G2WHandler`/`Z2WHandler`/`W2ZHandler`) -- 다섯 개가 전부
  유일한 이름이라 접두사가 필요 없다(`C2G`는 패킷 대역이 아니라 **홉**이다).
- **`Shared/` 안의 네임스페이스**: PascalCase, 폴더 구조와 대응하되 **`Core::` 접두사는 붙이지 않는다**
  (`Shared/Core/Src/Network/` → `namespace Network`, 이하 `Packet`/`Thread`/`Timer`/`Base`/`Log`
  동일 — 계속 감싸면 시그니처 전체가 `Core::`로 시작해 잡음이 컸다. 근거:
  `cpp-patterns.md`의 "왜 `Core::` 접두사가 없는가"). `ZoneServer`는 `Zone`/`Mail`/`Log` 세 개뿐이라
  변화 없음. 소문자(`core::net`)로 되돌리지 말 것. 카테고리 enum(`ELogCategory`)은 **서버·도구가
  `Common::ELogCategory` 하나를 공유**한다 -- 같은 태그가 파일마다 다른 뜻이면 요청 하나를
  RUID로 쫓을 때 로그를 나란히 못 읽는다(예전에는 World의 `[Zone]`이 존 링크, Zone의
  `[Zone]`이 존 로직이었다). Core만 자기 것(`Log::ELogCategory` -- General/Network/Packet/
  Thread)을 따로 갖는다. Core가 Gateway/Zone/Db를 알면 불변 규칙 1이 깨지기 때문이다.
- **멤버 변수**: trailing underscore + camelCase(`socket_`). 단 `Header`/`Position`/
  `PlayerState`/`Config` 같은 **POD 구조체의 public 필드**는 밑줄 없이 쓴다.
- **인코딩**: UTF-8 **without BOM** + 6개 vcxproj 전부의 `/utf-8` 플래그로 한글 주석 파싱 — 플래그가
  빠지면 CP949로 오인식돼 파싱 에러가 나니 새 vcxproj/`ItemDefinitionGroup` 수정 시 확인할 것.
- **include 경로**: 같은 프로젝트는 `Src` 기준 짧게(`#include "App/WorldConfig.h"`), 다른
  프로젝트는 솔루션 루트 기준(`#include "Shared/Core/Src/Network/Session.h"`) — 접두사
  유무로 내 것/남의 것이 갈린다. **단 다른 프로젝트가 가져다 쓰는 헤더(`Shared/Core`와
  `Shared/Common` 전부)는 자기 헤더도 전체 경로**로 쓴다(남의 프로젝트 안에서 컴파일되므로).
  근거: `cpp-patterns.md`.
- **서버끼리 서로의 헤더를 include 하지 않는다.** 둘 이상이 알아야 하는 것은 계약이므로
  `Shared/Common` 으로 올린다. 예전에는 Zone/Gateway 가 `WorldServer/Src/Packet/*.h` 를
  12곳에서 직접 봤고, 그래서 Gateway 코드에 `World::RelayEnvelope` 라고 적혀 있었다.
- **모던 C++23 적극 사용**: `std::span`/`std::byte`, concept, `[[nodiscard]]`, 템플릿화,
  `std::move`. 세부 규칙(const/sink/emplace/Get const 등)은 `cpp-patterns.md`.
- **`3rd/asio` 수정 금지** — `.claude/settings.json` PreToolUse 훅으로도 자동 차단.
- **구조가 바뀌면 `docs/`를 같은 커밋에서 고친다.** 코드만 고치고 문서를 두면 다음 세션이
  틀린 문서를 읽고 잘못 판단한다(실제로 그래서 "5-풀/BASIC" 서술이 개편 뒤에도 남아 있었다).
  무엇이 바뀌었느냐에 따라 손댈 곳:

  | 바뀐 것 | 같이 고칠 문서 |
  |---|---|
  | 스레드/레인 구성, 프로세스 역할, 포트 | **`docs/index.html`**(전체 그림 + 스레드 표) + 해당 `docs/*.html` |
  | 새 패킷 흐름 | `docs/sequences/`에 다이어그램 추가(규칙: `docs/sequences/README.md`) + `docs/index.html`·`sequences/index.html` 카드 |
  | 기존 흐름의 경로 변경 | 그 흐름의 시퀀스 HTML(단계 번호·레인 수·grid-column까지) |
  | DB 스키마/SP/트랜잭션 | `docs/DB.html` |
  | 측정 수치 | `docs/local/`의 측정 문서 (**공개 `docs/`에 두지 않는다**) |
  | 설계 근거(왜 이렇게 했나) | `docs/design/`(규칙: `docs/design/README.md`) |
  | **파일/폴더를 옮기거나 이름을 바꿈** | 그 프로젝트의 **`.vcxproj.filters`**(빌드는 통과하므로 안 고쳐도 안 터지고, **솔루션 탐색기만 조용히 어긋난다**) + 문서가 그 경로를 백틱으로 가리키는 곳 |

  `docs/index.html`은 **문서 진입점**이라 여기가 틀리면 나머지가 맞아도 길을 잘못 든다 —
  구조 변경 커밋에서는 항상 먼저 확인할 것. `README.md`는 진입점 역할만 하므로 보통
  손댈 필요가 없다(빌드/실행 방법이 바뀐 경우만).

---

## 프로젝트 구조

```
C:\Work\asio-server\
├── Directory.Build.props             $(RepoRoot) 정의 -- 솔루션이 둘이라 $(SolutionDir)를 쓸 수 없다
├── config/                           서버 설정(gateway/world/zone.cfg). key = value, # 주석, 중첩은 점
│                                     PostBuildEvent가 실행 파일 옆 config/로 복사한다
│                                     값이 틀리면 기본값으로 넘어가지 않고 끝낸다(docs/design/config-file.md)
├── Server/Server.slnx                서버 솔루션 (Core + Gateway/World/Zone)
├── Tool/TestClient.slnx              테스트 클라이언트 솔루션 (Core + ProtocolClient/StressClient)
├── Shared/                           서버·툴이 공유하는 모듈 (Client가 Server를 의존하지 않게 하는 층)
│   ├── Core/                         게임 로직을 전혀 모르는 재사용 가능 정적 라이브러리
│   │   └── Src/
│   │       ├── pch.h / pch.cpp       precompiled header (asio.hpp + 무거운 표준 헤더)
│   │       ├── Base/                 Types.h(SessionId 등 별칭), BasicTypes.h, CoreErrorCode.h,
│   │       │                         CoreException.h, RUID, ConfigFile. **Common 이 아니라 Base 다**
│   │       │                         -- Common:: 은 Shared/Common 의 게임 계약이 쓴다
│   │       ├── Packet/               Header/Buffer/Framer, BinaryWriter/Reader, Args(가변 인자를
│   │       │                         넣은 순서대로 쓰고 읽는다 -- WriteArgs/ReadArgs),
│   │       │                         Dispatcher<TId,TContext>, OwnerIdPeek(I/O 스레드에서
│   │       │                         페이로드 앞의 정수 하나만 훔쳐봐 주인을 뽑는다)
│   │       ├── Network/              IoContextPool, Listener(accept), Connector(outbound
│   │       │                         connect, Listener와 대칭), Session, SessionManager,
│   │       │                         SessionHolder(링크 하나를 붙들어 두는 자리 -- Gateway와
│   │       │                         Zone의 World 링크가 본문이 같아 여기로 합쳤다)
│   │       ├── Processor/            Group(큐 그룹 = asio io_context + strand N개), Stats
│   │       ├── Log/                  Logger/Proxy/Entry/LogLevel + **Core 전용** LogCategory
│   │       │                         (General/Network/Packet/Thread -- 서버는 Common 것을 쓴다)
│   │       ├── Timer/                RepeatingTimer
│   │       ├── Thread/Mutexed.h      shared_mutex 기반 `.Write()->`(쓰기)/`->`(읽기) 래퍼
│   │       ├── Console/KeyBinder.h   콘솔 F키 → 콜백(토글). **전용 입력 스레드 하나**가 읽고,
│   │       │                         콜백은 서버 레인이 아니라 그 스레드에서 돈다
│   │       ├── Message/Router.h      프로세서 id로 메시지를 보낸다(PushMsg). 키가 (프로세서,
│   │       │                         msgId) 쌍이라 같은 msgId를 프로세서마다 다르게 처리한다.
│   │       │                         **싱글턴이 아니라 App이 소유**하고 전역엔 포인터만 둔다
│   │       └── Task/                 ITask(변경 기록 하나 -- **데이터만** 갖는다: Kind + New/Prev),
│   │                                 Paired<T>(New/Prev 짝), UnitOfWork(범용 기반 클래스 -- 목록과
│   │                                 순서만 안다). 커밋은 **파생 클래스 소멸자**에서, 직렬화와
│   │                                 역연산은 **파생이 taskKind 로 분기**한다(Core 는 뜻을 모른다)
│   └── Common/                   Gateway/World/Zone/도구가 공유하는 계약 + 데이터 타입 (StaticLibrary)
│       ├── PacketId.h            모든 패킷 id 하나로 통합(Common::PacketId).
│       │                         규약: .claude/rules/packet-naming.md
│       ├── ErrorCode.h           콘텐츠 처리 결과 코드(Common::EErrorCode, 콘텐츠별 100 단위)
│       ├── TaskKind.h            UnitOfWork taskKind 인코딩(상위 8비트 카테고리 + 하위 8비트 동작)
│       ├── ContentLimit.h        가변 길이 본문 상한(채팅/우편 길이·통 수) -- 없으면 프레임
│       │                         상한을 넘겨 그 링크에 붙은 전원의 연결이 끊긴다
│       ├── Enum.h                콘텐츠 enum 모음(재화 종류 등). 값 목록 하나에 파일 하나를 만들지 않는다
│       ├── LogCategory.h         서버·도구 공용 로그 카테고리(Core는 자기 것을 따로 갖는다)
│       ├── Ids.h / StrongId.h    PlayerId/MailId/ZoneId -- 종류마다 자기 타입
│       ├── MailInfo.h            우편 한 통. **World 캐시와 Zone 모델이 같은 타입을 쓴다**
│       ├── CurrencyInfo.h        재화 하나의 잔액. 종류는 ECurrencyType 그대로 (밑바탕 타입이 고정이라 모르는 값도 담긴다)
│       ├── Packet/               **와이어 계약**. 패킷 하나 = 구조체 하나이고 이름은 패킷 id
│       │                         그대로다(규약: .claude/rules/packet-naming.md)
│       │                         RelayEnvelope(중계 봉투 -- Relay 4개가 공유),
│       │                         ClientPackets(C2Z 요청 + Parse), ZonePackets(Z2C 통지),
│       │                         LoginPackets(C2W·W2C), ZoneLinkPackets/WorldPackets(W2Z·Z2W),
│       │                         ToolLinkPackets(T2W·W2T), ToolResultCode,
│       │                         Send.h(패킷 구조체 하나를 받아 바이트로 -- 고정 레이아웃은
│       │                         memcpy, 가변은 자기 Serialize(). 방향은 컴파일 타임 검증).
│       │                         **서버 프로젝트 안에 두지 않는다** -- 두면
│       │                         다른 서버가 그 프로젝트를 include 하는 역방향이 된다
│       └── Common.cpp            빌드 앵커. 헤더뿐이라 .lib 에 심볼이 없으면 LNK4221 이 난다
├── Server/                           서버 실행 파일 3종
│   ├── GatewayServer/                클라이언트 accept + World로 순수 릴레이 (실행 파일)
│   ├── WorldServer/                  라우팅(BASIC 레인) + DB 레인 (실행 파일)
│   │   ├── Src/App/WorldApp.{h,cpp}  WorldApp -- 소유·기동·정지
│   │   ├── Src/App/WorldConfig.{h,cpp}  WorldConfig 구조체 + LoadConfig -- main은 한 줄로 받아 App에 넘긴다
│   │   ├── Src/Cli/                  실행 인자 모드: DbCheck / IdTest
│   │   │                             main에 있던 것을 뺐다(403 -> 72줄). 세 서버 모두 App/<서버>Config.{h,cpp} 구조가 같다
│   │   ├── Src/Db/                   DbConnection(ODBC, 레인 스레드마다 thread_local 1개),
│   │   │                             AutoSpCommands(UoW 하나 = 트랜잭션 하나), DbCommand, PasswordHash
│   │   ├── Src/Handler/              G2WHandler / Z2WHandler -- I/O 스레드 전용 링크 핸들러
│   │   ├── Src/Packet/               EnterZoneBody.h -- W2ZEnterZone **본문 조립**(우편·재화를
│   │   │                             바이트 예산만큼 실어 보낸다). 와이어 계약 자체는
│   │   │                             Shared/Common 이고 여기 있는 건 World 전용 조립 코드다
│   │   ├── Src/Processor/            레인에서 도는 프로세서 클래스를 한곳에 모은다 -- MainProcessor
│   │   │                             (라우팅), LoginProcessor, DbProcessor, ToolProcessor,
│   │   │                             TestProcessor + ProcessorId.h. EProcessorId 의 태그와 1:1이다
│   │   ├── Src/Test/                 F키 하네스의 보내는 쪽: TestKeys(F1→TestFunc1) + MsgId
│   │   │                             (PushMsg(msgId, 프로세서id, ownerId, 인자...)로 보낸다)
│   │   └── Src/World/                PlayerManager(로그인 캐시 + 라우팅), ZoneLinkRegistry
│   │                                 — 공지처럼 주인이 없는 경로가 있어 둘 다 Mutexed
│   └── ZoneServer/                   존 상태 + Mail 시스템 (실행 파일)
│       └── Src/
│           ├── Player/              **플레이어 한 명의 것을 전부 여기 모은다.** Player.h 의 멤버가
│           │                        곧 목록이다 -- MoveModel / MailModel / CurrencyModel.
│           │                        PlayerRegistry(clientSessionId -> Player, 레인 수만큼 샤딩),
│           │                        PlayerContext(핸들러가 받는 스택 컨텍스트 + 등록 도우미),
│           │                        PlayerMail(우편 요청 처리 -- 콘텐츠 큰 분류마다 파일 하나),
│           │                        PlayerTask.h(AddMail/DelMail/Currency 태스크 -- 전부
│           │                        데이터뿐이라 .cpp 가 없어 한 파일에 모은다)
│           ├── Mail/                **존 전역** 우편 서비스. MailRegistry(그 존 전원의
│           │                        MailModel::Mutexed 색인)와 MailExpiryService(만료 스윕 타이머).
│           │                        플레이어 소유가 아니다 -- 스윕이 다른 스레드라서
│           │                        MailModel 이 Mutexed 인 것이다
│           ├── Processor/           레인에서 도는 프로세서. **EZoneProcessorId 태그와 1:1**
│           │                        PlayerProcessor(수신 파싱 + 입장/퇴장 + 패킷 라우팅),
│           │                        ZoneProcessor(존별 권위 상태 -- 로스터/좌표/경계/틱),
│           │                        BroadcastProcessor(팬아웃 전송), ProcessorId.h
│           ├── App/                 ZoneApp, ZoneConfig, ZoneDef(담당 존 하나의 정의)
│           ├── Worker/              WorkerManager(Zone/Broadcast 그룹 + 존별 tick 타이머 소유)
│           ├── Handler/             W2ZHandler -- **I/O 스레드 전용**. 주인만 뽑아
│           │                        플레이어 레인으로 넘긴다
│           ├── Test/                F키 하네스(TestKeys) -- Gateway/World 와 같은 자리
│           └── Task/ZoneUnitOfWork  Task::UnitOfWork 파생 -- World(DB)/클라이언트 전송 + 역연산 롤백
├── Client/                           게임 클라이언트 — C#/MonoGame, 별도 솔루션
│   ├── Client.slnx                   (GmTool과 같은 이유로 C++ 솔루션에 넣지 않는다)
│   └── Client/Src/
│       ├── Protocol/                 코덱(GmTool.Core에서 복사) + PacketId(C2Z/Z2C/W2C 대역만)
│       │                             + ZoneLayout(서버 ParseZoneList 규칙의 복제 — 짝을 맞춰야 함)
│       ├── Net/                      GameLink(TCP+프레이밍, 수신은 큐에만 넣는다),
│       │                             CouponClient(GmTool.Web HTTP)
│       ├── Model/WorldModel          게임 스레드 전용 상태라 락이 없다(ZoneProcessor와 같은 이유)
│       ├── Text/GlyphAtlas           한글 글리프를 런타임에 GDI+로 굽는다(.mgcb 미사용)
│       └── Ui/                       Painter/Widgets/ZoneView/ChatPanel/MailPanel/CouponPanel/Hud
├── Tool/                             서버를 두드리는 도구들 (게임 클라이언트가 아니다)
│   ├── ProtocolClient/                   수동 테스트용 REPL (Core + Shared/Common 참조)
│   ├── StressClient/               비동기 멀티플렉싱 부하 테스트 도구(1만 세션까지 실측)
│   └── GmTool/                       서버 기능 검증용 운영툴 — C#/.NET 10, 별도 솔루션(기능 개발 중단)
│       ├── GmTool.slnx               (C++ 솔루션에 섞으면 서버만 빌드할 때 NuGet 복원까지 끌려온다)
│       ├── Sql/schema.sql            운영자/명령로그/쿠폰 캠페인·배치·등록시도 (쿠폰 테이블은 캠페인별 동적 생성)
│       ├── GmTool.Core/Src           Protocol(C++ BinaryWriter와 바이트 호환 코덱), Coupons(생성 엔진)
│       ├── GmTool.Web/               Blazor Web App(InteractiveServer) + Minimal API + SqlKata 리포지토리
│       └── GmTool.Tests/             xUnit 77개 (쿠폰 체계/대량 발급/와이어 호환성)
├── 3rd/asio/include/                 standalone ASIO 벤더 코드 (수정 금지)
├── docs/                             읽기용 문서 (전부 오프라인 HTML, 외부 리소스 금지)
│   ├── index.html                    문서 진입점 -- README.md가 여기를 가리킨다
│   ├── Client/Gateway/World/Zone/DB.html               서버별 특징·기능
│   ├── sequences/                    패킷 시퀀스 다이어그램 (규칙: sequences/README.md)
│   ├── design/                       소스에서 옮겨온 설계 근거 (규칙: design/README.md)
│   └── assets/style.css              문서 공용 스타일 (새로 만들지 말 것)
├── bat/                              start_server_all.bat(전체 기동 + VS attach용 PID 출력. wt.exe가 있으면
│                                     창 하나에 탭 4개, 없으면 창을 따로 — cmd.exe엔 탭이 없다)
│                                     stop_server_all.bat(종료)/start_protocol_client.bat(ProtocolClient)
│                                     start_client.bat(Client, 창 개수를 인자로)
│                                     setup_gmtool_db.bat/start_gmtool.bat(운영툴, 쿠폰에 필요)
│                                     setup_game_db.bat(게임 DB 생성 + 스키마/시드 적용)
├── Sql/                              게임 DB(asio_game) 스키마 — **테이블/콘텐츠 단위 파일**
│                                     players.sql / mails.sql / currencies.sql
│                                     unique_keys.sql(RUID 검증용, 게임 스키마 아님)
│                                     seed.sql / verify_unique_keys.sql
│                                     적용 순서는 FK 때문에 players가 먼저 — 자세한 건 Sql/README.md
├── bin/x64/{Debug,Release}/          산출물 (gitignored)
├── obj/                              중간 산출물 (gitignored)
└── .claude/
    ├── settings.json                 PreToolUse/PostToolUse 훅 등록
    ├── rules/                        C++ / MD / 패킷 네이밍 규약
    ├── skills/build/SKILL.md         CLI 빌드 스킬
    └── hooks/                        벤더 코드 차단, 빌드 결과 알림 스크립트
```

---

## 아키텍처: 4계층 분산 + 메시지 큐 프로세서(레인) 구조

```
Client → GatewayServer(순수 릴레이) → WorldServer(라우팅 + DB) → ZoneServer(게임 로직)
                                      I/O 2                      I/O 2
                                      Basic 8                   Player 8    owner=clientSessionId
                                        ├ Main  라우팅                        (수신 파싱도 여기)
                                        ├ Login 로그인            Zone n      owner=zoneId
                                        ├ Tool  운영툴            Broadcast 1 owner=zoneId
                                        └ Test  F키 테스트
                                      Db 4     owner=playerId (계정 단위 직렬화)
```

핵심 불변식: **큐 그룹은 `asio::io_context` + 스레드 N개 + `strand` N개**이고, 넣는 방법이
둘이다.

| 호출 | 배정 | 보장 |
|---|---|---|
| `Post(processorId, work)` | 남는 스레드 아무 데나 | 없음 |
| `Post(processorId, ownerId, work)` | `strand[ownerId % N]` | 같은 주인끼리 **동시 실행 없음 + 넣은 순서대로** |

`processorId`는 스레드 배정에 관여하지 않는 **계측용 태그**라, 한 그룹 안에 여러 프로세서가
공존한다(World의 Basic/Login/Tool이 그 예 — 셋이 같은 8스레드를 쓴다).

**Zone과 World는 이 두 방법을 쓰는 비율이 정반대다.**

- **Zone**: 모든 메시지가 주인을 갖는다(`clientSessionId` 또는 `zoneId`). 그래서 그 주인의
  데이터는 락이 필요 없다 — 어피니티가 곧 보호다.
- **World**: 기본이 "남는 스레드"이고, 순서·정합성이 걸린 것만 주인을 지정한다. 그래서 여러
  스레드가 같이 보는 전역 테이블(`PlayerManager`, `ZoneLinkRegistry`)은 **반드시
  `Thread::Mutexed`**다. "World도 샤딩했으니 락이 없다"는 한때의 전제였고 성립하지 않는다 —
  운영툴의 단일 대상 명령이 남의 샤드를 읽고 있었다.

**strand는 "같은 스레드"를 약속하지 않는다.** 약속하는 것은 비동시성·순서·happens-before
세 가지뿐이다. 직렬화가 목적이면 충분하지만, `thread_local`로 주인별 상태를 들고 있으면
깨진다(DB 커넥션이 `thread_local`인 것은 주인별이 아니라 스레드별이라 괜찮다).

**주인을 무엇으로 하느냐가 곧 설계다.** Zone은 "그 사람만의 것"(우편·재화·UnitOfWork,
owner=`clientSessionId`)과 "존 전체가 공유하는 것"(로스터·좌표·경계·틱, owner=`zoneId`)을
다른 그룹으로 나눈다. **한 레인에 두면 주인을 하나로 못 정해 결국 존 키로 통일되고, 그러면
그 존의 모든 콘텐츠가 스레드 하나로 직렬화된다** — 1만 세션 부하 테스트가 무너진 원인이
정확히 그것이었다(`Server/ZoneServer/Src/Processor/ProcessorId.h`).

주의: **한 메시지가 주인이 다른 데이터를 함께 만지면 이 보호가 깨진다.** 그때는 어피니티
대신 모델 단위 락(`Thread::Mutexed`)이 필요하다 — 근거와 함정은
`docs/design/processor-group.md`.

존 경계를 넘는 이동(핸드오프)은 WorldServer가 라우팅 테이블(`PlayerManager`)만 바꿔서
처리한다 — Gateway는 이동 자체를 모르고, 클라이언트는 EnterZoneNotify로 새 zoneId를 통지받을
뿐 재접속/재인증 없이 같은 TCP 연결을 그대로 쓴다.

접속 직후에는 라우팅 대상이 아니다. **`C2WLogin`을 통과해야** `PlayerManager`가 그 세션을
인증 상태로 바꾸고, 그 전에 온 게임 패킷은 존으로 넘어가지 않는다(`docs/sequences/login.html`).

`Shared/Core/`는 **게임 로직을 전혀 모르는** 정적 라이브러리, 각 서버 프로젝트가 자기 콘텐츠(존/
라우팅/릴레이)를 담당한다. Core를 `Server/` 밑이 아니라 `Shared/`에 둔 이유는 `Tool/`의
ProtocolClient/StressClient도 이걸 참조하기 때문이다 — `Server/` 안에 두면 도구·클라이언트가
서버를 의존하는 역방향 구조가 된다.

### 계층별 핵심 타입

| 프로젝트 | 타입 | 역할 |
| --- | --- | --- |
| `Core` | `IoContextPool` | io_context N개 + 전용 I/O 스레드 N개 |
| | `Listener` / `Connector` | accept / outbound connect. 둘 다 성공 시 `IPacketHandler::OnSessionOpened` 호출 |
| | `Session` | 소켓 1개, `strand_`로 보호 — `SendPacket()`은 어느 스레드에서든 호출 가능 |
| | `Dispatcher<TId,TContext>` | 패킷 타입 → 핸들러 템플릿 라우터 (게임 무관) |
| | `Processor::Group<TId>` | 큐 그룹 = asio `io_context` + 스레드 N개 + `strand` N개. `Post(id, work)`는 남는 스레드, `Post(id, ownerId, work)`는 `ownerId % N` strand로 직렬화 |
| | `Thread::Mutexed<T>` | `.Write()->`(unique_lock)/`->`(shared_lock) — 교차 스레드 접근 예외 지점만 보호 |
| | `Task::ITask` / `Task::UnitOfWork` | 변경 기록 하나 / 그 목록을 들고 있는 기반 클래스. Core는 콘텐츠 의미를 모르고, 직렬화·역연산은 파생 태스크가 구현한다. **커밋은 파생 클래스 소멸자**(기반 소멸자에서는 가상 함수가 파생 구현으로 안 불린다 → 파생을 `final`로 닫아 그 상황 자체를 없앰) |
| | `Base::Ruid` | 요청 하나를 전 서버에서 가리키는 `int64`(밀리초 41 + 노드 10 + 시퀀스 12비트). 기동 시 `Ruid::Init(nodeId)` 한 번, 이후 어디서든 `Ruid::Create()`. 랜덤 GUID를 안 쓴 이유는 클러스터드 인덱스 페이지 분할 |
| `WorldServer` | `PlayerManager` / `ZoneLinkRegistry` | 여러 스레드가 같이 보는 전역 테이블이라 `Mutexed`. `PlayerManager`는 라우팅뿐 아니라 **살아 있는 우편·재화 캐시**다 — 로그인 때 DB에서 채우고, 존이 올린 UnitOfWork를 BASIC 레인에서 계속 반영한다. 프로세스를 넘는 핸드오프가 이 캐시를 그대로 실어 보낸다 |
| | `World::LoginProcessor` | `C2WLogin` → 계정 조회 → (없으면) 자동 가입 → `usp_players_load` → 캐시 + 존 입장. **BASIC→DB→BASIC으로 레인을 갈아타지만 주인은 `clientSessionId` 하나로 고정**이다 — `playerId`를 알게 된 뒤에도 바꾸지 않는다(갈아타면 앞 구간과 직렬화가 끊긴다). 존 입장 뒤의 UnitOfWork만 `playerId`가 주인 |
| | `World::MainProcessor` | BASIC 레인(`Main`)의 라우팅 처리기. Gateway/Zone 링크가 I/O 스레드에서 던진 일이 실제로 도는 곳 -- 링크 핸들러는 주인만 뽑아 넘기고 상태를 만지지 않는다 |
| | `World::DbProcessor` | DB 레인(`Db`)의 처리기. 커넥션 획득 + 트랜잭션 + **실패 정책(Fire-and-Forget)** + 콜백이 여기 한곳에 있다 |
| | `World::AutoSpCommands` | SP 커맨드를 모았다가 소멸 시 `DbProcessor`로 한 번에. **UoW 하나 = 트랜잭션 하나** |
| | `Db::DbConnection` | ODBC 커넥션. **레인 스레드마다 `thread_local` 1개**라 이 계층에 락이 없다. 결과 집합이 여러 개인 SP는 `SQLMoreResults`로 다 읽는다. 로그인의 **읽기**와 UnitOfWork의 **쓰기** 경로가 둘 다 실제로 돈다 |
| `ZoneServer` | `ZoneProcessor` | 존 하나의 권위 상태(로스터·좌표·경계·틱). 존 레인 전용이라 락이 없다 |
| | `PlayerProcessor` | 플레이어 레인의 진입점 — **World 링크에서 온 패킷의 해석**부터 입장/퇴장 + 패킷 라우팅까지. Move/Chat만 직접 처리한다(모델 변경도 DB 저장도 없어 UoW를 열지 않는다) |
| | `PlayerMail` | 우편 요청 처리(Add/Del/Buy). **콘텐츠 큰 분류 = 파일 한 쌍**이고 자기 패킷을 스스로 등록한다. 전부 static — 필요한 것은 `PlayerContext`로 들어온다 |
| | `PlayerRegistry` | clientSessionId → Player. 레인 수만큼 샤딩돼 락이 없다 |
| | `WorkerManager` | Zone/Broadcast 그룹 + 존별 tick 타이머 소유 |
| | `WorldLinkHandler` | World와의 연결의 `IPacketHandler`. **I/O 스레드 전용** — 주인만 뽑아 플레이어 레인으로 넘기고 상태를 만지지 않는다 |
| | `Zone::Player` | 플레이어 한 명 + 그 사람의 모델들. 우편함만 `Mutexed` 핸들이고(만료 스윕이 다른 스레드) 재화는 값 — 모델마다 실제 접근 스레드 수에 맞춘다 |
| | `Mail::Model` / `Currency::Model` | 변경분을 `Task::UnitOfWork`에 태스크로 모았다가 **스코프를 벗어날 때** World(DB)와 클라이언트로 한 번에 전송. 실패는 `[[nodiscard]] EErrorCode`로 반환하고 호출부가 `SetError`로 옮긴다 |
| | `Zone::UnitOfWork` | `Task::UnitOfWork` 파생(`final`). 소멸자에서 결말을 낸다 — 실패면 기록을 역순으로 훑어 `taskKind` 로 분기해 되돌리고(직렬화도 같은 자리에서 분기한다 — Core 는 뜻을 모른다), 결과를 `Z2CTaskResult`로 클라이언트에 통지 |
| `GatewayServer` | `ClientLinkHandler`/`WorldLinkHandler` | 클라이언트↔World 양방향 릴레이만, 게임 로직 없음 |
| `WorldServer` | `World::ToolProcessor` | 운영툴 전용 포트(9300)의 `IPacketHandler`. 다른 두 링크 핸들러와 **같은 스레드 규약**이라 락 없음 |

---

## 빌드 / 실행

1. `Server/Server.slnx`(서버) 또는 `Tool/TestClient.slnx`(테스트 클라이언트)를 Visual Studio 2026 **이상**으로 연다 (`PlatformToolset=v145`,
   `/std:c++23`, `x64`만 지원).
2. 실행 파일이 5개(`GatewayServer`/`WorldServer`/`ZoneServer`/`ProtocolClient`/`StressClient`)라
   개별 F5보다 **`bat/start_server_all.bat`**(World→Zone(1,2)→Zone(3,4)→Gateway, 탭 4개)로 한 번에
   띄우고 `bat/start_protocol_client.bat`으로 `ProtocolClient`를 붙이는 걸 권장. **`ZoneServer`
   프로세스가 2개인 게 정상** — 격자의 위쪽 행(존 1,2)과 아래쪽 행(존 3,4)을 나눠 담당한다.
3. `F7`(빌드만) 또는 `F5`/`Ctrl+F5`(빌드 후 실행) — 특정 프로젝트만 빌드하려면 솔루션
   탐색기에서 우클릭 → 빌드.
4. 산출물: `bin/x64/Debug/{Core.lib, GatewayServer, WorldServer, ZoneServer, ProtocolClient,
   StressClient}.exe` (`obj/`, `bin/`은 `.gitignore`에 포함됨).

모든 실행 파일 프로젝트가 `Core.vcxproj`를 프로젝트 참조로 물고 있어 `Core` → 나머지 순서로
자동 빌드된다.

**CLI 빌드**(VS GUI 없이): `.claude/skills/build/SKILL.md` — MSBuild 경로 탐색, Git Bash
`/p:` 치환 우회법 정리.

**테스트**: 자동화 스위트 없음. `ProtocolClient.exe`가 실제 프로토콜(Echo/Move/Chat/Mail/
Z2CEnterZoneNotify)을 왕복시키는 REPL 더미 클라이언트, `StressClient.exe`가 1만 세션까지
동시 접속 부하 테스트 도구 — 바이너리 프로토콜이라 telnet 검증 불가라 둘 다 직접 만들었다.
사용법은 `README.md` "7. 테스트" 절 참고.

---

## 코드 작성 규칙 인덱스

| 항목 | 위치 |
|------|------|
| C++ 세부 패턴(include 순서/캐스팅/pch/상수 `k` 접두사/const/emplace_back/스마트 포인터 별칭 `SPtr`·`UPtr`·`WPtr`/asio 예외 안전) | `.claude/rules/cpp-patterns.md` |
| MD 파일명 규약 | `.claude/rules/md-patterns.md` |
| 패킷 id 네이밍 규칙 / 방향별 번호 대역 | `.claude/rules/packet-naming.md` |
| SQL 네이밍(테이블 복수형 snake_case) / SP 작성 규약 | `.claude/rules/sql-patterns.md` |
| CLI 빌드 절차 | `.claude/skills/build/SKILL.md` |
| 패킷 시퀀스 다이어그램 | `docs/sequences/` (규칙: `docs/sequences/README.md`) |
| 서버별 설명 / 설계 근거 | `docs/*.html` / `docs/design/` (규칙: `docs/design/README.md`) |

---

## 주의사항

- **개발 환경(Windows/NTFS)은 대소문자를 구분하지 않는다.** 새 폴더를 만들 때 기존 폴더와
  대소문자만 다른 이름(`core` vs `Core`처럼)을 쓰면 같은 폴더로 병합돼버린다. 실제로 이 문제로
  한 번 정리한 이력이 있음.
- `WorkerManager::Start()`는 `ZoneProcessor&` 참조를 캡처하는 람다를 타이머 콜백으로 쓴다.
  `Stop()`은 반드시 **타이머를 먼저 취소한 뒤** 워커를 정지시키는 순서를 지켜야 안전하다
  (순서를 바꾸면 안 됨).
- `PlatformToolset`은 `v145`(VS 2026 툴셋) + `/std:c++23`이다. 개발 환경이 VS 2026 Community라
  툴셋을 맞춰둔 것이고, 그 덕에 IDE가 "v145로 업그레이드" 대화상자를 띄우지 않는다.
  **대신 VS 2026이 없으면 `MSB8020`으로 빌드가 막힌다** — 예전에는 VS 2022 사용자도 clone 후
  바로 빌드할 수 있게 v143을 유지했지만, 이 저장소는 코드와 구조를 읽히는 것이 목적이라
  개발 편의를 택했다.
  옛 툴셋으로 확인만 하고 싶을 때는 vcxproj를 고치지 말고 빌드 인자로 덮어쓴다:
  `MSBuild.exe Server/Server.slnx -p:Configuration=Debug -p:Platform=x64 -p:PlatformToolset=v143 -p:LanguageStandard=stdcpp20 -m`
- `3rd/asio` 수정 금지는 `.claude/settings.json`의 PreToolUse 훅으로도 강제된다(Edit/Write가
  해당 경로를 건드리면 자동 차단).

---

## 로드맵 상태

Gateway/World/Zone 4계층 분리, 존 핸드오프(재접속 없음), 메시지 큐 프로세서 구조(asio
`io_context` + `strand`), Mail(Mutexed/UnitOfWork) 시스템, 재화(Currency) + 모델 두 개에 걸친
트랜잭션과 역순 롤백 실측(`C2ZMailBuy`), 요청 식별자(`RUID`), 부하 테스트 도구(StressClient),
시각 클라이언트(Client), **로그인(`C2WLogin`) + 자동 가입 + World 콘텐츠 캐시**,
**DB 쓰기 경로**(UnitOfWork -> BASIC 레인에서 캐시 반영 -> `playerId`를 주인으로 DB 레인, UoW 하나 =
트랜잭션 하나)까지 완료. 부하 테스트로 발견된 처리량 병목 수정이 진행 중(계획은 `docs/local/`).
남은 것: 중복 로그인 차단(playerId 색인이 선행), Actor/Monster/AOI. 자세한 표는
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
- 검증 방법 (빌드 성공 여부, `ProtocolClient`로 확인 가능하면 어떤 명령으로 확인하는지)
