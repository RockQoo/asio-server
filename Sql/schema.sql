-- =============================================================================
-- asio-server 게임 DB(asio_game) 스키마
--
-- 실행: bat\setup_game_db.bat  (컨테이너 준비 + DB/로그인 생성 + 이 파일 적용까지 한 번에)
--       수동으로 하려면:
--         sqlcmd -S 127.0.0.1,1433 -U asio_game -P 'GmTool1234!' -d asio_game -C -i Sql/schema.sql
--
-- **운영툴(gmtool) DB와 별도 DB다.** 같은 SQL Server 인스턴스를 쓰지만 스키마를 섞지 않는다 --
-- 섞으면 GmTool 스키마 초기화가 게임 스키마까지 끌고 다니게 되고, 둘의 수명 주기가 다르다.
--
-- 스타일은 Tool/GmTool/Sql/schema.sql과 맞췄다(타입 선택 근거도 거기 헤더 주석 참고):
--   * T-SQL에 부호 없는 정수가 없다 -> uint32/uint64 계열은 BIGINT로 받는다.
--   * 한글이 들어가는 컬럼은 NVARCHAR (VARCHAR는 DB 콜레이션 코드페이지를 타서 깨진다).
--   * 시각은 DATETIME2 (DATETIME은 3.33ms 단위로 반올림된다).
--   * 멱등성: OBJECT_ID 검사로 감싸 여러 번 실행해도 결과가 같다.
--
-- -----------------------------------------------------------------------------
-- 키 설계 — 왜 account가 아니라 player_id인가
-- -----------------------------------------------------------------------------
-- 실무 서버는 계정(account_id) 밑에 캐릭터가 N개 있고, DB 메시지의 소유자 키도 account_id다
-- (같은 계정의 DB 작업을 같은 소비자에 몰아 한 트랜잭션으로 묶기 위해서).
--
-- 이 프로젝트는 **로그인 서버도 캐릭터 선택창도 만들지 않으므로 계정:캐릭터가 1:1**이다.
-- 그래서 계정 계층을 두지 않고 **player_id를 영속 키**로 쓴다 -- DB 메시지의 소유자 키,
-- 테이블의 키가 전부 player_id다. 세션 id를 쓰지 않는 이유가 중요한데, 세션 id는 재접속마다
-- 바뀌어서 "같은 사람의 DB 작업이 같은 스레드로 간다"는 성질이 깨지기 때문이다.
-- =============================================================================

-- -----------------------------------------------------------------------------
-- 플레이어 (= 로그인 계정 겸 캐릭터)
-- -----------------------------------------------------------------------------

IF OBJECT_ID('dbo.players', 'U') IS NULL
CREATE TABLE dbo.players
(
    player_id     BIGINT        NOT NULL IDENTITY(1,1),
    -- 로그인 ID 겸 캐릭터 이름. 기본 콜레이션(대소문자 구분 없음)을 그대로 쓴다 --
    -- "Alice"와 "alice"가 다른 계정이면 사용자가 자기 계정을 못 찾는 사고가 난다.
    player_name   NVARCHAR(32)  NOT NULL,
    -- PBKDF2(HMAC-SHA256) 결과를 "반복횟수.솔트(base64).해시(base64)" 한 문자열로 보관한다.
    -- GmTool의 gm_operator.password_hash와 같은 형식이다 -- 반복 횟수를 값 안에 넣어두면
    -- 나중에 횟수를 올려도 로그인 시점에 계정을 하나씩 재해싱할 수 있다(전체 마이그레이션 없이).
    password_hash NVARCHAR(255) NOT NULL,
    created_at    DATETIME2(3)  NOT NULL CONSTRAINT df_players_created DEFAULT SYSUTCDATETIME(),
    last_login_at DATETIME2(3)  NULL,
    CONSTRAINT pk_players PRIMARY KEY (player_id),
    CONSTRAINT uk_players_name UNIQUE (player_name)
);
GO

-- -----------------------------------------------------------------------------
-- 우편함
--
-- mail_id는 **DB가 아니라 Zone(Mail::MailModel)이 배정한다.** IDENTITY로 두지 않은 이유:
-- Zone이 메모리에서 먼저 확정하고 클라이언트에 응답한 뒤 DB에 반영하는 구조라(UnitOfWork),
-- DB가 id를 정하면 그 응답에 담을 id가 없다. 그래서 (player_id, mail_id)가 복합 키다.
--
-- mail_id가 BIGINT인 이유: Zone 쪽 타입이 uint32인데 T-SQL에는 부호 없는 정수가 없다.
-- INT로 받으면 21억을 넘는 순간 음수가 된다.
-- -----------------------------------------------------------------------------

