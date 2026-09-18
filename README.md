# asio-server

C++23 / standalone ASIO(Boost 비의존) 기반 **분산 게임 서버** 포트폴리오.

MMORPG 서버 구조를 4개 프로세스로 단순화해, **"게임 상태에 락을 쓰지 않고 어떻게
멀티스레드로 처리할 것인가"** 한 가지 질문에 집중해 만들었습니다.

| | |
|---|---|
| **핵심 주장** | 락을 없애는 게 목적이 아니라, **락이 필요한 지점을 세어서 줄이는 것**이 목적 — 상황별로 4가지 전략(레인 어피니티 / 단일 스레드 / 스냅샷 전달 / 명시적 락)을 나눠 적용했습니다 |
| **규모** | C++ 소스 115개 파일 / 프로젝트 6개(정적 라이브러리 1 + 실행 파일 5) + C# 4개 프로젝트(운영툴 3 + MonoGame 시각 클라이언트 1) |
| **환경** | MSVC v145, x64 전용, `/std:c++23`, 외부 의존성은 벤더링한 standalone ASIO 하나뿐 (운영툴만 .NET 10 / SQL Server 별도) |
| **검증** | 서버는 자동화 스위트 없음 — 프로토콜 REPL 클라이언트와 **1만 세션까지 실측한 부하 도구를 직접 만들어** 정확성·처리량·지연을 측정. 운영툴은 xUnit 77개 |

```
Client ──▶ GatewayServer ──▶ WorldServer ──▶ ZoneServer ──▶ SQL Server
           (순수 릴레이)      (라우팅 + DB)    (게임 로직)
                                  ▲
                              GmTool (9300)
```

---

## 📖 문서

**설명은 전부 [`docs/index.html`](docs/index.html)에 있습니다** — 브라우저로 열어보세요
(오프라인 열람용이라 외부 리소스를 쓰지 않습니다).

| 문서 | 내용 |
|------|------|
| [docs/index.html](docs/index.html) | **문서 진입점** — 전체 그림과 목록 |
| [docs/Client.html](docs/Client.html) | 시각 클라이언트. 지금 클라이언트가 메워주고 있는 서버의 빈자리 |
| [docs/GatewayServer.html](docs/GatewayServer.html) | 접속 종단, 게임 로직 0 |
| [docs/WorldServer.html](docs/WorldServer.html) | 라우팅 두뇌와 DB 관문 |
| [docs/ZoneServer.html](docs/ZoneServer.html) | 레인 다섯, 담당 존마다 처리기 네 벌. UnitOfWork와 역순 롤백 |
| [docs/DB.html](docs/DB.html) | 스키마·SP 골격·트랜잭션 |
| [docs/sequences/](docs/sequences/index.html) | **패킷 시퀀스 다이어그램** — 대표 기능이 어느 스레드를 거치는지 |
| [docs/design/](docs/design/README.md) | 소스에서 옮겨온 설계 근거 |
| [PROGRESS.md](PROGRESS.md) | 다음에 할 일과 그 순서 |

대표 흐름 세 개를 시퀀스 다이어그램으로 그려뒀습니다:

- [C2ZMailAdd → DB](docs/sequences/mail-add-to-db.html) — 우편 하나가 Gateway·World·Zone 두 레인·DB 레인을 지나는 전 경로
- [C2ZMailBuy 역순 롤백](docs/sequences/mail-buy-rollback.html) — 모델 두 개에 걸친 트랜잭션이 실패했을 때
- [존 핸드오프](docs/sequences/zone-handoff.html) — 재접속 없이 존을 넘는다

---

## 빌드

`Server/Server.slnx` 열기 → `Ctrl+F5`. 테스트 클라이언트는 `Tool/TestClient.slnx`.
`PlatformToolset=v145`, `/std:c++23`, x64 전용이라
**VS 2026 이상**이 필요합니다.

```bash
MSBuild.exe Server/Server.slnx     -p:Configuration=Release -p:Platform=x64 -m
MSBuild.exe Tool/TestClient.slnx -p:Configuration=Release -p:Platform=x64 -m
```

모든 실행 파일이 `Core.vcxproj`를 프로젝트 참조로 물고 있어 `Core` → 나머지 순서로 자동
빌드됩니다. 산출물은 `bin/x64/{Debug,Release}/`.

---

## 실행

```bat
bat\start_server_all.bat          :: World → Zone(1,2) → Zone(3,4) → Gateway, 탭 4개 (Debug)
bat\start_server_all.bat Release  :: 성능 수치를 재현하려면 이쪽 — Debug는 약 5배 느립니다
bat\start_protocol_client.bat     :: ProtocolClient (REPL)
bat\start_client.bat 2            :: Client 창 2개 (MonoGame, 별도 .NET 솔루션)
bat\stop_server_all.bat           :: 종료 (-keep 을 주면 콘솔 창은 남김)
```

