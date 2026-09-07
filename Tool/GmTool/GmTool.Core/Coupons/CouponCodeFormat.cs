namespace GmTool.Core.Coupons;

/// <summary>
/// 쿠폰 번호 체계. 저장은 하이픈 없는 25자, 표시할 때만 5자리 블록으로 끊는다.
///
/// <code>
///   XXXXX-XXXXX-XXXXX-XXXXX-XXXCC
///   캠페인 5   난수(CSPRNG) 18   체크 2
/// </code>
///
/// 자리 배분의 근거와 캠페인 네임스페이스 전략은 <c>Tool/GmTool/README.md</c>의 "쿠폰" 절에
/// 정리해뒀다.
/// </summary>
public static class CouponCodeFormat
{
    public const int TotalLength = 25;

    public const int BlockLength = 5;

    public const int BlockCount = TotalLength / BlockLength;

    public const int CampaignCodeLength = 5;

    public const int CheckLength = 2;

    public const int RandomLength = TotalLength - CampaignCodeLength - CheckLength;

    /// <summary>체크 문자를 계산할 대상(= 앞 23자리) 길이.</summary>
    public const int PayloadLength = TotalLength - CheckLength;

    public const char Separator = '-';

    /// <summary>25자 코드를 <c>XXXXX-XXXXX-...</c> 형태로 만든다(표시/파일 출력용).</summary>
    public static string ToDisplay(string normalizedCode)
    {
        ArgumentNullException.ThrowIfNull(normalizedCode);
        if (normalizedCode.Length != TotalLength)
        {
            throw new ArgumentException($"쿠폰 코드는 {TotalLength}자여야 합니다: {normalizedCode.Length}자",
                nameof(normalizedCode));
        }

        // string.Create로 중간 할당을 없앤다 -- 100만 건 CSV 출력에서 이 경로가 100만 번 돈다.
        return string.Create(TotalLength + BlockCount - 1, normalizedCode, static (span, code) =>
        {
            var write = 0;
            for (var block = 0; block < BlockCount; ++block)
            {
                if (block > 0)
                {
                    span[write++] = Separator;
                }

                code.AsSpan(block * BlockLength, BlockLength).CopyTo(span[write..]);
                write += BlockLength;
            }
        });
    }

    /// <summary>
    /// 사용자 입력을 저장 형태(하이픈 없는 대문자 25자)로 정규화한다. 하이픈·공백·밑줄은 위치에
    /// 상관없이 제거한다 — 붙여넣기 과정에서 섞이는 게 흔하고, 형식 오류로 거절하면 CS 문의만
    /// 늘어난다.
    /// </summary>
    public static bool TryNormalize(string? input, out string normalized)
    {
        normalized = string.Empty;
        if (string.IsNullOrWhiteSpace(input))
        {
            return false;
        }

        Span<char> buffer = stackalloc char[TotalLength];
        var count = 0;

        foreach (var raw in input)
        {
            if (raw is Separator or ' ' or '\t' or '_')
            {
                continue;
            }

            if (count >= TotalLength)
            {
                // 뒤쪽을 잘라내면 25자를 넘는 입력이 우연히 통과할 수 있어 즉시 실패시킨다.
                return false;
            }

            if (!CrockfordBase32.TryGetValue(raw, out var value))
            {
                return false;
            }

            // 관용 입력(O -> 0 등)까지 정규화해서 저장 형태와 일치시킨다.
            buffer[count++] = CrockfordBase32.ToChar(value);
        }

        if (count != TotalLength)
        {
            return false;
        }

        normalized = new string(buffer);
        return true;
    }

    public static string ExtractCampaignCode(string normalizedCode)
    {
        ArgumentNullException.ThrowIfNull(normalizedCode);
        if (normalizedCode.Length < CampaignCodeLength)
        {
            throw new ArgumentException("코드가 캠페인 코드 길이보다 짧습니다.", nameof(normalizedCode));
        }

        return normalizedCode[..CampaignCodeLength];
    }

    /// <summary>
    /// 캠페인 코드로 쓸 수 있는지 검사한다. 관용 입력(<c>O</c>/<c>I</c>/<c>L</c>)은
    /// <b>허용하지 않는다</b> — 생성 코드에 절대 안 들어가는 글자를 캠페인 코드로 받으면 그
    /// 캠페인의 쿠폰은 정규화 후 캠페인 코드가 달라져 조회가 어긋난다.
    ///
    /// 이 검사가 테이블 이름 조립의 유일한 방어선이기도 하다(<c>CouponTableNaming</c> 참고).
    /// </summary>
    public static bool IsValidCampaignCode(string? campaignCode)
    {
        if (campaignCode is null || campaignCode.Length != CampaignCodeLength)
        {
            return false;
        }

        foreach (var c in campaignCode)
        {
            if (!CrockfordBase32.Alphabet.Contains(c))
            {
                return false;
            }
        }

        return true;
    }
}