IF OBJECT_ID('dbo.mails', 'U') IS NULL
CREATE TABLE dbo.mails
(
    player_id BIGINT         NOT NULL,
    mail_id   BIGINT         NOT NULL,
    title     NVARCHAR(128)  NOT NULL,
    body      NVARCHAR(1024) NOT NULL,
    -- Zone의 MailInfo.sendUt / endUt를 그대로 받는다. 유닉스 시각(초)이라 BIGINT다.
    -- DATETIME2로 바꾸지 않는 이유: 서버 메모리·와이어·클라이언트가 전부 정수로 다루는데
    -- DB에서만 형식이 다르면 경계마다 변환이 생기고 그 자리가 버그가 된다.
    send_ut   BIGINT         NOT NULL,
    end_ut    BIGINT         NOT NULL,
    CONSTRAINT pk_mails PRIMARY KEY (player_id, mail_id),
    CONSTRAINT fk_mails_players FOREIGN KEY (player_id) REFERENCES dbo.players (player_id)
);
GO

-- 만료 스윕이 "지금 시각보다 end_ut가 작은 것"을 찾는 경로. 현재 만료 판정은 Zone의
-- MailExpiryService가 메모리에서 하지만, 오프라인 유저 우편을 정리하려면 DB 쪽 경로가 필요해진다.
IF OBJECT_ID('dbo.mails', 'U') IS NOT NULL
   AND NOT EXISTS (SELECT 1 FROM sys.indexes
                    WHERE name = 'ix_mails_end_ut'
                      AND object_id = OBJECT_ID('dbo.mails'))
CREATE INDEX ix_mails_end_ut ON dbo.mails (end_ut);
GO

-- -----------------------------------------------------------------------------
-- 재화
--
-- 종류별로 행을 나눈다(컬럼으로 두지 않는다) -- 재화가 늘 때마다 ALTER TABLE을 하지 않기
-- 위해서다. Zone의 Currency::CurrencyModel도 "종류를 인자로 받는" 형태라 모양이 맞는다.
-- currency_type 0은 "종류 없음" 예약값이라(Shared/Protocol/Src/CurrencyType.h) 저장되지 않는다.
-- -----------------------------------------------------------------------------

IF OBJECT_ID('dbo.currencies', 'U') IS NULL
CREATE TABLE dbo.currencies
(
    player_id     BIGINT   NOT NULL,
    currency_type TINYINT  NOT NULL,
    amount        BIGINT   NOT NULL,
    CONSTRAINT pk_currencies PRIMARY KEY (player_id, currency_type),
    CONSTRAINT ck_currencies_type CHECK (currency_type > 0),
    CONSTRAINT ck_currencies_amount CHECK (amount >= 0),
    CONSTRAINT fk_currencies_players FOREIGN KEY (player_id) REFERENCES dbo.players (player_id)
);
GO

-- -----------------------------------------------------------------------------
-- 재화 변경 감사 로그
--
-- Zone의 CurrencyTask가 새 값과 **이전 값을 둘 다** 실어 보내는 이유가 여기 있다
-- (Currency/CurrencyTask 주석): 잔액만 갱신하면 "누가 언제 얼마에서 얼마로 바뀌었는지"가
-- 남지 않아 재화 사고를 추적할 수 없다. 잔액 UPDATE와 이 INSERT는 같은 트랜잭션이다.
-- -----------------------------------------------------------------------------

IF OBJECT_ID('dbo.currency_logs', 'U') IS NULL
CREATE TABLE dbo.currency_logs
(
    log_id        BIGINT   NOT NULL IDENTITY(1,1),
    player_id     BIGINT   NOT NULL,
    currency_type TINYINT  NOT NULL,
    old_amount    BIGINT   NOT NULL,
    new_amount    BIGINT   NOT NULL,
    -- Zone이 발급한 요청 식별자(Common::RequestId). Zone -> World -> DB 로그를 한 줄로
    -- 이어 붙이는 값이라 여기에도 남긴다. int64이고 시각이 상위 비트에 있어 시간순으로 증가한다.
    request_id    BIGINT   NOT NULL,
    logged_at     DATETIME2(3) NOT NULL CONSTRAINT df_currency_logs_at DEFAULT SYSUTCDATETIME(),
    CONSTRAINT pk_currency_logs PRIMARY KEY (log_id)
);
GO

IF OBJECT_ID('dbo.currency_logs', 'U') IS NOT NULL
   AND NOT EXISTS (SELECT 1 FROM sys.indexes
                    WHERE name = 'ix_currency_logs_player'
                      AND object_id = OBJECT_ID('dbo.currency_logs'))
CREATE INDEX ix_currency_logs_player ON dbo.currency_logs (player_id, log_id);
GO

