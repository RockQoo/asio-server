-- =============================================================================
-- asio_game 개발/테스트용 시드 데이터
--
-- 실행: bat\setup_game_db.bat 가 schema.sql 다음에 자동으로 적용한다.
-- 멱등하다. 이미 있는 이름은 건너뛰므로 여러 번 실행해도 중복이 생기지 않는다.
--
-- -----------------------------------------------------------------------------
-- player_id 를 T-SQL 에서 만드는 방법과 그게 안전한 이유
-- -----------------------------------------------------------------------------
-- 스키마상 player_id 는 IDENTITY 가 아니라 Common::UniqueIdGenerator 가 발급하는 값이다.
-- 시드는 서버를 거치지 않으므로 여기서 같은 비트 배치를 직접 조립한다.
--
--   [시각 41비트][노드 8비트][시퀀스 14비트]   ->  (ms << 22) | (node << 14) | seq
--
-- **노드 번호 254 를 쓴다.** 254 는 시드/도구 전용으로 예약돼 있어서(UniqueId.h 대역표)
-- 실행 중인 어느 프로세스도 쓰지 않는다. 그래서 시드가 만든 id 와 서버가 실제로 발급한 id 는
-- **시각이 겹쳐도 충돌하지 않는다** -- 노드 칸이 다르기 때문이다.
--
-- 시퀀스는 14비트(16,384)라 한 밀리초에 그 이상은 못 담는다. 그래서 16,384개마다 밀리초를
-- 1 올린다(아래 n / 16384).
--
-- -----------------------------------------------------------------------------
-- 비밀번호는 전부 0000 (개발 전용)
-- -----------------------------------------------------------------------------
-- 모든 시드 계정이 같은 솔트와 같은 비밀번호를 쓴다. 실제 계정이라면 계정마다 난수 솔트를
-- 써야 하지만, 부하 테스트용 계정 2만 개를 스크립트로 만들려면 고정할 수밖에 없다.
--
-- 형식은 schema.sql 의 players.password_hash 주석 참고: "반복횟수.솔트(base64).해시(base64)"
-- **반복 횟수 1000 은 실서비스 값이 아니다** -- 로그인 1만 건이 몰리는 부하 테스트에서 PBKDF2
-- CPU 가 DB 커넥션 대기 시간을 가려버리면 정작 재려던 값을 못 잰다.
-- =============================================================================

SET NOCOUNT ON;
GO

-- UniqueId 비트 배치 상수. 2^22 = 4194304 (노드 8비트 + 시퀀스 14비트), 2^14 = 16384.
DECLARE @hash      NVARCHAR(255) =
    N'1000.Bw4VHCMqMTg/Rk1UW2JpcA==.blkrC5KZy5okhw2ctnjQq3O1De5yzdJWA06yzeMPvFI=';
DECLARE @nodeShift BIGINT = 16384;
DECLARE @timeShift BIGINT = 4194304;
DECLARE @seedNode  BIGINT = 254;
DECLARE @baseMs    BIGINT = DATEDIFF_BIG(millisecond, '2026-01-01T00:00:00', SYSUTCDATETIME());

-- -----------------------------------------------------------------------------
-- 수동 확인용 계정 (ProtocolClient / Client)
-- -----------------------------------------------------------------------------
-- Client(MonoGame)는 창을 여러 개 띄워 브로드캐스트와 핸드오프를 보므로 최소 2개가 필요하다.
-- 4개면 존 2x2 격자에 하나씩 배치해 볼 수 있다.
INSERT INTO dbo.players (player_id, player_name, password_hash)
SELECT ((@baseMs + (v.n / 16384)) * @timeShift) + (@seedNode * @nodeShift) + (v.n % 16384),
       v.name, @hash
  FROM (VALUES (0, N'tester1'), (1, N'tester2'), (2, N'tester3'), (3, N'tester4')) AS v(n, name)
 WHERE NOT EXISTS (SELECT 1 FROM dbo.players p WITH(NOLOCK) WHERE p.player_name = v.name);
GO

-- -----------------------------------------------------------------------------
-- 부하 테스트용 계정 stress_00001 ~ stress_20000
-- -----------------------------------------------------------------------------
-- 2만 개인 이유: StressClient 가 1만 세션까지 실측하는데, 계정이 세션 수와 같으면 "계정이
-- 모자라서 실패한 것"과 "서버가 못 받아서 실패한 것"이 구분되지 않는다. 여유를 둔다.
--
-- 재귀 CTE 로 행을 만든다. MAXRECURSION 0 은 재귀 깊이 제한(기본 100)을 푸는 것이라 필수다.
DECLARE @seedHash  NVARCHAR(255) =
    N'1000.Bw4VHCMqMTg/Rk1UW2JpcA==.blkrC5KZy5okhw2ctnjQq3O1De5yzdJWA06yzeMPvFI=';
DECLARE @nodeShift2 BIGINT = 16384;
DECLARE @timeShift2 BIGINT = 4194304;
DECLARE @seedNode2  BIGINT = 254;
-- 위 tester 계정과 겹치지 않도록 밀리초를 넉넉히 띄운다.
DECLARE @baseMs2    BIGINT = DATEDIFF_BIG(millisecond, '2026-01-01T00:00:00', SYSUTCDATETIME()) + 1000;

WITH numbers AS
(
    SELECT 1 AS n
    UNION ALL
    SELECT n + 1 FROM numbers WHERE n < 20000
)
INSERT INTO dbo.players (player_id, player_name, password_hash)
SELECT ((@baseMs2 + (v.n / 16384)) * @timeShift2) + (@seedNode2 * @nodeShift2) + (v.n % 16384),
       v.name, @seedHash
  FROM (SELECT n, N'stress_' + RIGHT(N'00000' + CAST(n AS NVARCHAR(10)), 5) AS name
          FROM numbers) AS v
 WHERE NOT EXISTS (SELECT 1 FROM dbo.players p WITH(NOLOCK) WHERE p.player_name = v.name)
OPTION (MAXRECURSION 0);
GO

-- -----------------------------------------------------------------------------
-- 초기 재화
-- -----------------------------------------------------------------------------
-- currency_type 1 = Gold (Shared/Protocol/Src/CurrencyType.h). 0 은 "종류 없음" 예약값이다.
--
-- 1000 은 Zone 의 CurrencyModel::gold_ 초기값과 같은 값이다. DB 연동이 끝나면 그 하드코딩된
-- 초기값은 0 이 되고 여기서 읽은 값을 SetCurrency 로 채우는 경로만 남는다.
INSERT INTO dbo.currencies (player_id, currency_type, amount)
SELECT p.player_id, 1, 1000
  FROM dbo.players p WITH(NOLOCK)
 WHERE NOT EXISTS (SELECT 1 FROM dbo.currencies c WITH(NOLOCK)
                    WHERE c.player_id = p.player_id AND c.currency_type = 1);
GO

-- 검증: 계정 수 / 재화 행 수 / player_id 의 노드 칸이 전부 254(시드)인지
SELECT 'players'    AS item, CAST(COUNT(*) AS NVARCHAR(20)) AS value FROM dbo.players WITH(NOLOCK)
UNION ALL
SELECT 'currencies', CAST(COUNT(*) AS NVARCHAR(20)) FROM dbo.currencies WITH(NOLOCK)
UNION ALL
SELECT 'non_seed_node', CAST(COUNT(*) AS NVARCHAR(20)) FROM dbo.players WITH(NOLOCK)
 WHERE (player_id / 16384) % 256 <> 254;
GO
