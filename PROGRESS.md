# 진행 상황 정리 (다음 세션 이어하기용)

마지막 업데이트: 2026-09-08

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
  팬아웃). 프로세스 1개가 존 여러 개를 동시에 호스팅 가능(`ZoneServer.exe 1,2`처럼 실행).
- **존 배치는 2×2 격자**(`1 2` / `3 4`, 각 존 10×10). 기본 기동이 **프로세스 2개**라 위쪽 행
  (존 1,2)과 아래쪽 행(존 3,4)을 나눠 담당하고, 그래서 **가로 이동은 스레드 간, 세로 이동은
  프로세스(TCP 링크)를 넘는 핸드오프**가 된다 — 두 경로를 한 화면에서 비교할 수 있다.
  **zoneId는 1부터 시작**하고 0은 "존 없음/미배정" 예약값이다 — 0을 유효 존으로 쓰면 "0번
  존에 있다"와 "아직 아무 존에도 없다"가 같은 값이 되어 구분할 수 없다.
  배치를 정하는 코드는 `ParseZoneList` 하나(`kZoneSize`/`kZonesPerRow`/`kZoneRows`)이고,
  그 아래 계층은 `ZoneDef`(담당 사각형)를 그대로 들고 다니므로 배치 규칙을 모른다.
- **WorldServer**: 단일 처리 스레드(`WorldWorker`)가 라우팅 상태(`ClientRegistry`/
  `ZoneLinkRegistry`)를 락 없이 소유. I/O 스레드는 `PostTask`로만 넘긴다. DB 워커 풀
  (`Db::DbWorker`, owner-hash)은 실제 쿼리 없이 로그만 남기는 스텁 상태.
- **Mail 시스템**: `MailModel`/`MailRegistry`/`MailExpiryService`(만료 자동삭제, 별도
  유지보수 타이머). `Threading::Synchronized`(Core, `.Write()->`=쓰기/`->`=읽기)가 "평소엔 락 없음,
  BASIC 스레드와 유지보수 타이머가 만나는 유일한 지점만 락"을 보여준다. 상태 변경은
  `Task::UnitOfWork`(범용 Unit-of-Work)에 기록했다가 `Commit()` 시점에 한 번에 내보낸다 —
  성공하면 같은 태스크 목록이 World(DB)와 클라이언트(`Z2CTaskResult`) 양쪽으로 가고,
  실패하면 역순으로 되짚어 메모리를 롤백한 뒤 에러 코드만 클라이언트로 간다.
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
- **`bat/start_server_all.bat`/`bat/start_protocol_client.bat`/`bat/start_visual_client.bat`**:
  전체 프로세스를 한 번에 띄우는 배치 파일. VisualClient는 별도 .NET 솔루션이라 bin/x64가 아닌
  자기 경로로 빌드되므로 `dotnet run`으로 띄운다(창 개수를 인자로 받는다 -- 브로드캐스트
  확인에는 최소 2개가 필요하다).
- 새 기능 추가 시 `docs/flowcharts/`에 다이어그램을 같이 갱신하는 규칙이 실제로 잘 지켜지고
  있음(`zone-handoff-and-mail.html`, `protocolclient-echo-move-chat.html`, `gmtool-operations.html`,
  `visualclient-screen-and-coupon.html`).
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
    `ProtocolClient`가 `W2CNotice`와 우편 결과(당시 `Z2CMailAddAck`/`Z2CMailDelAck`, 지금은
    `Z2CTaskResult`)를 실제로 수신하는 것까지 확인했다. 툴 자체는 xUnit 77개.
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
- **UnitOfWork 트랜잭션화 + 클라이언트 동기화**(2026-09-08): 기록만 하고 스코프 끝에
  내보내던 `Task::UnitOfWork`에 **실패/롤백/대상 구분**을 넣었다.
  - `Task::UnitOfWork`는 기반 클래스가 되고, 콘텐츠가 아는 일(전송·역연산)은
    `Zone::ZoneUnitOfWork`가 맡는다. 내보내기는 소멸자가 아니라 **명시적 `Commit()`**이다 —
    기반 소멸자 시점에는 파생이 이미 파괴돼 가상 함수가 파생 구현으로 불리지 않기 때문.
    소멸자는 "Commit 없이 소멸"을 잡는 안전망만 맡는다(예외 전파 중이면 죽이지 않는다).
  - 롤백은 기록해둔 태스크를 **역순으로 되짚어 역연산**(Added↔Removed)을 부르는 방식이라,
    태스크 페이로드에 역연산에 필요한 정보가 들어 있어야 한다는 규칙이 생겼다. 역연산은
    모델의 `Undo*` 전용 함수를 쓴다(롤백 중 재기록 금지).
  - 태스크에 대상(`Db`/`Client`/`Both`)이 붙었다. 성공하면 같은 목록이 World와 클라이언트로
    나가고, 클라이언트는 `Z2CTaskResult`로 받아 자기 메모리에 적용한다 — 콘텐츠마다 Ack를
    새로 만들던 `Z2CMailAddAck`/`Z2CMailDelAck`은 폐기됐다(**와이어 포맷 변경**).
  - `ZoneWorld` → `ZoneInstance` 리네임(WorldServer와 헷갈렸다), 콘텐츠 에러 코드는
    `Protocol::EErrorCode`로 분리(Core의 것은 `Common::ECoreErrorCode`), taskKind는
    `Protocol::TaskKind.h`에서 **상위 8비트 카테고리 + 하위 8비트 세부 동작**으로 인코딩한다.
  - 검증: Debug 빌드 에러·경고 0, 서버 3종을 띄워 `ProtocolClient`로 mail add/del 왕복과
    없는 mailId 삭제 시 `error=100(MailNotFound)` 응답까지 확인, `StressClient` 20세션×5사이클
    100/100 완료(불일치 0).
