-- =============================================================================
-- asio-server 게임 DB(asio_game) 스키마
--
-- 실행: bat\setup_game_db.bat  (컨테이너 준비 + DB/로그인 생성 + 이 파일 적용까지 한 번에)
--
-- **운영툴(gmtool) DB와 별도 DB다.** 같은 SQL Server 인스턴스를 쓰지만 스키마를 섞지 않는다 --
-- 섞으면 GmTool 스키마 초기화가 게임 스키마까지 끌고 다니게 되고, 둘의 수명 주기가 다르다.
--
-- 작성 규약은 .claude/rules/sql-patterns.md 에 있다. 요약하면:
--   1. 테이블 이름은 복수형 snake_case
--   2. 클러스터 인덱스는 반드시 있어야 한다 (PK와 같을 수도, 아닐 수도 있다)
--   3. SP 이름은 dbo.[콘텐츠명]_[행위]
--   4. INSERT와 UPDATE는 합쳐서 upsert 하나로
--   5. DELETE는 행을 지우지 않고 delete_ut 에 삭제 시각을 남긴다
--
-- -----------------------------------------------------------------------------
-- 키 설계 -- 모든 id는 Common::UniqueIdGenerator 가 발급한다
-- -----------------------------------------------------------------------------
-- IDENTITY 를 쓰지 않는다. 서버가 메모리에서 먼저 확정하고 클라이언트에 응답한 뒤 DB에
-- 반영하는 구조(UnitOfWork)라, DB가 id를 정하면 그 응답에 담을 id가 없기 때문이다.
--
-- UniqueId 는 [시각 41비트][노드 8비트][시퀀스 14비트] 라 두 가지를 동시에 만족한다.
--   * 전역 유일  -> mail_id 하나만으로 PK가 성립한다 (플레이어별 복합키가 필요 없다)
--   * 시간순 증가 -> 클러스터 인덱스에 append-only 로 쌓여 페이지 분할이 나지 않는다
-- 두 번째 성질은 주장이 아니라 실측 대상이다 -- Sql/id-test.sql 과 검증 결과 문서 참고.
--
-- 세션 id 를 키로 쓰지 않는 이유: 재접속마다 바뀌어서 "같은 사람의 DB 작업이 같은 스레드로
-- 간다"(owner 어피니티)가 깨진다.
-- =============================================================================

-- -----------------------------------------------------------------------------
-- 플레이어 (= 로그인 계정 겸 캐릭터)
--
-- 계정 계층을 두지 않는다. 로그인 서버도 캐릭터 선택창도 만들지 않으므로 계정:캐릭터가
-- 1:1 이고, player_id 하나가 영속 키 역할을 전부 한다.
-- -----------------------------------------------------------------------------

IF OBJECT_ID('dbo.players', 'U') IS NULL
CREATE TABLE dbo.players
(
    player_id     BIGINT        NOT NULL,
    -- 로그인 ID 겸 캐릭터 이름. 기본 콜레이션(대소문자 구분 없음)을 그대로 쓴다 --
    -- "Alice"와 "alice"가 다른 계정이면 사용자가 자기 계정을 못 찾는 사고가 난다.
    player_name   NVARCHAR(32)  NOT NULL,
    -- PBKDF2(HMAC-SHA256) 결과를 "반복횟수.솔트(base64).해시(base64)" 한 문자열로 보관한다.
    -- 반복 횟수를 값 안에 넣어두면 나중에 횟수를 올려도 로그인 시점에 계정을 하나씩
    -- 재해싱할 수 있다(전체 마이그레이션 없이). GmTool 의 운영자 계정도 같은 형식이다.
    password_hash NVARCHAR(255) NOT NULL,
    created_at    DATETIME2(3)  NOT NULL CONSTRAINT df_players_created DEFAULT SYSUTCDATETIME(),
    last_login_at DATETIME2(3)  NULL,
    CONSTRAINT pk_players PRIMARY KEY CLUSTERED (player_id)
);
GO

-- 로그인은 이름으로 조회하므로 넌클러스터 인덱스가 필요하다. UNIQUE 로 두면 같은 이름이
-- 두 번 만들어지는 것도 DB가 막아준다 -- 자동 가입에서 동시 첫 로그인이 겹칠 때의 안전망이다.
IF OBJECT_ID('dbo.players', 'U') IS NOT NULL
   AND NOT EXISTS (SELECT 1 FROM sys.indexes
                    WHERE name = 'uk_players_name' AND object_id = OBJECT_ID('dbo.players'))
