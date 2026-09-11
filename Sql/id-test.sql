-- =============================================================================
-- UniqueId 검증용 테이블 / 프로시저
--
-- **게임 스키마가 아니다.** 생성기가 만든 id를 실제 DB에 부어보고 확인하기 위한 도구용이며,
-- 검증이 끝나면 drop한다(`Sql/id-test-drop.sql`).
--
-- 확인하려는 것 (docs/local/의 검증 결과 문서 참고):
--   1. 여러 프로세스 x 여러 스레드가 동시에 발급해도 **중복이 없는가**
--      -> BIGINT 클러스터 PK가 중복을 거부하므로, 넣은 개수 == 행 수 면 통과
--   2. 시간순으로 커지는 키라 **클러스터 인덱스 재배치(페이지 분할)가 없는가**
--      -> avg_fragmentation_in_percent / avg_page_space_used_in_percent 로 판정
--   3. 스레드(DB 레인)별로 **고르게 퍼지는가**
--      -> id % 레인수 분포로 판정
--
-- id 하나만 저장하고 노드 번호/시퀀스는 비트에서 역산한다. 별도 컬럼으로 받으면 "그 컬럼에
-- 넣은 값"을 검증하는 셈이 되어, 정작 **id 안에 제대로 들어갔는지**를 확인하지 못한다.
-- =============================================================================

IF OBJECT_ID('dbo.id_tests', 'U') IS NULL
CREATE TABLE dbo.id_tests
(
    id BIGINT NOT NULL,
    CONSTRAINT pk_id_tests PRIMARY KEY CLUSTERED (id)
);
GO

-- 대조군. 같은 개수를 **무작위 키**로 넣어서 1번 표의 단편화와 비교한다.
-- 대조군이 없으면 "시간순 키라 단편화가 없다"가 측정이 아니라 주장으로 남는다.
IF OBJECT_ID('dbo.id_test_randoms', 'U') IS NULL
CREATE TABLE dbo.id_test_randoms
(
    id BIGINT NOT NULL,
    CONSTRAINT pk_id_test_randoms PRIMARY KEY CLUSTERED (id)
);
GO

CREATE OR ALTER PROCEDURE dbo.id_test_insert
    @id BIGINT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;

    INSERT INTO dbo.id_tests (id) VALUES (@id);
END
GO

CREATE OR ALTER PROCEDURE dbo.id_test_random_insert
    @id BIGINT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;

    INSERT INTO dbo.id_test_randoms (id) VALUES (@id);
END
GO
