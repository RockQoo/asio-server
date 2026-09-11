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
| **저장 프로시저** | `up_{콘텐츠}_{동작}` | `dbo.up_mails_upsert`, `dbo.up_mails_delete`, `dbo.up_currencies_upsert` |
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

## 스크립트는 호스트 `sqlcmd`로 적용한다

`bat/setup_game_db.bat` / `bat/setup_gmtool_db.bat` 은 호스트에 설치된 `sqlcmd`로
`127.0.0.1,1433` 에 붙는다. **엔진이 로컬 설치본인지 포트를 뚫어둔 컨테이너인지 구분하지
않는다** -- 붙는 쪽이 TCP 1433 이기만 하면 같은 스크립트가 양쪽에 그대로 동작한다.

```bat
sqlcmd -S 127.0.0.1,1433 -U sa -P 0000 -C -b -f 65001 -d asio_game -i Sql\players.sql
```

`-C`는 개발용 자체 서명 인증서를 신뢰하고(없으면 SSL 오류), `-b`는 오류를 `errorlevel`로
돌려줘 배치가 중간에 멈출 수 있게 한다. 서버 주소와 sa 비밀번호는 각각 환경 변수
`ASIO_SERVER_DB_SERVER` / `ASIO_SERVER_SA_PASSWORD` 로 덮어쓴다.

**`-f 65001`을 빠뜨리면 안 된다.** `.sql` 파일은 UTF-8(BOM 없음)인데 `sqlcmd`의 기본 입력
코드페이지는 시스템 ANSI(한국어 환경이면 CP949)라, 한글이 든 파일을 조용히 깨진 채로
적용한다. 주석만 깨지면 그나마 낫지만 한글 문자열 리터럴이나 시드 데이터가 들어가면 **깨진
값이 그대로 DB에 저장된다.** 소스가 `/utf-8` 플래그를 요구하는 것과 같은 함정이다
(`cpp-patterns.md`의 "파일 인코딩" 절).

## 저장 프로시저 골격 (전부 이 모양으로 쓴다)

**모든 SP 는 `@is_trans_outside` 를 첫 파라미터로 받고, `RETURN` 으로 결과 코드를 돌려준다.**
예외 없이 같은 모양이라야 호출부(`DbConnection`)가 SP 종류를 몰라도 파라미터와 반환값을
일괄로 붙일 수 있다.

```sql
CREATE OR ALTER PROCEDURE [dbo].[up_mails_upsert]
    @is_trans_outside TINYINT,           -- ① 항상 첫 파라미터
    @mail_id          BIGINT,
    ...
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;                   -- ② 에러나면 트랜잭션 통째로 무효화
    DECLARE @ret_val INT = 0;

    IF @is_trans_outside = 0 AND XACT_STATE() = -1       -- ③ 진입 가드
    BEGIN SET @ret_val = 0xA0000001; GOTO ErrorHandler; END;

    BEGIN TRY
        IF @is_trans_outside = 0 BEGIN TRANSACTION;      -- ④ 조건부 시작

        -- ... 실제 업무 로직 ...

        IF @@ROWCOUNT <> 1                               -- ⑤ 업무 거절
        BEGIN SET @ret_val = 0xA0020001; GOTO ErrorHandler; END;

        IF @is_trans_outside = 0 COMMIT TRANSACTION;     -- ④ 조건부 커밋
    END TRY
    BEGIN CATCH
        SET @ret_val = 0xA0000000 + ERROR_NUMBER();      -- ⑥ 예외 -> 코드
        GOTO ErrorHandler;
    END CATCH

ErrorHandler:                                            -- ⑦ 단일 출구
    IF @is_trans_outside = 0 AND XACT_STATE() <> 0
        ROLLBACK TRANSACTION;

    RETURN @ret_val;
END
GO
```

### ① 왜 `@is_trans_outside` 인가

트랜잭션 경계가 **두 곳**에서 잡힐 수 있기 때문이다. 호출부가 SP 여러 개를 한 묶음으로 보낼
때는 ODBC 커넥션 수준에서 트랜잭션이 이미 열려 있고(`AutoDbCommand` 의 `useTransaction`),
SP 하나만 보낼 때는 열려 있지 않다. 이걸 SP 가 모르면:

- 바깥이 이미 트랜잭션인데 SP 가 또 열면 **중첩 트랜잭션**이 된다. T-SQL 의 중첩 `COMMIT` 은
  카운터만 줄이고 실제로 커밋하지 않는데, SP 는 커밋했다고 믿고 빠져나간다.
