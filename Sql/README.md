# 게임 DB(`asio_game`) 스키마

**운영툴(`gmtool`) DB와 별도 DB다.** 같은 SQL Server 인스턴스를 쓰지만 스키마를 섞지 않는다 —
섞으면 GmTool 스키마 초기화가 게임 스키마까지 끌고 다니게 되고, 둘의 수명 주기가 다르다.
운영툴 스키마는 `Tool/GmTool/Sql/schema.sql`이다.

작성 규약은 `.claude/rules/sql-patterns.md`(네이밍, SP 골격, 에러 코드 대역).

## 파일 구성 — 테이블/콘텐츠 단위

| 파일 | 내용 |
|------|------|
| `players.sql` | `players` + `uk_players_name` + `usp_players_select` / `usp_players_upsert` |
| `mails.sql` | `mails` + `ix_mails_player` + `usp_mails_select` / `usp_mails_upsert` / `usp_mails_delete` |
| `currencies.sql` | `currencies` + `usp_currencies_select` / `usp_currencies_upsert` (감사 로그는 나중에 이 파일에 추가) |
| `unique_keys.sql` | RUID 검증용 `id_tests` / `id_test_randoms` + 삽입 SP **(게임 스키마 아님)** |
| `seed.sql` | 개발용 시드 계정 2만 4건 (비밀번호 전부 `0000`) |
| `verify_unique_keys.sql` | 검증 결과 확인 쿼리 (중복/노드 분배/레인 분배/단편화) |

**한 콘텐츠의 테이블과 그 SP를 같은 파일에 둔다.** 테이블 정의와 그걸 만지는 SP가 떨어져
있으면 컬럼을 하나 바꿀 때 고쳐야 할 자리를 놓친다. `currencies.sql`에 테이블이 두 개인 것도
같은 이유다 — 잔액 갱신과 감사 로그는 같은 트랜잭션 안에서만 의미가 있다.

## 적용 순서 — FK 때문에 고정이다

```
players  →  mails, currencies  →  (unique_keys)  →  seed
```

`mails`/`currencies`가 `players.player_id`를 FK로 참조하므로 `players.sql`이 먼저다.
`unique_keys.sql`은 FK가 없어 순서에 제약이 없다.

`bat/setup_game_db.bat`이 이 순서를 코드로 들고 있다. 직접 적용할 때는:

```bat
sqlcmd -S 127.0.0.1,1433 -U asio_game -P 0000 -C -b -f 65001 -d asio_game -i Sql\players.sql
```

`-f 65001`을 빠뜨리면 UTF-8 파일이 CP949로 읽혀 한글이 깨진 채 저장된다.

## 키 설계 — 모든 id는 `Base::Ruid`가 발급한다

`IDENTITY`를 쓰지 않는다. 서버가 메모리에서 먼저 확정하고 클라이언트에 응답한 뒤 DB에
반영하는 구조(UnitOfWork)라, **DB가 id를 정하면 그 응답에 담을 id가 없기 때문이다.**

`RUID`는 `[시각 41비트][노드 10비트][시퀀스 12비트]`라 두 가지를 동시에 만족한다.

* **전역 유일** → `mail_id` 하나만으로 PK가 성립한다(플레이어별 복합키가 필요 없다)
* **시간순 증가** → 클러스터 인덱스에 뒤쪽으로 쌓인다

두 번째 성질은 주장이 아니라 실측 대상이다(`unique_keys.sql`). 실측 결과 **발급 노드가
하나일 때만** "재배치가 없다"가 성립한다 — 단일 노드는 단편화 0.428% / 페이지 채움 99.94%지만,
5개 노드가 동시에 발급하면 단편화가 98%대로 오른다. 그래도 무작위 키 대조군보다 페이지 수가
22% 적다. 자세한 수치는 `docs/local/`의 검증 결과 문서.

세션 id를 키로 쓰지 않는 이유: 재접속마다 바뀌어서 "같은 사람의 DB 작업이 같은 스레드로
간다"(owner 어피니티)가 깨진다.
