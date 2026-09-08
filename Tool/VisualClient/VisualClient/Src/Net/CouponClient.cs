using System.Net.Http.Json;
using System.Text.Json.Serialization;

namespace VisualClient.Net;

/// <summary>쿠폰 등록 요청. GmTool.Web의 <c>CouponRedeemRequest</c>와 필드 이름이 같아야 한다.</summary>
public sealed record CouponRedeemRequest(
    [property: JsonPropertyName("code")] string Code,
    [property: JsonPropertyName("clientSessionId")] ulong? ClientSessionId);

/// <summary>
/// 쿠폰 등록 응답. GmTool.Web은 <c>{ success, message }</c> 두 필드만 돌려준다 — 진짜 실패
/// 사유(이미 사용됨/만료됨/없는 번호)를 구분해주면 공격자가 유효 번호 범위를 좁히는 단서가
/// 되므로, 그건 서버의 <c>coupon_redeem_attempt</c> 테이블에만 남는다.
/// </summary>
public sealed record CouponRedeemResponse(
    [property: JsonPropertyName("success")] bool Succeeded,
    [property: JsonPropertyName("message")] string? Message);

/// <summary>
/// 쿠폰 등록을 GmTool.Web의 <c>POST /api/coupon/redeem</c>에 맡긴다.
///
/// <para>
/// <b>왜 패킷이 아니라 HTTP인가</b>: 쿠폰 등록은 클라이언트 대면 패킷 대역(C2W/C2Z)에 아예
/// 없다. 대신 쿠폰 체계 전체(캠페인, 발급 배치, 체크 문자, 레이트 리밋, 사용 처리, 등록 시도
/// 로그)가 SQL Server와 GmTool.Web에 이미 구현돼 있고, 그 엔드포인트 하나만 운영자 토큰
/// 없이 열려 있다 — 게임 사용자가 직접 부르는 자리로 설계된 것이기 때문이다. 같은 일을 C++
/// 서버에 다시 만들려면 World에 DB 연동부터 새로 해야 한다(<c>Db::DbWorker</c>는 현재 로그만
/// 남긴다).
/// </para>
///
/// <para>
/// 보상은 이 응답으로 오지 않는다. 등록이 성공하면 GmTool이 World에
/// <c>T2WMailSendRequest</c>를 보내고, 그게 존을 거쳐 <c>Z2CTaskResult</c>(Mail Added)로
/// <b>소켓 쪽으로</b> 도착한다 — 화면에서는 우편함에 한 통이 새로 생기는 것으로 보인다.
/// </para>
/// </summary>
public sealed class CouponClient : IDisposable
{
    private readonly HttpClient http_;

    public string BaseUrl { get; }

    public CouponClient(string baseUrl)
    {
        BaseUrl = baseUrl.TrimEnd('/');
        http_ = new HttpClient
        {
            BaseAddress = new Uri(BaseUrl),
            // 쿠폰 등록은 레이트 리밋 백오프까지 서버가 판단하므로 오래 기다릴 이유가 없다.
            Timeout = TimeSpan.FromSeconds(10),
        };
    }

    /// <summary>
    /// 쿠폰을 등록한다. 성공/실패 모두 사용자에게 보여줄 한 줄을 돌려준다 — 통신 자체가
    /// 실패한 경우(GmTool.Web이 안 떠 있음 등)도 예외를 올리지 않고 같은 모양으로 알린다.
    /// </summary>
    /// <param name="clientSessionId">
    /// 보상 우편을 받을 접속 중인 클라이언트. <c>Z2CEnterZoneNotify</c>가 준 playerId를 그대로
    /// 쓴다 — 서버의 playerId는 <c>static_cast&lt;uint32_t&gt;(clientSessionId)</c>이고
    /// SessionId는 1부터 증가하는 카운터라 상위 32비트가 0이어서 두 값이 같다.
    /// <b>세션 id 발급 방식을 바꾸면 이 전제가 깨진다.</b>
    /// </param>
    public async Task<CouponRedeemResponse> RedeemAsync(string code, ulong? clientSessionId)
    {
        try
        {
            var response = await http_.PostAsJsonAsync(
                "/api/coupon/redeem", new CouponRedeemRequest(code, clientSessionId)).ConfigureAwait(false);

            var body = await response.Content.ReadFromJsonAsync<CouponRedeemResponse>().ConfigureAwait(false);
            if (body is not null)
            {
                return body;
            }

            return new CouponRedeemResponse(false, $"응답을 해석할 수 없습니다(HTTP {(int)response.StatusCode}).");
        }
        catch (Exception ex)
        {
            return new CouponRedeemResponse(false, $"GmTool.Web에 붙지 못했습니다: {ex.Message}");
        }
    }

    public void Dispose() => http_.Dispose();
}