CREATE UNIQUE NONCLUSTERED INDEX uk_players_name ON dbo.players (player_name);
GO

-- -----------------------------------------------------------------------------
-- 우편함
--
-- mail_id 가 UniqueId 라 전역 유일이므로 **단독 PK + 클러스터**로 둔다. 시간순으로 커지는
-- 키라 삽입이 항상 맨 뒤에 붙는다.
--
-- delete_ut: 규칙 5(소프트 삭제). 0 이면 살아 있는 우편이다. NULL 이 아니라 0 을 기본값으로
-- 쓰는 이유는 send_ut/end_ut 와 같은 "유닉스 시각(초)" 도메인을 유지하기 위해서다 --
-- 조회 조건도 `delete_ut = 0` 으로 단순해진다.
-- -----------------------------------------------------------------------------

IF OBJECT_ID('dbo.mails', 'U') IS NULL
CREATE TABLE dbo.mails
(
    mail_id   BIGINT         NOT NULL,
    player_id BIGINT         NOT NULL,
    title     NVARCHAR(128)  NOT NULL,
    body      NVARCHAR(1024) NOT NULL,
    -- Zone 의 MailInfo.sendUt / endUt 를 그대로 받는다. 유닉스 시각(초)이라 BIGINT 다.
    -- DATETIME2 로 바꾸지 않는 이유: 서버 메모리·와이어·클라이언트가 전부 정수로 다루는데
    -- DB에서만 형식이 다르면 경계마다 변환이 생기고 그 자리가 버그가 된다.
    send_ut   BIGINT         NOT NULL,
    end_ut    BIGINT         NOT NULL,
    delete_ut BIGINT         NOT NULL CONSTRAINT df_mails_delete_ut DEFAULT 0,
    CONSTRAINT pk_mails PRIMARY KEY CLUSTERED (mail_id),
    CONSTRAINT fk_mails_players FOREIGN KEY (player_id) REFERENCES dbo.players (player_id)
);
GO

-- "그 사람의 살아 있는 우편 전부"가 로그인마다 도는 조회다. player_id 로 넌클러스터를 두고
-- delete_ut 를 키에 함께 넣어, 삭제된 행을 인덱스 단계에서 걸러 키 조회를 줄인다.
IF OBJECT_ID('dbo.mails', 'U') IS NOT NULL
   AND NOT EXISTS (SELECT 1 FROM sys.indexes
                    WHERE name = 'ix_mails_player' AND object_id = OBJECT_ID('dbo.mails'))
CREATE NONCLUSTERED INDEX ix_mails_player ON dbo.mails (player_id, delete_ut);
GO

-- -----------------------------------------------------------------------------
-- 재화
--
-- 종류별로 행을 나눈다(컬럼으로 두지 않는다) -- 재화가 늘 때마다 ALTER TABLE 을 하지 않기
-- 위해서다. Zone 의 Currency::CurrencyModel 도 "종류를 인자로 받는" 형태라 모양이 맞는다.
--
-- 한 플레이어에 종류 수만큼 행이 있으므로 player_id 단독으로는 유일하지 않다. 복합 PK 이고,
-- 클러스터를 (player_id, currency_type) 으로 두면 **그 사람의 재화가 물리적으로 연속**이라
-- 로그인 시 적재가 range seek 한 번으로 끝난다.
--
-- delete_ut 가 없다. 재화는 "삭제"라는 개념이 없고 값이 0이 될 뿐이다 -- 규칙 5는 삭제가
-- 있는 테이블에만 적용한다.
-- -----------------------------------------------------------------------------

IF OBJECT_ID('dbo.currencies', 'U') IS NULL
CREATE TABLE dbo.currencies
(
    player_id     BIGINT   NOT NULL,
    currency_type TINYINT  NOT NULL,
    amount        BIGINT   NOT NULL,
    CONSTRAINT pk_currencies PRIMARY KEY CLUSTERED (player_id, currency_type),
    CONSTRAINT ck_currencies_type CHECK (currency_type > 0),
    CONSTRAINT ck_currencies_amount CHECK (amount >= 0),
    CONSTRAINT fk_currencies_players FOREIGN KEY (player_id) REFERENCES dbo.players (player_id)
);
GO

