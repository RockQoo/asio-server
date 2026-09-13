-- =============================================================================
-- mails -- 우편함
--
-- 적용 순서: players.sql **다음**(player_id 를 FK 로 참조한다).
-- 작성 규약은 .claude/rules/sql-patterns.md 참고.
-- =============================================================================

-- mail_id 가 RUID 라 전역 유일이므로 **단독 PK + 클러스터**로 둔다. 시간순으로 커지는
-- 키라 삽입이 항상 맨 뒤에 붙는다.
--
-- delete_ut: 규칙 5(소프트 삭제). 0 이면 살아 있는 우편이다. NULL 이 아니라 0 을 기본값으로
-- 쓰는 이유는 send_ut/end_ut 와 같은 "유닉스 시각(초)" 도메인을 유지하기 위해서다 --
-- 조회 조건도 `delete_ut = 0` 으로 단순해진다.
IF OBJECT_ID('dbo.mails', 'U') IS NULL
CREATE TABLE dbo.mails
(
    mail_id   BIGINT         NOT NULL,
    player_id BIGINT         NOT NULL,
    title     NVARCHAR(128)  NOT NULL,
    body      NVARCHAR(1024) NOT NULL,
    -- Zone 의 Info.sendUt / endUt 를 그대로 받는다. 유닉스 시각(초)이라 BIGINT 다.
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
-- 로그인 직후 World 가 캐시에 적재할 우편함. 삭제된 것은 빼고 준다.
-- 조회라 0행이 정상이므로 @@ROWCOUNT 검사(⑤)만 없다.
-- -----------------------------------------------------------------------------
CREATE OR ALTER PROCEDURE [dbo].[up_mails_select]
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

        SELECT mail_id, title, body, send_ut, end_ut
          FROM dbo.mails WITH(NOLOCK)
         WHERE player_id = @player_id
           AND delete_ut = 0
         ORDER BY mail_id;

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
-- 규칙 4: 우편 추가와 내용 변경이 하나의 SP.
--
-- **delete_ut 를 건드리지 않는다.** 이미 삭제된 우편에 upsert 가 들어와도 되살아나면 안 되고,
-- 살아 있는 우편의 삭제 상태를 실수로 지우지도 않아야 한다. 삭제/복구는 up_mails_delete 만의 일이다.
-- -----------------------------------------------------------------------------
CREATE OR ALTER PROCEDURE [dbo].[up_mails_upsert]
    @is_trans_outside TINYINT,
    @mail_id          BIGINT,
    @player_id        BIGINT,
    @title            NVARCHAR(128),
    @body             NVARCHAR(1024),
    @send_ut          BIGINT,
    @end_ut           BIGINT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    DECLARE @ret_val INT = 0;

    IF @is_trans_outside = 0 AND XACT_STATE() = -1
    BEGIN SET @ret_val = 0xA0000001; GOTO ErrorHandler; END;

    BEGIN TRY
        IF @is_trans_outside = 0 BEGIN TRANSACTION;

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

        IF @@ROWCOUNT <> 1
        BEGIN SET @ret_val = 0xA0020001; GOTO ErrorHandler; END;

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
-- 규칙 5: 행을 지우지 않고 삭제 시각만 남긴다.
--
-- 이름이 up_mails_delete 인 이유는 **호출부 입장에서 하는 일이 "삭제"이기 때문**이다. 실제
-- 쿼리가 UPDATE 라는 건 이 SP 안의 구현 사항이고, 그걸 이름에 드러내면 부르는 쪽이 "왜
-- 삭제인데 upsert 를 부르지" 하고 헷갈린다.
--
-- **영향 행 0 을 거절로 보지 않는다.** 만료 스윕과 사용자 삭제가 겹칠 수 있는데, 그건 오류가
-- 아니라 정상적인 경합이다. 그래서 여기만 @@ROWCOUNT 검사가 없다.
-- -----------------------------------------------------------------------------
CREATE OR ALTER PROCEDURE [dbo].[up_mails_delete]
    @is_trans_outside TINYINT,
    @mail_id          BIGINT,
    @delete_ut        BIGINT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    DECLARE @ret_val INT = 0;

    IF @is_trans_outside = 0 AND XACT_STATE() = -1
    BEGIN SET @ret_val = 0xA0000001; GOTO ErrorHandler; END;

    BEGIN TRY
        IF @is_trans_outside = 0 BEGIN TRANSACTION;

        UPDATE dbo.mails
           SET delete_ut = @delete_ut
         WHERE mail_id = @mail_id
           AND delete_ut = 0;

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
