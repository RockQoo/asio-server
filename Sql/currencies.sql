-- =============================================================================
-- currencies -- 재화
--
-- 적용 순서: players.sql **다음**(player_id 를 FK 로 참조한다).
-- 작성 규약은 .claude/rules/sql-patterns.md 참고.
--
-- **감사 로그(currency_logs)는 아직 없다.** 잔액이 "언제 얼마에서 얼마로" 바뀌었는지 남기는
-- 테이블은 나중에 따로 붙인다. 그때 이 파일에 같이 들어온다 -- 잔액 갱신과 로그 기록은 같은
-- 트랜잭션 안에서만 의미가 있어서 파일을 나누면 둘이 별개로 다뤄진다.
-- =============================================================================

-- 종류별로 행을 나눈다(컬럼으로 두지 않는다) -- 재화가 늘 때마다 ALTER TABLE 을 하지 않기
-- 위해서다. Zone 의 Currency::Model 도 "종류를 인자로 받는" 형태라 모양이 맞는다.
--
-- 한 플레이어에 종류 수만큼 행이 있으므로 player_id 단독으로는 유일하지 않다. 복합 PK 이고,
-- 클러스터를 (player_id, currency_type) 으로 두면 **그 사람의 재화가 물리적으로 연속**이라
-- 로그인 시 적재가 range seek 한 번으로 끝난다.
--
-- delete_ut 가 없다. 재화는 "삭제"라는 개념이 없고 값이 0이 될 뿐이다 -- 규칙 5는 삭제가
-- 있는 테이블에만 적용한다.
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
-- 로그인 직후 World 가 캐시에 적재할 재화 전체.
-- 조회라 0행이 정상이므로 @@ROWCOUNT 검사(⑤)만 없다.
-- -----------------------------------------------------------------------------
CREATE OR ALTER PROCEDURE [dbo].[up_currencies_select]
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

        SELECT currency_type, amount
          FROM dbo.currencies WITH(NOLOCK)
         WHERE player_id = @player_id;

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
-- 잔액 갱신. 증감이 아니라 **절대값**을 받는다 -- Zone 의 Model::SetTracked 가 이미
-- 새 값과 이전 값을 확정해서 보내므로, DB 에서 다시 계산하면 두 곳이 서로 다른 값을 권위로
-- 삼게 된다.
--
-- 감사 로그가 붙으면 여기에 INSERT 가 하나 더 들어와 문장이 두 개가 된다 -- 그때가 조건부
-- 트랜잭션이 실제로 값을 하는 자리다(하나만 적용된 채 남으면 안 되므로).
-- -----------------------------------------------------------------------------
CREATE OR ALTER PROCEDURE [dbo].[up_currencies_upsert]
    @is_trans_outside TINYINT,
    @player_id        BIGINT,
    @currency_type    TINYINT,
    @amount           BIGINT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    DECLARE @ret_val INT = 0;

    IF @is_trans_outside = 0 AND XACT_STATE() = -1
    BEGIN SET @ret_val = 0xA0000001; GOTO ErrorHandler; END;

    BEGIN TRY
        IF @is_trans_outside = 0 BEGIN TRANSACTION;

        MERGE dbo.currencies AS target
        USING (SELECT @player_id AS player_id, @currency_type AS currency_type) AS source
           ON target.player_id = source.player_id
          AND target.currency_type = source.currency_type
        WHEN MATCHED THEN
            UPDATE SET amount = @amount
        WHEN NOT MATCHED THEN
            INSERT (player_id, currency_type, amount)
            VALUES (@player_id, @currency_type, @amount);

        IF @@ROWCOUNT <> 1
        BEGIN SET @ret_val = 0xA0030001; GOTO ErrorHandler; END;

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
