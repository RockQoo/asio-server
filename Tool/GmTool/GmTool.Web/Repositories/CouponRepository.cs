using System.Data;
using GmTool.Web.Data;
using GmTool.Web.Models;
using Microsoft.Data.SqlClient;
using SqlKata.Execution;

namespace GmTool.Web.Repositories;

/// <summary>
/// 캠페인별 쿠폰 테이블에 대한 접근. 테이블 이름이 캠페인마다 다르므로
/// (<see cref="CouponTableNaming"/> 참고) 모든 메서드가 캠페인 코드를 받는다.
/// </summary>
public sealed class CouponRepository
{
    private readonly SqlServerConnectionFactory factory_;
    private readonly ILogger<CouponRepository> logger_;

    public CouponRepository(SqlServerConnectionFactory factory, ILogger<CouponRepository> logger)
    {
        factory_ = factory;
        logger_ = logger;
    }

    /// <summary>캠페인 전용 쿠폰 테이블을 만든다(이미 있으면 아무것도 하지 않는다).</summary>
    public async Task EnsureTableAsync(string campaignCode, CancellationToken cancellationToken = default)
    {
        var tableName = CouponTableNaming.ResolveTableName(campaignCode);

        await using var connection = await factory_.OpenConnectionAsync(cancellationToken).ConfigureAwait(false);
        await using var command = connection.CreateCommand();
        command.CommandText = CouponTableNaming.BuildCreateTableSql(tableName);
        await command.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);

