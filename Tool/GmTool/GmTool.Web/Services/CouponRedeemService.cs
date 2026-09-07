using GmTool.Core.Coupons;
using GmTool.Web.Data;
using GmTool.Web.Options;
using GmTool.Web.Repositories;
using Microsoft.Extensions.Options;

namespace GmTool.Web.Services;

/// <summary>
/// 쿠폰 등록 결과. 사용자에게 나가는 값은 <see cref="Succeeded"/>와 <see cref="PublicMessage"/>
/// 둘뿐이다 — <see cref="InternalReason"/>은 로그/운영 화면 전용이다.
/// </summary>
public sealed record CouponRedeemResult(
    bool Succeeded, string PublicMessage, string InternalReason, int? RetryAfterSeconds = null);

/// <summary>
/// 사용자가 입력한 쿠폰 번호를 검증하고 사용 처리한다.
///
/// <b>실패 응답을 세분화하지 말 것.</b> "이미 사용됨"/"만료됨"/"존재하지 않음"을 구분해주면
/// 공격자가 유효 번호의 범위를 좁히는 단서가 된다. 진짜 이유는
/// <c>coupon_redeem_attempt</c>와 로그에만 남긴다.
///
/// 검증 순서는 비용이 싼 것부터다 — 레이트 리밋 → 형식 정규화 → 체크 문자 → 캠페인 조회 →
/// 쿠폰 조회/사용. 체크 문자까지가 DB를 건드리지 않으므로 무작위 입력의 약 99.9%가 커넥션을
/// 쓰지 않고 걸러진다.
/// </summary>
public sealed class CouponRedeemService
{
    private const string GenericFailureMessage = "유효하지 않은 쿠폰입니다.";

    private readonly CouponCampaignRepository campaigns_;
    private readonly CouponRepository coupons_;
    private readonly GameCommandService gameCommands_;
    private readonly CouponOptions options_;
    private readonly ILogger<CouponRedeemService> logger_;

    public CouponRedeemService(
        CouponCampaignRepository campaigns,
        CouponRepository coupons,
        GameCommandService gameCommands,
        IOptions<CouponOptions> options,
        ILogger<CouponRedeemService> logger)
    {
        campaigns_ = campaigns;
        coupons_ = coupons;
        gameCommands_ = gameCommands;
        options_ = options.Value;
        logger_ = logger;
    }

