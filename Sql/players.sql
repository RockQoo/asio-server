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
-- DB 로그에 남는다. 계정이 없으면 0행이고, 그때 서버가 up_players_upsert 로 자동 가입시킨다.
--
-- 조회라 0행이 정상이므로 @@ROWCOUNT 검사(⑤)만 없다. 나머지 골격은 쓰기 SP 와 같다.
-- -----------------------------------------------------------------------------
CREATE OR ALTER PROCEDURE [dbo].[up_players_select]
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
CREATE OR ALTER PROCEDURE [dbo].[up_players_upsert]
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
