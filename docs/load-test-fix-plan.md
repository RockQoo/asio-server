# 부하 테스트에서 발견된 병목 수정 계획 (다음 세션 진행용)

작성일: 2026-08-04. 이 문서는 계획만 담는다 — **아직 아무 코드도 고치지 않았다.**
배경/원본 데이터는 부하 테스트 리포트(대화 중 Artifact로 공유한 HTML, 재생성 필요하면
`StressClient.exe 127.0.0.1 9000 1000 200` / `... 10000 100 1000 20 900`로 재현) 참고.

## 요약

**2026-09-02 Release 빌드 재측정** (Ryzen 7 9800X3D 16스레드, 서버·클라이언트 동일 머신,
지연 백분위 계측 추가 후). 처음 측정은 Debug 빌드였고 처리량이 5배 차이 나므로 아래 수치를
기준으로 삼는다.

| 시나리오 | 처리량 | P50 / P95 / P99 | 정확성 | 브로드캐스트 |
| --- | --- | --- | --- | --- |
| 1,000세션 × 200사이클 | 18,337 사이클/초 (10.9초) | 16ms / 40ms / 180ms | 불일치 0, 스톨 0 | 100% |
| 10,000세션 × 100사이클 | 471 사이클/초 (300초에 14.2%) | 73ms / 62s / ≥100s | 불일치 0, 스톨 10,000 | 49.5% |

- 1,000세션: 정확성(mailId 일치/데드락 없음/브로드캐스트 수신) 전부 통과.
- 10,000세션: 데드락도 데이터 불일치도 없지만 처리량이 1/39로 무너지는 병목 발견.
  (참고: Debug 빌드 최초 측정에서는 900초에 목표의 2.7%였다.)
- 원인: 이 테스트가 `ZoneServer.exe 0` 하나(존 1개)에 1만 명을 몰아넣었기 때문에, zoneId
  기반 어피니티 라우팅(`zoneId % 풀크기`)이 사실상 스레드 1개로 축소됨. 여기에 브로드캐스터
  50개가 존 인구수만큼(최대 1만 명) 개별 프레임을 쏘는 팬아웃이 겹쳐 같은 큐를 밀어냄.
- 서버는 죽거나 멈추지 않았다(WARN/ERROR 0건, CPU 계속 사용, 종료 시 깔끔히 정리됨) — "느린
  것"이지 "멈춘 것"이 아니다.

## 수정 항목 (우선순위 순)

### 1. 인구를 여러 존에 분산 — 근본 원인 해결, 영향도 최고

**문제**: `Server/WorldServer/Src/Handler/GatewayLinkHandler.cpp`의 `HandleClientConnected`가
모든 신규 클라이언트를 항상 `kDefaultEntryZoneId`(=0)로만 배정한다. 존이 여러 개 떠 있어도
전부 존 0으로만 몰린다.

**할 일**:
- `GatewayLinkHandler`(또는 `WorldServerApp`)가 현재 등록된 존 목록(`ZoneLinkRegistry`)을
  보고 신규 접속을 라운드로빈(또는 인구 기반)으로 여러 zoneId에 분산 배정하도록 변경.
- 이때 존 경계(x 범위)가 서로 겹치지 않는 기존 규칙(`zoneId*10 ~ zoneId*10+10`)과 충돌하지
  않게 주의 — "입장 시 배정되는 존"과 "좌표 기반 존"이 지금은 암묵적으로 같은 개념이라,
  단순 라운드로빈 배정 시 입장 좌표(x,y)도 그 존의 구간 안에 있도록 같이 맞춰야 한다(현재
  `ZoneWorld::OnPlayerEnter`가 좌표를 그대로 받아들이므로, 배정 존과 안 맞는 좌표를 주면
  다음 Move에서 바로 핸드오프가 발생해버림).
- 검증: `ZoneServer.exe 0,1,2,3`처럼 존 여러 개를 띄운 뒤 `StressClient.exe`로 10,000세션
  재실행 → 인구가 각 존에 고르게 나뉘는지(`zoneserver-*.log`의 "플레이어 입장" 로그로 확인),
  처리량이 존 개수에 비례해 회복되는지 확인.

### 2. WorldWorker에서 브로드캐스트 릴레이를 별도 큐로 분리 — 영향도 중간

**문제**: `Server/WorldServer/Src/Worker/WorldWorker.h`가 스레드 1개로 라우팅(Mail Ack 릴레이 등)과
브로드캐스트 릴레이(`ForwardToWorld` 다량 수신)를 같은 큐에서 처리한다. 브로드캐스트가 몰리면
평소 트래픽이 그 뒤에서 계속 밀린다.

**할 일**:
- 브로드캐스트 전용 릴레이 워커(`Thread::WorkerThread` 하나 더, 또는 소규모 풀)를 추가해
  `ZoneLinkHandler::HandleForwardToWorld`(또는 그 처리 경로)만 분리.
- 존 1개당 인구가 합리적인 수준(수백~수천)으로 유지된다면(1번 수정 이후) 이 병목은 완화되지만,
  대규모 이벤트/전투처럼 브로드캐스트가 몰리는 상황을 감안하면 여전히 유효한 보완책.

### 3. 브로드캐스트 팬아웃 프레임 배칭 — 영향도 중간

**문제**: `Server/ZoneServer/Src/Worker/BroadcastDispatcher.cpp`가 대상 1명당 `ForwardToWorld`
프레임을 하나씩 Zone↔World 단일 TCP 링크로 전송한다. 존 인구가 늘수록 프레임 수가 그대로
비례해서 늘어난다.

**할 일**:
- 여러 clientSessionId를 한 프레임에 묶어 보내도록 와이어 포맷 확장(예: `targetCount +
  clientSessionId 배열 + innerPacketId + payload`).
- `Server/WorldServer/Src/Handler/ZoneLinkHandler.cpp`의 수신 쪽도 배치 언패킹하도록 같이 수정.
- 1번 수정 이후에도 남는 잔여 병목을 줄이는 용도.

## 검증 방법 (매 수정 후 공통)

1. `.claude/skills/build/SKILL.md` 절차로 빌드.
2. WorldServer → ZoneServer(존 여러 개) → GatewayServer 기동.
3. `StressClient.exe 127.0.0.1 9000 10000 100 1000 20 900` 재실행.
4. 확인 포인트:
   - `Done` 이 `Attempted`에 도달하는지(전 세션이 제한 시간 안에 완료).
   - `MismatchTotal`/`StalledCount` 여전히 0인지(수정이 정확성을 깨지 않았는지).
   - `CyclesPerSec`이 1,000세션 기준(Release 18,337/초) 대비 세션 수 비례로 유지되는지.
   - `BroadcastDeliveryRatio`가 1.0에 가까워지는지(현재 0.495).
   - 지연 백분위 P95/P99가 초 단위(62s / ≥100s)에서 ms 단위로 내려오는지 — 처리량만 보면
     "느리다"까지만 알 수 있고, 꼬리가 어디까지 늘어졌는지는 백분위로만 보인다.

## 참고: 이미 해결된 항목 (재작업 불필요)

- 부하 도구(`StressClient`) 자체의 스톨 감지 오탐 버그는 이전 세션에서 이미 수정 완료
  (`StressSession::HandleBroadcastPacket`이 더 이상 `MarkProgress()`를 호출하지 않음).
- `MailAddAck`/`MailDelAck` 프로토콜 확장은 이미 반영·빌드 완료.
