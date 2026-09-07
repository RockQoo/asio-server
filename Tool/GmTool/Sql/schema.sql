-- =============================================================================
-- GmTool(운영툴) SQL Server 스키마
--
-- 실행: sqlcmd -S 127.0.0.1,1433 -U gmtool -P '...' -d gmtool -C -i Sql/schema.sql
--       (기동 시 DatabaseInitializer가 자동으로 적용하므로 보통 직접 실행할 일은 없다)
--
-- 쿠폰 테이블만 이 파일에 없다 -- 캠페인 하나당 전용 테이블 하나를 캠페인 등록 시점에
-- 동적으로 CREATE하기 때문이다(테이블 이름은 coupon_campaign.coupon_table에 기록된다).
-- 이유는 CouponTableNaming.cs 주석에 정리해뒀다: 캠페인 코드를 네임스페이스로 써서 중복
-- 검사 범위와 인덱스 크기를 캠페인 하나 안으로 가두는 것이 이 시스템의 핵심 전략이다.
--
-- MySQL에서 옮겨온 스키마라 타입 선택에 근거가 필요한 자리들이 있다:
--   * BIGINT UNSIGNED -> BIGINT      T-SQL에는 부호 없는 정수가 없다. id/카운터는 음수가
--                                    나올 수 없는 값이라 BIGINT 양수 범위로 충분하다.
--   * TINYINT(1)      -> BIT         T-SQL의 TINYINT는 0~255이고 불리언 의미가 없다.
--   * VARCHAR         -> NVARCHAR    한글이 들어가는 컬럼은 NVARCHAR여야 한다. VARCHAR는
--                                    DB 콜레이션의 코드페이지를 따르므로 컨테이너 기본
--                                    콜레이션에서 한글이 '?'로 깨진다.
--   * DATETIME        -> DATETIME2   DATETIME은 정밀도가 3.33ms 단위로 반올림된다.
--                                    UTC 시각을 그대로 보관하려면 DATETIME2가 맞다.
--   * CHAR(5)/CHAR(25)               코드값은 [0-9A-Z]뿐이라 NVARCHAR로 넓힐 이유가 없다.
--                                    Latin1_General_BIN2 콜레이션으로 대소문자를 구분한다.
--   * KEY / UNIQUE KEY               T-SQL은 인라인 인덱스 선언이 없어 CREATE INDEX로 분리.
--
-- 멱등성: MySQL의 CREATE TABLE IF NOT EXISTS 대응이 없어 OBJECT_ID 검사로 감쌌다. 여러 번
-- 실행해도 결과가 같아야 기동 시 자동 적용이 안전하다.
-- =============================================================================

-- -----------------------------------------------------------------------------
-- 운영자 계정 / 인증
-- -----------------------------------------------------------------------------

IF OBJECT_ID('dbo.gm_operator', 'U') IS NULL
CREATE TABLE dbo.gm_operator
(
    operator_id   BIGINT         NOT NULL IDENTITY(1,1),
    login_id      NVARCHAR(64)   NOT NULL,
    display_name  NVARCHAR(64)   NOT NULL,
    -- PBKDF2(HMAC-SHA256) 결과를 "반복횟수.솔트(base64).해시(base64)" 한 문자열로 보관한다.
    -- 반복 횟수를 값 안에 같이 넣어두면, 나중에 반복 횟수를 올려도 기존 계정을 로그인 시점에
    -- 하나씩 재해싱할 수 있다(전체 마이그레이션 없이).
    password_hash NVARCHAR(255)  NOT NULL,
    role          NVARCHAR(32)   NOT NULL CONSTRAINT df_gm_operator_role DEFAULT 'Operator',
    is_active     BIT            NOT NULL CONSTRAINT df_gm_operator_active DEFAULT 1,
    last_login_at DATETIME2(3)   NULL,
    created_at    DATETIME2(3)   NOT NULL CONSTRAINT df_gm_operator_created DEFAULT SYSUTCDATETIME(),
    CONSTRAINT pk_gm_operator PRIMARY KEY (operator_id),
    CONSTRAINT uk_gm_operator_login UNIQUE (login_id)
);
GO

