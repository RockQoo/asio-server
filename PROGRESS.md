# 진행 상황 정리 (다음 세션 이어하기용)

마지막 업데이트: 2026-09-07

`README.md`(소개용)와 별개로, 다음 세션에서 빠르게 컨텍스트를 복구하기 위한 문서. 자세한
아키텍처/타입 표는 `CLAUDE.md`, 코딩 규약은 `.claude/rules/`를 우선 참고 — 여기는 "지금
뭐가 되어 있고 다음에 뭘 하면 되는가"만 압축해서 담는다.

---

## 1. 지금까지 된 것

- **4계층 분산 구조**: Client → GatewayServer(순수 릴레이) → WorldServer(라우팅+DB워커) →
  ZoneServer(존 상태). Gateway/World는 이번에 새로 추가된 프로젝트.
- **존 핸드오프**: 존 경계를 넘으면 World가 라우팅 테이블(`ClientRegistry`)만 바꾼다 —
  Gateway는 이동 자체를 모르고, 클라이언트는 Z2CEnterZoneNotify 통지만 받을 뿐 재접속 없이
  같은 연결을 유지한다.
- **ZoneServer 5-풀 구조**: NETWORK(소켓 I/O) → LB(패킷 파싱, zoneId 판단) → BASIC(zoneId
  sticky, 게임 로직) → TICK(존별 주기 처리, 현재 placeholder) → BROADCAST(zoneId sticky,
  팬아웃). 프로세스 1개가 존 여러 개를 동시에 호스팅 가능(`ZoneServer.exe 0,1`처럼 실행).
- **WorldServer**: 단일 처리 스레드(`WorldWorker`)가 라우팅 상태(`ClientRegistry`/
  `ZoneLinkRegistry`)를 락 없이 소유. I/O 스레드는 `PostTask`로만 넘긴다. DB 워커 풀
  (`Db::DbWorker`, owner-hash)은 실제 쿼리 없이 로그만 남기는 스텁 상태.
- **Mail 시스템**: `MailModel`/`MailRegistry`/`MailExpiryService`(만료 자동삭제, 별도
  유지보수 타이머). `Threading::Synchronized`(Core, `.Write()->`=쓰기/`->`=읽기)가 "평소엔 락 없음,
  BASIC 스레드와 유지보수 타이머가 만나는 유일한 지점만 락"을 보여준다. 상태 변경은
  `Core::Task::UnitOfWork`(범용 Unit-of-Work)에 기록했다가 스코프 종료 시 한 번에 World로
  전송. `Z2CMailAddAck`/`Z2CMailDelAck`으로 클라이언트가 서버가 실제 배정한 mailId를 확인 가능.
- **부하 테스트 도구(`StressClient`)**: `Core::Network::Connector`/`Session`을 그대로
  재사용해 세션당 스레드 없이 io_context 풀 하나로 1만 소켓까지 비동기 멀티플렉싱.
  Release 기준 1,000세션×200사이클은 18,337 사이클/초로 완전 통과(불일치 0, 스톨 0,
  브로드캐스트 100%, P95 40ms). 10,000세션은 데드락/데이터 불일치 0을 유지하면서도 처리량이
  471 사이클/초로 무너짐(P95 62초) — 원인·수정 계획은 `docs/load-test-fix-plan.md`.
- **지연(RTT) 백분위 계측**: `StressClient`가 처리량뿐 아니라 P50/P95/P99/P99.9를 낸다.
  원시 샘플 대신 551개 로그스케일 버킷(`Stats/LatencyHistogram.h`)에 relaxed atomic으로
  기록 — 수백만 샘플에도 상수 메모리·O(1)이라 계측이 실험 자체를 방해하지 않는다. 재는
  구간은 `MailAdd→Ack`, `MailDel→Ack`, 사이클 전체 3종. 추적 상한은 100초(처음 10초로
  뒀다가 과부하 실험에서 P95/P99가 전부 상한에 몰려 구분이 안 돼 넓혔다).
- **`bat/start_server_all.bat`/`bat/start_protocol_client.bat`**: 전체 프로세스를 한 번에 띄우는 배치 파일.
- 새 기능 추가 시 `docs/flowcharts/`에 다이어그램을 같이 갱신하는 규칙이 실제로 잘 지켜지고
  있음(`zone-handoff-and-mail.html`, `protocolclient-echo-move-chat.html`, `gmtool-operations.html`).
