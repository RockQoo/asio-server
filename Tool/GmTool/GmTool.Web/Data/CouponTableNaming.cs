using GmTool.Core.Coupons;

namespace GmTool.Web.Data;

/// <summary>
/// 캠페인 하나당 쿠폰 테이블 하나를 쓰기 위한 테이블 이름 규칙과 DDL. 왜 캠페인별로 나누는지는
/// <c>Tool/GmTool/README.md</c>의 "쿠폰" 절 참고.
///
/// <b>보안 주의</b>: 테이블 이름은 파라미터 바인딩이 안 되고 SQL에 문자열로 박힌다.
/// <see cref="ResolveTableName"/>의 형식 검사가 이 파일에서 가장 중요한 코드다.
/// </summary>
public static class CouponTableNaming
{
    public const string Prefix = "coupon_";

    /// <summary>
    /// 캠페인 코드에 대응하는 쿠폰 테이블 이름을 만든다. 형식 검사에 실패하면 예외를 던진다.
    /// </summary>
    public static string ResolveTableName(string campaignCode)
    {
        if (!CouponCodeFormat.IsValidCampaignCode(campaignCode))
        {
            // 여기서 막지 못하면 곧바로 SQL 인젝션이 된다(테이블 이름은 바인딩 불가).
            throw new ArgumentException(
                $"캠페인 코드 형식이 올바르지 않아 테이블 이름을 만들 수 없습니다: '{campaignCode}'",
                nameof(campaignCode));
        }

        return Prefix + campaignCode.ToLowerInvariant();
    }

    /// <summary>
    /// 캠페인 전용 쿠폰 테이블 DDL.
    ///
    /// <c>coupon_code</c>를 클러스터드 PK로 둔 이유: 접근 패턴이 사실상 "코드 하나로 찾기"뿐이라
    /// key lookup 없이 한 번에 행에 도달하게 된다. 대리 키(IDENTITY)를 둘 이유가 없다.
    ///
    /// <c>IF OBJECT_ID ... BEGIN/END</c>는 T-SQL에 <c>CREATE TABLE IF NOT EXISTS</c>가 없어서다.
    /// </summary>
    public static string BuildCreateTableSql(string tableName) => $"""
        IF OBJECT_ID('dbo.{tableName}', 'U') IS NULL
        BEGIN
            CREATE TABLE dbo.[{tableName}]
            (
                -- CHAR + BIN2: NVARCHAR면 인덱스 키가 50바이트, CHAR면 25바이트다. BIN2는
                -- 대소문자를 구분하므로 정규화 단계에서 대문자로 통일한 코드만 매칭된다.
                coupon_code        CHAR(25) COLLATE Latin1_General_BIN2 NOT NULL,
                -- 0=Created 1=Issued 2=Used 3=Expired 4=Revoked (CouponStatus)
                status             TINYINT      NOT NULL CONSTRAINT [df_{tableName}_status]   DEFAULT 1,
                use_count          INT          NOT NULL CONSTRAINT [df_{tableName}_usecount] DEFAULT 0,
                issued_at          DATETIME2(3) NOT NULL,
                expires_at         DATETIME2(3) NULL,
                first_used_at      DATETIME2(3) NULL,
                last_used_at       DATETIME2(3) NULL,
                used_by_session_id BIGINT       NULL,
                CONSTRAINT [pk_{tableName}] PRIMARY KEY CLUSTERED (coupon_code)
            );

            CREATE INDEX [ix_{tableName}_status] ON dbo.[{tableName}] (status);
        END
        """;

    public static string BuildDropTableSql(string tableName) =>
        $"IF OBJECT_ID('dbo.{tableName}', 'U') IS NOT NULL DROP TABLE dbo.[{tableName}]";

    public const string StageTableName = "#coupon_stage";

    /// <summary>
    /// 벌크 적재용 세션 임시 테이블 DDL. 목적 테이블과 <c>coupon_code</c>의 타입·콜레이션을
    /// <b>정확히 맞춰야 한다</b> — 뒤따르는 <c>WHERE NOT EXISTS</c>의 비교가 콜레이션이 다르면
    /// "collation conflict" 오류로 실패한다.
    /// </summary>
    public static string BuildCreateStageTableSql() => $"""
        CREATE TABLE {StageTableName}
        (
            coupon_code CHAR(25) COLLATE Latin1_General_BIN2 NOT NULL PRIMARY KEY CLUSTERED
        )
        """;
}

/// <summary>쿠폰 한 장의 생명주기 상태.</summary>
public enum CouponStatus : byte
{
    /// <summary>생성됐지만 아직 배포 전(현재 흐름에서는 쓰지 않는다).</summary>
    Created = 0,

    Issued = 1,

    Used = 2,

    Expired = 3,

    Revoked = 4,
}
