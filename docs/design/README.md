# 설계 근거 문서

소스에서 옮겨온 **"왜 이 설계를 택했나"** 를 담는다. 코드 옆에는 한 줄 포인터만 남기고
배경 설명은 여기로 모은다 — 헤더를 열었을 때 클래스 구조가 먼저 보이게 하려는 것이다.

## 소스에 남기는 것 / 여기로 옮기는 것

| 남긴다 | 옮긴다 |
|--------|--------|
| 고칠 때 모르면 사고 나는 것(스레드 규약, 호출 순서 의존, 수명 트릭) | 한 번 읽으면 되는 설계 배경 |
| 한두 줄로 끝나는 경고 | 대안을 검토하고 버린 이유 |
| 접근 규칙 표처럼 그 자리에서 봐야 하는 것 | 측정 결과·이력 |

## 포인터 형식

소스에는 이렇게 남긴다:

```cpp
// 큐 그룹 하나 = 소비자 스레드 N개. ownerId % N 으로 스레드가 정해진다.
// 설계 근거: docs/design/processor-group.md
```

## 파일명

kebab-case(`.claude/rules/md-patterns.md`). 대상 타입 이름을 그대로 쓰되 소문자로 푼다
(`Group` → `processor-group.md`).

## 목록

| 문서 | 대상 |
|------|------|
| [processor-group.md](processor-group.md) | `Shared/Core/Src/Processor/Group.h` |
| [player-and-models.md](player-and-models.md) | `Server/ZoneServer/Src/Game/Player.h` |
| [locking-strategy.md](locking-strategy.md) | `Thread/Mutexed.h`, `ClientRegistry`, `ZoneLinkRegistry` |
| [unit-of-work.md](unit-of-work.md) | `Task/UnitOfWork.h`, `Zone/Task/UnitOfWork.cpp` |
| [wire-format.md](wire-format.md) | `Packet/ToolLinkPackets.h`, `Packet/ZoneLinkPackets.h` |
| [unique-id.md](unique-id.md) | `Shared/Core/Src/Common/RUID.h` |
| [config-file.md](config-file.md) | `Shared/Core/Src/Common/ConfigFile.h`, `config/*.cfg` |
