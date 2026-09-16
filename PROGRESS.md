# 진행 상황 정리 (다음 세션 이어하기용)

마지막 업데이트: 2026-09-13

`README.md`(소개용)와 별개로, 다음 세션에서 빠르게 컨텍스트를 복구하기 위한 문서. 자세한
아키텍처/타입 표는 `CLAUDE.md`, 코딩 규약은 `.claude/rules/`를 우선 참고 — 여기는 "지금
뭐가 되어 있고 다음에 뭘 하면 되는가"만 압축해서 담는다.

---

## 1. 지금까지 된 것

### 2026-09-12에 추가된 것 — 로컬 SQL Server / SP 골격 / 툴셋 v145

- **Docker 제거**: 컨테이너를 띄우던 `bat/start_mssql.bat`을 없애고 `bat/setup_gmtool_db.bat`으로
  이름을 바꿨다 — 엔진 설치가 선행 조건이 되면서 배치가 할 일은 DB/로그인 준비만 남았다.
  두 준비 배치 모두 호스트 `sqlcmd`로 `127.0.0.1,1433`에 붙으므로 엔진이 로컬 설치본이든
  포트를 뚫어둔 컨테이너든 그대로 돈다. **`sqlcmd`에 `-f 65001`이 빠지면 UTF-8 `.sql`을
  CP949로 읽는다** — 실제로 저장된 SP 본문의 한글 주석이 깨져 있었다. 소스의 `/utf-8`과
  같은 함정이고, 지금은 주석뿐이지만 한글 리터럴이 들어가면 깨진 값이 DB에 남는다.
  개발용 비밀번호는 서버/운영툴 연결 문자열 전부 `0000`으로 통일.
- **SQL을 콘텐츠 단위로 분리 + SP 골격 통일**: `schema.sql` 한 덩어리를 `players`/`mails`/
  `currencies`/`unique_keys`로 쪼갰다 — 테이블과 그걸 만지는 SP가 떨어져 있으면 컬럼 하나를
  바꿀 때 고칠 자리를 놓친다. SP 9개는 같은 골격으로 다시 썼다: `@is_trans_outside` 첫
  파라미터, `XACT_STATE` 진입 가드, 조건부 트랜잭션, `GOTO`로 단일 출구, `RETURN` 코드,
  `usp_` 접두사. **`@is_trans_outside`가 필요한 이유**는 트랜잭션 경계가 두 곳에서 잡히기
  때문이다 — 호출부가 SP 여러 개를 묶어 보낼 때는 커넥션에 이미 트랜잭션이 열려 있는데 SP가
  그걸 모르고 또 열면 중첩이라 안쪽 COMMIT이 실제로 커밋하지 않고, 반대로 양쪽 다 안 열면
  문장이 여러 개인 SP가 중간에 실패했을 때 앞 문장이 그대로 남는다. 조회 SP도 같은 골격을
  쓴다(모양을 다르게 두면 호출부에 분기가 생긴다 — 0행이 정상이라 `@@ROWCOUNT` 검사만 뺐다).
  규약은 `.claude/rules/sql-patterns.md`, 파일 구성/적용 순서(FK 때문에 `players`가 먼저)는
  `Sql/README.md`.
- **DB 계층이 SP의 RETURN 코드를 받는다**: 호출문을 `{? = CALL p(?,...)}`로 바꿔 1번에 RETURN
  값, 2번에 `@is_trans_outside`를 `DbConnection`이 일괄로 붙인다 — 콘텐츠 코드는 이 둘을
  몰라도 된다. **ODBC 규약상 결과 집합을 끝까지 소비하기 전에는 출력 파라미터가 채워지지
  않아서**, 결과를 버리는 쓰기 경로에서도 `SQLMoreResults`로 훑은 뒤에 반환값을 읽는다.
  0이 아니면 `DbProcedureException`을 던진다 — 반환값으로 돌려주면 호출부가 검사를 빠뜨렸을 때
  실패한 작업이 그대로 커밋되지만, 던지면 `Execute`의 트랜잭션 경로가 롤백을 태운다.
  ODBC 실패(`DbException`)와 둘로 나눈 기준은 **복구 방법**이다 — 전자는 재연결로 살아날 수
  있고 후자는 다시 시도해도 같은 결과다.
- **툴셋 `v145` + C++23**: 개발 환경이 VS 2026 Community인데 저장소가 `v143`이라 솔루션을 열
  때마다 업그레이드 대화상자를 무시해야 했다. vcxproj 6개의 Debug/Release를 전부 올렸고 클린
  리빌드로 양쪽 에러·경고 0을 확인했다. **대신 VS 2026이 없으면 `MSB8020`으로 빌드가 막힌다** —
  "clone 후 바로 빌드" 방침을 포기한 것이고, 옛 툴셋 확인은 빌드 인자로 덮어쓴다(`CLAUDE.md`
  주의사항 참고).
- **`bat/start_client.bat`이 창을 하나도 못 띄우던 문제**: `Client`를 `Tool/` 밖으로 옮긴 뒤
  경로가 옛것이었고, 더 고약한 건 **cmd가 괄호 블록 본문을 바이트 오프셋으로 되감는 것**이다 —
  이 파일은 UTF-8 한글이라 그 오프셋이 글자 중간에 떨어져 멀쩡한 줄이 잘리고 뒷토막이 명령으로
  실행됐다. `if`/`for` 본문을 전부 한 줄로 접어 되감기 자체를 없앴다(`start_server_all.bat`의
  "서브루틴 금지"도 같은 원인). 루프 안의 `if`도 걷어냈다 — 한 줄로 접으면 조건이 거짓일 때
  cmd가 뒤의 `& start`까지 건너뛰어 `auto` 없이 실행하면 창이 안 떴다(오류 메시지도 없다).
- **실행 확인**(Debug, 01:33~02:23): 서버 3종 + `Client` 2~3창을 자동 순회로 돌렸다 —
  플레이어 입장 343회 / 존 경계 핸드오프 339회, WARN/ERROR 0건(종료 시 "World 연결 끊김"
  1건 제외), 전 레인 `Pending 0`, `Zone` 레인 61,277콜 평균 대기 22µs.