IF OBJECT_ID('dbo.gm_auth_token', 'U') IS NULL
CREATE TABLE dbo.gm_auth_token
(
    token_id    BIGINT        NOT NULL IDENTITY(1,1),
    operator_id BIGINT        NOT NULL,
    -- HMAC-SHA256(operatorId|issuedUt|nonce) 결과를 base64url로 인코딩한 값.
    -- 토큰 자체에 서명이 들어 있어 형식 검증은 DB 없이도 가능하지만, 강제 로그아웃과
    -- 중복 로그인 차단을 하려면 "지금 살아있는 토큰" 목록이 필요해서 DB에도 남긴다.
    token       VARCHAR(255)  NOT NULL,
    issued_at   DATETIME2(3)  NOT NULL,
    expires_at  DATETIME2(3)  NOT NULL,
    revoked_at  DATETIME2(3)  NULL,
    CONSTRAINT pk_gm_auth_token PRIMARY KEY (token_id),
    CONSTRAINT uk_gm_auth_token UNIQUE (token)
);
GO

IF OBJECT_ID('dbo.gm_auth_token', 'U') IS NOT NULL
   AND NOT EXISTS (SELECT 1 FROM sys.indexes
                    WHERE name = 'ix_gm_auth_token_operator'
                      AND object_id = OBJECT_ID('dbo.gm_auth_token'))
CREATE INDEX ix_gm_auth_token_operator ON dbo.gm_auth_token (operator_id, expires_at);
GO

-- -----------------------------------------------------------------------------
-- 우편 / 공지 발송 이력
--
-- 운영툴에서 나간 모든 게임 서버 명령을 남긴다. 우편 내용 자체의 권위 저장소는 게임 쪽
-- (ZoneServer의 MailModel -> WorldServer의 DbWorker)이고, 여기 남는 건 "누가 언제 무엇을
-- 요청했고 World가 어떻게 답했는지"라는 운영 감사 기록이다.
-- -----------------------------------------------------------------------------

IF OBJECT_ID('dbo.gm_command_log', 'U') IS NULL
CREATE TABLE dbo.gm_command_log
(
    command_id        BIGINT          NOT NULL IDENTITY(1,1),
    operator_id       BIGINT          NOT NULL,
    -- 'MailSend' | 'MailDelete' | 'Notice' | 'CouponChunkPush'
    command_kind      NVARCHAR(32)    NOT NULL,
    -- 0 = 접속 중 전체, 1 = client_session_id 한 명
    target_kind       TINYINT         NOT NULL CONSTRAINT df_gm_cmdlog_target DEFAULT 0,
    client_session_id BIGINT          NOT NULL CONSTRAINT df_gm_cmdlog_session DEFAULT 0,
    mail_id           BIGINT          NOT NULL CONSTRAINT df_gm_cmdlog_mail DEFAULT 0,
    title             NVARCHAR(128)   NOT NULL CONSTRAINT df_gm_cmdlog_title DEFAULT '',
    body              NVARCHAR(1024)  NOT NULL CONSTRAINT df_gm_cmdlog_body DEFAULT '',
    duration_sec      BIGINT          NOT NULL CONSTRAINT df_gm_cmdlog_duration DEFAULT 0,
    -- World가 돌려준 ToolCommandAck 값. result_code는 EToolResultCode와 같은 값이다
    -- (0=Ok, 1=NotAuthenticated, 2=BadRequest, 3=TargetNotFound, 4=ZoneUnavailable).
    result_code       INT             NOT NULL,
    affected_count    BIGINT          NOT NULL CONSTRAINT df_gm_cmdlog_affected DEFAULT 0,
    error_message     NVARCHAR(255)   NOT NULL CONSTRAINT df_gm_cmdlog_error DEFAULT '',
    requested_at      DATETIME2(3)    NOT NULL,
    CONSTRAINT pk_gm_command_log PRIMARY KEY (command_id)
);
GO