- 바깥이 트랜잭션이 아닌데 SP 도 안 열면, 문장이 여러 개인 SP 가 **중간에 실패했을 때 앞 문장이
  그대로 남는다**(`up_currencies_upsert` 의 잔액 갱신 + 감사 로그가 그 경우다).

`DbConnection::ExecuteOne` 이 이 값을 자동으로 채운다 -- 콘텐츠 코드는 넘기지 않는다.

### ⑦ 왜 `GOTO` 로 단일 출구인가

정리 코드(롤백)가 **한 군데**에만 있어야 하기 때문이다. 성공 경로도 이 레이블을 지나가지만,
`COMMIT` 뒤에는 `XACT_STATE()` 가 0 이라 롤백이 실행되지 않는다. 조건마다 `ROLLBACK` 을
복사해두면 나중에 분기를 하나 추가할 때 그 자리만 빠뜨리게 된다.

### 에러 코드 대역

`RETURN` 은 `INT` 만 돌려줄 수 있어 상위 바이트로 분류한다. 0 이 성공이다.

| 대역 | 뜻 |
|------|-----|
| `0x00000000` | 성공 |
| `0xA0000001` | 진입 가드 -- 커밋 불가 상태(`XACT_STATE() = -1`)에서 들어왔다 |
| `0xA0000000 + ERROR_NUMBER()` | SP 내부 예외(제약 위반 등)를 코드로 바꾼 것 |
| `0xA0010001~` | `players` 업무 거절 |
| `0xA0020001~` | `mails` 업무 거절 |
| `0xA0030001~` | `currencies` 업무 거절 |
| `0xA0040001~` | `unique_keys` 업무 거절(검증 도구) |

콘텐츠별로 대역을 나누는 것은 `Protocol::EErrorCode` 가 콘텐츠별 100 단위를 쓰는 것과 같은
이유다 -- 로그에 숫자만 남아도 어느 콘텐츠에서 났는지 바로 읽힌다. **기존 값의 숫자는 바꾸지
않는다**(과거 로그와 어긋난다). 새 조건은 자기 대역 맨 뒤에 추가한다.

C++ 쪽에서는 0 이 아닌 반환값이 `World::DbProcedureException` 으로 올라온다. 예외로 만든 이유는
던져야 `Execute` 의 트랜잭션 경로가 롤백을 태우기 때문이다 -- 반환값으로 주면 호출부가 검사를
빠뜨렸을 때 실패한 작업이 그대로 커밋된다.

### 예외를 두지 않는다 -- 조회 SP 도 같은 골격이다

읽기 하나를 트랜잭션으로 감싸도 얻는 게 없지만, **모양을 다르게 두면 "이 SP 는 왜 다르지"를
매번 판단해야 하고 호출부에도 분기가 생긴다.** 조회 SP 는 `@@ROWCOUNT` 검사(⑤)만 빠진다 --
0행이 정상인 조회에 그 검사를 붙이면 없는 계정을 찾는 정상 경로가 실패로 뒤집힌다.

### `CATCH` 가 오류를 삼킨다는 것의 대가

골격이 `CATCH` 로 예외를 `@ret_val` 로 바꾸므로, **DB 오류가 더 이상 호출부로 예외로 올라가지
않는다.** 반환값을 읽는 경로(`ExecuteOne`)는 그대로 실패를 알지만, **`ExecuteMany`(파라미터 배열)
는 반환값을 읽을 수 없다** -- 한 번에 수천 건을 밀어넣는데 출력 파라미터가 그중 어느 건의
결과인지 말해주지 못하기 때문이다.

그래서 그 경로를 쓰는 쪽은 **성공 여부를 따로 확인해야 한다.** `--idtest` 는 삽입 전후의
행 수를 노드 번호로 걸러 세고 그 증가분이 넣은 개수와 같은지 본다(`up_unique_keys_count_by_node`).
중복 키가 조용히 건너뛰어지면 거기서 갈린다. **절대값이 아니라 증가분을 보는 이유**는 테이블을
비우지 않고 다시 돌렸을 때 멀쩡한 실행이 실패로 뒤집히지 않게 하기 위해서다.

### 영향 행 0 이 항상 거절은 아니다

`up_mails_delete` 에는 `@@ROWCOUNT` 검사가 없다. 만료 스윕과 사용자 삭제가 같은 우편을 두고
겹칠 수 있는데, 그건 오류가 아니라 정상적인 경합이다. **"한 행이 바뀌어야 정상인가"를 SP 마다
판단해서 붙인다** -- 기계적으로 복사하면 정상 경로가 실패로 뒤집힌다.