### 2026-09-11에 추가된 것 — 게임 DB 연동 기반

- **게임 DB(`asio_game`)**: 운영툴(`gmtool`)과 같은 SQL Server 인스턴스를 쓰되 DB는 따로 둔다.
  `players` / `mails` / `currencies`(감사 로그 테이블은 보류). 처음에는 Docker 컨테이너로
  띄웠지만 09-12에 로컬 설치본으로 옮겼다(아래 참고).
  스키마 규약은 `.claude/rules/sql-patterns.md` — 테이블 복수형, 클러스터 인덱스 필수,
  SP는 `[콘텐츠명]_[행위]`, INSERT/UPDATE는 upsert 하나로, DELETE는 `delete_ut` 소프트 삭제.
- **ODBC 기반 DB 계층**(`Server/WorldServer/Src/Db/`): 실무 서버의 `AutoSpCommands`와 같은
  모양 — `(ownerId, isTran, callback)`을 받아 SP 커맨드를 쌓았다가 **소멸 시 DB 큐 그룹으로**
  한 번에 보낸다. **UnitOfWork 하나 = 트랜잭션 하나**이고 여러 UoW를 모으지 않는다.
  커넥션은 레인 스레드마다 `thread_local` 1개라 "커넥션 수 = 소비자 수 1:1"이 그대로 성립하고
  이 계층에 락이 없다. 비밀번호는 CNG PBKDF2-HMAC-SHA256.
- **`RequestId` → `RUID`**: 이 생성기가 요청 추적용 id만이 아니라 `mailId`/`playerId`도
  발급하게 되어 이름을 용도에서 떼어냈다. 노드 번호를 대역으로 고정(0 예약 / 1\~99 World /
  100\~199 Zone / 254 시드 / 255 운영툴) — 예전처럼 World가 0, Zone이 zoneId면 World를 늘리는
  순간 겹친다. **모든 id는 이 생성기로 발급한다**(규칙: `cpp-patterns.md`), DB `IDENTITY` 제거.
- **RUID 실측 검증**(500만 개, 5프로세스 × 10스레드): 중복 0건, 노드 배정 정확,
  **DB 레인 분배 12.49\~12.51%**(이상값 12.5%), 단일 노드 **단편화 0.428% / 페이지 채움 99.94%**.
  다중 노드에서는 논리 단편화가 올라가지만 무작위 키 대조군 대비 **페이지 수 26% 절감**.
  → "시간순 키라 append-only"는 **발급자가 하나일 때만** 성립한다. 상세는 `docs/local/`의
  검증 결과 문서.

### 기존

- **4계층 분산 구조**: Client → GatewayServer(순수 릴레이) → WorldServer(라우팅+DB워커) →
  ZoneServer(존 상태). Gateway/World는 이번에 새로 추가된 프로젝트.
- **존 핸드오프**: 존 경계를 넘으면 World가 라우팅 테이블(`PlayerManager`(당시 이름 `ClientRegistry`))만 바꾼다 —
  Gateway는 이동 자체를 모르고, 클라이언트는 Z2CEnterZoneNotify 통지만 받을 뿐 재접속 없이
  같은 연결을 유지한다.
- **ZoneServer 5-풀 구조**: NETWORK(소켓 I/O) → LB(패킷 파싱, zoneId 판단) → BASIC(zoneId
  sticky, 게임 로직) → TICK(존별 주기 처리, 현재 placeholder) → BROADCAST(zoneId sticky,
  팬아웃). 프로세스 1개가 존 여러 개를 동시에 호스팅 가능(`ZoneServer.exe 1,2`처럼 실행).
- **존 배치는 2×2 격자**(`1 2` / `3 4`, 각 존 10×10). 기본 기동이 **프로세스 2개**라 위쪽 행
  (존 1,2)과 아래쪽 행(존 3,4)을 나눠 담당하고, 그래서 **가로 이동은 스레드 간, 세로 이동은
  프로세스(TCP 링크)를 넘는 핸드오프**가 된다 — 두 경로를 한 화면에서 비교할 수 있다.
  **zoneId는 1부터 시작**하고 0은 "존 없음/미배정" 예약값이다 — 0을 유효 존으로 쓰면 "0번
  존에 있다"와 "아직 아무 존에도 없다"가 같은 값이 되어 구분할 수 없다.
  배치를 정하는 코드는 `ParseZoneList` 하나(`kZoneSize`/`kZonesPerRow`/`kZoneRows`)이고,
  그 아래 계층은 `Def`(담당 사각형)를 그대로 들고 다니므로 배치 규칙을 모른다.
- **WorldServer**: `Basic`(8) / `Db`(4) 두 큐 그룹. **Zone과 스레드 모델이 다르다** — Zone은
  모든 메시지가 주인을 갖지만 World는 기본이 "남는 스레드"이고 순서·정합성이 필요한 것만
  주인을 지정한다. 그래서 전역 테이블(`PlayerManager`/`ZoneLinkRegistry`)이 `Mutexed`다.
  `PlayerManager`는 한동안 샤딩이었는데, "그 샤드는 그 번호 스레드만 만진다"는 전제가 World에
  성립하지 않았다(운영툴의 단일 대상 명령이 남의 샤드를 읽고 있었다).
- **로그인(`C2WLogin`)**: 계정 확인 → 없으면 자동 가입 → `usp_players_load`로 우편·재화를 한
  왕복에 읽어 캐시 → `W2ZEnterZone`에 실어 존까지. 레인을 네 번 갈아타되 **주인은 clientSessionId 하나로
  고정**이다. TCP 연결이 곧 플레이어이던 동작이 여기서 끝났다.
  DB **읽기만** 되고 쓰기(UnitOfWork→SP)는 아직 로그만 남긴다.
