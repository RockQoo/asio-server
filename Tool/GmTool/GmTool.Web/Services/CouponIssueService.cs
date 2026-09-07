using GmTool.Core.Coupons;
using GmTool.Web.Data;
using GmTool.Web.Models;
using GmTool.Web.Options;
using GmTool.Web.Repositories;
using Microsoft.Extensions.Options;

namespace GmTool.Web.Services;

/// <summary>대량 발급 결과 요약(화면 표시용).</summary>
public sealed record CouponIssueSummary(
    ulong BatchId,
    string CampaignCode,
    long Requested,
    long Generated,
    long Inserted,
    long DuplicateRejected,
    long ChunkCount,
    long ElapsedMs,
    string SpoolPath)
{
    public double CodesPerSecond => ElapsedMs <= 0 ? 0 : Generated * 1000.0 / ElapsedMs;
}

/// <summary>
/// 캠페인 등록과 대량 쿠폰 발급을 지휘한다. 실제 생성 알고리즘은
/// <see cref="CouponBatchGenerator"/>(GmTool.Core)에 있고, 여기서는 DB/World 연동과 배치
/// 기록을 붙인다.
///
/// 전체 흐름:
/// <code>
///   [캠페인 등록]  코드 5자리 유일성 검사(전역 UNIQUE) → 전용 쿠폰 테이블 CREATE
///        ↓
///   [대량 발급]    로컬 메모리에서 생성 + 중복 검사(캠페인 범위)
///        ↓ 청크마다
///   [스풀 파일]    append → (DB 적재 성공 후) fsync + 오프셋 기록
///        ↓
///   [SQL Server]   SqlBulkCopy -> 임시 테이블 -> WHERE NOT EXISTS (한 트랜잭션)
///        ↓ (옵션)
///   [WorldServer]  CouponChunkPush → DbWorker(owner-hash로 캠페인별 직렬 처리)
/// </code>
/// </summary>
public sealed class CouponIssueService
{
    private readonly CouponCampaignRepository campaigns_;
    private readonly CouponRepository coupons_;
    private readonly WorldLinkClient worldLink_;
    private readonly CouponOptions options_;
    private readonly IHostEnvironment environment_;
    private readonly ILogger<CouponIssueService> logger_;

    public CouponIssueService(
        CouponCampaignRepository campaigns,
        CouponRepository coupons,
        WorldLinkClient worldLink,
        IOptions<CouponOptions> options,
        IHostEnvironment environment,
        ILogger<CouponIssueService> logger)
    {
        campaigns_ = campaigns;
        coupons_ = coupons;
        worldLink_ = worldLink;
        options_ = options.Value;
        environment_ = environment;
        logger_ = logger;
    }

    public Task<IReadOnlyList<CouponCampaign>> ListCampaignsAsync(CancellationToken cancellationToken = default)
        => campaigns_.ListAsync(cancellationToken);

    public Task<IReadOnlyList<CouponIssueBatch>> ListBatchesAsync(
        int limit = 20, CancellationToken cancellationToken = default)
        => campaigns_.ListBatchesAsync(limit, cancellationToken);

    /// <summary>
    /// 사용 가능한(아직 등록되지 않은) 캠페인 코드 후보를 하나 찾는다. 몇 번 시도해도
    /// 안 나오면 포기한다 — 32^5 = 3,355,443가지라 실제로는 첫 시도에 거의 성공한다.
    /// </summary>
    public async Task<string?> SuggestCampaignCodeAsync(CancellationToken cancellationToken = default)
    {
        for (var attempt = 0; attempt < 16; ++attempt)
        {
            var candidate = CouponCodeGenerator.GenerateCampaignCode();
            var existing = await campaigns_.FindByCodeAsync(candidate, cancellationToken).ConfigureAwait(false);
            if (existing is null)
            {
                return candidate;
            }
        }

        return null;
    }

    /// <summary>
    /// 캠페인을 등록하고 전용 쿠폰 테이블을 만든다.
    /// </summary>
    public async Task<CouponCampaign> CreateCampaignAsync(
        CouponCampaign draft, CancellationToken cancellationToken = default)
    {
        if (!CouponCodeFormat.IsValidCampaignCode(draft.CampaignCode))
        {
            throw new ArgumentException(
                $"캠페인 코드는 Crockford Base32 {CouponCodeFormat.CampaignCodeLength}자여야 합니다: '{draft.CampaignCode}'");
        }

        var existing = await campaigns_.FindByCodeAsync(draft.CampaignCode, cancellationToken).ConfigureAwait(false);
        if (existing is not null)
        {
            throw new InvalidOperationException($"이미 등록된 캠페인 코드입니다: {draft.CampaignCode}");
        }

        var tableName = CouponTableNaming.ResolveTableName(draft.CampaignCode);

        // 테이블을 먼저 만든다. 반대 순서로 하면 캠페인 행은 있는데 테이블이 없는 상태가
        // 잠깐 생기고, 그 사이 등록 요청이 오면 "테이블 없음" 예외가 사용자에게 노출된다.
        await coupons_.EnsureTableAsync(draft.CampaignCode, cancellationToken).ConfigureAwait(false);

        var campaign = new CouponCampaign
        {
            CampaignCode = draft.CampaignCode,
            CampaignName = draft.CampaignName,
            CouponTable = tableName,
            RewardKind = draft.RewardKind,
            RewardTitle = draft.RewardTitle,
            RewardBody = draft.RewardBody,
            RewardDurationSec = draft.RewardDurationSec,
            MaxUseCount = draft.MaxUseCount,
            ValidFrom = draft.ValidFrom,
            ValidTo = draft.ValidTo,
            ValidDaysAfterIssue = draft.ValidDaysAfterIssue,
            Status = "Active",
            CreatedBy = draft.CreatedBy,
        };

        var id = await campaigns_.CreateAsync(campaign, cancellationToken).ConfigureAwait(false);
        logger_.LogInformation("캠페인 등록: {Code} (id={Id}, table={Table})", campaign.CampaignCode, id, tableName);

        return (await campaigns_.FindByIdAsync(id, cancellationToken).ConfigureAwait(false))!;
    }