    /// <summary>
    /// 쿠폰을 사용 처리하고, 캠페인 보상이 우편이면 해당 클라이언트에게 우편을 발송한다.
    /// </summary>
    /// <param name="requesterKey">레이트 리밋 기준 키(보통 호출자 IP).</param>
    /// <param name="clientSessionId">보상을 받을 접속 중인 클라이언트. null이면 사용 처리만 한다.</param>
    public async Task<CouponRedeemResult> RedeemAsync(
        string requesterKey,
        string? inputCode,
        ulong? clientSessionId,
        CancellationToken cancellationToken = default)
    {
        // 1. 레이트 리밋 + 지수 백오프
        var backoff = await GetBackoffSecondsAsync(requesterKey, cancellationToken).ConfigureAwait(false);
        if (backoff > 0)
        {
            await LogAttemptAsync(requesterKey, null, false, "RateLimited", cancellationToken).ConfigureAwait(false);
            return new CouponRedeemResult(false, GenericFailureMessage, "RateLimited", backoff);
        }

        // 2. 형식 정규화(하이픈/공백 제거, 대문자화, 25자 확인)
        if (!CouponCodeFormat.TryNormalize(inputCode, out var normalized))
        {
            await LogAttemptAsync(requesterKey, null, false, "Format", cancellationToken).ConfigureAwait(false);
            return new CouponRedeemResult(false, GenericFailureMessage, "Format");
        }

        // 3. 체크 문자 — DB에 가기 전의 1차 필터
        if (!CouponCheckDigit.Verify(normalized))
        {
            await LogAttemptAsync(requesterKey, null, false, "CheckDigit", cancellationToken).ConfigureAwait(false);
            return new CouponRedeemResult(false, GenericFailureMessage, "CheckDigit");
        }

        // 4. 캠페인(네임스페이스) 조회 — 앞 5자리로 곧바로 대상 테이블이 정해진다
        var campaignCode = CouponCodeFormat.ExtractCampaignCode(normalized);
        var campaign = await campaigns_.FindByCodeAsync(campaignCode, cancellationToken).ConfigureAwait(false);
        if (campaign is null)
        {
            await LogAttemptAsync(requesterKey, campaignCode, false, "UnknownCampaign", cancellationToken)
                .ConfigureAwait(false);
            return new CouponRedeemResult(false, GenericFailureMessage, "UnknownCampaign");
        }

        var now = DateTime.UtcNow;

        if (campaign.ValidFrom is { } from && now < from)
        {
            await LogAttemptAsync(requesterKey, campaignCode, false, "NotStarted", cancellationToken)
                .ConfigureAwait(false);
            return new CouponRedeemResult(false, GenericFailureMessage, "NotStarted");
        }

        if (campaign.ValidTo is { } to && now > to)
        {
            await LogAttemptAsync(requesterKey, campaignCode, false, "CampaignEnded", cancellationToken)
                .ConfigureAwait(false);
            return new CouponRedeemResult(false, GenericFailureMessage, "CampaignEnded");
        }

        // 5. 사용 처리. 조회 후 갱신으로 나누지 않고 조건부 UPDATE 한 방으로 끝낸다 —
        //    같은 코드가 동시에 두 번 들어와도 한쪽만 성공해야 하기 때문이다.
        var consumed = await coupons_
            .TryConsumeAsync(campaignCode, normalized, campaign.MaxUseCount, clientSessionId, now, cancellationToken)
            .ConfigureAwait(false);

        if (!consumed)
        {
            // 실패 원인을 구분하려면 여기서 한 번 더 조회해야 한다. 사용자 응답은 어차피
            // 같지만, 운영 화면에서 "왜 안 됐는지"를 못 보면 CS 대응이 불가능하다.
            var record = await coupons_.FindAsync(campaignCode, normalized, cancellationToken).ConfigureAwait(false);
            var reason = record switch
            {
                null => "NotFound",
                { Status: (byte)CouponStatus.Used } => "AlreadyUsed",
                { Status: (byte)CouponStatus.Revoked } => "Revoked",
                { Status: (byte)CouponStatus.Expired } => "Expired",
                { ExpiresAt: { } exp } when exp <= now => "Expired",
                _ => "Rejected",
            };

            await LogAttemptAsync(requesterKey, campaignCode, false, reason, cancellationToken).ConfigureAwait(false);
            return new CouponRedeemResult(false, GenericFailureMessage, reason);
        }

        await LogAttemptAsync(requesterKey, campaignCode, true, "Ok", cancellationToken).ConfigureAwait(false);

        // 6. 보상 지급. 우편 보상이고 대상 클라이언트가 접속 중이면 World로 우편을 보낸다.
        //    지급 실패해도 사용 처리는 되돌리지 않는다 — 롤백하면 "사용됐다가 안 됐다가" 하는
        //    상태가 생겨 중복 지급의 여지가 열린다. 대신 실패를 로그로 남겨 수동 보상이
        //    가능하게 한다.
        if (campaign.RewardKind == "Mail" && clientSessionId is { } sessionId)
        {
            var mail = await gameCommands_.SendMailAsync(
                campaign.CreatedBy, sessionId,
                string.IsNullOrWhiteSpace(campaign.RewardTitle) ? campaign.CampaignName : campaign.RewardTitle,
                campaign.RewardBody,
                campaign.RewardDurationSec,
                cancellationToken).ConfigureAwait(false);

            if (!mail.IsSuccess)
            {
                logger_.LogError(
                    "쿠폰 보상 우편 발송 실패(쿠폰은 이미 사용 처리됨): code={Code}, session={Session}, {Reason}",
                    normalized, sessionId, mail.Describe());

                return new CouponRedeemResult(
                    true, "쿠폰이 등록되었습니다. 보상 지급이 지연될 수 있습니다.", "OkRewardDelayed");
            }
        }

        return new CouponRedeemResult(true, "쿠폰이 등록되었습니다.", "Ok");
    }