- **Mail 시스템**: `Model`/`Registry`/`ExpiryService`(만료 자동삭제, 별도
  유지보수 타이머). `Thread::Mutexed`(Core, `.Write()->`=쓰기/`->`=읽기)가 "평소엔 락 없음,
  BASIC 스레드와 유지보수 타이머가 만나는 유일한 지점만 락"을 보여준다. 상태 변경은
  `Task::UnitOfWork`(범용 Unit-of-Work)에 태스크로 기록했다가 **스코프를 벗어날 때**
  한 번에 내보낸다 — 성공하면 같은 태스크 목록이 World(DB)와 클라이언트(`Z2CTaskResult`)
  양쪽으로 가고, 실패하면 역순으로 되짚어 메모리를 롤백한 뒤 에러 코드만 클라이언트로 간다.
- **재화 시스템**: `Currency::Model`(골드). 값을 바꾸는 통로를 `SetTracked` 하나로
  좁혀서 검증·이전값 포착·태스크 기록·대입이 한 자리에서 일어난다. `C2ZMailBuy`(우편 지급 +
  골드 차감)가 **모델 두 개에 걸친 트랜잭션**이라 롤백이 실제로 밟히는 경로다.
- **부하 테스트 도구(`StressClient`)**: `Core::Network::Connector`/`Session`을 그대로
  재사용해 세션당 스레드 없이 io_context 풀 하나로 1만 소켓까지 비동기 멀티플렉싱.
  Release 기준 1,000세션×200사이클은 18,337 사이클/초로 완전 통과(불일치 0, 스톨 0,
  브로드캐스트 100%, P95 40ms). 10,000세션은 데드락/데이터 불일치 0을 유지하면서도 처리량이
  471 사이클/초로 무너짐(P95 62초). 원인과 수정 계획은 따로 정리해 두었다.
- **지연(RTT) 백분위 계측**: `StressClient`가 처리량뿐 아니라 P50/P95/P99/P99.9를 낸다.
  원시 샘플 대신 551개 로그스케일 버킷(`Stats/LatencyHistogram.h`)에 relaxed atomic으로
  기록 — 수백만 샘플에도 상수 메모리·O(1)이라 계측이 실험 자체를 방해하지 않는다. 재는
  구간은 `MailAdd→Ack`, `MailDel→Ack`, 사이클 전체 3종. 추적 상한은 100초(처음 10초로
  뒀다가 과부하 실험에서 P95/P99가 전부 상한에 몰려 구분이 안 돼 넓혔다).
- **`bat/start_server_all.bat`/`bat/start_protocol_client.bat`/`bat/start_client.bat`**:
  전체 프로세스를 한 번에 띄우는 배치 파일. Client는 별도 .NET 솔루션이라 bin/x64가 아닌
  자기 경로로 빌드되므로 `dotnet run`으로 띄운다(창 개수를 인자로 받는다 -- 브로드캐스트
  확인에는 최소 2개가 필요하다).
- 문서를 `docs/`로 재편했다(2026-09-13). 플로우차트(단계 나열)를 걷어내고 **패킷 시퀀스
  다이어그램**(`docs/sequences/`)으로 바꿨다 -- 누가 누구에게 보냈는지가 드러나지 않던 것이
  이유다. 서버별 설명은 `docs/*.html`, 설계 근거는 `docs/design/`, README는 진입점으로 축소.
