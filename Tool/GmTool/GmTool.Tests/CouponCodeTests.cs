using GmTool.Core.Coupons;

namespace GmTool.Tests;

/// <summary>
/// 쿠폰 번호 체계(문자셋 / 형식 / 체크 문자 / 생성기) 검증.
///
/// 이 부분을 테스트로 묶어둔 이유: 발급된 쿠폰은 되돌릴 수 없다. 문자셋이나 체크 문자 계산이
/// 한 번 바뀌면 이미 나간 수십만 장이 전부 검증에 실패하므로, 규칙이 우연히 바뀌는 것을
/// 막아주는 장치가 필요하다.
/// </summary>
public class CrockfordBase32Tests
{
    [Fact]
    public void 문자셋은_32글자이고_중복이_없다()
    {
        Assert.Equal(32, CrockfordBase32.Alphabet.Length);
        Assert.Equal(32, CrockfordBase32.Alphabet.Distinct().Count());
    }

    [Theory]
    [InlineData('I')]
    [InlineData('L')]
    [InlineData('O')]
    [InlineData('U')]
    public void 혼동되는_글자는_문자셋에_없다(char confusing)
    {
        Assert.DoesNotContain(confusing, CrockfordBase32.Alphabet);
    }

    [Fact]
    public void 값과_문자가_왕복한다()
    {
        for (var value = 0; value < CrockfordBase32.Radix; ++value)
        {
            var c = CrockfordBase32.ToChar(value);
            Assert.True(CrockfordBase32.TryGetValue(c, out var decoded));
            Assert.Equal(value, decoded);
        }
    }

    [Theory]
    [InlineData('O', 0)]  // 사용자가 숫자 0을 대문자 O로 잘못 읽는 경우
    [InlineData('I', 1)]  // 숫자 1을 I로
    [InlineData('L', 1)]  // 숫자 1을 소문자 l로 → 대문자 L
    public void 혼동_글자를_입력해도_의도한_값으로_읽어준다(char input, int expected)
    {
        Assert.True(CrockfordBase32.TryGetValue(input, out var value));
        Assert.Equal(expected, value);
    }

    [Fact]
    public void 소문자도_받아준다()
    {
        Assert.True(CrockfordBase32.TryGetValue('z', out var value));
        Assert.Equal(31, value);
    }

    [Theory]
    [InlineData('-')]
    [InlineData('@')]
    [InlineData(' ')]
    public void 문자셋에_없는_글자는_실패한다(char invalid)
    {
        Assert.False(CrockfordBase32.TryGetValue(invalid, out _));
    }
}

public class CouponCheckDigitTests
{
    [Fact]
    public void 생성한_코드는_항상_체크_문자_검증을_통과한다()
    {
        for (var i = 0; i < 2000; ++i)
        {
            var code = CouponCodeGenerator.Generate("X7Q4M");
            Assert.True(CouponCheckDigit.Verify(code), $"검증 실패: {code}");
        }
    }

    [Fact]
    public void 한_글자를_바꾸면_대부분_검증에_실패한다()
    {
        // 단순 합산 방식이라 "같은 자리를 값이 같은 다른 글자로" 바꿀 수는 없지만, 값이 다른
        // 글자로 바꾸면 합이 달라져 반드시 실패한다. 여기서는 확실히 값이 달라지는 치환만 쓴다.
        var code = CouponCodeGenerator.Generate("X7Q4M");
        var chars = code.ToCharArray();

        for (var i = 0; i < CouponCodeFormat.PayloadLength; ++i)
        {
            var original = chars[i];
            CrockfordBase32.TryGetValue(original, out var value);
            chars[i] = CrockfordBase32.ToChar((value + 1) % CrockfordBase32.Radix);

            Assert.False(CouponCheckDigit.Verify(new string(chars)),
                $"{i}번째 글자를 바꿨는데도 통과함: {new string(chars)}");

            chars[i] = original;
        }
    }

