-- =============================================================================
-- RUID 검증 결과 확인 쿼리
--
-- 앞서 WorldServer.exe --idtest 로 채운 dbo.id_tests / dbo.id_test_randoms 를 읽는다.
-- 실행: sqlcmd -S 127.0.0.1,1433 -U sa -P 0000 -C -d asio_game -i Sqlverify_unique_keys.sql
--
-- 노드 번호와 시퀀스는 **컬럼이 아니라 id 비트에서 역산**한다. 별도 컬럼으로 받아 두면
-- "그 컬럼에 넣은 값"을 검증하는 셈이 되어, 정작 id 안에 제대로 들어갔는지를 확인하지 못한다.
--   node     = (id / 4096) % 1024     -- 시퀀스 12비트를 버리고 노드 10비트만
--   sequence = id % 4096
-- =============================================================================

SET NOCOUNT ON;

PRINT '--- 1) 중복 검증: 생성 개수 == 행 수 == 고유 id 수 ---';
SELECT COUNT(*) AS total_rows, COUNT(DISTINCT id) AS distinct_ids
  FROM dbo.id_tests WITH(NOLOCK);

PRINT '';
PRINT '--- 2) 노드별 분배 (프로세스가 실제로 다른 노드 번호를 썼는가) ---';
SELECT (id / 4096) % 1024 AS node_id, COUNT(*) AS cnt
  FROM dbo.id_tests WITH(NOLOCK)
 GROUP BY (id / 4096) % 1024
 ORDER BY node_id;

PRINT '';
PRINT '--- 3) DB 레인 분배: id % 8 (샤딩 키가 고르게 퍼지는가) ---';
SELECT id % 8 AS lane,
       COUNT(*) AS cnt,
       CAST(COUNT(*) * 100.0 / SUM(COUNT(*)) OVER () AS DECIMAL(5,2)) AS pct
  FROM dbo.id_tests WITH(NOLOCK)
 GROUP BY id % 8
 ORDER BY lane;

PRINT '';
PRINT '--- 4) 단편화: 시간순 키(id_tests) vs 무작위 키(id_test_randoms) ---';
PRINT '    frag_pct 가 낮고 page_fill_pct 가 높아야 "재배치가 없다"가 성립한다.';
SELECT OBJECT_NAME(s.object_id) AS table_name,
       s.page_count,
       CAST(s.avg_fragmentation_in_percent AS DECIMAL(6,3)) AS frag_pct,
       CAST(s.avg_page_space_used_in_percent AS DECIMAL(6,2)) AS page_fill_pct,
       s.fragment_count
  FROM sys.dm_db_index_physical_stats(DB_ID(), NULL, NULL, NULL, 'DETAILED') s
 WHERE s.index_level = 0
   AND OBJECT_NAME(s.object_id) IN ('id_tests', 'id_test_randoms')
 ORDER BY table_name;
