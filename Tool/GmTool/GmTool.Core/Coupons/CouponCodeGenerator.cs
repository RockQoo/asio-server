using System.Security.Cryptography;

namespace GmTool.Core.Coupons;

/// <summary>
/// 쿠폰 번호 하나를 만드는 생성기.
///
/// 난수는 반드시 CSPRNG로 뽑는다. <c>System.Random</c>은 내부 상태가 시드로 결정되므로 발급된
/// 번호 몇 개만 확보하면 <b>이후 번호를 예측</b>할 수 있다. 쿠폰은 사실상 돈이라 여기서 타협하면
/// 안 된다.
/// </summary>
public static class CouponCodeGenerator
{
    /// <summary>캠페인 코드를 네임스페이스로 하는 쿠폰 번호 하나(하이픈 없는 25자).</summary>
    public static string Generate(string campaignCode)
    {
        if (!CouponCodeFormat.IsValidCampaignCode(campaignCode))
        {
            throw new ArgumentException(
                $"캠페인 코드는 Crockford Base32 {CouponCodeFormat.CampaignCodeLength}자여야 합니다: '{campaignCode}'",
                nameof(campaignCode));
        }

        Span<char> code = stackalloc char[CouponCodeFormat.TotalLength];

        campaignCode.AsSpan().CopyTo(code);

        // GetItems를 쓰는 이유: 바이트를 직접 뽑아 % 32를 하면 문자셋 크기가 2의 거듭제곱이
        // 아니게 되는 순간 조용히 편향(modulo bias)이 생긴다. 이 API는 거절 샘플링으로
        // 균등 분포를 보장한다.
        var random = RandomNumberGenerator.GetItems<char>(
            CrockfordBase32.Alphabet, CouponCodeFormat.RandomLength);
        random.AsSpan().CopyTo(code[CouponCodeFormat.CampaignCodeLength..]);

        var check = CouponCheckDigit.Compute(code[..CouponCodeFormat.PayloadLength]);
        check.AsSpan().CopyTo(code[CouponCodeFormat.PayloadLength..]);

        return new string(code);
    }

    /// <summary>
    /// 새 캠페인 코드 후보를 하나 만든다. 형식만 맞춘 무작위 값이므로 <b>실제 사용 전에 DB에서
    /// 유일성을 확인해야 한다</b>.
    /// </summary>
    public static string GenerateCampaignCode()
    {
        var chars = RandomNumberGenerator.GetItems<char>(
            CrockfordBase32.Alphabet, CouponCodeFormat.CampaignCodeLength);
        return new string(chars);
    }
}