- **시각 클라이언트(`Tool/VisualClient`, C#/MonoGame)**(2026-09-08): 존 이동·채팅·우편·쿠폰을
  한 창에서 눈으로 확인하는 클라이언트. **서버 C++ 코드는 한 줄도 고치지 않았다** — 기존
  프로토콜과 이미 있는 쿠폰 API만 쓴다. GmTool과 같은 이유로 별도 솔루션
  (`Tool/VisualClient/VisualClient.slnx`).
  - **코덱은 GmTool.Core의 `BinaryPacketWriter`/`Reader`를 복사**해 왔다(`Int32`/`Single`
    메서드만 추가). 프로젝트 참조로 엮지 않은 이유: 두 도구가 쓰는 링크가 겹치지 않고
    (운영툴 T2W/W2T vs 이쪽 C2Z/Z2C/W2C) 솔루션도 따로라, 참조로 묶어 GmTool 빌드에 이
    클라이언트를 끌고 들어오는 값이 복사 비용보다 크지 않다. 공유 표면은 이 2개뿐이다.
  - **쿠폰만 소켓이 아니라 HTTP**다. 쿠폰 등록은 클라이언트 패킷 대역에 아예 없고, 대신
    GmTool.Web의 `POST /api/coupon/redeem`(운영자 토큰 없이 열려 있는 유일한 엔드포인트 —
    게임 사용자용으로 설계된 자리)이 이미 사용 처리와 **보상 우편 발송**까지 한다. 보상은
    HTTP 응답이 아니라 `T2WMailSendRequest` → 존 → `Z2CTaskResult` 경로로 소켓으로 온다.
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
    - **퇴장 통지가 없다.** 서버는 `W2ZLeaveZoneNotify`로 자기 상태에서만 지우고 같은 존의
      남은 사람들에게 알리지 않는다. 위 하트비트가 끊긴 것으로 퇴장을 추정해 6초 뒤 목록에서
      지운다(`WorldModel.ForgetStalePlayers`). 안 지우면 끊은 창이 화면에 영원히 남는다.
  - 존 경계 좌표는 서버가 안 알려준다(`Z2CEnterZoneNotify`는 playerId+zoneId뿐). 서버
    `main.cpp`의 `ParseZoneList` 규칙("각 존은 10칸 폭")을 `Src/Protocol/ZoneLayout.cs`가
    복제한다 — **한쪽만 고치면 화면의 경계와 실제 핸드오프 지점이 어긋난다.**
  - 쿠폰의 `clientSessionId`로는 `playerId`(uint32)를 그대로 넘긴다. 서버의 `playerId`가
    `static_cast<uint32_t>(clientSessionId)`이고 `Listener`의 세션 id가 1부터 증가하는
    카운터라 상위 32비트가 0이기 때문이다 — **세션 id 발급 방식을 바꾸면 깨지는 전제**다.
  - 검증: 빌드 경고 0. 서버 3종 + GmTool.Web + SQL Server를 실제로 띄우고, VisualClient의
    `Protocol`/`Net`/`Model` 소스를 그대로 링크한 헤드리스 하네스로 왕복을 확인했다 —
    존 입장(playerId/zoneId), Echo RTT 30.6ms, 한글 채팅 왕복, Move 후 서버 확정 좌표,
    MailAdd의 `Z2CTaskResult` 스트림 파싱(한글 제목/본문 정상), 없는 mailId 삭제 시
    `MailNotFound`, 실제 삭제 시 Removed 태스크 적용, x=12로 이동해 zoneId 0→1 핸드오프.
    쿠폰은 캠페인 생성 → 5장 발급 → 등록 성공 → **보상 우편이 소켓으로 도착** → 같은 쿠폰
    재등록 실패(maxUseCount=1)까지 확인. 창 실행은 15초간 크래시 없이 렌더 루프가 돌았다.

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

   **이때 같이 정리할 것 — 존 입장 스냅샷과 퇴장 통지.** `VisualClient`를 만들면서 드러났다:
   브로드캐스트는 그 순간 존에 있는 사람에게만 가고, 입장할 때 기존 플레이어 목록을 주는
   패킷도 누가 나갔는지 알리는 패킷도 없다. 지금은 클라이언트가 좌표 하트비트(1초)와
   타임아웃(6초)으로 메우고 있지만, 서버가 `Z2CEnterZoneNotify`에 존 안의 플레이어 스냅샷을
   싣고 퇴장을 브로드캐스트하는 쪽이 맞다. AOI를 넣으면 "누가 내 시야에 들어왔나/나갔나"를
   어차피 서버가 판단해야 하므로 그 작업과 한 세트다.

   **존 배치를 CSV 데이터로 빼기 — 같은 자리에서 정리할 후보.** 2차원 격자까지는 구현됐고
   (`ZoneDef`가 담당 사각형을 갖고, `ZoneRegisterPacket`으로 World에 알리고, World는
   `FindZoneContaining(x, y)`로 라우팅한다), 남은 것은 **좌표를 코드에서 데이터로 빼는 것**이다.
   지금은 `ParseZoneList`가 `kZoneSize`/`kZonesPerRow`/`kZoneRows`로 계산하므로 존이 전부
   같은 크기의 균일 격자여야 한다. CSV(`zoneId, xMin, xMax, yMin, yMax`)로 읽으면 크기·위치가
   불규칙한 배치도 되고, 프로세스에는 담당 zoneId만 주면 된다. **고칠 자리는 `ParseZoneList`
   하나**다 — 아래 계층은 이미 `ZoneDef`를 그대로 들고 다녀서 배치 규칙을 모른다.

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