-- -----------------------------------------------------------------------------
-- 재화 변경 감사 로그
--
-- Zone 의 CurrencyTask 가 새 값과 **이전 값을 둘 다** 실어 보내는 이유가 여기 있다:
-- 잔액만 갱신하면 "누가 언제 얼마에서 얼마로 바뀌었는지"가 남지 않아 재화 사고를 추적할 수
-- 없다. 잔액 UPDATE 와 이 INSERT 는 같은 트랜잭션이다.
--
-- log_id 도 UniqueId 다(규칙: 식별자는 전부 생성기가 발급). 시간순으로 커지는 키라 로그처럼
-- 계속 append 되는 테이블과 특히 잘 맞는다.
-- -----------------------------------------------------------------------------

IF OBJECT_ID('dbo.currency_logs', 'U') IS NULL
CREATE TABLE dbo.currency_logs
(
    log_id        BIGINT       NOT NULL,
    player_id     BIGINT       NOT NULL,
    currency_type TINYINT      NOT NULL,
    old_amount    BIGINT       NOT NULL,
    new_amount    BIGINT       NOT NULL,
    -- Zone 이 발급한 요청 식별자. Zone -> World -> DB 로그를 한 줄로 이어 붙이는 값이다.
    -- 같은 UniqueId 지만 용도가 "요청 추적"이라 컬럼 이름은 request_id 다.
    request_id    BIGINT       NOT NULL,
    logged_at     DATETIME2(3) NOT NULL CONSTRAINT df_currency_logs_at DEFAULT SYSUTCDATETIME(),
    CONSTRAINT pk_currency_logs PRIMARY KEY CLUSTERED (log_id)
);
GO

IF OBJECT_ID('dbo.currency_logs', 'U') IS NOT NULL
   AND NOT EXISTS (SELECT 1 FROM sys.indexes
                    WHERE name = 'ix_currency_logs_player' AND object_id = OBJECT_ID('dbo.currency_logs'))
CREATE NONCLUSTERED INDEX ix_currency_logs_player ON dbo.currency_logs (player_id, log_id);
GO

-- =============================================================================
-- 저장 프로시저
--
-- 서버는 테이블에 직접 쿼리하지 않고 전부 SP 를 거친다. 실질적인 이유는 **SET XACT_ABORT ON
-- 을 SP 안에 강제로 넣을 수 있다는 것**이다 -- 이게 없으면 런타임 오류가 나도 트랜잭션이
-- 열린 채 남아 다음 문장이 반쯤 적용된다.
--
-- 서버 쪽 트랜잭션 경계는 **UnitOfWork 하나 = 트랜잭션 하나**다. 여러 UoW 를 모으지 않는다.
-- 같은 player_id 의 UoW 들은 DB 소비자 어피니티(player_id % N)로 같은 스레드에 배정되므로
-- 도착 순서대로 실행되고, 그래서 순서 보장을 위한 별도 장치가 필요 없다.
--
-- 조회는 규약대로 WITH(NOLOCK) 이다(.claude/rules/sql-patterns.md).
-- =============================================================================

-- 로그인 조회. 비밀번호 비교는 서버에서 한다 -- 해시 비교를 SP 에서 하면 평문이 와이어와
-- DB 로그에 남는다. 계정이 없으면 0행이고, 그때 서버가 players_upsert 로 자동 가입시킨다.
CREATE OR ALTER PROCEDURE dbo.players_select
    @player_name NVARCHAR(32)
AS
BEGIN
    SET NOCOUNT ON;

    SELECT player_id, player_name, password_hash
      FROM dbo.players WITH(NOLOCK)
     WHERE player_name = @player_name;
END
GO

-- 규칙 4: INSERT 와 UPDATE 를 하나로. 자동 가입(없으면 생성)과 로그인 시각 갱신이 같은
-- 경로를 쓴다.
--
-- **확정된 player_id 를 돌려준다.** 같은 이름으로 동시에 첫 로그인이 들어오면 한쪽은 INSERT,
-- 다른 쪽은 유니크 인덱스에 걸린다. 그때도 호출부가 다시 SELECT 하지 않고 여기서 나온 값을
-- 쓰면 되도록, 항상 마지막에 현재 행의 id 를 반환한다.
CREATE OR ALTER PROCEDURE dbo.players_upsert
    @player_id     BIGINT,
    @player_name   NVARCHAR(32),
    @password_hash NVARCHAR(255)
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;

    MERGE dbo.players WITH (HOLDLOCK) AS target
    USING (SELECT @player_name AS player_name) AS source
       ON target.player_name = source.player_name
    WHEN MATCHED THEN
        UPDATE SET last_login_at = SYSUTCDATETIME()
    WHEN NOT MATCHED THEN
        INSERT (player_id, player_name, password_hash, last_login_at)
        VALUES (@player_id, @player_name, @password_hash, SYSUTCDATETIME());

    SELECT player_id FROM dbo.players WITH(NOLOCK) WHERE player_name = @player_name;