        logger_.LogInformation("쿠폰 테이블 준비 완료: {Table}", tableName);
    }

    public async Task DropTableAsync(string campaignCode, CancellationToken cancellationToken = default)
    {
        var tableName = CouponTableNaming.ResolveTableName(campaignCode);

        await using var connection = await factory_.OpenConnectionAsync(cancellationToken).ConfigureAwait(false);
        await using var command = connection.CreateCommand();
        command.CommandText = CouponTableNaming.BuildDropTableSql(tableName);
        await command.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);
    }

    /// <summary>
    /// 청크 하나를 한 트랜잭션으로 적재하고 실제로 들어간 행 수를 돌려준다.
    ///
    /// 임시 테이블을 거치는 이유는 T-SQL 제약 두 가지다 — 파라미터 상한 2,100개(청크가 1만
    /// 행이라 <c>VALUES</c> 방식이 불가능)와 <c>INSERT IGNORE</c> 부재(재개 시 중복을 무시할
    /// 문법이 <c>WHERE NOT EXISTS</c>/<c>MERGE</c>뿐이고 둘 다 소스 테이블이 필요). 자세한 건
    /// <c>Tool/GmTool/README.md</c>의 "이관하면서 걸린 것들" 참고.
    ///
    /// 임시 테이블은 연결 세션에 묶이므로 <c>SqlBulkCopy</c>와 후속 <c>INSERT</c>가 <b>같은
    /// 연결을 공유해야 한다</b>.
    /// </summary>
    public async Task<int> BulkInsertAsync(
        string campaignCode,
        IReadOnlyList<string> couponCodes,
        DateTime issuedAt,
        DateTime? expiresAt,
        CancellationToken cancellationToken = default)
    {
        if (couponCodes.Count == 0)
        {
            return 0;
        }

        var tableName = CouponTableNaming.ResolveTableName(campaignCode);

        await using var connection = await factory_.OpenConnectionAsync(cancellationToken).ConfigureAwait(false);
        await using var transaction =
            (SqlTransaction)await connection.BeginTransactionAsync(cancellationToken).ConfigureAwait(false);

        await using (var stage = connection.CreateCommand())
        {
            stage.Transaction = transaction;
            stage.CommandText = CouponTableNaming.BuildCreateStageTableSql();
            await stage.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);
        }

        // TableLock: 임시 테이블은 이 세션 전용이라 경합 상대가 없다. 행 단위 잠금 비용을 없애고
        // 최소 로깅 경로를 타게 한다.
        using (var bulk = new SqlBulkCopy(connection, SqlBulkCopyOptions.TableLock, transaction))
        {
            bulk.DestinationTableName = CouponTableNaming.StageTableName;
            bulk.BatchSize = couponCodes.Count;
            bulk.BulkCopyTimeout = 0;
            bulk.ColumnMappings.Add("coupon_code", "coupon_code");

            using var reader = new StringColumnDataReader(couponCodes, "coupon_code");
            await bulk.WriteToServerAsync(reader, cancellationToken).ConfigureAwait(false);
        }

        int affected;
        await using (var move = connection.CreateCommand())
        {
            move.Transaction = transaction;

            // 테이블 이름은 바인딩 불가. ResolveTableName의 형식 검사가 유일한 방어선이다.
            move.CommandText = $"""
                INSERT INTO dbo.[{tableName}] (coupon_code, status, use_count, issued_at, expires_at)
                SELECT s.coupon_code, 1, 0, @issued, @expires
                  FROM {CouponTableNaming.StageTableName} AS s
                 WHERE NOT EXISTS (SELECT 1
                                     FROM dbo.[{tableName}] AS t
                                    WHERE t.coupon_code = s.coupon_code)
                """;

            move.Parameters.Add("@issued", SqlDbType.DateTime2).Value = issuedAt;
            move.Parameters.Add("@expires", SqlDbType.DateTime2).Value = (object?)expiresAt ?? DBNull.Value;

            affected = await move.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);
        }

        await transaction.CommitAsync(cancellationToken).ConfigureAwait(false);

        return affected;
    }

    /// <summary>
    /// 코드 하나를 찾는다. 쿠폰 등록 경로에서 매 요청마다 타는 조회다.
    ///
    /// SqlKata를 쓰지 않은 이유: 문자열을 NVARCHAR로 바인딩하는데 <c>coupon_code</c>는 CHAR라
    /// 계획에 <c>GetRangeThroughConvert</c>가 붙는다(탐색은 유지되므로 성능 붕괴는 아니다 —
    /// 근거는 README "이관하면서 걸린 것들" 참고). 불필요한 변환을 남길 이유가 없어 여기만
    /// 파라미터 타입을 고정한다.
    /// </summary>
    public async Task<CouponRecord?> FindAsync(
        string campaignCode, string normalizedCode, CancellationToken cancellationToken = default)
    {
        var tableName = CouponTableNaming.ResolveTableName(campaignCode);

        await using var connection = await factory_.OpenConnectionAsync(cancellationToken).ConfigureAwait(false);
        await using var command = connection.CreateCommand();
        command.CommandText = $"""
            SELECT coupon_code, status, use_count, issued_at, expires_at,
                   first_used_at, last_used_at, used_by_session_id
              FROM dbo.[{tableName}]
             WHERE coupon_code = @code
            """;
        command.Parameters.Add("@code", SqlDbType.Char, 25).Value = normalizedCode;

        await using var reader = await command.ExecuteReaderAsync(cancellationToken).ConfigureAwait(false);
        if (!await reader.ReadAsync(cancellationToken).ConfigureAwait(false))
        {
            return null;
        }

        return new CouponRecord
        {
            CouponCode = reader.GetString(0),
            Status = reader.GetByte(1),
            UseCount = (uint)reader.GetInt32(2),
            IssuedAt = reader.GetDateTime(3),
            ExpiresAt = reader.IsDBNull(4) ? null : reader.GetDateTime(4),
            FirstUsedAt = reader.IsDBNull(5) ? null : reader.GetDateTime(5),
            LastUsedAt = reader.IsDBNull(6) ? null : reader.GetDateTime(6),
            UsedBySessionId = reader.IsDBNull(7) ? null : (ulong)reader.GetInt64(7),
        };
    }

    /// <summary>
    /// 쿠폰 사용을 확정한다. 조건부 <c>UPDATE</c> 한 방으로 처리해서, 같은 코드가 동시에 두 번
    /// 들어와도 한쪽만 성공하게 만든다 — 조회 후 갱신(read-modify-write)으로 나누면 그 사이에
    /// 다른 요청이 끼어들어 <b>같은 쿠폰이 두 번 보상을 받는</b> 전형적인 경쟁 조건이 생긴다.
    /// 반환값이 1이면 이번 호출이 사용 권한을 가져간 것이다.
    /// </summary>
    public async Task<bool> TryConsumeAsync(
        string campaignCode,
        string normalizedCode,
        int maxUseCount,
        ulong? usedBySessionId,
        DateTime now,
        CancellationToken cancellationToken = default)
    {
        var tableName = CouponTableNaming.ResolveTableName(campaignCode);

        await using var connection = await factory_.OpenConnectionAsync(cancellationToken).ConfigureAwait(false);
        await using var command = connection.CreateCommand();

        // maxUseCount = 0 은 무제한. 그 경우 use_count 상한 조건을 빼야 하므로 SQL을 나눈다.
        var useCountCondition = maxUseCount > 0 ? " AND use_count < @maxUse" : string.Empty;

        command.CommandText = $"""
            UPDATE dbo.[{tableName}]
               SET use_count          = use_count + 1,
                   first_used_at      = COALESCE(first_used_at, @now),
                   last_used_at       = @now,
                   used_by_session_id = @session,
                   status             = CASE
                                            WHEN @maxUse > 0 AND use_count + 1 >= @maxUse THEN 2
                                            ELSE status
                                        END
             WHERE coupon_code = @code
               AND status = 1
               AND (expires_at IS NULL OR expires_at > @now)
               {useCountCondition}
            """;

        // AddWithValue는 string을 NVARCHAR로 추론한다. FindAsync 주석과 같은 이유로 타입을 고정.
        command.Parameters.Add("@code", SqlDbType.Char, 25).Value = normalizedCode;
        command.Parameters.Add("@now", SqlDbType.DateTime2).Value = now;
        command.Parameters.Add("@maxUse", SqlDbType.Int).Value = maxUseCount;
        command.Parameters.Add("@session", SqlDbType.BigInt).Value =
            usedBySessionId is null ? DBNull.Value : (object)(long)usedBySessionId.Value;

        var affected = await command.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);
        return affected == 1;
    }

    public async Task<long> CountAsync(string campaignCode, CancellationToken cancellationToken = default)
    {
        var tableName = CouponTableNaming.ResolveTableName(campaignCode);

        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        return await db.Query(tableName).CountAsync<long>(cancellationToken: cancellationToken).ConfigureAwait(false);
    }

    /// <summary>미리보기용 상위 N건.</summary>
    public async Task<IReadOnlyList<CouponRecord>> TakeAsync(
        string campaignCode, int count, CancellationToken cancellationToken = default)
    {
        var tableName = CouponTableNaming.ResolveTableName(campaignCode);

        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        var rows = await db.Query(tableName)
            .Limit(count)
            .GetAsync<dynamic>(cancellationToken: cancellationToken)
            .ConfigureAwait(false);

        return rows.Select(row => (CouponRecord)MapCoupon(row)).ToList();
    }

    /// <summary>
    /// CSV/TXT 다운로드용 스트리밍 조회. 100만 건을 리스트로 모두 올리면 메모리가 터지므로
    /// <c>IAsyncEnumerable</c>로 한 행씩 흘려보낸다.
    /// </summary>
    public async IAsyncEnumerable<string> StreamCodesAsync(
        string campaignCode,
        [System.Runtime.CompilerServices.EnumeratorCancellation] CancellationToken cancellationToken = default)
    {
        var tableName = CouponTableNaming.ResolveTableName(campaignCode);

        await using var connection = await factory_.OpenConnectionAsync(cancellationToken).ConfigureAwait(false);
        await using var command = connection.CreateCommand();
        command.CommandText = $"SELECT coupon_code FROM dbo.[{tableName}]";

        // CommandBehavior.SequentialAccess: 행 전체를 버퍼링하지 않고 순차로 읽는다.
        await using var reader = await command.ExecuteReaderAsync(
            System.Data.CommandBehavior.SequentialAccess, cancellationToken).ConfigureAwait(false);

        while (await reader.ReadAsync(cancellationToken).ConfigureAwait(false))
        {
            yield return reader.GetString(0);
        }
    }

    private static CouponRecord MapCoupon(dynamic row)
    {
        var dict = (IDictionary<string, object?>)row;
        return new CouponRecord
        {
            CouponCode = (string)dict["coupon_code"]!,
            Status = Convert.ToByte(dict["status"]),
            UseCount = Convert.ToUInt32(dict["use_count"]),
            IssuedAt = Convert.ToDateTime(dict["issued_at"]),
            ExpiresAt = dict["expires_at"] as DateTime?,
            FirstUsedAt = dict["first_used_at"] as DateTime?,
            LastUsedAt = dict["last_used_at"] as DateTime?,
            UsedBySessionId = dict["used_by_session_id"] is null
                ? null
                : Convert.ToUInt64(dict["used_by_session_id"]),
        };
    }
}
