using GmTool.Core.Coupons;
using GmTool.Web.Services;

namespace GmTool.Web.Endpoints;

/// <summary>
/// HTTP API. 웹 UI와 별개로, 다른 운영 도구나 스크립트가 운영툴 기능을 호출할 수 있게 한다.
///
/// 인증은 두 갈래다:
/// <list type="bullet">
///   <item><c>/api/auth/login</c> — 로그인해서 토큰을 받는다.</item>
///   <item>나머지 운영 API — <c>Authorization: Bearer &lt;token&gt;</c> 헤더 필요.</item>
///   <item><c>/api/coupon/redeem</c> — <b>예외적으로 토큰이 필요 없다.</b> 이건 운영자가 아니라
///         게임 사용자가 호출하는 엔드포인트라서다. 대신 이 하나만 응답을 뭉뚱그리고 레이트
///         리밋을 건다(<see cref="CouponRedeemService"/> 참고).</item>
/// </list>
/// </summary>
public static class GmToolApiEndpoints
{
    public static void MapGmToolApi(this WebApplication app)
    {
        var api = app.MapGroup("/api");

        // 인증
        api.MapPost("/auth/login", async (
            LoginRequest request, OperatorAuthService auth, CancellationToken cancellationToken) =>
        {
            var result = await auth.LoginAsync(request.LoginId, request.Password, cancellationToken);
            if (!result.Succeeded)
            {
                // 401 + 통일된 메시지. 아이디 존재 여부를 구분해서 알려주지 않는다.
                return Results.Json(new { message = result.Message }, statusCode: StatusCodes.Status401Unauthorized);
            }

            return Results.Ok(new
            {
                token = result.ApiToken,
                operatorId = result.Account!.OperatorId,
                displayName = result.Account.DisplayName,
                role = result.Account.Role,
            });
        });

        // 게임 서버 명령 (운영자 토큰 필요)
        api.MapGet("/world/status", (GameCommandService commands) => Results.Ok(new
        {
            connected = commands.IsWorldConnected,
            endpoint = commands.WorldEndpoint,
            lastError = commands.WorldLastError,
        }));

        api.MapGet("/world/clients", async (
            HttpContext http, OperatorAuthService auth, GameCommandService commands,
            CancellationToken cancellationToken) =>
        {
            var account = await AuthorizeAsync(http, auth, cancellationToken);
            if (account is null)
            {
                return Unauthorized();
            }

            var clients = await commands.GetOnlineClientsAsync(cancellationToken);
            return Results.Ok(clients.Select(c => new { c.ClientSessionId, c.ZoneId }));
        });

        api.MapPost("/world/notice", async (
            NoticeRequest request, HttpContext http, OperatorAuthService auth, GameCommandService commands,
            CancellationToken cancellationToken) =>
        {
            var account = await AuthorizeAsync(http, auth, cancellationToken);
            if (account is null)
            {
                return Unauthorized();
            }

            if (string.IsNullOrWhiteSpace(request.Message))
            {
                return Results.BadRequest(new { message = "공지 내용이 비어 있습니다." });
            }

            var result = await commands.SendNoticeAsync(account.OperatorId, request.Message, cancellationToken);
            return Results.Ok(new { success = result.IsSuccess, result.AffectedCount, message = result.Describe() });
        });

        api.MapPost("/mail/send", async (
            MailSendRequest request, HttpContext http, OperatorAuthService auth, GameCommandService commands,
            CancellationToken cancellationToken) =>
        {
            var account = await AuthorizeAsync(http, auth, cancellationToken);
            if (account is null)
            {
                return Unauthorized();
            }

            if (string.IsNullOrWhiteSpace(request.Title))
            {
                return Results.BadRequest(new { message = "제목이 비어 있습니다." });
            }

            if (request.DurationSec <= 0)
            {
                return Results.BadRequest(new { message = "유효 기간(초)은 1 이상이어야 합니다." });
            }

            var result = await commands.SendMailAsync(
                account.OperatorId, request.ClientSessionId, request.Title, request.Body ?? string.Empty,
                request.DurationSec, cancellationToken);

            return Results.Ok(new { success = result.IsSuccess, result.AffectedCount, message = result.Describe() });
        });

        api.MapPost("/mail/delete", async (
            MailDeleteRequest request, HttpContext http, OperatorAuthService auth, GameCommandService commands,
            CancellationToken cancellationToken) =>
        {
            var account = await AuthorizeAsync(http, auth, cancellationToken);
            if (account is null)
            {
                return Unauthorized();
            }

            var result = await commands.DeleteMailAsync(
                account.OperatorId, request.ClientSessionId, request.MailId, cancellationToken);

            return Results.Ok(new { success = result.IsSuccess, message = result.Describe() });
        });

        // 쿠폰 (운영자 토큰 필요)
        api.MapGet("/coupon/campaigns", async (
            HttpContext http, OperatorAuthService auth, CouponIssueService coupons,
            CancellationToken cancellationToken) =>
        {
            var account = await AuthorizeAsync(http, auth, cancellationToken);
            if (account is null)
            {
                return Unauthorized();
            }

            var campaigns = await coupons.ListCampaignsAsync(cancellationToken);
            return Results.Ok(campaigns);
        });

        api.MapPost("/coupon/campaigns", async (
            CampaignCreateRequest request, HttpContext http, OperatorAuthService auth, CouponIssueService coupons,
            CancellationToken cancellationToken) =>
        {
            var account = await AuthorizeAsync(http, auth, cancellationToken);
            if (account is null)
            {
                return Unauthorized();
            }

            var code = string.IsNullOrWhiteSpace(request.CampaignCode)
                ? await coupons.SuggestCampaignCodeAsync(cancellationToken)
                : request.CampaignCode.ToUpperInvariant();

            if (code is null)
            {
                return Results.Problem("사용 가능한 캠페인 코드를 찾지 못했습니다.");
            }

            try
            {
                var campaign = await coupons.CreateCampaignAsync(new Models.CouponCampaign
                {
                    CampaignCode = code,
                    CampaignName = request.CampaignName,
                    RewardKind = "Mail",
                    RewardTitle = request.RewardTitle ?? request.CampaignName,
                    RewardBody = request.RewardBody ?? string.Empty,
                    RewardDurationSec = request.RewardDurationSec <= 0 ? 604800 : request.RewardDurationSec,
                    MaxUseCount = request.MaxUseCount,
                    ValidFrom = request.ValidFrom,
                    ValidTo = request.ValidTo,
                    ValidDaysAfterIssue = request.ValidDaysAfterIssue,
                    CreatedBy = account.OperatorId,
                }, cancellationToken);

                return Results.Ok(campaign);
            }
            catch (Exception ex) when (ex is ArgumentException or InvalidOperationException)
            {
                return Results.BadRequest(new { message = ex.Message });
            }
        });

        api.MapPost("/coupon/issue", async (
            CouponIssueRequest request, HttpContext http, OperatorAuthService auth, CouponIssueService coupons,
            CancellationToken cancellationToken) =>
        {
            var account = await AuthorizeAsync(http, auth, cancellationToken);
            if (account is null)
            {
                return Unauthorized();
            }

            if (request.Count <= 0)
            {
                return Results.BadRequest(new { message = "발급 개수는 1 이상이어야 합니다." });
            }

            try
            {
                var summary = await coupons.IssueAsync(
                    request.CampaignCode.ToUpperInvariant(), request.Count, account.OperatorId,
                    progress: null, cancellationToken);

                return Results.Ok(summary);
            }
            catch (Exception ex) when (ex is ArgumentException or InvalidOperationException)
            {
                return Results.BadRequest(new { message = ex.Message });
            }
        });

        api.MapGet("/coupon/inspect", async (
            string code, HttpContext http, OperatorAuthService auth, CouponRedeemService redeem,
            CancellationToken cancellationToken) =>
        {
            var account = await AuthorizeAsync(http, auth, cancellationToken);
            if (account is null)
            {
                return Unauthorized();
            }

            // 운영자에게는 실제 상태를 그대로 보여준다(응답 최소화는 외부 사용자용 규칙이다).
            var description = await redeem.InspectAsync(code, cancellationToken);
            return Results.Ok(new { code, description });
        });

        // 캠페인의 전체 쿠폰 코드를 CSV로 내려받는다. 마케터가 제휴사에 넘길 때 쓰는 경로라
        // 대량 발급 기능과 사실상 한 세트다.
        api.MapGet("/coupon/download/{campaignCode}", async (
            string campaignCode, HttpContext http, OperatorAuthService auth,
            Repositories.CouponRepository repository, CancellationToken cancellationToken) =>
        {
            var account = await AuthorizeAsync(http, auth, cancellationToken);
            if (account is null)
            {
                return Unauthorized();
            }

            if (!CouponCodeFormat.IsValidCampaignCode(campaignCode.ToUpperInvariant()))
            {
                return Results.BadRequest(new { message = "캠페인 코드 형식이 올바르지 않습니다." });
            }

            var code = campaignCode.ToUpperInvariant();

            // 100만 건을 메모리에 모으지 않고 응답 스트림에 바로 흘려보낸다.
            http.Response.ContentType = "text/csv; charset=utf-8";
            http.Response.Headers.ContentDisposition = $"attachment; filename=coupon_{code}.csv";

            // UTF8Encoding(false): BOM을 넣지 않는다. 코드가 전부 ASCII라 BOM은 정보가 없고,
            // 헤더 행을 그대로 자르는 순진한 CSV 파서에서는 첫 컬럼 이름이 깨진다
            // (스풀 파일도 같은 이유로 BOM 없이 쓴다 -- CouponSpoolWriter 참고).
            await using var writer = new StreamWriter(
                http.Response.Body, new System.Text.UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
            await writer.WriteLineAsync("coupon_code");
            await foreach (var couponCode in repository.StreamCodesAsync(code, cancellationToken))
            {
                await writer.WriteLineAsync(CouponCodeFormat.ToDisplay(couponCode));
            }

            await writer.FlushAsync(cancellationToken);
            return Results.Empty;
        });

        // 쿠폰 등록 (게임 사용자용 -- 토큰 불필요, 응답 최소화 + 레이트 리밋)
        api.MapPost("/coupon/redeem", async (
            CouponRedeemRequest request, HttpContext http, CouponRedeemService redeem,
            CancellationToken cancellationToken) =>
        {
            var requesterKey = http.Connection.RemoteIpAddress?.ToString() ?? "unknown";
            var result = await redeem.RedeemAsync(
                requesterKey, request.Code, request.ClientSessionId, cancellationToken);

            if (result.RetryAfterSeconds is { } retryAfter)
            {
                http.Response.Headers.RetryAfter = retryAfter.ToString();
            }

            // 실패 이유(InternalReason)는 응답에 넣지 않는다.
            return Results.Ok(new { success = result.Succeeded, message = result.PublicMessage });
        });
    }

    private static IResult Unauthorized()
        => Results.Json(new { message = "인증이 필요합니다." }, statusCode: StatusCodes.Status401Unauthorized);

    private static async Task<Models.OperatorAccount?> AuthorizeAsync(
        HttpContext http, OperatorAuthService auth, CancellationToken cancellationToken)
    {
        var header = http.Request.Headers.Authorization.ToString();
        if (string.IsNullOrWhiteSpace(header) || !header.StartsWith("Bearer ", StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }

        return await auth.ValidateTokenAsync(header["Bearer ".Length..].Trim(), cancellationToken);
    }
}

public sealed record LoginRequest(string LoginId, string Password);

public sealed record NoticeRequest(string Message);

public sealed record MailSendRequest(ulong? ClientSessionId, string Title, string? Body, long DurationSec);

public sealed record MailDeleteRequest(ulong ClientSessionId, uint MailId);

public sealed record CampaignCreateRequest(
    string? CampaignCode,
    string CampaignName,
    string? RewardTitle,
    string? RewardBody,
    long RewardDurationSec,
    int MaxUseCount,
    DateTime? ValidFrom,
    DateTime? ValidTo,
    int ValidDaysAfterIssue);

public sealed record CouponIssueRequest(string CampaignCode, long Count);

public sealed record CouponRedeemRequest(string Code, ulong? ClientSessionId);