END
GO

-- 로그인 직후 World 가 캐시에 적재할 우편함. 삭제된 것은 빼고 준다.
CREATE OR ALTER PROCEDURE dbo.mails_select
    @player_id BIGINT
AS
BEGIN
    SET NOCOUNT ON;

    SELECT mail_id, title, body, send_ut, end_ut
      FROM dbo.mails WITH(NOLOCK)
     WHERE player_id = @player_id
       AND delete_ut = 0
     ORDER BY mail_id;
END
GO

-- 규칙 4: 우편 추가와 내용 변경이 하나의 SP.
--
-- **delete_ut 를 건드리지 않는다.** 이미 삭제된 우편에 upsert 가 들어와도 되살아나면 안 되고,
-- 살아 있는 우편의 삭제 상태를 실수로 지우지도 않아야 한다. 삭제/복구는 mails_delete 만의 일이다.
CREATE OR ALTER PROCEDURE dbo.mails_upsert
    @mail_id   BIGINT,
    @player_id BIGINT,
    @title     NVARCHAR(128),
    @body      NVARCHAR(1024),
    @send_ut   BIGINT,
    @end_ut    BIGINT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;

    MERGE dbo.mails AS target
    USING (SELECT @mail_id AS mail_id) AS source
       ON target.mail_id = source.mail_id
    WHEN MATCHED THEN
        UPDATE SET title   = @title,
                   body    = @body,
                   send_ut = @send_ut,
                   end_ut  = @end_ut
    WHEN NOT MATCHED THEN
        INSERT (mail_id, player_id, title, body, send_ut, end_ut)
        VALUES (@mail_id, @player_id, @title, @body, @send_ut, @end_ut);
END
GO

-- 규칙 5: 행을 지우지 않고 삭제 시각만 남긴다.
--
-- 이름이 mails_delete 인 이유는 **호출부 입장에서 하는 일이 "삭제"이기 때문**이다. 실제 쿼리가
-- UPDATE 라는 건 이 SP 안의 구현 사항이고, 그걸 이름에 드러내면 부르는 쪽이 "왜 삭제인데
-- upsert 를 부르지" 하고 헷갈린다.
--
-- 이미 지워진 우편을 다시 지워도 실패로 보지 않는다(영향 행 0). 만료 스윕과 사용자 삭제가
-- 겹칠 수 있는데, 그건 오류가 아니라 정상적인 경합이다.
CREATE OR ALTER PROCEDURE dbo.mails_delete
    @mail_id   BIGINT,
    @delete_ut BIGINT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;

    UPDATE dbo.mails
       SET delete_ut = @delete_ut
     WHERE mail_id = @mail_id
       AND delete_ut = 0;
END
GO

-- 로그인 직후 World 가 캐시에 적재할 재화 전체.
CREATE OR ALTER PROCEDURE dbo.currencies_select
    @player_id BIGINT
AS
BEGIN
    SET NOCOUNT ON;

    SELECT currency_type, amount
      FROM dbo.currencies WITH(NOLOCK)
     WHERE player_id = @player_id;
END
GO

-- 잔액 갱신 + 감사 로그를 한 트랜잭션으로. 증감이 아니라 **절대값**을 받는다 -- Zone 의
-- CurrencyModel::SetTracked 가 이미 새 값과 이전 값을 확정해서 보내므로, DB 에서 다시
-- 계산하면 두 곳이 서로 다른 값을 권위로 삼게 된다.
CREATE OR ALTER PROCEDURE dbo.currencies_upsert
    @player_id     BIGINT,
    @currency_type TINYINT,
    @old_amount    BIGINT,
    @new_amount    BIGINT,
    @log_id        BIGINT,
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
        (log_id, player_id, currency_type, old_amount, new_amount, request_id)
    VALUES (@log_id, @player_id, @currency_type, @old_amount, @new_amount, @request_id);
END
GO
