-- =============================================================================
-- players -- 플레이어(로그인 계정 겸 캐릭터)
--
-- 적용 순서: **가장 먼저**. mails/currencies 가 player_id 를 FK 로 참조한다.
-- 작성 규약은 .claude/rules/sql-patterns.md 참고.
--
-- 계정 계층을 두지 않는다. 로그인 서버도 캐릭터 선택창도 만들지 않으므로 계정:캐릭터가
-- 1:1 이고, player_id 하나가 영속 키 역할을 전부 한다.
-- =============================================================================

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
-- 로그인 조회. 비밀번호 비교는 서버에서 한다 -- 해시 비교를 SP 에서 하면 평문이 와이어와
-- DB 로그에 남는다. 계정이 없으면 0행이고, 그때 서버가 usp_players_upsert 로 자동 가입시킨다.
--
-- 조회라 0행이 정상이므로 @@ROWCOUNT 검사(⑤)만 없다. 나머지 골격은 쓰기 SP 와 같다.
-- -----------------------------------------------------------------------------
CREATE OR ALTER PROCEDURE [dbo].[usp_players_select]
    @is_trans_outside TINYINT,
    @player_name      NVARCHAR(32)
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    DECLARE @ret_val INT = 0;

    IF @is_trans_outside = 0 AND XACT_STATE() = -1
    BEGIN SET @ret_val = 0xA0000001; GOTO ErrorHandler; END;

    BEGIN TRY
        IF @is_trans_outside = 0 BEGIN TRANSACTION;

        SELECT player_id, player_name, password_hash
          FROM dbo.players WITH(NOLOCK)
         WHERE player_name = @player_name;

        IF @is_trans_outside = 0 COMMIT TRANSACTION;
    END TRY
    BEGIN CATCH
        SET @ret_val = 0xA0000000 + ERROR_NUMBER();
        GOTO ErrorHandler;
    END CATCH

ErrorHandler:
    IF @is_trans_outside = 0 AND XACT_STATE() <> 0
        ROLLBACK TRANSACTION;

    RETURN @ret_val;
END
GO

-- -----------------------------------------------------------------------------
-- 규칙 4: INSERT 와 UPDATE 를 하나로. 자동 가입(없으면 생성)과 로그인 시각 갱신이 같은
-- 경로를 쓴다.
--
-- **확정된 player_id 를 돌려준다.** 같은 이름으로 동시에 첫 로그인이 들어오면 한쪽은 INSERT,
-- 다른 쪽은 유니크 인덱스에 걸린다. 그때도 호출부가 다시 SELECT 하지 않고 여기서 나온 값을
-- 쓰면 되도록, 항상 마지막에 현재 행의 id 를 반환한다.
--
-- MERGE 에 HOLDLOCK 을 붙인 이유: 이 SP 만 여러 세션이 같은 키로 동시에 칠 수 있다(첫 로그인).
-- 나머지 SP 는 같은 player_id 가 항상 같은 DB 레인으로 가서 동시 실행이 없다.
-- -----------------------------------------------------------------------------
CREATE OR ALTER PROCEDURE [dbo].[usp_players_upsert]
    @is_trans_outside TINYINT,
    @player_id        BIGINT,
    @player_name      NVARCHAR(32),
    @password_hash    NVARCHAR(255)
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    DECLARE @ret_val INT = 0;

    IF @is_trans_outside = 0 AND XACT_STATE() = -1
    BEGIN SET @ret_val = 0xA0000001; GOTO ErrorHandler; END;

    BEGIN TRY
        IF @is_trans_outside = 0 BEGIN TRANSACTION;

        MERGE dbo.players WITH (HOLDLOCK) AS target
        USING (SELECT @player_name AS player_name) AS source
           ON target.player_name = source.player_name
        WHEN MATCHED THEN
            UPDATE SET last_login_at = SYSUTCDATETIME()
        WHEN NOT MATCHED THEN
            INSERT (player_id, player_name, password_hash, last_login_at)
            VALUES (@player_id, @player_name, @password_hash, SYSUTCDATETIME());

        -- MERGE 는 한 행을 정확히 한 번 처리해야 한다. 0 이면 조건이 어긋난 것이고 2 이상이면
        -- 이름 유니크 제약이 깨진 것이라, 둘 다 조용히 넘기면 안 된다.
        IF @@ROWCOUNT <> 1
        BEGIN SET @ret_val = 0xA0010001; GOTO ErrorHandler; END;

        SELECT player_id FROM dbo.players WITH(NOLOCK) WHERE player_name = @player_name;

        IF @is_trans_outside = 0 COMMIT TRANSACTION;
    END TRY
    BEGIN CATCH
        SET @ret_val = 0xA0000000 + ERROR_NUMBER();
        GOTO ErrorHandler;
    END CATCH