    [Fact]
    public void 무작위_입력은_99퍼센트_이상이_DB_조회_전에_걸러진다()
    {
        // 체크 문자 2자리 = 1024가지이므로 이론상 통과율은 1/1024 ≈ 0.098%다.
        // 이 테스트는 "DB 앞단 필터"라는 이 기능의 존재 이유를 수치로 고정한다.
        const int samples = 50_000;
        var random = new Random(20260907);
        var passed = 0;

        for (var i = 0; i < samples; ++i)
        {
            var chars = new char[CouponCodeFormat.TotalLength];
            for (var j = 0; j < chars.Length; ++j)
            {
                chars[j] = CrockfordBase32.Alphabet[random.Next(CrockfordBase32.Radix)];
            }

            if (CouponCheckDigit.Verify(new string(chars)))
            {
                ++passed;
            }
        }

        var passRate = passed / (double)samples;
        Assert.True(passRate < 0.005, $"통과율이 너무 높다: {passRate:P3} ({passed}/{samples})");
    }

    [Fact]
    public void 체크_문자는_두_글자다()
    {
        var payload = new string('0', CouponCodeFormat.PayloadLength);
        Assert.Equal(CouponCodeFormat.CheckLength, CouponCheckDigit.Compute(payload).Length);
    }

    [Fact]
    public void 길이가_다른_입력은_예외를_던진다()
    {
        Assert.Throws<ArgumentException>(() => CouponCheckDigit.Compute("SHORT"));
    }
}

public class CouponCodeFormatTests
{
    [Fact]
    public void 자릿수_배분이_25자로_맞는다()
    {
        Assert.Equal(25, CouponCodeFormat.TotalLength);
        Assert.Equal(
            CouponCodeFormat.TotalLength,
            CouponCodeFormat.CampaignCodeLength + CouponCodeFormat.RandomLength + CouponCodeFormat.CheckLength);
        Assert.Equal(18, CouponCodeFormat.RandomLength);
    }

    [Fact]
    public void 표시_형식은_5자_블록_5개를_하이픈으로_잇는다()
    {
        var code = CouponCodeGenerator.Generate("X7Q4M");
        var display = CouponCodeFormat.ToDisplay(code);

        Assert.Equal(29, display.Length);
        Assert.Equal(4, display.Count(c => c == '-'));
        Assert.All(display.Split('-'), block => Assert.Equal(5, block.Length));
    }

    [Fact]
    public void 표시_형식과_정규화가_왕복한다()
    {
        var code = CouponCodeGenerator.Generate("X7Q4M");
        var display = CouponCodeFormat.ToDisplay(code);

        Assert.True(CouponCodeFormat.TryNormalize(display, out var normalized));
        Assert.Equal(code, normalized);
    }

    [Theory]
    [InlineData("X7Q4M0123456789ABCDEFGH")]        // 23자 -- 짧다
    [InlineData("X7Q4M0123456789ABCDEFGHJKM")]     // 26자 -- 길다
    [InlineData("")]
    [InlineData(null)]
    public void 길이가_틀리면_정규화에_실패한다(string? input)
    {
        Assert.False(CouponCodeFormat.TryNormalize(input, out _));
    }

    [Fact]
    public void 하이픈과_공백은_위치에_상관없이_제거된다()
    {
        var code = CouponCodeGenerator.Generate("X7Q4M");
        var messy = $" {code[..7]} - {code[7..15]}  {code[15..]} ";

        Assert.True(CouponCodeFormat.TryNormalize(messy, out var normalized));
        Assert.Equal(code, normalized);
    }

    [Fact]
    public void 혼동_글자를_섞어_입력해도_정규화되어_원본과_일치한다()
    {
        // 생성된 코드에는 0과 1이 들어갈 수 있다. 사용자가 그걸 O와 I로 잘못 읽어 입력해도
        // 원래 코드로 정규화되어야 한다 -- 이게 안 되면 정상 쿠폰이 "없는 쿠폰"이 된다.
        // 캠페인 5자 + 난수 18자 = 23자. 난수 자리에 0과 1을 일부러 섞어둔다.
        const string payload = "X7Q4M" + "012345678901234567";
        Assert.Equal(CouponCodeFormat.PayloadLength, payload.Length);

        var full = payload + CouponCheckDigit.Compute(payload);

        var misread = full.Replace('0', 'O').Replace('1', 'I');

        Assert.True(CouponCodeFormat.TryNormalize(misread, out var normalized));
        Assert.Equal(full, normalized);
        Assert.True(CouponCheckDigit.Verify(normalized));
    }

