using GmTool.Web.Data;
using GmTool.Web.Models;
using SqlKata.Execution;

namespace GmTool.Web.Repositories;

/// <summary>
/// <c>gm_operator</c> / <c>gm_auth_token</c> 접근. SqlKata 쿼리 빌더를 쓰되, 조회 결과는
/// 익명 타입이 아니라 <see cref="OperatorAccount"/>로 받는다 — 컬럼 이름 오타가 런타임까지
/// 살아남지 않게 하려면 매핑 지점을 한 곳에 모아두는 편이 낫다.
/// </summary>
public sealed class OperatorRepository
{
    private const string Table = "gm_operator";
    private const string TokenTable = "gm_auth_token";

    private readonly SqlServerConnectionFactory factory_;

    public OperatorRepository(SqlServerConnectionFactory factory) => factory_ = factory;

    public async Task<OperatorAccount?> FindByLoginIdAsync(string loginId, CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);

        var row = await db.Query(Table)
            .Where("login_id", loginId)
            .FirstOrDefaultAsync<dynamic>(cancellationToken: cancellationToken)
            .ConfigureAwait(false);

        return row is null ? null : MapOperator(row);
    }

    public async Task<OperatorAccount?> FindByIdAsync(ulong operatorId, CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);

        var row = await db.Query(Table)
            .Where("operator_id", SqlNum.Of(operatorId))
            .FirstOrDefaultAsync<dynamic>(cancellationToken: cancellationToken)
            .ConfigureAwait(false);

        return row is null ? null : MapOperator(row);
    }

    public async Task<long> CountAsync(CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        return await db.Query(Table).CountAsync<long>(cancellationToken: cancellationToken).ConfigureAwait(false);
    }

    public async Task<ulong> CreateAsync(
        string loginId, string displayName, string passwordHash, string role = "Operator",
        CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);

        var id = await db.Query(Table).InsertGetIdAsync<long>(new
        {
            login_id = loginId,
            display_name = displayName,
            password_hash = passwordHash,
            role,
            is_active = 1,
            created_at = DateTime.UtcNow,
        }, cancellationToken: cancellationToken).ConfigureAwait(false);

        return (ulong)id;
    }

    public async Task UpdatePasswordHashAsync(
        ulong operatorId, string passwordHash, CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        await db.Query(Table)
            .Where("operator_id", SqlNum.Of(operatorId))
            .UpdateAsync(new { password_hash = passwordHash }, cancellationToken: cancellationToken)
            .ConfigureAwait(false);
    }

    public async Task TouchLastLoginAsync(ulong operatorId, CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        await db.Query(Table)
            .Where("operator_id", SqlNum.Of(operatorId))
            .UpdateAsync(new { last_login_at = DateTime.UtcNow }, cancellationToken: cancellationToken)
            .ConfigureAwait(false);
    }

    // API 토큰

    public async Task StoreTokenAsync(
        ulong operatorId, string token, DateTime issuedAt, DateTime expiresAt,
        CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        await db.Query(TokenTable).InsertAsync(new
        {
            operator_id = SqlNum.Of(operatorId),
            token,
            issued_at = issuedAt,
            expires_at = expiresAt,
        }, cancellationToken: cancellationToken).ConfigureAwait(false);
    }

    /// <summary>
    /// 아직 살아 있는(만료 전 + 회수되지 않은) 토큰이면 그 운영자 id를 돌려준다.
    ///
    /// 토큰 자체에 HMAC 서명이 들어 있어 위조는 서명 검증만으로 걸러낼 수 있지만, 그것만으로는
    /// <b>강제 로그아웃</b>과 <b>중복 로그인 차단</b>을 할 수 없다(서명은 발급 후 취소가 안 된다).
    /// 그래서 살아있는 토큰 목록을 DB에도 둔다.
    /// </summary>
    public async Task<ulong?> FindActiveTokenOwnerAsync(string token, CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);

        var row = await db.Query(TokenTable)
            .Where("token", token)
            .Where("expires_at", ">", DateTime.UtcNow)
            .WhereNull("revoked_at")
            .FirstOrDefaultAsync<dynamic>(cancellationToken: cancellationToken)
            .ConfigureAwait(false);

        if (row is null)
        {
            return null;
        }

        var dict = (IDictionary<string, object?>)row;
        return Convert.ToUInt64(dict["operator_id"]);
    }

    /// <summary>이 운영자의 살아있는 토큰을 모두 회수한다(로그아웃 / 중복 로그인 차단).</summary>
    public async Task RevokeAllTokensAsync(ulong operatorId, CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        await db.Query(TokenTable)
            .Where("operator_id", SqlNum.Of(operatorId))
            .WhereNull("revoked_at")
            .UpdateAsync(new { revoked_at = DateTime.UtcNow }, cancellationToken: cancellationToken)
            .ConfigureAwait(false);
    }

    private static OperatorAccount MapOperator(dynamic row)
    {
        var dict = (IDictionary<string, object?>)row;
        return new OperatorAccount
        {
            OperatorId = Convert.ToUInt64(dict["operator_id"]),
            LoginId = (string)dict["login_id"]!,
            DisplayName = (string)dict["display_name"]!,
            PasswordHash = (string)dict["password_hash"]!,
            Role = (string)dict["role"]!,
            IsActive = Convert.ToBoolean(dict["is_active"]),
            LastLoginAt = dict["last_login_at"] as DateTime?,
            CreatedAt = Convert.ToDateTime(dict["created_at"]),
        };
    }
}