-- =============================================================================
-- 저장 프로시저
--
-- 서버는 테이블에 직접 쿼리하지 않고 전부 SP를 거친다. 실무 구조를 따른 것이고, 실질적인
-- 이유는 **SET XACT_ABORT ON을 SP 안에 강제로 넣을 수 있다는 것**이다 -- 이게 없으면 런타임
-- 오류가 나도 트랜잭션이 열린 채 남아 다음 문장이 반쯤 적용된다.
--
-- 서버 쪽 트랜잭션 경계는 **UnitOfWork 하나 = 트랜잭션 하나**다. 여러 UoW를 모으지 않는다.
-- 같은 player_id의 UoW들은 DB 소비자 어피니티(player_id % N)로 같은 스레드에 배정되므로
-- 도착 순서대로 실행되고, 그래서 순서 보장을 위한 별도 장치가 필요 없다.
-- =============================================================================

-- 로그인 조회. 비밀번호 비교는 서버에서 한다 -- 해시 비교를 SP에서 하면 평문이 와이어와
-- DB 로그에 남는다.
--
-- 조회는 이 프로젝트 규약대로 WITH(NOLOCK)이다(.claude/rules/sql-patterns.md). 회원가입 경로가
-- 생기면 "막 만든 계정이 아직 안 보일 수 있다"는 성질이 유일하게 여기서 드러나므로 그때 다시 볼 것.
CREATE OR ALTER PROCEDURE dbo.player_login_select
    @player_name NVARCHAR(32)
AS
BEGIN
    SET NOCOUNT ON;

    SELECT player_id, player_name, password_hash
      FROM dbo.players WITH(NOLOCK)
     WHERE player_name = @player_name;
END
GO

-- 로그인 성공 시각 갱신. 실패해도 로그인 자체를 막을 이유가 없어 조회와 분리했다.
CREATE OR ALTER PROCEDURE dbo.player_login_touch
    @player_id BIGINT
AS
BEGIN
    SET NOCOUNT ON;

    UPDATE dbo.players
       SET last_login_at = SYSUTCDATETIME()
     WHERE player_id = @player_id;
END
GO

-- 로그인 직후 World가 캐시에 적재할 우편함 전체.
CREATE OR ALTER PROCEDURE dbo.mail_load
    @player_id BIGINT
AS
BEGIN
    SET NOCOUNT ON;

    SELECT mail_id, title, body, send_ut, end_ut
      FROM dbo.mails WITH(NOLOCK)
     WHERE player_id = @player_id
     ORDER BY mail_id;
END
GO

-- 로그인 직후 World가 캐시에 적재할 재화 전체.
CREATE OR ALTER PROCEDURE dbo.currency_load
    @player_id BIGINT
AS
BEGIN
    SET NOCOUNT ON;

    SELECT currency_type, amount
      FROM dbo.currencies WITH(NOLOCK)
     WHERE player_id = @player_id;
END
GO

CREATE OR ALTER PROCEDURE dbo.mail_insert
    @player_id BIGINT,
    @mail_id   BIGINT,
    @title     NVARCHAR(128),
    @body      NVARCHAR(1024),
    @send_ut   BIGINT,
    @end_ut    BIGINT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;

    INSERT INTO dbo.mails (player_id, mail_id, title, body, send_ut, end_ut)
    VALUES (@player_id, @mail_id, @title, @body, @send_ut, @end_ut);
END
GO

CREATE OR ALTER PROCEDURE dbo.mail_delete
    @player_id BIGINT,
    @mail_id   BIGINT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;

    DELETE FROM dbo.mails
     WHERE player_id = @player_id AND mail_id = @mail_id;
END
GO

-- 잔액 갱신 + 감사 로그를 한 번에. 증감이 아니라 **절대값**을 받는다 -- Zone의
-- CurrencyModel::SetTracked가 이미 새 값과 이전 값을 확정해서 보내므로, DB에서 다시
-- 계산하면 두 곳이 서로 다른 값을 권위로 삼게 된다.
CREATE OR ALTER PROCEDURE dbo.currency_update
    @player_id     BIGINT,
    @currency_type TINYINT,
    @old_amount    BIGINT,
    @new_amount    BIGINT,
    @request_id    BIGINT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;

    MERGE dbo.currencies AS target
    USING (SELECT @player_id AS player_id, @currency_type AS currency_type) AS source
       ON target.player_id = source.player_id
      AND target.currency_type = source.currency_type
    WHEN MATCHED THEN
        UPDATE SET amount = @new_amount
    WHEN NOT MATCHED THEN
        INSERT (player_id, currency_type, amount)
        VALUES (@player_id, @currency_type, @new_amount);

    INSERT INTO dbo.currency_logs
        (player_id, currency_type, old_amount, new_amount, request_id)
    VALUES (@player_id, @currency_type, @old_amount, @new_amount, @request_id);
END
GO