IF OBJECT_ID('dbo.gm_command_log', 'U') IS NOT NULL
   AND NOT EXISTS (SELECT 1 FROM sys.indexes
                    WHERE name = 'ix_gm_command_log_time'
                      AND object_id = OBJECT_ID('dbo.gm_command_log'))
CREATE INDEX ix_gm_command_log_time ON dbo.gm_command_log (requested_at);
GO

IF OBJECT_ID('dbo.gm_command_log', 'U') IS NOT NULL
   AND NOT EXISTS (SELECT 1 FROM sys.indexes
                    WHERE name = 'ix_gm_command_log_operator'
                      AND object_id = OBJECT_ID('dbo.gm_command_log'))
CREATE INDEX ix_gm_command_log_operator ON dbo.gm_command_log (operator_id, requested_at);
GO

-- -----------------------------------------------------------------------------
-- 쿠폰 캠페인 (= 쿠폰 번호의 네임스페이스)
--
-- campaign_code 5자리가 쿠폰 25자리의 앞 5자리로 그대로 들어간다. 전역 유일성 검사가
-- 필요한 대상은 이 5자리뿐이고(1년에 수십~수백 건 규모), 그 아래 수십만~수백만 개의
-- 쿠폰 번호는 캠페인 전용 테이블 안에서만 유일하면 된다.
-- -----------------------------------------------------------------------------

IF OBJECT_ID('dbo.coupon_campaign', 'U') IS NULL
CREATE TABLE dbo.coupon_campaign
(
    campaign_id            BIGINT         NOT NULL IDENTITY(1,1),
    campaign_code          CHAR(5)        COLLATE Latin1_General_BIN2 NOT NULL,
    campaign_name          NVARCHAR(128)  NOT NULL,
    -- 이 캠페인 전용 쿠폰 테이블 이름(coupon_<소문자 campaign_code>)
    coupon_table           VARCHAR(64)    NOT NULL,
    -- 쿠폰 사용 시 지급할 보상. 지금은 우편 발송만 지원한다('Mail').
    reward_kind            NVARCHAR(32)   NOT NULL CONSTRAINT df_campaign_reward_kind DEFAULT 'Mail',
    reward_title           NVARCHAR(128)  NOT NULL CONSTRAINT df_campaign_reward_title DEFAULT '',
    reward_body            NVARCHAR(1024) NOT NULL CONSTRAINT df_campaign_reward_body DEFAULT '',
    reward_duration_sec    BIGINT         NOT NULL CONSTRAINT df_campaign_reward_duration DEFAULT 604800,
    -- 0 = 무제한, 1 = 1회용, N = N회까지
    max_use_count          INT            NOT NULL CONSTRAINT df_campaign_max_use DEFAULT 1,
    -- 절대 유효 기간. 둘 다 NULL이면 기간 제한 없음.
    valid_from             DATETIME2(3)   NULL,
    valid_to               DATETIME2(3)   NULL,
    -- 상대 유효 기간(발급일로부터 N일). 0이면 사용하지 않는다. 절대/상대가 함께 설정되면
    -- 둘 중 더 이른 시점이 만료 시점이 된다(CouponRedeemService 참고).
    valid_days_after_issue INT            NOT NULL CONSTRAINT df_campaign_valid_days DEFAULT 0,
    -- 'Draft' | 'Issuing' | 'Active' | 'Closed'
    status                 NVARCHAR(16)   NOT NULL CONSTRAINT df_campaign_status DEFAULT 'Draft',
    issued_count           BIGINT         NOT NULL CONSTRAINT df_campaign_issued DEFAULT 0,
    created_by             BIGINT         NOT NULL,
    created_at             DATETIME2(3)   NOT NULL,
    CONSTRAINT pk_coupon_campaign PRIMARY KEY (campaign_id),
    CONSTRAINT uk_coupon_campaign_code UNIQUE (campaign_code)
);
GO

-- -----------------------------------------------------------------------------
-- 쿠폰 발급 배치 이력
--
-- 대량 발급은 "로컬 메모리에서 생성 -> 스풀 파일에 먼저 기록 -> 청크 단위로 DB 벌크
-- 인서트" 순서로 진행되므로, 중간에 죽었을 때 어디까지 적재됐는지 알아야 재개할 수 있다.
-- spool_path와 inserted_count가 그 재개 지점이다.
-- -----------------------------------------------------------------------------

