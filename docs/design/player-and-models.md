# Player — 모델마다 다른 보호 방식

대상 코드: `Server/ZoneServer/Src/Unit/Unit.h`, `Server/ZoneServer/Src/Player/Player.h`

## 한 줄 요약

`Unit` 은 존 안에서 자리를 갖고 틱을 받는 것, `Player` 는 거기에 주인(클라 연결)과 콘텐츠
모델을 더한 것이다. **모델마다 보호 방식이 다르고, 그 기준은 "실제로 몇 개의 레인이
건드리는가"다.**

## 모델별 소유 레인 — 이 표가 곧 접근 규칙이다

| 모델 | 어디에 | 보호 | 건드리는 레인 |
|------|------|------|---------------|
| `unitId_` / `type_` | `Unit` | 없음 | 생성 후 불변이라 어느 레인에서 읽어도 안전하다 |
| `zoneId_` | `Unit` | 없음 | BASIC 전용 — 입장·퇴장이 전부 BASIC 이라 쓰는 쪽도 읽는 쪽도 한 레인이다 |
| `move_` | `Unit` | `Mutexed` | BASIC(검증·요청 기록) + TICK(적분·경계 판정) |
| `wallet_` | `Player` | 값 그대로(락 없음) | BASIC 전용 |
| `mailBox_` | `Player` | `Mutexed` | BASIC(요청 처리) + TICK(만료 삭제) |

**락이 없는 모델을 다른 레인에서 건드리기 시작하면 그 순간 조용히 깨진다.**
새 모델을 붙일 때는 이 표에 한 줄을 먼저 적을 것.

BASIC 레인의 주인은 `playerId` 다 — 로그인 전까지만 `clientSessionId` 이고, 그 뒤로는 그
사람의 일이 어느 서버에 있든 한 값으로 모인다(`CLAUDE.md` 불변 규칙 3).

## 왜 객체를 둘로 쪼개지 않았나

이 객체는 **두 레인이 같이 본다** — BASIC 은 `UnitContainer` 의 맵(`shared_mutex`)으로,
TICK 은 그 컨테이너가 만들어 둔 **락 없는 스냅샷**으로 도달한다.

쪼개면 "이 사람의 것"이 두 군데로 흩어져, 콘텐츠가 늘 때마다 어느 쪽에 넣을지를 매번 다시
정해야 한다. 그래서 객체는 하나로 두고 **모델 단위로 소유 레인을 명시**하는 쪽을 택했다.

## 왜 `zoneId` 를 `Unit` 이 들고 있나

이 값을 유닛이 직접 들어서 예전의 `clientSessionId -> zoneId` 공유 맵(+ `shared_mutex`)이
통째로 없어졌다. 핸드오프도 결국 BASIC 으로 오는 입장·퇴장 메시지로 처리되므로 레인이
하나로 유지된다.

## `GetWallet()`이 const가 아닌 이유

가변 참조를 반환하는 접근자라 const를 붙일 수 없다 — 호출부가 재화를 실제로 바꿔야 하기
때문이다. `.claude/rules/cpp-patterns.md`의 "Get 계열은 const 필수"에 대한 예외 항목이다.

관련: [메시지 파이프라인과 레인 어피니티](message-pipeline.md) ·
[락 전략](locking-strategy.md)