**설정은 `config/*.cfg`** 입니다 — 스레드 수, 포트, 틱 주기를 코드 수정 없이 바꿉니다.
빌드할 때 실행 파일 옆으로 복사되고, 다른 파일로 띄우려면 경로를 인자로 줍니다
(`ZoneServer.exe 1,2 D:\configs\zone-load-test.cfg`). 형식과 정책은
[docs/design/config-file.md](docs/design/config-file.md).

**프로세스가 4개인 게 정상입니다** — `ZoneServer`를 두 개 띄워 존을 2×2 격자로 나눕니다.

```
 y:[10,20)   존 1   존 2      ← ZoneServer.exe 1,2   (프로세스 #1)
 y:[0,10)    존 3   존 4      ← ZoneServer.exe 3,4   (프로세스 #2)
             x:[0,10)  x:[10,20)
```

가로 이동(1↔2)은 **같은 프로세스의 스레드 간**, 세로 이동(1↔3)은 **프로세스를 넘는**
핸드오프입니다 — 자세한 건 [존 핸드오프 다이어그램](docs/sequences/zone-handoff.html).

기동 후 네 프로세스의 PID를 출력하므로 Visual Studio의 **디버그 → 프로세스에 연결**
(`Ctrl+Alt+P`)에서 Ctrl로 다중 선택하면 한 번에 붙을 수 있습니다.

**운영툴(GmTool)** — 별도 솔루션(`Tool/GmTool/GmTool.slnx`)입니다.

```bat
bat\setup_gmtool_db.bat   :: gmtool DB/로그인 준비 (SQL Server 가 이미 떠 있어야 함)
bat\start_gmtool.bat      :: http://127.0.0.1:5080  (초기 계정 admin / 0000)
```

게임 DB는 `bat\setup_game_db.bat` — 적용 순서는 [Sql/README.md](Sql/README.md) 참고.

---

## 테스트

서버(C++)는 **자동화 스위트가 없습니다.** 바이너리 프로토콜이라 telnet 검증이 안 돼서
클라이언트 3개를 직접 만들었습니다.

| 도구 | 목적 | 사용법 |
|------|------|--------|
| `ProtocolClient` | 패킷 왕복을 손으로 확인하는 REPL | `echo` / `move` / `chat` / `mail add·del·buy` |
| `StressClient` | 대규모 동시 접속·정확성·지연 | `StressClient.exe 127.0.0.1 9000 1000 200` |
| `Client` | 존 이동·채팅·우편·쿠폰을 눈으로 | `bat\start_client.bat 2 Debug auto` |

클라이언트로 무엇을 보는지: [docs/Client.html](docs/Client.html)

운영툴은 xUnit 77개가 붙어 있습니다 — `cd Tool\GmTool && dotnet test`

---

## 로드맵

| 단계 | 내용 | 상태 |
|---|---|---|
| 0~3 | 셋업, echo 서버, 패킷 프레이밍, 존 어피니티 라우팅 | 완료 |
| 4 | Gateway/World/Zone 분리, 재접속 없는 핸드오프, Mail(Mutexed/UnitOfWork) | 완료 |
| 4-1 | 재화 + 모델 두 개에 걸친 트랜잭션·역순 롤백, 요청 식별자(`RUID`) | 완료 |
| 5 | 메시지 파이프라인(레인) 구조 — 존을 LB/BASIC/TICK/BROADCAST/TIMER 다섯 레인으로 | 완료 |
| 6 | 부하 도구(StressClient), 지연 백분위 계측 | 완료 |
| 7 | 부하 병목 수정 후 **재측정** | 진행 중 |
| 8 | 운영툴(GmTool) — 우편/공지/대량 쿠폰 | 완료 |
| 9 | **실제 DB 연동** — 로그인이 계정·우편·재화를 읽고, 존의 UnitOfWork가 SP로 저장된다 | 완료 |
| 9-1 | 로그인/자동 가입, World 콘텐츠 캐시, 클라이언트 단계 분리 | 완료 |
| 10 | Actor/Monster, AOI(시야 동기화) | 예정 |
| 11 | MonoGame 시각 클라이언트 | 완료 |

## 의도적으로 범위 밖에 둔 것

물어보시면 설명드릴 수 있습니다. 각 항목의 자세한 사정은 위 문서에 적어뒀습니다.

- **비밀번호 말고는 없는 인증** — 로그인(`C2WLogin`)은 붙었지만 계정이 없으면 그 자리에서
  만듭니다. 세션 토큰도, 중복 로그인 차단도 없습니다 ([login](docs/sequences/login.html))
- **존 입장 스냅샷 / 퇴장 통지 없음** — 클라이언트가 하트비트와 타임아웃으로 메우고 있습니다
  ([Client.html](docs/Client.html))
- **수평 확장** — 세션 id가 프로세스별 1부터라 Gateway를 2대 띄우면 라우팅 키가 충돌합니다
- **자동화 테스트** — 회귀 방지 스위트가 없습니다

---

## 라이선스

학습 및 포트폴리오 용도. ASIO는 Boost Software License를 따릅니다.
