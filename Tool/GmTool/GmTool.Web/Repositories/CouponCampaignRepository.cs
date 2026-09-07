using GmTool.Web.Data;
using GmTool.Web.Models;
using SqlKata.Execution;

namespace GmTool.Web.Repositories;

/// <summary><c>coupon_campaign</c> / <c>coupon_issue_batch</c> / <c>coupon_redeem_attempt</c> 접근.</summary>
public sealed class CouponCampaignRepository
{
    private const string CampaignTable = "coupon_campaign";
    private const string BatchTable = "coupon_issue_batch";
    private const string AttemptTable = "coupon_redeem_attempt";

    private readonly SqlServerConnectionFactory factory_;

    public CouponCampaignRepository(SqlServerConnectionFactory factory) => factory_ = factory;

    // 캠페인

    public async Task<IReadOnlyList<CouponCampaign>> ListAsync(CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        var rows = await db.Query(CampaignTable)
            .OrderByDesc("campaign_id")
            .GetAsync<dynamic>(cancellationToken: cancellationToken)
            .ConfigureAwait(false);

        // MapCampaign 호출이 dynamic 바인딩이라 반환 타입도 dynamic이 된다. 캐스트를 붙여야
        // Select가 IEnumerable<CouponCampaign>으로 추론된다.
        return rows.Select(row => (CouponCampaign)MapCampaign(row)).ToList();
    }

    public async Task<CouponCampaign?> FindByCodeAsync(string campaignCode, CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        var row = await db.Query(CampaignTable)
            .Where("campaign_code", campaignCode)
            .FirstOrDefaultAsync<dynamic>(cancellationToken: cancellationToken)
            .ConfigureAwait(false);

        return row is null ? null : MapCampaign(row);
    }

    public async Task<CouponCampaign?> FindByIdAsync(ulong campaignId, CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        var row = await db.Query(CampaignTable)
            .Where("campaign_id", SqlNum.Of(campaignId))
            .FirstOrDefaultAsync<dynamic>(cancellationToken: cancellationToken)
            .ConfigureAwait(false);

        return row is null ? null : MapCampaign(row);
    }

    /// <summary>
    /// 캠페인을 등록한다. <c>campaign_code</c>에 UNIQUE 인덱스가 걸려 있으므로, 두 운영자가
    /// 같은 코드를 동시에 등록하면 한쪽이 DB 레벨에서 실패한다 —
    /// "조회해서 없으면 INSERT"로는 그 사이 경쟁을 막지 못하기 때문에 유일성의 최종 판정은
    /// 항상 인덱스에 맡긴다. 이 5자리 전역 유일성 검사가 <b>전체 시스템에서 유일하게</b>
    /// 필요한 전역 중복 검사이고, 그 아래 수백만 개 쿠폰 번호는 캠페인 안에서만 검사한다.
    /// </summary>
    public async Task<ulong> CreateAsync(CouponCampaign campaign, CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);

        // IDENTITY 값은 SCOPE_IDENTITY()로 돌아오는데 그 타입이 BIGINT라 long으로 받는다.
        // ulong로 직접 받으면 SqlClient가 매핑을 못 찾는다(SqlNum 주석 참고).
        var id = await db.Query(CampaignTable).InsertGetIdAsync<long>(new
        {
            campaign_code = campaign.CampaignCode,
            campaign_name = campaign.CampaignName,
            coupon_table = campaign.CouponTable,
            reward_kind = campaign.RewardKind,
            reward_title = campaign.RewardTitle,
            reward_body = campaign.RewardBody,
            reward_duration_sec = campaign.RewardDurationSec,
            max_use_count = campaign.MaxUseCount,
            valid_from = campaign.ValidFrom,
            valid_to = campaign.ValidTo,
            valid_days_after_issue = campaign.ValidDaysAfterIssue,
            status = campaign.Status,
            issued_count = SqlNum.Of(campaign.IssuedCount),
            created_by = SqlNum.Of(campaign.CreatedBy),
            created_at = DateTime.UtcNow,
        }, cancellationToken: cancellationToken).ConfigureAwait(false);

