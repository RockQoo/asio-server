namespace GmTool.Core.Coupons;

/// <summary>
/// 쿠폰 번호 맨 뒤 2자리 체크 문자. 앞 23자리의 Base32 값 합을 1024(=32^2)로 나눈 나머지를
/// 32진수 두 자리로 인코딩한다.
///
/// 용도는 <b>DB에 가기 전의 1차 필터</b>다 — 조합이 1024가지라 아무렇게나 만든 번호는 약
/// 99.9%가 조회 없이 거절된다.
///
/// <b>보안 장치가 아니다.</b> 계산식이 공개되어 있으면 체크 문자를 맞춰서 만들 수 있다.
/// 위·변조를 막으려면 서버만 아는 비밀로 HMAC 서명을 넣어야 한다. 유효성의 권위는 언제나
/// DB에 있다.
///
/// 단순 합산이라 두 자리를 뒤집은 오타(transposition)는 잡히지 않는다. 위치 가중치를 주면
/// (Luhn/Verhoeff 계열) 잡히지만, 랜덤 문자열에서 그런 오타는 드물고 최종 판정은 어차피 DB에서
/// 나므로 합산을 유지한다.
/// </summary>
public static class CouponCheckDigit
{
    public const int Modulus = CrockfordBase32.Radix * CrockfordBase32.Radix;

    public static string Compute(ReadOnlySpan<char> payload)
    {
        if (payload.Length != CouponCodeFormat.PayloadLength)
        {
            throw new ArgumentException(
                $"체크 문자 계산 대상은 {CouponCodeFormat.PayloadLength}자여야 합니다: {payload.Length}자",
                nameof(payload));
        }

        var sum = 0;
        foreach (var c in payload)
        {
            if (!CrockfordBase32.TryGetValue(c, out var value))
            {
                throw new ArgumentException($"문자셋에 없는 글자가 있습니다: '{c}'", nameof(payload));
            }

            sum += value;
        }

        var remainder = sum % Modulus;
        return string.Create(CouponCodeFormat.CheckLength, remainder, static (span, value) =>
        {
            span[0] = CrockfordBase32.ToChar(value / CrockfordBase32.Radix);
            span[1] = CrockfordBase32.ToChar(value % CrockfordBase32.Radix);
        });
    }

    /// <summary>정규화된 25자 코드의 체크 문자를 검사한다. DB 조회 <b>전에</b> 호출한다.</summary>
    public static bool Verify(string normalizedCode)
    {
        if (normalizedCode is null || normalizedCode.Length != CouponCodeFormat.TotalLength)
        {
            return false;
        }

        var payload = normalizedCode.AsSpan(0, CouponCodeFormat.PayloadLength);
        var expected = normalizedCode.AsSpan(CouponCodeFormat.PayloadLength);

        var sum = 0;
        foreach (var c in payload)
        {
            if (!CrockfordBase32.TryGetValue(c, out var value))
            {
                return false;
            }

            sum += value;
        }

        var remainder = sum % Modulus;
        return expected[0] == CrockfordBase32.ToChar(remainder / CrockfordBase32.Radix)
            && expected[1] == CrockfordBase32.ToChar(remainder % CrockfordBase32.Radix);
    }
}
