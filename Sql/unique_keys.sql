-- =============================================================================
-- unique_keys -- RUID 생성기 검증용 테이블 / 프로시저
--
-- **게임 스키마가 아니다.** 생성기가 만든 id 를 실제 DB 에 부어보고 확인하기 위한 도구용이다.
-- 다른 파일과 달리 FK 가 없어 적용 순서에 제약이 없다.
--
-- 확인하려는 것 (docs/local/ 의 검증 결과 문서 참고):
--   1. 여러 프로세스 x 여러 스레드가 동시에 발급해도 **중복이 없는가**
--      -> BIGINT 클러스터 PK 가 중복을 거부하므로, 넣은 개수 == 행 수 면 통과
--   2. 시간순으로 커지는 키라 **클러스터 인덱스 재배치(페이지 분할)가 없는가**
--      -> avg_fragmentation_in_percent / avg_page_space_used_in_percent 로 판정
--   3. 스레드(DB 레인)별로 **고르게 퍼지는가**
--      -> id % 레인수 분포로 판정
--
-- id 하나만 저장하고 노드 번호/시퀀스는 비트에서 역산한다. 별도 컬럼으로 받으면 "그 컬럼에
-- 넣은 값"을 검증하는 셈이 되어, 정작 **id 안에 제대로 들어갔는지**를 확인하지 못한다.
--
-- 결과 확인은 Sql/verify_unique_keys.sql.
-- =============================================================================

IF OBJECT_ID('dbo.id_tests', 'U') IS NULL
CREATE TABLE dbo.id_tests
(
    id BIGINT NOT NULL,
    CONSTRAINT pk_id_tests PRIMARY KEY CLUSTERED (id)
);
GO

-- 대조군. 같은 개수를 **무작위 키**로 넣어서 단편화를 비교한다.
-- 대조군이 없으면 "시간순 키라 단편화가 없다"가 측정이 아니라 주장으로 남는다.
IF OBJECT_ID('dbo.id_test_randoms', 'U') IS NULL
CREATE TABLE dbo.id_test_randoms
(
    id BIGINT NOT NULL,
    CONSTRAINT pk_id_test_randoms PRIMARY KEY CLUSTERED (id)
);
GO

-- -----------------------------------------------------------------------------
-- 삽입 SP 두 개. 골격은 다른 SP 와 같다.
--
-- **호출부가 RETURN 값을 읽지 못한다는 점만 다르다.** ExecuteMany(ODBC 파라미터 배열)로
-- 수천 건을 한 번에 보내는 경로라, 출력 파라미터가 그중 어느 건의 결과인지 말해주지 못한다.
-- 그 경로는 배치 하나가 통째로 성공하거나 실패하는 것이 맞는 단위라 ODBC 오류만 본다.
-- SP 쪽 모양까지 다르게 둘 이유는 없으므로 골격은 그대로 지킨다.
--
-- 배치는 바깥에서 트랜잭션으로 묶여 들어오므로(@is_trans_outside=1) 여기서 건별 트랜잭션이
-- 열리지 않는다 -- 열리면 건당 커밋이 되어 실측 기준 95배 느려진다.
-- -----------------------------------------------------------------------------
CREATE OR ALTER PROCEDURE [dbo].[usp_unique_keys_insert]
    @is_trans_outside TINYINT,
    @id               BIGINT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    DECLARE @ret_val INT = 0;

    IF @is_trans_outside = 0 AND XACT_STATE() = -1
    BEGIN SET @ret_val = 0xA0000001; GOTO ErrorHandler; END;

    BEGIN TRY
        IF @is_trans_outside = 0 BEGIN TRANSACTION;

        INSERT INTO dbo.id_tests (id) VALUES (@id);

        IF @@ROWCOUNT <> 1
        BEGIN SET @ret_val = 0xA0040001; GOTO ErrorHandler; END;

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

CREATE OR ALTER PROCEDURE [dbo].[usp_unique_keys_random_insert]
    @is_trans_outside TINYINT,
    @id               BIGINT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    DECLARE @ret_val INT = 0;

    IF @is_trans_outside = 0 AND XACT_STATE() = -1
    BEGIN SET @ret_val = 0xA0000001; GOTO ErrorHandler; END;

    BEGIN TRY
        IF @is_trans_outside = 0 BEGIN TRANSACTION;

        INSERT INTO dbo.id_test_randoms (id) VALUES (@id);

        IF @@ROWCOUNT <> 1
        BEGIN SET @ret_val = 0xA0040002; GOTO ErrorHandler; END;

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
-- 이 노드가 실제로 몇 행을 남겼는지 센다.
--
-- **왜 필요한가**: 삽입 SP 가 골격대로 CATCH 를 갖게 되면서, 중복 키 오류가 예외로 올라오지
-- 않고 @ret_val 로 바뀐다. 그런데 배치 경로(ExecuteMany)는 그 값을 읽을 수 없다 -- 즉
-- 중복이 나도 조용히 건너뛴 채 성공으로 보인다. 그래서 "넣은 개수 == 행 수"를 삽입이 끝난 뒤
-- 직접 확인한다(이 파일 헤더의 확인 항목 1번이 원래 그 방법이다).
--
-- 노드 번호로 거르는 이유는 여러 프로세스가 같은 테이블에 동시에 넣기 때문이다 -- 전체 행 수로는
-- 어느 프로세스 몫인지 가릴 수 없다. 비트 배치(시퀀스 12비트 / 노드 10비트)를 SQL 이 복제하는
-- 셈이라, C++ 쪽 RUID 비트를 바꾸면 여기도 같이 고쳐야 한다.
-- -----------------------------------------------------------------------------
CREATE OR ALTER PROCEDURE [dbo].[usp_unique_keys_count_by_node]
    @is_trans_outside TINYINT,
    @node_id          BIGINT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;
    DECLARE @ret_val INT = 0;

    IF @is_trans_outside = 0 AND XACT_STATE() = -1
    BEGIN SET @ret_val = 0xA0000001; GOTO ErrorHandler; END;

    BEGIN TRY
        IF @is_trans_outside = 0 BEGIN TRANSACTION;

        SELECT COUNT_BIG(*) AS row_count
          FROM dbo.id_tests WITH(NOLOCK)
         WHERE (id / 4096) % 1024 = @node_id;

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