ErrorHandler:
    IF @is_trans_outside = 0 AND XACT_STATE() <> 0
        ROLLBACK TRANSACTION;

    RETURN @ret_val;
END
GO

-- -----------------------------------------------------------------------------
-- 로그인 성공 직후 World 가 캐시에 적재할 콘텐츠 전부를 **한 번의 왕복으로** 가져온다.
--
-- 결과 집합 두 개를 순서대로 낸다:
--   ① 우편  mail_id, title, body, send_ut, end_ut
--   ② 재화  currency_type, amount
--
-- 이 순서가 곧 계약이다 -- World 의 LoginProcessor 가 인덱스 0/1 로 꺼낸다. 집합을
-- 추가할 일이 생기면 **뒤에 붙인다.** 중간에 끼우면 읽는 쪽이 조용히 밀린다.
--
-- **왜 usp_mails_select / usp_currencies_select 둘을 부르지 않고 SP 를 새로 만들었나**:
-- 로그인은 사람이 기다리는 경로라 왕복 수가 그대로 체감 지연이 된다. 둘을 따로 부르면
-- 커넥션 왕복이 두 번이고, 그 사이에 다른 작업이 끼어들어 두 조회가 서로 다른 시점을
-- 보게 된다(우편은 적재됐는데 재화는 그 뒤 상태).
--
-- **재화 조회에만 WITH(NOLOCK) 이 없다.** sql-patterns.md 의 조회 기본값은 NOLOCK 이지만
-- 같은 문서가 "한 행도 놓치면 안 되는 집계"를 예외로 둔다. 이 SP 가 정확히 그 자리다 --
-- 여기서 읽은 잔액이 World 의 권위 캐시가 되고, 짝이 되는 usp_currencies_upsert 가
-- 증감이 아니라 **절대값**을 쓰기 때문에 읽기 한 번의 오차가 다음 쓰기에서 확정된다
-- (행을 놓치면 0 으로 캐시돼 잔액이 소멸한다). 우편은 놓쳐도 다시 받으면 되므로 그대로 둔다.
--
-- 조회라 0행이 정상이므로 @@ROWCOUNT 검사(⑤)만 없다. 나머지 골격은 쓰기 SP 와 같다.
-- -----------------------------------------------------------------------------
CREATE OR ALTER PROCEDURE [dbo].[usp_players_load]
    @is_trans_outside TINYINT,
    @player_id        BIGINT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    DECLARE @ret_val INT = 0;

    IF @is_trans_outside = 0 AND XACT_STATE() = -1
    BEGIN SET @ret_val = 0xA0000001; GOTO ErrorHandler; END;

    BEGIN TRY
        IF @is_trans_outside = 0 BEGIN TRANSACTION;

        -- ① 우편. delete_ut 가 0 이 아닌 것은 이미 지워진 것이라 제외한다.
        SELECT mail_id, title, body, send_ut, end_ut
          FROM dbo.mails WITH(NOLOCK)
         WHERE player_id = @player_id
           AND delete_ut = 0
         ORDER BY mail_id;

        -- ② 재화. 위 주석대로 여기만 NOLOCK 을 빼둔다.
        SELECT currency_type, amount
          FROM dbo.currencies
         WHERE player_id = @player_id
         ORDER BY currency_type;

        IF @is_trans_outside = 0 COMMIT TRANSACTION;
    END TRY
    BEGIN CATCH
        SET @ret_val = 0xA0000000 + ERROR_NUMBER();
        GOTO ErrorHandler;
    END CATCH

ErrorHandler:
    IF @is_trans_outside = 0 AND XACT_STATE() <> 0
        ROLLBACK TRANSACTION;

    RETURN @ret_val;
END
GO