- **운영툴(`Tool/GmTool`, C#/.NET 10/SQL Server)**: 서버 쪽은 WorldServer에 세 번째 accept
  포트(9300)와 `Processor/ToolProcessor`를 추가했다 — `GatewayLinkHandler`/`ZoneLinkHandler`와
  **같은 스레드 규약**(I/O 스레드는 바이트 복사만 → Basic 그룹으로 Post)을 따른다.
  `authenticatedSessions_`는 툴 세션이 주인이라 지금도 락이 없지만, **운영툴이 만지는
  플레이어 테이블은 남의 것**이라 `PlayerManager`가 `Mutexed`여야 했다(당시엔 샤딩만 믿고
  넘어갔고, 단일 대상 우편 명령이 남의 샤드를 읽는 경합이 남아 있었다).
  툴 쪽은 Blazor Web App + Minimal API +
  SqlKata/Microsoft.Data.SqlClient 구성.
  - **우편은 새 패킷을 만들지 않는다**: 기존 `PacketId::C2ZMailAdd`/`C2ZMailDel`을
    `ClientEnvelopeHeader`로 감싸 `W2ZRelay`으로 주입한다. 존 입장에서는 클라이언트가
    직접 보낸 것과 바이트 단위로 구분이 안 되므로, Mail/`UnitOfWork`/DB 레인 경로가 그대로
    재사용된다(운영 전용 우회로를 만들면 "운영툴 우편만 만료가 안 되는" 사고가 난다).
  - **대량 쿠폰 발급**: 캠페인 코드 5자리를 네임스페이스로 써서 중복 검사 범위를 캠페인
    하나로 가두고(전역 유일성 검사는 그 5자리뿐), 캠페인마다 전용 테이블(`coupon_<코드>`)을
    동적 생성한다. 생성·중복검사는 툴 프로세스 메모리에서 끝내고, 청크마다 스풀 파일
    append → SqlBulkCopy 적재 → 스풀 `fsync` + 오프셋 기록 순으로 진행한다.
    **실측(100만 장, 청크 1만×100): Release 11.5초(87,123장/초, World 전송 포함) /
    10.5초(95,066장/초, World 전송 제외) / Debug 12.3초(81,453장/초). 세 번 모두 중복
    재시도 0건, DB 100만 건 전부 고유.** Debug↔Release 차이가 15% 미만이라는 것이 핵심
    정보다 — 병목이 코드 생성이 아니라 I/O(SqlBulkCopy 적재 + 스풀 fsync)라는 뜻이고,
    더 줄이려면 청크 크기나 인서트 방식(`LOAD DATA INFILE` 등)을 손봐야 한다.
    마지막 측정은 WorldServer를 일부러 죽인 채 돌렸는데 발급이 정상 완료됐다 —
    World 전송이 부가 경로라는 설계가 의도대로 동작함을 확인.
  - 검증: 서버 3종 + 운영툴을 실제로 띄워 공지·우편 발송/삭제·쿠폰 발급/등록을 왕복시켰고,
    `ProtocolClient`가 `W2CNotice`와 우편 결과(당시 `Z2CMailAddAck`/`Z2CMailDelAck`, 지금은
    `Z2CTaskResult`)를 실제로 수신하는 것까지 확인했다. 툴 자체는 xUnit 77개.
- **패킷 id 통합/네이밍**(2026-09-07): 링크별로 흩어져 있던 4개 enum(`Zone::PacketId`,
  `GatewayLinkPacketId`, `ZoneLinkPacketId`, `ToolLinkPacketId`)이 전부 1번부터 값을 쓰고
  있어서, 같은 숫자가 링크마다 다른 뜻이었다. `Shared/Common/Src/PacketId.h`의
  **`Common::PacketId` 하나**로 합치고 이름 앞 3글자를 발신→수신 방향으로 고정했다
  (`C2ZMove`, `W2TCommandResult`). 방향마다 1000 단위로 대역을 잘라 **값 하나로 어느
  소켓의 패킷인지 판정**된다 — 규약 원문은 `.claude/rules/packet-naming.md`.
  - 부수로 갈라진 것: 요청과 응답이 id를 공유하던 `C2ZEcho`/`C2ZMove`/`C2ZChat`이 분리됐고
    (`C2ZEcho`/`Z2CEchoAck` 등), `Z2CMoveNotify` 본문 앞에 `sessionId(uint32)`가 붙었다
    (그전에는 브로드캐스트에 누가 움직였는지가 없었다 — **와이어 포맷 변경**).
  - `Session::SendPacket`/`Packet::BuildFrame`에 `std::is_enum_v` 제약의 enum 오버로드를
    얹어 호출부 `static_cast` 38곳을 없앴다(Core는 여전히 콘텐츠를 모른다).
  - 운영툴은 자기 대역(`T2W`/`W2T`)만 선언한다 — 클라이언트 패킷을 참고용으로 미러링하던
    `ZoneClientPacketId.cs`는 삭제(테스트 3건도 함께, 80→77개).
  - 검증: Debug/Release 클린 리빌드 에러·경고 0, GmTool 테스트 77개 통과, 서버 3종을 띄워
    `ProtocolClient`로 Echo/Move/Chat/Mail/존 핸드오프 왕복까지 실제 확인.
- **UnitOfWork 트랜잭션화 + 클라이언트 동기화**(2026-09-08): 기록만 하고 스코프 끝에
  내보내던 `Task::UnitOfWork`에 **실패/롤백/대상 구분**을 넣었다.
  - `Task::UnitOfWork`는 기반 클래스가 되고, 콘텐츠가 아는 일(전송·역연산)은
    `Zone::UnitOfWork`가 맡는다. 내보내기는 소멸자가 아니라 **명시적 `Commit()`**이다 —
    기반 소멸자 시점에는 파생이 이미 파괴돼 가상 함수가 파생 구현으로 불리지 않기 때문.
    소멸자는 "Commit 없이 소멸"을 잡는 안전망만 맡는다(예외 전파 중이면 죽이지 않는다).
  - 롤백은 기록해둔 태스크를 **역순으로 되짚어 역연산**(Added↔Removed)을 부르는 방식이라,
    태스크 페이로드에 역연산에 필요한 정보가 들어 있어야 한다는 규칙이 생겼다. 역연산은
    모델의 `Undo*` 전용 함수를 쓴다(롤백 중 재기록 금지).
  - 태스크에 대상(`Db`/`Client`/`Both`)이 붙었다. 성공하면 같은 목록이 World와 클라이언트로
    나가고, 클라이언트는 `Z2CTaskResult`로 받아 자기 메모리에 적용한다 — 콘텐츠마다 Ack를
    새로 만들던 `Z2CMailAddAck`/`Z2CMailDelAck`은 폐기됐다(**와이어 포맷 변경**).
  - `ZoneWorld` → `Instance` 리네임(WorldServer와 헷갈렸다), 콘텐츠 에러 코드는
    `Common::EErrorCode`로 분리(Core의 것은 `Base::ECoreErrorCode`), taskKind는
    `Common::TaskKind.h`에서 **상위 8비트 카테고리 + 하위 8비트 세부 동작**으로 인코딩한다.
  - 검증: Debug 빌드 에러·경고 0, 서버 3종을 띄워 `ProtocolClient`로 mail add/del 왕복과
    없는 mailId 삭제 시 `error=100(MailNotFound)` 응답까지 확인, `StressClient` 20세션×5사이클
    100/100 완료(불일치 0).
- **시각 클라이언트(`Client`, C#/MonoGame)**(2026-09-08): 존 이동·채팅·우편·쿠폰을
  한 창에서 눈으로 확인하는 클라이언트. **서버 C++ 코드는 한 줄도 고치지 않았다** — 기존
  프로토콜과 이미 있는 쿠폰 API만 쓴다. GmTool과 같은 이유로 별도 솔루션
  (`Client/Client.slnx`).
  - **코덱은 GmTool.Core의 `BinaryPacketWriter`/`Reader`를 복사**해 왔다(`Int32`/`Single`
    메서드만 추가). 프로젝트 참조로 엮지 않은 이유: 두 도구가 쓰는 링크가 겹치지 않고
    (운영툴 T2W/W2T vs 이쪽 C2Z/Z2C/W2C) 솔루션도 따로라, 참조로 묶어 GmTool 빌드에 이
    클라이언트를 끌고 들어오는 값이 복사 비용보다 크지 않다. 공유 표면은 이 2개뿐이다.
  - **쿠폰만 소켓이 아니라 HTTP**다. 쿠폰 등록은 클라이언트 패킷 대역에 아예 없고, 대신
    GmTool.Web의 `POST /api/coupon/redeem`(운영자 토큰 없이 열려 있는 유일한 엔드포인트 —
    게임 사용자용으로 설계된 자리)이 이미 사용 처리와 **보상 우편 발송**까지 한다. 보상은
    HTTP 응답이 아니라 `T2WMailSend` → 존 → `Z2CTaskResult` 경로로 소켓으로 온다.
    C++ 서버에 다시 만들지 않은 이유: World에 DB 연동부터 새로 해야 한다(3절 2번 항목).
  - **한글 글리프는 런타임에 GDI+로 굽는다**(`Src/Text/GlyphAtlas.cs`). SpriteFont로 한글을
    넣으려면 U+AC00~U+D7A3(11172자)을 통째로 선언해야 해서 텍스처가 감당이 안 된다. 실제로
    쓰인 글자만 아틀라스에 채우는 방식이라 `.mgcb` 콘텐츠 파이프라인 자체를 프로젝트에서
    뺐다. 그래서 `TargetFramework`가 `net10.0-windows`다.
  - **한계 두 개는 화면에 드러내 뒀다**: ① MonoGame이 IME 조합 문자를 받아오지 못해 한글
    입력이 안 된다 — 입력칸은 ASCII만 받고 한글 왕복은 채팅 패널의 프리셋 버튼으로 확인한다
    (수신 표시는 정상). ② 존을 넘어가면 서버가 우편함을 버리므로 클라이언트도 목록을 비우고
    그 이유를 배너로 띄운다(존 간 우편 이관 미구현).
  - **만들면서 드러난 프로토콜 공백 2개를 클라이언트가 메우고 있다** — 서버를 고치지 않기로
    했으니 클라이언트 쪽 우회이고, 제대로 고칠 자리는 서버다:
    - **입장 시 플레이어 스냅샷이 없다.** `BroadcastToZone`은 그 순간 `players_`에 있는
      사람에게만 보내는데 "입장 시 존 안의 플레이어 목록"을 주는 패킷이 없다. 그래서 나중에
      들어온 창은 이미 있던 플레이어가 **움직일 때까지** 그 존재를 알 수 없다(창 2개를 띄우고
      한쪽을 가만히 두면 다른 쪽에서 아예 안 보인다 — 실제로 이 증상으로 발견했다). 클라이언트가
      이동이 없어도 1초마다 좌표를 다시 보내 메운다(`PositionHeartbeatInterval`).
      **제대로 하려면 `Z2CEnterZoneNotify`(또는 별도 패킷)에 존 안의 플레이어 스냅샷이 실려야 한다.**
    - **퇴장 통지가 없다.** 서버는 `W2ZLeaveZone`로 자기 상태에서만 지우고 같은 존의
      남은 사람들에게 알리지 않는다. 위 하트비트가 끊긴 것으로 퇴장을 추정해 6초 뒤 목록에서
      지운다(`WorldModel.ForgetStalePlayers`). 안 지우면 끊은 창이 화면에 영원히 남는다.
  - 존 경계 좌표는 서버가 안 알려준다(`Z2CEnterZoneNotify`는 playerId+zoneId뿐). 서버
    `main.cpp`의 `ParseZoneList` 규칙("각 존은 10칸 폭")을 `Src/Protocol/ZoneLayout.cs`가
    복제한다 — **한쪽만 고치면 화면의 경계와 실제 핸드오프 지점이 어긋난다.**
  - 쿠폰의 `clientSessionId`로는 `playerId`(uint32)를 그대로 넘긴다. 서버의 `playerId`가
    `static_cast<uint32_t>(clientSessionId)`이고 `Listener`의 세션 id가 1부터 증가하는
    카운터라 상위 32비트가 0이기 때문이다 — **세션 id 발급 방식을 바꾸면 깨지는 전제**다.
  - 검증: 빌드 경고 0. 서버 3종 + GmTool.Web + SQL Server를 실제로 띄우고, Client의
    `Protocol`/`Net`/`Model` 소스를 그대로 링크한 헤드리스 하네스로 왕복을 확인했다 —
    존 입장(playerId/zoneId), Echo RTT 30.6ms, 한글 채팅 왕복, Move 후 서버 확정 좌표,
    MailAdd의 `Z2CTaskResult` 스트림 파싱(한글 제목/본문 정상), 없는 mailId 삭제 시
    `MailNotFound`, 실제 삭제 시 Removed 태스크 적용, x=12로 이동해 zoneId 0→1 핸드오프.
    쿠폰은 캠페인 생성 → 5장 발급 → 등록 성공 → **보상 우편이 소켓으로 도착** → 같은 쿠폰
    재등록 실패(maxUseCount=1)까지 확인. 창 실행은 15초간 크래시 없이 렌더 루프가 돌았다.
- **UnitOfWork 재구성 + Player 모델 컨테이너 + 재화**(2026-09-09): 롤백이 한 번도 실제로
  밟히지 않던 구조를 실측 가능한 형태로 바꿨다.
  - **커밋을 명시적 `Commit()`에서 파생 클래스 소멸자로** 옮겼다. 기반 소멸자에서는 가상
    함수가 파생 구현으로 불리지 않는 게 문제였는데, 커밋을 파생 소멸자에 두고 파생을
    `final`로 닫으면 그 상황 자체가 성립하지 않는다. 덕분에 "커밋을 깜빡한다"는 실수도
    없어져서 미커밋 감지용 `abort` 안전망을 걷어냈다.
  - **기록을 직렬화된 바이트에서 `Task::ITask` 객체로** 바꿨다. 실패로 끝나는 요청이 직렬화
    비용을 내지 않고, 롤백이 바이트를 다시 파싱하지 않는다. 각 태스크가 자기 역연산을 알아서
    **롤백 분기 `switch`가 사라졌다** — 새 태스크에 롤백 구현을 빼먹으면 순수 가상 때문에
    컴파일이 안 된다(전에는 런타임 로그였다).
  - 롤백 전용 `Undo*` 함수를 없애고 **전송 기능이 없는 `Task::RollbackUnitOfWork`**를 넘겨
    정상 함수를 재사용한다. 모델 함수는 `[[nodiscard]] EErrorCode`를 반환하고 호출부가
    `SetError`로 옮긴다 — 이 속성이 실제로 롤백 두 곳에서 반환값을 버린 걸 잡아냈다.
  - `ETaskTarget`(Db/Client 구분)을 **제거**했다. DB에 저장하는 변경을 클라이언트에 알리지
    않으면 그게 곧 불일치라 구분할 이유가 없었다(**와이어 포맷 변경**).
  - `PlayerState` → **`Zone::Player`**로 바꾸고 그 사람의 모델들을 여기 모았다. 우편함만
    `Mutexed` 핸들이고 재화는 값으로 직접 든다 — 모델마다 실제 접근 스레드 수에 맞춘다.
    `Registry`는 지우지 않고 **만료 스윕용 색인**으로 남겼다(지우면 `players_`에 락을
    걸어 "존 상태는 락 없음"을 깨거나, 스윕을 BASIC으로 옮겨 `Mutexed`가 필요한 자리를
    없애야 했다).
  - **`Base::Ruid`**: 요청 하나를 가리키는 `int64`(밀리초 41 + 노드 10 +
    시퀀스 12비트, 기준 시각 2026-01-01). 랜덤 GUID를 쓰지 않은 이유는 `BIGINT`가 signed고
    무작위 키가 클러스터드 인덱스 페이지 분할을 만들기 때문이다. 노드 번호는 담당 존 중
    가장 작은 zoneId를 그대로 쓴다(별도 설정 없음).
  - **`C2ZMailBuy`**(우편 지급 + 골드 차감)를 추가했다. 모델 두 개에 걸친 트랜잭션이라
    이 프로젝트에서 롤백이 실제로 밟히는 첫 경로다.
  - 검증: Debug 빌드 에러·경고 0. 서버 4개(World/Zone 1,2/Zone 3,4/Gateway)를 띄우고
    `ProtocolClient`로 실측 — `mail buy` 성공 시 한 응답에 태스크 2건(Mail Added +
    Currency 1000→900), 가격 999999로 재요청하면 `error=200(NotEnoughCurrency)`에 태스크 0건,
    그 뒤 `mail add`가 mailId=4를 받는다(3은 롤백된 우편이 태운 번호). Zone 로그에
    `UnitOfWork 실패로 롤백 . RequestPacketId : 6, ErrorCode : 200` 확인, 롤백 실패 로그는
    없음. 같은 명령을 연달아 보내면 `requestId`가 매번 다르게 돌아온다.
  - 회귀: `StressClient` 20세션×5사이클 100/100 완료(불일치 0, 스톨 0). 처음 돌렸을 때 전
    세션이 스톨했는데, `Z2CTaskResult`에 `requestId`(8바이트)가 추가된 걸 부하 도구가 읽지
    않아 스트림 오프셋이 밀린 것이었다 -- **와이어 포맷을 바꾸면 세 클라이언트(ProtocolClient/
    StressClient/Client)를 모두 확인해야 한다**는 신호다. Client(C#)도 같은
    파싱을 고쳤고 경고 0으로 빌드된다 -- 재화 태스크는 아직 모르는 kind로 건너뛰지만, 길이
    프리픽스 덕에 나머지 태스크는 정상 적용된다.

## 2. 코딩 컨벤션 (요약, 자세한 근거는 `.claude/rules/cpp-patterns.md`)

- 네임스페이스 PascalCase, `Core::` 접두사 없음, 멤버 변수 trailing underscore(POD 구조체
  public 필드는 예외), 주석은 전부 한글로 "왜"를 설명, C++23 적극 사용, 빌드는 CMake 아닌
  `.vcxproj`/`.slnx`.
- 타입 이름은 **그 타입이 하는 일을 설명하는 이름**으로 짓는다. 일반에 통용되는 패턴 이름
  (`UnitOfWork`, `Mutexed` 등)이 있으면 그걸 쓰고, 기능과 무관한 별칭은 만들지 않는다.

## 3. 다음에 하면 좋을 일

1. **부하 병목 수정 진행** — 10,000세션 부하 테스트에서 나온 처리량 병목
   수정(우선순위: 인구를 여러 존에 분산 배정 → World Basic 그룹의 브로드캐스트 릴레이 큐 분리 →
   팬아웃 프레임 배칭). 수정 후 같은 시나리오로 재검증.
1-B. **로그인 + World 콘텐츠 캐시**

   ```
   3. [완료] EProcessorId::Login + LoginProcessor + C2WLogin / W2CLogin + EErrorCode 300 대역
             자동 가입(계정이 없으면 RUID 발급 -> usp_players_upsert)까지 포함
   4. [완료] World 플레이어 콘텐츠 캐시 (PlayerManager 의 PlayerInfo.mails / .currencies)
             로그인 때 usp_players_load 로 적재, 접속 종료 때 함께 폐기
             W2ZEnterZone 이 가변 길이가 되어 존까지 실어 보낸다
   5. [남음] playerId -> clientSessionId 색인 (중복 로그인, 검사+삽입이 원자적이어야 함)
   6. [완료] 존이 올린 UnitOfWork 를 BASIC 레인에서 캐시에 반영 + playerId 를 주인으로 SP 호출
             [남음] 캐시와 대조해 위조를 걸러내는 단계
   7. [완료] player_id int64 확대 -- 와이어(PlayerZoneStatePacket / Z2CEnterZoneNotify)까지
             [남음] StressClient 로그인 대응
   ```

   **6번이 열리면서 두 가지가 같이 풀렸다.** `Z2WUnitOfWorkStream` 이 BASIC 을 건너뛰고 DB
   그룹으로 직행하던 것을 다른 패킷과 같은 레인(owner = `clientSessionId`)으로 돌렸다. 덕분에
   (1) 캐시가 로그인 시점 스냅샷에서 풀려 존에서 만든 우편이 프로세스를 넘어도 살아남고,
   (2) 그 레인이 접속 종료와 같은 strand 라 "이미 지워진 사람의 캐시를 되살리는" 순서 역전이
   없다. DB 작업만 `playerId` 를 주인으로 DB 레인에 넘긴다 -- 계정 단위로 직렬화해야 재접속
   해도 쓰기 순서가 유지되기 때문이다. **남은 것은 대조 검증**이다(캐시에 없는 우편을 지웠다는
   태스크를 걸러내는 자리).

   그 과정에서 **프로세스를 넘는 핸드오프에 퇴장 통지가 없던 것**도 같이 고쳤다. 원본
   프로세스에 `Player` 와 우편함이 유령으로 남아 만료 스윕이 같은 우편을 두 번 지우고 있었다
   (삭제 태스크의 RUID 노드 번호가 서로 다른 두 프로세스를 가리켜 확인했다). World 가
   `W2ZEnterZone` 과 짝으로 원본 링크에 `W2ZLeaveZone` 을 보낸다 -- 같은 프로세스 안의 이동은
   살아 있는 `Player` 를 그대로 옮기므로 보내지 않는다(판정 기준은 zoneId 가 아니라 링크 세션).

   **7번이 열렸다**: `player_id` 가 `uint32_t` -> `Common::PlayerId`(int64) 가 되면서 와이어
   포맷이 바뀌었다(`PlayerZoneStatePacket` / `Z2CEnterZoneNotify`). Zone `Player` -> 프로토콜
   -> `Client`(C#) / `ProtocolClient` / `StressClient` 를 한 커밋에 같이 고쳤다.

   그 과정에서 `Z2CEnterZoneNotify` 에 `clientSessionId` 가 추가됐다. 예전에는 playerId 가
   세션 id 를 uint32 로 자른 값이라 **하나가 두 역할을 겸하고 있었고**, C# 클라이언트가 그걸
   브로드캐스트(Move/Chat)의 발신자 키와 대조하는 데 쓰고 있었다. 진짜 계정 키를 싣게 되면서
   둘이 갈라져, 클라이언트가 자기 세션 id 를 알 방법이 따로 필요해졌다.

   **`StressClient` 는 현재 동작하지 않는다** -- 로그인을 보내지 않아 존 입장이 막힌다.
   7번에서 같이 고친다. 시드에 `stress_00001~20000` 이 이미 있으므로 세션 index 로 이름만
   조립하면 된다. (`ProtocolClient` 는 `login` 명령과 `--id=`/`--pw=` 인자가 붙어 동작한다.)

1-C. **strand 세분화** (부하 측정 뒤)

   `Processor::Group` 이 `asio::io_context` + `strand` 로 바뀌면서 strand 개수를 owner 마다
   둘 수 있게 됐다(예전 구조에서는 큐 = 스레드라 불가능했다). 지금은 스레드 수와 같아
   `owner % N` 이 겹치는 남남끼리 서로를 막는다.

   **바꾸기 전에 부하 수치를 먼저 남긴다** — 그래야 왜 바꿨는지가 숫자로 남는다.
   그러려면 `StressClient` 가 먼저 살아야 하므로 1-B 7번 뒤다.

2. **남은 DB 연동**: 스키마와 계층은 준비됐고, Zone의 `UnitOfWork` 태스크를 실제 SP로
   흘리는 것과 쿠폰 청크(`CouponChunkPush`) 적재가 남았다. 운영툴을 SQL
   Server로 옮겨둔 이유가 이것이다: C++에서는 ODBC(`<sql.h>` + `odbc32.lib`)가 Windows SDK
   내장이라 `3rd/`에 바이너리 의존성이 늘지 않는다. 엔진을 맞춰두면 쿠폰
   청크(`CouponChunkPush`)와 Mail `UnitOfWork` 태스크를 같은 워커에서 실제로 적재할 수 있다.
   (운영툴 쪽 스키마: `Tool/GmTool/Sql/schema.sql`)

   같이 정할 것: 게임 스키마와 공유 저장 프로시저는 `Shared/Sql/`에 둔다 — `Tool/` 아래
   두면 서버가 툴을 의존하는 역방향이 된다(`Core`를 `Shared/`에 둔 것과 같은 이유). 그리고
   접속 중인 플레이어의 권위 상태는 메모리(`Instance`/`Model`)에 있으므로, 운영툴이
   게임 데이터를 **직접 UPDATE하는 경로는 두지 않는다**(오프라인 대상만 직접, 온라인 대상은
   9300 경유).
3. **AOI/몬스터**: 존 내부를 그리드로 나눠 "가까운 플레이어에게만" 브로드캐스트하도록
   확장, 몬스터(NPC)와 간단한 FSM 추가.

   **이때 같이 정리할 것 — 존 입장 스냅샷과 퇴장 통지.** `Client`를 만들면서 드러났다:
   브로드캐스트는 그 순간 존에 있는 사람에게만 가고, 입장할 때 기존 플레이어 목록을 주는
   패킷도 누가 나갔는지 알리는 패킷도 없다. 지금은 클라이언트가 좌표 하트비트(1초)와
   타임아웃(6초)으로 메우고 있지만, 서버가 `Z2CEnterZoneNotify`에 존 안의 플레이어 스냅샷을
   싣고 퇴장을 브로드캐스트하는 쪽이 맞다. AOI를 넣으면 "누가 내 시야에 들어왔나/나갔나"를
   어차피 서버가 판단해야 하므로 그 작업과 한 세트다.

   **존 배치를 CSV 데이터로 빼기 — 같은 자리에서 정리할 후보.** 2차원 격자까지는 구현됐고
   (`Def`가 담당 사각형을 갖고, `ZoneRegisterPacket`으로 World에 알리고, World는
   `FindZoneContaining(x, y)`로 라우팅한다), 남은 것은 **좌표를 코드에서 데이터로 빼는 것**이다.
   지금은 `ParseZoneList`가 `kZoneSize`/`kZonesPerRow`/`kZoneRows`로 계산하므로 존이 전부
   같은 크기의 균일 격자여야 한다. CSV(`zoneId, xMin, xMax, yMin, yMax`)로 읽으면 크기·위치가
   불규칙한 배치도 되고, 프로세스에는 담당 zoneId만 주면 된다. **고칠 자리는 `ParseZoneList`
   하나**다 — 아래 계층은 이미 `Def`를 그대로 들고 다녀서 배치 규칙을 모른다.

   같이 처리해야 하는 것:
   - **월드에 구멍이 생기는 경우.** 배치가 불규칙해지면 어느 존도 담당하지 않는 좌표가 나온다.
     `HandleMove`는 경계를 넘을 때 자기 상태에서 먼저 지운 뒤 World에 넘기므로, 담당 존을 못
     찾으면 플레이어가 아무 존에도 없는 상태가 된다. 지금은 World가 이 경우 **원래 존 안쪽으로
     좌표를 보정해 되돌려 넣는다**(`ZoneLinkHandler::ReturnToSourceZone`) — 균일 격자에서는
     월드 밖으로 나가려는 경우뿐이라 이걸로 충분하지만, 배치가 불규칙해지면 "이동 실패"를
     클라이언트에 알려주는 쪽이 맞다(지금은 좌표가 조용히 되돌아간다).
   - **클라이언트가 규칙을 복제하는 방식이 성립하지 않는다.** 균일 격자라는 규칙 자체가
     없어지므로, CSV를 클라이언트에도 배포하거나 서버가 존 경계를 패킷으로 알려줘야 한다.
     후자가 맞고, 위의 입장 스냅샷 작업과 자리가 같다.
4. **자동화 테스트**: C++ 쪽은 여전히 `ProtocolClient`/`StressClient` 수동·부하 확인뿐 —
   회귀 방지용 자동화 스위트가 없다. 운영툴은 xUnit 77개가 붙어 있으니(`cd Tool\GmTool &&
   dotnet test`), 같은 방식으로 C++ 쪽에도 최소한 패킷 코덱/프레이밍 단위 테스트부터
   붙이는 게 다음 후보다.
5. **운영툴 기능 추가 — 지금은 보류.** `Tool/GmTool`은 서버 기능을 검증하기 위한 도구이고,
   기능 개발은 중단한 상태다(코드 정리와 문서 정합성만 손댄다). 역할 검사, 운영자 계정 관리
   화면, 비밀 관리, 쿠폰 발급 재개(스풀 오프셋을 읽어 이어 적재)가 후보로 남아 있지만 지금
   범위 밖이다 — 근거는 `Tool/GmTool/README.md`의 "지금 범위 밖으로 둔 것". 나중에 필요해지면
   그때 진행한다.
6. **README 성능 표 채우기**: 지연 백분위 계측을 붙였으므로 1,000세션·10,000세션 시나리오를
   다시 돌려 `README.md` 5절의 P50/P95/P99 칸을 실측치로 교체한다(현재 "재측정 중" 상태).
7. **프로토콜 코드젠(yml -> Gen 파일) — 방향만 정해두고 보류.** 지금 패킷 id·에러 코드·
   존 배치 상수가 **C++ 헤더와 C# 두 곳에 손으로 복제**돼 있고, 지키는 수단이 "C++ 와 이름·값이
   1:1 로 같아야 한다"는 주석뿐이다. 이미 어긋나 있다 — `PacketId.h` 의 `C2ZMailBuy = 6` 이
   `Client` 에도 `GmTool.Core` 에도 없다(아직 안 써서 드러나지 않았을 뿐이다).
   `ZoneLayout.cs` 와 `ParseZoneList` 의 `kZoneSize`/`kZonesPerRow`/`kZoneRows` 도 같은 상태다.

   **정한 방향**: 정의 파일(yml)을 원본으로 두고 **C++ 헤더까지 포함해 전부 생성물로** 만든다.
   C++ 을 원본으로 두고 C# 만 생성하면 언어 하나가 특별해지고 파서가 헤더 문법에 얽매인다.

   ```
   Shared/Common/Def/*.yml   (packet_id / error_code / task_kind / currency_type / zone_layout)
             |
             v   bat\gen_protocol.bat
   Shared/Common/Src/*.g.h  +  Client/Src/Protocol/*.g.cs  +  GmTool.Core/Protocol/*.g.cs
   ```

   - **범위는 enum·상수까지**다. 패킷 **본문**(필드 레이아웃과 Read/Write)은 계속 손으로 쓴다 --
     정의 파일에서 직렬화 코드까지 뽑는 방식은 `ZonePackets.h`/`ClientPackets.h` 를 통째로
     옮겨야 해서 비용이 몇 배다. 필요해지면 그때 올린다.
   - 생성기는 **.NET 10 파일 기반 앱**(csproj 없이 `dotnet run Foo.cs`)으로 둔다. 새 솔루션도
     NuGet 복원도 안 생겨서 "C++ 솔루션에 NuGet 을 끌어들이지 않는다"를 안 건드린다.
   - **생성 결과물은 커밋한다.** clone 한 사람이 bat 을 돌리지 않아도 빌드되고, diff 에서
     패킷 변경이 눈에 보인다.
   - **같이 옮겨야 하는 것**: `PacketId.h` 가 생성물이 되면 지금 거기 있는 규약 주석(방향 3글자,
     1000 단위 대역, 폐기 번호 재사용 금지, `G2WClientConnected` 가 왜 C2W 가 아닌지)이 yml
     맨 위로 가야 한다. `.claude/rules/packet-naming.md` 와 `CLAUDE.md` 가 `PacketId.h` 를
     가리키고 있으므로 같은 커밋에서 고친다.

   **순서**: 부하 병목 수정(1번)과 로그인 잔여분(1-B)보다 뒤다. 다만 패킷이 더 늘기 전에
   하는 게 이사 비용이 싸다 -- 3번(AOI/몬스터)에서 패킷이 여러 개 추가되므로 **그 앞**이 좋다.
   3번의 "존 배치를 CSV 데이터로 빼기"와도 자리가 겹친다(`zone_layout.yml` 이 그 입구다).
