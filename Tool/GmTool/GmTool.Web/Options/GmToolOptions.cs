namespace GmTool.Web.Options;

/// <summary>SQL Server 접속 설정.</summary>
public sealed class DatabaseOptions
{
    public const string SectionName = "Database";

    /// <summary>
    /// 개발 기본값은 <c>appsettings.json</c>에 있다. 운영에서는 환경 변수
    /// (<c>Database__ConnectionString</c>)나 User Secrets로 덮어쓴다 — 계정 정보를 저장소에
    /// 커밋하지 않기 위한 최소 장치다.
    /// </summary>
    public string ConnectionString { get; set; } = string.Empty;

    /// <summary>
    /// 기동 시 <c>Sql/schema.sql</c> 적용 + 시드 계정 생성. 운영에서는 끄고 마이그레이션을
    /// 별도로 돌린다.
    /// </summary>
    public bool ApplySchemaOnStartup { get; set; } = true;
}

/// <summary>WorldServer 운영툴 링크 설정.</summary>
public sealed class WorldLinkOptions
{
    public const string SectionName = "WorldLink";

    public string Host { get; set; } = "127.0.0.1";

    /// <summary>C++ <c>WorldServerConfig::toolPort</c>와 같아야 한다.</summary>
    public int Port { get; set; } = 9300;

    /// <summary>
    /// C++ <c>WorldServerConfig::toolSharedSecret</c>와 같아야 한다. World 쪽은 환경 변수
    /// <c>ASIO_SERVER_TOOL_SECRET</c>로, 이쪽은 <c>WorldLink__SharedSecret</c>으로 덮어쓴다.
    /// </summary>
    public string SharedSecret { get; set; } = "dev-only-gmtool-secret";

    /// <summary>ToolCommandAck 대기 시간. 넘기면 ToolTimeout으로 처리한다.</summary>
    public int RequestTimeoutSeconds { get; set; } = 10;

    public int ReconnectDelaySeconds { get; set; } = 3;
}

/// <summary>운영자 인증 설정.</summary>
public sealed class AuthOptions
{
    public const string SectionName = "Auth";

    /// <summary>API 토큰 서명(HMAC-SHA256) 키. <b>반드시</b> 환경 변수로 덮어쓴다.</summary>
    public string TokenSigningKey { get; set; } = "dev-only-token-signing-key-change-me";

    public int TokenLifetimeHours { get; set; } = 12;

    /// <summary>
    /// 초기 운영자 계정. 계정이 하나도 없을 때만 만든다 — 매번 되돌리면 운영 중 비밀번호 변경이
    /// 무의미해진다.
    /// </summary>
    public string SeedLoginId { get; set; } = "admin";

    public string SeedDisplayName { get; set; } = "관리자";

    public string SeedPassword { get; set; } = "admin1234!";
}

/// <summary>쿠폰 발급/등록 설정.</summary>
public sealed class CouponOptions
{
    public const string SectionName = "Coupon";

    public string SpoolDirectory { get; set; } = "spool";

    /// <summary>DB 벌크 적재 한 번의 건수(= 스풀 fsync 주기).</summary>
    public int DbChunkSize { get; set; } = 10_000;

    /// <summary>
    /// 발급한 청크를 WorldServer로도 보낼지 여부. World 쪽 DB 적재는 아직 TODO라(로그만 남는다)
    /// 기본은 켜두되, 100만 건 규모 발급 때는 끄는 편이 빠르다.
    /// </summary>
    public bool PushChunksToWorld { get; set; } = true;

    /// <summary>
    /// 쿠폰 등록 레이트 리밋: 이 시간(초) 안에 <see cref="RateLimitMaxFailures"/>번 실패하면
    /// 이후 요청을 <c>BackoffBase * 2^(초과 횟수)</c>초 동안 거절한다.
    /// </summary>
    public int RateLimitWindowSeconds { get; set; } = 60;

    public int RateLimitMaxFailures { get; set; } = 10;

    public int RateLimitBackoffBaseSeconds { get; set; } = 5;

    /// <summary>백오프 상한(초). 없으면 정상 사용자가 영구 차단될 수 있다.</summary>
    public int RateLimitBackoffMaxSeconds { get; set; } = 300;
}