IF OBJECT_ID('dbo.coupon_issue_batch', 'U') IS NULL
CREATE TABLE dbo.coupon_issue_batch
(
    batch_id        BIGINT        NOT NULL IDENTITY(1,1),
    campaign_id     BIGINT        NOT NULL,
    campaign_code   CHAR(5)       COLLATE Latin1_General_BIN2 NOT NULL,
    requested_count BIGINT        NOT NULL,
    generated_count BIGINT        NOT NULL CONSTRAINT df_batch_generated DEFAULT 0,
    inserted_count  BIGINT        NOT NULL CONSTRAINT df_batch_inserted DEFAULT 0,
    duplicate_count BIGINT        NOT NULL CONSTRAINT df_batch_duplicate DEFAULT 0,
    spool_path      NVARCHAR(512) NOT NULL CONSTRAINT df_batch_spool DEFAULT '',
    -- 'Running' | 'Completed' | 'Failed'
    status          NVARCHAR(16)  NOT NULL CONSTRAINT df_batch_status DEFAULT 'Running',
    error_message   NVARCHAR(512) NOT NULL CONSTRAINT df_batch_error DEFAULT '',
    elapsed_ms      BIGINT        NOT NULL CONSTRAINT df_batch_elapsed DEFAULT 0,
    created_by      BIGINT        NOT NULL,
    created_at      DATETIME2(3)  NOT NULL,
    finished_at     DATETIME2(3)  NULL,
    CONSTRAINT pk_coupon_issue_batch PRIMARY KEY (batch_id)
);
GO

IF OBJECT_ID('dbo.coupon_issue_batch', 'U') IS NOT NULL
   AND NOT EXISTS (SELECT 1 FROM sys.indexes
                    WHERE name = 'ix_coupon_issue_batch_campaign'
                      AND object_id = OBJECT_ID('dbo.coupon_issue_batch'))
CREATE INDEX ix_coupon_issue_batch_campaign ON dbo.coupon_issue_batch (campaign_id, created_at);
GO

-- -----------------------------------------------------------------------------
-- 쿠폰 등록(사용) 시도 로그
--
-- 무차별 대입 공격 탐지와 레이트 리밋 판단에 쓴다. reason에는 실패 원인을 구체적으로
-- 남기지만, 이 값은 절대 응답으로 내보내지 않는다 -- 사용자에게는 "유효하지 않은
-- 쿠폰입니다" 한 가지로만 답한다(CouponRedeemService 주석 참고).
-- -----------------------------------------------------------------------------

IF OBJECT_ID('dbo.coupon_redeem_attempt', 'U') IS NULL
CREATE TABLE dbo.coupon_redeem_attempt
(
    attempt_id    BIGINT       NOT NULL IDENTITY(1,1),
    -- 레이트 리밋의 기준 키. 지금은 호출자 IP를 쓴다.
    requester_key VARCHAR(64)  NOT NULL,
    campaign_code CHAR(5)      COLLATE Latin1_General_BIN2 NULL,
    succeeded     BIT          NOT NULL,
    -- 'Ok' | 'CheckDigit' | 'Format' | 'UnknownCampaign' | 'NotFound'
    -- | 'AlreadyUsed' | 'Expired' | 'Revoked' | 'RateLimited'
    reason        NVARCHAR(32) NOT NULL,
    attempted_at  DATETIME2(3) NOT NULL,
    CONSTRAINT pk_coupon_redeem_attempt PRIMARY KEY (attempt_id)
);
GO

IF OBJECT_ID('dbo.coupon_redeem_attempt', 'U') IS NOT NULL
   AND NOT EXISTS (SELECT 1 FROM sys.indexes
                    WHERE name = 'ix_coupon_redeem_attempt_key'
                      AND object_id = OBJECT_ID('dbo.coupon_redeem_attempt'))
CREATE INDEX ix_coupon_redeem_attempt_key ON dbo.coupon_redeem_attempt (requester_key, attempted_at);
GO
