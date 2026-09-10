-- =============================================================================
-- asio_game 개발/테스트용 시드 데이터
--
-- 실행: bat\setup_game_db.bat 가 schema.sql 다음에 자동으로 적용한다.
--       수동으로 하려면:
--         sqlcmd -S 127.0.0.1,1433 -U asio_game -P 'GmTool1234!' -d asio_game -C -i Sql/seed.sql
--
-- 멱등하다. 이미 있는 이름은 건너뛰므로 여러 번 실행해도 중복이 생기지 않는다.
--
-- -----------------------------------------------------------------------------
-- ⚠ 이 시드의 비밀번호 해시는 테스트 전용이다
-- -----------------------------------------------------------------------------
-- 모든 시드 계정이 **같은 솔트와 같은 비밀번호**를 쓴다. 실제 계정이라면 계정마다 난수 솔트를
-- 써야 하고(레인보우 테이블/한 계정이 뚫리면 전부 뚫리는 문제), 회원가입 경로가 생기면 거기서
-- 계정마다 새 솔트를 뽑는다. 여기서 하나로 고정한 이유는 단순하다 -- 1만 세션 부하 테스트용
-- 계정 2만 개를 만드는데 솔트가 제각각이면 이 스크립트로는 만들 수 없기 때문이다.
--
-- 값의 형식은 schema.sql의 players.password_hash 주석 참고: "반복횟수.솔트(base64).해시(base64)"
--   비밀번호  : 0000
--   반복 횟수 : 1000
--
-- **반복 횟수 1000은 실서비스 값이 아니다.** 로그인 1만 건이 동시에 몰리는 부하 테스트에서
-- PBKDF2 CPU가 DB 커넥션 대기 시간을 가려버리면 정작 재려던 값을 못 잰다. 실서비스라면
-- 10만 회 이상으로 올려야 하고, 그때는 password_hash 앞의 반복 횟수만 바꾸면 로그인 시점에
-- 계정을 하나씩 재해싱할 수 있다(schema.sql 주석 참고).
-- =============================================================================

SET NOCOUNT ON;
GO

DECLARE @hash NVARCHAR(255) =
    N'1000.Bw4VHCMqMTg/Rk1UW2JpcA==.blkrC5KZy5okhw2ctnjQq3O1De5yzdJWA06yzeMPvFI=';

-- -----------------------------------------------------------------------------
-- 수동 확인용 계정 (ProtocolClient / Client)
-- -----------------------------------------------------------------------------
-- Client(MonoGame)는 창을 여러 개 띄워 브로드캐스트와 핸드오프를 보므로 최소 2개가 필요하다.
-- 4개를 만들어 두면 존 2x2 격자에 하나씩 배치해 보기 편하다.
INSERT INTO dbo.players (player_name, password_hash)
SELECT name, @hash
  FROM (VALUES (N'tester1'), (N'tester2'), (N'tester3'), (N'tester4')) AS v(name)
 WHERE NOT EXISTS (SELECT 1 FROM dbo.players p WHERE p.player_name = v.name);
GO

-- -----------------------------------------------------------------------------
-- 부하 테스트용 계정 stress_00001 ~ stress_20000
-- -----------------------------------------------------------------------------
-- 2만 개인 이유: StressClient가 1만 세션까지 실측하는데, 계정이 세션 수와 같으면 "계정이
-- 모자라서 실패한 것"과 "서버가 못 받아서 실패한 것"이 구분되지 않는다. 여유를 둔다.
--
-- 숫자 테이블을 만들지 않고 재귀 CTE로 행을 만든다. MAXRECURSION 0은 재귀 깊이 제한(기본
-- 100)을 푸는 것이라 2만 행에 반드시 필요하다.
DECLARE @seedHash NVARCHAR(255) =
    N'1000.Bw4VHCMqMTg/Rk1UW2JpcA==.blkrC5KZy5okhw2ctnjQq3O1De5yzdJWA06yzeMPvFI=';

WITH numbers AS
(
    SELECT 1 AS n
    UNION ALL
    SELECT n + 1 FROM numbers WHERE n < 20000
)
INSERT INTO dbo.players (player_name, password_hash)
SELECT v.name, @seedHash
  FROM (SELECT N'stress_' + RIGHT(N'00000' + CAST(n AS NVARCHAR(10)), 5) AS name
          FROM numbers) AS v
 WHERE NOT EXISTS (SELECT 1 FROM dbo.players p WHERE p.player_name = v.name)
OPTION (MAXRECURSION 0);
GO

-- -----------------------------------------------------------------------------
-- 초기 재화
-- -----------------------------------------------------------------------------
-- currency_type 1 = Gold (Shared/Protocol/Src/CurrencyType.h). 0은 "종류 없음" 예약값이라
-- 쓰지 않는다.
--
-- 1000은 Zone의 CurrencyModel::gold_ 초기값과 같은 값이다. DB 연동이 끝나면 그 하드코딩된
-- 초기값은 0이 되고 여기서 읽은 값을 SetCurrency로 채우는 경로만 남는다(CurrencyModel 주석).
INSERT INTO dbo.currencies (player_id, currency_type, amount)
SELECT p.player_id, 1, 1000
  FROM dbo.players p
 WHERE NOT EXISTS (SELECT 1
                     FROM dbo.currencies c
                    WHERE c.player_id = p.player_id AND c.currency_type = 1);
GO

PRINT '[seed] 완료';
GO

SELECT '계정 수' AS 항목, CAST(COUNT(*) AS NVARCHAR(20)) AS 값 FROM dbo.players
UNION ALL
SELECT '재화 행 수', CAST(COUNT(*) AS NVARCHAR(20)) FROM dbo.currencies;
GO