    [Theory]
    [InlineData("X7Q4M", true)]
    [InlineData("x7q4m", false)]   // 소문자는 캠페인 코드로 허용하지 않는다(저장 형태와 어긋난다)
    [InlineData("X7Q4", false)]    // 4자
    [InlineData("X7Q4MM", false)]  // 6자
    [InlineData("X7Q4O", false)]   // O는 생성 문자셋에 없다 -- 캠페인 코드로 받으면 조회가 어긋난다
    [InlineData("X7Q4I", false)]
    [InlineData(null, false)]
    public void 캠페인_코드_형식_검사(string? code, bool expected)
    {
        Assert.Equal(expected, CouponCodeFormat.IsValidCampaignCode(code));
    }

    [Fact]
    public void 캠페인_코드를_앞_5자리에서_떼어낸다()
    {
        var code = CouponCodeGenerator.Generate("ZZ999");
        Assert.Equal("ZZ999", CouponCodeFormat.ExtractCampaignCode(code));
    }
}

public class CouponCodeGeneratorTests
{
    [Fact]
    public void 생성된_코드는_25자이고_문자셋_안의_글자만_쓴다()
    {
        var code = CouponCodeGenerator.Generate("X7Q4M");

        Assert.Equal(CouponCodeFormat.TotalLength, code.Length);
        Assert.All(code, c => Assert.Contains(c, CrockfordBase32.Alphabet));
    }

    [Fact]
    public void 앞_5자리는_캠페인_코드다()
    {
        const string campaign = "7ABCD";
        for (var i = 0; i < 100; ++i)
        {
            Assert.StartsWith(campaign, CouponCodeGenerator.Generate(campaign), StringComparison.Ordinal);
        }
    }

    [Fact]
    public void 형식이_틀린_캠페인_코드는_예외를_던진다()
    {
        Assert.Throws<ArgumentException>(() => CouponCodeGenerator.Generate("TOOLONG"));
        Assert.Throws<ArgumentException>(() => CouponCodeGenerator.Generate("X7Q4O"));
    }

    [Fact]
    public void 십만_장을_뽑아도_중복이_생기지_않는다()
    {
        // 난수 공간이 32^18 ≈ 1.2×10^27 이라 이 규모에서 충돌 확률은 사실상 0이다.
        // 여기서 중복이 나오면 난수원이나 자릿수 설계가 잘못된 것이다.
        const int count = 100_000;
        var unique = new HashSet<string>(count, StringComparer.Ordinal);

        for (var i = 0; i < count; ++i)
        {
            Assert.True(unique.Add(CouponCodeGenerator.Generate("X7Q4M")));
        }

        Assert.Equal(count, unique.Count);
    }

    [Fact]
    public void 난수_자리가_고르게_분포한다()
    {
        // CSPRNG가 문자셋 위에서 편향 없이 뽑히는지 대략적으로 확인한다. 특정 글자가
        // 기대치의 절반 미만/두 배 초과로 나오면 modulo bias 같은 문제를 의심해야 한다.
        const int samples = 20_000;
        var histogram = new int[CrockfordBase32.Radix];

        for (var i = 0; i < samples; ++i)
        {
            var code = CouponCodeGenerator.Generate("X7Q4M");
            foreach (var c in code.AsSpan(
                CouponCodeFormat.CampaignCodeLength, CouponCodeFormat.RandomLength))
            {
                CrockfordBase32.TryGetValue(c, out var value);
                ++histogram[value];
            }
        }

        var expected = samples * CouponCodeFormat.RandomLength / (double)CrockfordBase32.Radix;
        for (var value = 0; value < histogram.Length; ++value)
        {
            Assert.InRange(histogram[value], expected * 0.85, expected * 1.15);
        }
    }

    [Fact]
    public void 캠페인_코드_후보는_형식에_맞는다()
    {
        for (var i = 0; i < 200; ++i)
        {
            Assert.True(CouponCodeFormat.IsValidCampaignCode(CouponCodeGenerator.GenerateCampaignCode()));
        }
    }
}