    /// <summary>대량 발급을 실행한다.</summary>
    public async Task<CouponIssueSummary> IssueAsync(
        string campaignCode,
        long count,
        ulong operatorId,
        IProgress<CouponBatchProgress>? progress = null,
        CancellationToken cancellationToken = default)
    {
        var campaign = await campaigns_.FindByCodeAsync(campaignCode, cancellationToken).ConfigureAwait(false)
            ?? throw new InvalidOperationException($"등록되지 않은 캠페인입니다: {campaignCode}");

        // 테이블이 없으면(예: 수동으로 지웠거나 예전 데이터) 여기서 다시 만든다.
        await coupons_.EnsureTableAsync(campaignCode, cancellationToken).ConfigureAwait(false);

        var batchId = await campaigns_
            .CreateBatchAsync(campaign.CampaignId, campaignCode, count, operatorId, cancellationToken)
            .ConfigureAwait(false);

        var issuedAt = DateTime.UtcNow;
        var expiresAt = ResolveExpiry(campaign, issuedAt);

        var spoolDirectory = Path.IsPathRooted(options_.SpoolDirectory)
            ? options_.SpoolDirectory
            : Path.Combine(environment_.ContentRootPath, options_.SpoolDirectory);

        long insertedTotal = 0;
        var spoolPath = string.Empty;

        try
        {
            var generator = new CouponBatchGenerator();
            var result = await generator.GenerateAsync(
                new CouponBatchRequest
                {
                    CampaignCode = campaignCode,
                    RequestedCount = count,
                    ChunkSize = options_.DbChunkSize,
                    SpoolDirectory = spoolDirectory,
                    BatchTag = batchId.ToString(),
                },
                async (chunk, chunkSeq, ct) =>
                {
                    var inserted = await coupons_
                        .BulkInsertAsync(campaignCode, chunk, issuedAt, expiresAt, ct)
                        .ConfigureAwait(false);
                    Interlocked.Add(ref insertedTotal, inserted);

                    if (options_.PushChunksToWorld && worldLink_.IsReady)
                    {
                        // World 전송 실패는 발급 자체를 실패시키지 않는다. 쿠폰의 권위 저장소는
                        // 운영툴 쪽 SQL Server이고, World로 보내는 건 게임 서버가 같은 목록을
                        // 적재할 수 있게 하는 부가 경로다(World 쪽 DB 적재는 아직 TODO).
                        var push = await worldLink_
                            .PushCouponChunkAsync(campaignCode, chunk, chunkSeq, ct)
                            .ConfigureAwait(false);
                        if (!push.IsSuccess)
                        {
                            logger_.LogWarning(
                                "쿠폰 청크 World 전송 실패(발급은 계속 진행): chunk={ChunkSeq}, {Reason}",
                                chunkSeq, push.Describe());
                        }
                    }
                },
                progress,
                cancellationToken).ConfigureAwait(false);

            spoolPath = result.SpoolPath;

            await campaigns_.CompleteBatchAsync(
                batchId, result.GeneratedCount, insertedTotal, result.DuplicateRejectedCount,
                result.SpoolPath, result.ElapsedMilliseconds, cancellationToken).ConfigureAwait(false);

            await campaigns_.AddIssuedCountAsync(campaign.CampaignId, insertedTotal, cancellationToken)
                .ConfigureAwait(false);

            logger_.LogInformation(
                "쿠폰 발급 완료: {Code} {Generated}건 / {ElapsedMs}ms ({Rate:N0}건/초), 중복재시도 {Duplicates}건",
                campaignCode, result.GeneratedCount, result.ElapsedMilliseconds,
                result.ElapsedMilliseconds <= 0 ? 0 : result.GeneratedCount * 1000.0 / result.ElapsedMilliseconds,
                result.DuplicateRejectedCount);

            return new CouponIssueSummary(
                batchId, campaignCode, count, result.GeneratedCount, insertedTotal,
                result.DuplicateRejectedCount, result.ChunkCount, result.ElapsedMilliseconds, result.SpoolPath);
        }
        catch (Exception ex)
        {
            await campaigns_.FailBatchAsync(batchId, ex.Message, insertedTotal, spoolPath, CancellationToken.None)
                .ConfigureAwait(false);
            logger_.LogError(ex, "쿠폰 발급 실패: {Code} (batch={BatchId})", campaignCode, batchId);
            throw;
        }
    }

    /// <summary>
    /// 이 캠페인 쿠폰의 만료 시각을 정한다. 절대 유효 기간(<c>valid_to</c>)과 상대 유효 기간
    /// (발급일 + N일)이 둘 다 있으면 <b>더 이른 쪽</b>을 쓴다 — 늦은 쪽을 쓰면 캠페인이 끝난
    /// 뒤에도 쿠폰이 살아 있게 되어 운영 사고가 된다.
    /// </summary>
    private static DateTime? ResolveExpiry(CouponCampaign campaign, DateTime issuedAt)
    {
        DateTime? relative = campaign.ValidDaysAfterIssue > 0
            ? issuedAt.AddDays(campaign.ValidDaysAfterIssue)
            : null;

        return (campaign.ValidTo, relative) switch
        {
            (null, null) => null,
            (null, { } r) => r,
            ({ } absolute, null) => absolute,
            ({ } absolute, { } r) => absolute < r ? absolute : r,
        };
    }
}
