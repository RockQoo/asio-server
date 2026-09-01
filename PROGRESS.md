# 진행 상황 정리 (다음 세션 이어하기용)

마지막 업데이트: 2026-09-02

`README.md`(소개용)와 별개로, 다음 세션에서 빠르게 컨텍스트를 복구하기 위한 문서. 자세한
아키텍처/타입 표는 `CLAUDE.md`, 코딩 규약은 `.claude/rules/`를 우선 참고 — 여기는 "지금
뭐가 되어 있고 다음에 뭘 하면 되는가"만 압축해서 담는다.

---

## 1. 지금까지 된 것

- **4계층 분산 구조**: Client → GatewayServer(순수 릴레이) → WorldServer(라우팅+DB워커) →
  ZoneServer(존 상태). Gateway/World는 이번에 새로 추가된 프로젝트.
- **존 핸드오프**: 존 경계를 넘으면 World가 라우팅 테이블(`ClientRegistry`)만 바꾼다 —
  Gateway는 이동 자체를 모르고, 클라이언트는 EnterZoneNotify 통지만 받을 뿐 재접속 없이
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
  전송. `MailAddAck`/`MailDelAck`으로 클라이언트가 서버가 실제 배정한 mailId를 확인 가능.
- **부하 테스트 도구(`LoadTestClient`)**: `Core::Network::Connector`/`Session`을 그대로
  재사용해 세션당 스레드 없이 io_context 풀 하나로 최대 1만 소켓을 비동기 멀티플렉싱.
  Release 기준 1,000세션×200사이클은 18,337 사이클/초로 완전 통과(불일치 0, 스톨 0,
  브로드캐스트 100%, P95 40ms). 10,000세션은 데드락/데이터 불일치 0을 유지하면서도 처리량이
  471 사이클/초로 무너짐(P95 62초) — 원인·수정 계획은 `docs/load-test-fix-plan.md`.
- **지연(RTT) 백분위 계측**: `LoadTestClient`가 처리량뿐 아니라 P50/P95/P99/P99.9를 낸다.
  원시 샘플 대신 551개 로그스케일 버킷(`Stats/LatencyHistogram.h`)에 relaxed atomic으로
  기록 — 수백만 샘플에도 상수 메모리·O(1)이라 계측이 실험 자체를 방해하지 않는다. 재는
  구간은 `MailAdd→Ack`, `MailDel→Ack`, 사이클 전체 3종. 추적 상한은 100초(처음 10초로
  뒀다가 과부하 실험에서 P95/P99가 전부 상한에 몰려 구분이 안 돼 넓혔다).
- **`bat/server.bat`/`bat/client.bat`**: 전체 프로세스를 한 번에 띄우는 배치 파일.
- 새 기능 추가 시 `docs/flowcharts/`에 다이어그램을 같이 갱신하는 규칙이 실제로 잘 지켜지고
  있음(`zone-handoff-and-mail.html`, `testclient-echo-move-chat.html`).

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
2. **DB 연동**: `Db::DbWorker`가 지금은 로그만 남긴다 — 실제 DB(PostgreSQL 등) 붙이기.
3. **AOI/몬스터**: 존 내부를 그리드로 나눠 "가까운 플레이어에게만" 브로드캐스트하도록
   확장, 몬스터(NPC)와 간단한 FSM 추가.
4. **자동화 테스트**: 지금은 `TestClient`/`LoadTestClient` 수동·부하 확인뿐 — 회귀 방지용
   자동화 스위트는 없음.
5. **README 성능 표 채우기**: 지연 백분위 계측을 붙였으므로 1,000세션·10,000세션 시나리오를
   다시 돌려 `README.md` 5절의 P50/P95/P99 칸을 실측치로 교체한다(현재 "재측정 중" 상태).
