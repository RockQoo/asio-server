---
paths:
  - "Sql/**/*.sql"
  - "Tool/GmTool/Sql/**/*.sql"
---

# SQL Patterns

게임 DB(`asio_game`) 스키마와 저장 프로시저의 네이밍/작성 규약.

## 네이밍: 전부 snake_case

C++ 쪽 규약(PascalCase 타입, camelCase + trailing underscore 멤버)을 DB로 끌고 오지 않는다.
**SQL 식별자는 전부 소문자 snake_case**로 쓴다 — 대소문자 구분이 콜레이션에 따라 달라지는
자리라, 섞어 쓰면 환경마다 다르게 동작할 여지를 남긴다.

| 대상 | 규칙 | 예 |
|------|------|-----|
| **테이블** | **복수형** snake_case | `dbo.mails`, `dbo.players`, `dbo.currencies` |
| **컬럼** | 단수형 snake_case | `mail_id`, `player_id`, `body`, `send_ut` |
| **저장 프로시저** | `{단수 엔티티}_{동작}` | `dbo.mail_insert`, `dbo.mail_delete`, `dbo.currency_update` |
| **파라미터** | `@` + 컬럼과 같은 이름 | `@player_id`, `@mail_id` |
| 기본 키 | `pk_{테이블}` | `pk_mails` |
| 유니크 키 | `uk_{테이블}_{용도}` | `uk_players_name` |
| 외래 키 | `fk_{테이블}_{참조테이블}` | `fk_mails_players` |
| 인덱스 | `ix_{테이블}_{컬럼들}` | `ix_mails_end_ut` |
| 기본값 제약 | `df_{테이블}_{컬럼}` | `df_players_created` |
| 체크 제약 | `ck_{테이블}_{용도}` | `ck_currencies_amount` |

**테이블만 복수형인 이유**: 테이블은 행의 집합이고 컬럼은 행 하나의 속성이다.
`mails.mail_id`처럼 읽으면 "메일들 중 하나의 메일 id"가 되어 자연스럽다.

**제약/인덱스에 이름을 반드시 붙인다.** 이름을 생략하면 SQL Server가
`PK__mails__3213E83F1A2B3C4D` 같은 임의 이름을 붙여서, 스키마를 다시 적용한 환경마다 이름이
달라지고 `DROP CONSTRAINT`를 스크립트로 쓸 수 없게 된다.

## 예외 — `Tool/GmTool/Sql/schema.sql`은 단수형이다

`gm_operator`, `gm_auth_token`, `coupon_campaign`처럼 **단수형**으로 이미 만들어져 있다.
운영툴은 기능 개발이 중단된 상태(`CLAUDE.md` 참고)이고 테이블 이름을 바꾸면 리포지토리
코드와 마이그레이션이 전부 딸려오므로 **그대로 둔다.** 새 게임 DB만 위 규칙을 따른다.

## 서버는 테이블에 직접 쿼리하지 않고 SP만 부른다

실무 구조를 따른 것이고, 실질적인 이유가 하나 더 있다 — **`SET XACT_ABORT ON`을 SP 안에
강제로 넣을 수 있다.** 이게 없으면 런타임 오류가 나도 트랜잭션이 열린 채 남아 다음 문장이
반쯤 적용된다.

쓰기 SP는 예외 없이 이렇게 시작한다:

```sql
SET NOCOUNT ON;
SET XACT_ABORT ON;
```

`SET NOCOUNT ON`이 없으면 "N행이 영향을 받았습니다" 메시지가 결과 집합처럼 따라와서,
ODBC 드라이버가 실제 결과 집합을 찾을 때 한 번 더 넘겨야 한다.

## 조회는 기본이 `WITH(NOLOCK)`

**SELECT는 기본적으로 `WITH(NOLOCK)`을 붙인다.**

```sql
SELECT player_id, player_name, password_hash
  FROM dbo.players WITH(NOLOCK)
 WHERE player_name = @player_name;
```

읽기가 압도적으로 많은 게임 DB에서 조회가 쓰기를 막지 않게 하려는 것이고, 실무 서버가
쓰던 방식이다.

**대신 이 성질을 알고 쓴다**: `NOLOCK`은 `READ UNCOMMITTED`라 커밋 안 된 값을 읽을 수 있고,
페이지 분할이 진행 중이면 드물게 행을 건너뛰거나 같은 행을 두 번 읽는다. 실질적으로 걸릴 수
있는 자리는 **로그인 조회 하나**다 — 계정을 막 만든 직후 그 행이 안 보이면 "방금 만든 계정으로
로그인 실패"로 나타난다. 지금은 회원가입 경로가 없어 계정이 시드로만 생기므로 해당되지 않는다.

돈이 오가는 정산처럼 **한 행도 놓치면 안 되는 집계**가 생기면 그때만 `NOLOCK`을 빼거나
**READ COMMITTED SNAPSHOT**을 검토한다.

## 타입 선택

T-SQL에는 부호 없는 정수가 없다. C++ 쪽 타입과의 대응은 이렇게 고정한다.

| C++ | SQL Server | 이유 |
|-----|-----------|------|
| `uint32_t` / `uint64_t` | `BIGINT` | `INT`로 받으면 21억을 넘는 순간 음수가 된다 |
| `uint8_t` | `TINYINT` | `TINYINT`가 0\~255라 정확히 대응 |
| `int64_t` (유닉스 시각) | `BIGINT` | 서버·와이어·클라이언트가 전부 정수로 다룬다. DB만 `DATETIME2`로 바꾸면 경계마다 변환이 생기고 그 자리가 버그가 된다 |
| 서버가 만든 시각 | `DATETIME2(3)` | `DATETIME`은 3.33ms 단위로 반올림된다 |
| 한글이 들어가는 문자열 | `NVARCHAR` | `VARCHAR`는 DB 콜레이션 코드페이지를 타서 컨테이너 기본 콜레이션에서 한글이 `?`로 깨진다 |
| 코드값(`[0-9A-Z]`만) | `VARCHAR` | 넓힐 이유가 없다 |

## 멱등성

스키마 스크립트는 여러 번 실행해도 결과가 같아야 한다(기동 시 자동 적용이 안전해야 하므로).

- 테이블: `IF OBJECT_ID('dbo.xxx', 'U') IS NULL CREATE TABLE ...`
- 인덱스: `IF NOT EXISTS (SELECT 1 FROM sys.indexes WHERE name = ... AND object_id = ...)`
- 프로시저: `CREATE OR ALTER PROCEDURE` (SQL Server 2016 SP1+)

## Git Bash에서 `docker exec`로 sqlcmd를 부를 때

`/opt/mssql-tools18/bin/sqlcmd`가 `C:/Program Files/Git/opt/...`로 치환되어 실패한다(MSYS
경로 변환). **`MSYS_NO_PATHCONV=1`을 앞에 붙인다.** `bat/*.bat`은 cmd에서 도니 해당 없다.

```bash
MSYS_NO_PATHCONV=1 docker exec asio-server-mssql /opt/mssql-tools18/bin/sqlcmd -S localhost ...
```
