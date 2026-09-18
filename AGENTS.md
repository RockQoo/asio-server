# Repository Guidelines

이 파일은 **다른 곳을 가리키기만 한다.** 규약을 여기에 옮겨 적으면 원본과 갈리고,
갈린 요약은 읽는 쪽을 잘못 이끈다(실제로 그랬다 — "Client 는 아직 비어 있다"가 한동안
남아 있었지만 그때 이미 MonoGame 클라이언트가 돌고 있었다).

| 알고 싶은 것 | 볼 곳 |
|---|---|
| 프로젝트 구조 · 아키텍처 · 필수 규칙 | `CLAUDE.md` |
| C++ 세부 규약(include 순서 · 캐스팅 · pch · 네이밍 · 예외 안전) | `.claude/rules/cpp-patterns.md` |
| 패킷 id 네이밍과 번호 대역 | `.claude/rules/packet-naming.md` |
| SQL 네이밍 · SP 작성 | `.claude/rules/sql-patterns.md` |
| CLI 빌드 절차(MSBuild 경로 탐색, Git Bash 우회) | `.claude/skills/build/SKILL.md` |
| 서버별 설명 · 시퀀스 다이어그램 · 설계 근거 | `docs/index.html` |
| 다음에 할 일과 그 순서 | `PROGRESS.md` |

## 반드시 지킬 것 (여기서만 말하는 것)

- **`3rd/asio` 를 수정하지 않는다.** 벤더 코드이고 훅으로도 막혀 있다.
- **커밋 전에 사용자 확인을 받는다.** 자동 커밋 금지.
- **참고한 외부 자료의 출처를 커밋되는 파일에 적지 않는다.** 기록이 필요하면
  `docs/local/`(gitignore 대상)에만 둔다.
- **구조가 바뀌면 `docs/` 를 같은 커밋에서 고친다.** 무엇이 바뀌었을 때 어느 문서를
  손대야 하는지는 `CLAUDE.md` 의 표에 있다.

## 커밋과 PR

커밋 메시지는 **무엇을 왜 바꿨는지**를 한글로 쓴다. 접두사(`feat:` 등)는 쓰지 않는다.
제목 한 줄에 결과를 적고, 본문에 그렇게 한 이유와 버린 대안을 적는다.

PR 은 구조에 준 영향, 확인한 방법(어떤 명령으로 무엇을 봤는지), 프로토콜이나 스레드 주인이
바뀌었다면 그 사실을 명시한다. `docs/` 의 HTML 을 고쳤으면 화면을 첨부한다.

## 테스트

자동화 스위트가 없다. 빌드 뒤 `bat/start_server_all.bat` 으로 서버를 띄우고
`ProtocolClient` 로 실제 패킷을 왕복시킨다. 동시성에 영향을 주는 변경은 `StressClient`
로 부하를 걸고 `logs/` 를 확인한다. **성능을 건드렸으면 `docs/performance.html` 의 고정
시나리오로 재측정하고 그 표에 한 행을 추가한다.**