    /// <summary>
    /// 사용 처리 없이 검증만 한다(운영 화면의 쿠폰 조회용). 운영자에게는 실제 이유를 보여준다 —
    /// 응답 최소화 원칙은 <b>외부 사용자</b>를 향한 것이지, 로그인한 운영자를 향한 것이 아니다.
    /// </summary>
    public async Task<string> InspectAsync(string? inputCode, CancellationToken cancellationToken = default)
    {
        if (!CouponCodeFormat.TryNormalize(inputCode, out var normalized))
        {
            return "형식 오류: Crockford Base32 25자가 아닙니다.";
        }

        if (!CouponCheckDigit.Verify(normalized))
        {
            return $"체크 문자 불일치: {CouponCodeFormat.ToDisplay(normalized)} (DB 조회 없이 거절되는 코드)";
        }

        var campaignCode = CouponCodeFormat.ExtractCampaignCode(normalized);
        var campaign = await campaigns_.FindByCodeAsync(campaignCode, cancellationToken).ConfigureAwait(false);
        if (campaign is null)
        {
            return $"등록되지 않은 캠페인 코드입니다: {campaignCode}";
        }

        var record = await coupons_.FindAsync(campaignCode, normalized, cancellationToken).ConfigureAwait(false);
        if (record is null)
        {
            return $"캠페인 '{campaign.CampaignName}'({campaignCode})에 존재하지 않는 쿠폰입니다.";
        }

        var status = (CouponStatus)record.Status switch
        {
            CouponStatus.Created => "생성됨",
            CouponStatus.Issued => "사용 가능",
            CouponStatus.Used => "사용 완료",
            CouponStatus.Expired => "만료",
            CouponStatus.Revoked => "회수됨",
            _ => $"알 수 없음({record.Status})",
        };

        var expiry = record.ExpiresAt is { } exp ? exp.ToLocalTime().ToString("yyyy-MM-dd HH:mm") : "무기한";
        var maxUse = campaign.MaxUseCount == 0 ? "무제한" : $"{campaign.MaxUseCount}회";

        return $"캠페인 '{campaign.CampaignName}'({campaignCode}) / 상태: {status} / "
             + $"사용 {record.UseCount}회 (상한 {maxUse}) / 만료: {expiry}";
    }

    /// <summary>
    /// 최근 실패 횟수를 보고 지수 백오프 초를 계산한다. 임계치를 넘으면 넘긴 횟수만큼
    /// <c>Base × 2^n</c>으로 늘어나고 상한에서 멈춘다 — 상한이 없으면 정상 사용자가 오타 몇 번에
    /// 사실상 영구 차단될 수 있다.
    /// </summary>
    private async Task<int> GetBackoffSecondsAsync(string requesterKey, CancellationToken cancellationToken)
    {
        var window = TimeSpan.FromSeconds(options_.RateLimitWindowSeconds);
        var failures = await campaigns_.CountRecentFailuresAsync(requesterKey, window, cancellationToken)
            .ConfigureAwait(false);

        if (failures < options_.RateLimitMaxFailures)
        {
            return 0;
        }

        var over = failures - options_.RateLimitMaxFailures;
        // 지수가 커지면 int가 넘치므로 미리 자른다(2^20이면 이미 상한을 한참 넘는다).
        var exponent = Math.Min(over, 20);
        var seconds = options_.RateLimitBackoffBaseSeconds * (1L << exponent);

        return (int)Math.Min(seconds, options_.RateLimitBackoffMaxSeconds);
    }

    private async Task LogAttemptAsync(
        string requesterKey, string? campaignCode, bool succeeded, string reason, CancellationToken cancellationToken)
    {
        try
        {
            await campaigns_.LogAttemptAsync(requesterKey, campaignCode, succeeded, reason, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            // 시도 로그가 실패해도 등록 자체를 막지는 않는다. 다만 레이트 리밋이 이 로그를
            // 근거로 동작하므로, 계속 실패하면 무차별 대입 방어가 사실상 꺼진 상태가 된다.
            logger_.LogError(ex, "쿠폰 등록 시도 로그 기록 실패: reason={Reason}", reason);
        }
    }
}