- **운영툴(`Tool/GmTool`, C#/.NET 10/SQL Server)**: 서버 쪽은 WorldServer에 세 번째 accept
  포트(9300)와 `Tool/ToolProcessor`를 추가했다 — `GatewayLinkHandler`/`ZoneLinkHandler`와
  **같은 스레드 규약**(I/O 스레드는 바이트 복사만 → `WorldWorker::PostTask`)이라
  `authenticatedSessions_`에도 락이 없다. 툴 쪽은 Blazor Web App + Minimal API +
  SqlKata/Microsoft.Data.SqlClient 구성.
  - **우편은 새 패킷을 만들지 않는다**: 기존 `PacketId::C2ZMailAdd`/`C2ZMailDel`을
    `ClientEnvelopeHeader`로 감싸 `W2ZRelay`으로 주입한다. 존 입장에서는 클라이언트가
    직접 보낸 것과 바이트 단위로 구분이 안 되므로, Mail/`UnitOfWork`/DbWorker 경로가 그대로
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
    `ProtocolClient`가 `W2CNotice`/`Z2CMailAddAck`/`Z2CMailDelAck(success=true/false)`을 실제로 수신하는 것까지
    확인했다. 툴 자체는 xUnit 77개.
- **패킷 id 통합/네이밍**(2026-09-07): 링크별로 흩어져 있던 4개 enum(`Zone::PacketId`,
  `GatewayLinkPacketId`, `ZoneLinkPacketId`, `ToolLinkPacketId`)이 전부 1번부터 값을 쓰고
  있어서, 같은 숫자가 링크마다 다른 뜻이었다. `Shared/Protocol/Src/PacketId.h`의
  **`Protocol::PacketId` 하나**로 합치고 이름 앞 3글자를 발신→수신 방향으로 고정했다
  (`C2ZMove`, `W2TToolCommandAck`). 방향마다 1000 단위로 대역을 잘라 **값 하나로 어느
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

## 2. 코딩 컨벤션 (요약, 자세한 근거는 `.claude/rules/cpp-patterns.md`)

- 네임스페이스 PascalCase, `Core::` 접두사 없음, 멤버 변수 trailing underscore(POD 구조체
  public 필드는 예외), 주석은 전부 한글로 "왜"를 설명, C++20 적극 사용, 빌드는 CMake 아닌
  `.vcxproj`/`.slnx`.
- 타입 이름은 **그 타입이 하는 일을 설명하는 이름**으로 짓는다. 일반에 통용되는 패턴 이름
  (`UnitOfWork`, `Synchronized` 등)이 있으면 그걸 쓰고, 기능과 무관한 별칭은 만들지 않는다.

## 3. 다음에 하면 좋을 일

1. **`docs/load-test-fix-plan.md` 진행** — 10,000세션 부하 테스트에서 나온 처리량 병목
   수정(우선순위: 인구를 여러 존에 분산 배정 → WorldWorker 브로드캐스트 릴레이 큐 분리 →
   팬아웃 프레임 배칭). 수정 후 같은 시나리오로 재검증.
2. **DB 연동**: `Db::DbWorker`가 지금은 로그만 남긴다 — 실제 DB 붙이기. 운영툴을 SQL
   Server로 옮겨둔 이유가 이것이다: C++에서는 ODBC(`<sql.h>` + `odbc32.lib`)가 Windows SDK
   내장이라 `3rd/`에 바이너리 의존성이 늘지 않는다. 엔진을 맞춰두면 쿠폰
   청크(`CouponChunkPush`)와 Mail `UnitOfWork` 태스크를 같은 워커에서 실제로 적재할 수 있다.
   (운영툴 쪽 스키마: `Tool/GmTool/Sql/schema.sql`)

   같이 정할 것: 게임 스키마와 공유 저장 프로시저는 `Shared/Sql/`에 둔다 — `Tool/` 아래
   두면 서버가 툴을 의존하는 역방향이 된다(`Core`를 `Shared/`에 둔 것과 같은 이유). 그리고
   접속 중인 플레이어의 권위 상태는 메모리(`ZoneInstance`/`MailModel`)에 있으므로, 운영툴이
   게임 데이터를 **직접 UPDATE하는 경로는 두지 않는다**(오프라인 대상만 직접, 온라인 대상은
   9300 경유).
3. **AOI/몬스터**: 존 내부를 그리드로 나눠 "가까운 플레이어에게만" 브로드캐스트하도록
   확장, 몬스터(NPC)와 간단한 FSM 추가.
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