        return (ulong)id;
    }

    public async Task UpdateStatusAsync(
        ulong campaignId, string status, CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        await db.Query(CampaignTable)
            .Where("campaign_id", SqlNum.Of(campaignId))
            .UpdateAsync(new { status }, cancellationToken: cancellationToken)
            .ConfigureAwait(false);
    }

    public async Task AddIssuedCountAsync(
        ulong campaignId, long delta, CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        await db.Query(CampaignTable)
            .Where("campaign_id", SqlNum.Of(campaignId))
            .IncrementAsync("issued_count", (int)delta, cancellationToken: cancellationToken)
            .ConfigureAwait(false);
    }

    // 발급 배치

    public async Task<ulong> CreateBatchAsync(
        ulong campaignId, string campaignCode, long requestedCount, ulong createdBy,
        CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);

        var id = await db.Query(BatchTable).InsertGetIdAsync<long>(new
        {
            campaign_id = SqlNum.Of(campaignId),
            campaign_code = campaignCode,
            requested_count = requestedCount,
            status = "Running",
            created_by = SqlNum.Of(createdBy),
            created_at = DateTime.UtcNow,
        }, cancellationToken: cancellationToken).ConfigureAwait(false);

        return (ulong)id;
    }

    public async Task CompleteBatchAsync(
        ulong batchId, long generated, long inserted, long duplicates, string spoolPath, long elapsedMs,
        CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        await db.Query(BatchTable)
            .Where("batch_id", SqlNum.Of(batchId))
            .UpdateAsync(new
            {
                generated_count = generated,
                inserted_count = inserted,
                duplicate_count = duplicates,
                spool_path = spoolPath,
                elapsed_ms = elapsedMs,
                status = "Completed",
                finished_at = DateTime.UtcNow,
            }, cancellationToken: cancellationToken).ConfigureAwait(false);
    }

    public async Task FailBatchAsync(
        ulong batchId, string errorMessage, long inserted, string spoolPath,
        CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        await db.Query(BatchTable)
            .Where("batch_id", SqlNum.Of(batchId))
            .UpdateAsync(new
            {
                status = "Failed",
                // 512자 컬럼이라 넘치면 잘라 넣는다 -- 예외 메시지 길이 때문에 실패 기록 자체를
                // 못 남기는 상황이 제일 곤란하다.
                error_message = errorMessage.Length > 500 ? errorMessage[..500] : errorMessage,
                inserted_count = inserted,
                spool_path = spoolPath,
                finished_at = DateTime.UtcNow,
            }, cancellationToken: cancellationToken).ConfigureAwait(false);
    }

    public async Task<IReadOnlyList<CouponIssueBatch>> ListBatchesAsync(
        int limit = 20, CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        var rows = await db.Query(BatchTable)
            .OrderByDesc("batch_id")
            .Limit(limit)
            .GetAsync<dynamic>(cancellationToken: cancellationToken)
            .ConfigureAwait(false);

        return rows.Select(row => (CouponIssueBatch)MapBatch(row)).ToList();
    }

    // 등록 시도 로그

    public async Task LogAttemptAsync(
        string requesterKey, string? campaignCode, bool succeeded, string reason,
        CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        await db.Query(AttemptTable).InsertAsync(new
        {
            requester_key = requesterKey,
            campaign_code = campaignCode,
            succeeded = succeeded ? 1 : 0,
            reason,
            attempted_at = DateTime.UtcNow,
        }, cancellationToken: cancellationToken).ConfigureAwait(false);
    }

    public async Task<int> CountRecentFailuresAsync(
        string requesterKey, TimeSpan window, CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        return await db.Query(AttemptTable)
            .Where("requester_key", requesterKey)
            .Where("succeeded", 0)
            .Where("attempted_at", ">", DateTime.UtcNow - window)
            .CountAsync<int>(cancellationToken: cancellationToken)
            .ConfigureAwait(false);
    }


    private static CouponCampaign MapCampaign(dynamic row)
    {
        var dict = (IDictionary<string, object?>)row;
        return new CouponCampaign
        {
            CampaignId = Convert.ToUInt64(dict["campaign_id"]),
            CampaignCode = (string)dict["campaign_code"]!,
            CampaignName = (string)dict["campaign_name"]!,
            CouponTable = (string)dict["coupon_table"]!,
            RewardKind = (string)dict["reward_kind"]!,
            RewardTitle = (string)dict["reward_title"]!,
            RewardBody = (string)dict["reward_body"]!,
            RewardDurationSec = Convert.ToInt64(dict["reward_duration_sec"]),
            MaxUseCount = Convert.ToInt32(dict["max_use_count"]),
            ValidFrom = dict["valid_from"] as DateTime?,
            ValidTo = dict["valid_to"] as DateTime?,
            ValidDaysAfterIssue = Convert.ToInt32(dict["valid_days_after_issue"]),
            Status = (string)dict["status"]!,
            IssuedCount = Convert.ToUInt64(dict["issued_count"]),
            CreatedBy = Convert.ToUInt64(dict["created_by"]),
            CreatedAt = Convert.ToDateTime(dict["created_at"]),
        };
    }

    private static CouponIssueBatch MapBatch(dynamic row)
    {
        var dict = (IDictionary<string, object?>)row;
        return new CouponIssueBatch
        {
            BatchId = Convert.ToUInt64(dict["batch_id"]),
            CampaignId = Convert.ToUInt64(dict["campaign_id"]),
            CampaignCode = (string)dict["campaign_code"]!,
            RequestedCount = Convert.ToUInt64(dict["requested_count"]),
            GeneratedCount = Convert.ToUInt64(dict["generated_count"]),
            InsertedCount = Convert.ToUInt64(dict["inserted_count"]),
            DuplicateCount = Convert.ToUInt64(dict["duplicate_count"]),
            SpoolPath = (string)dict["spool_path"]!,
            Status = (string)dict["status"]!,
            ErrorMessage = (string)dict["error_message"]!,
            ElapsedMs = Convert.ToInt64(dict["elapsed_ms"]),
            CreatedBy = Convert.ToUInt64(dict["created_by"]),
            CreatedAt = Convert.ToDateTime(dict["created_at"]),
            FinishedAt = dict["finished_at"] as DateTime?,
        };
    }
}
