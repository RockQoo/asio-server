namespace GmTool.Core.Coupons;

/// <summary>
/// Crockford Base32 문자셋. 혼동 문자(<c>I</c>/<c>L</c>/<c>O</c>/<c>U</c>)가 없어 사람이 눈으로
/// 읽고 손으로 입력하는 코드에 적합하다. 32 = 2^5라 문자 하나가 정확히 5비트다.
/// </summary>
public static class CrockfordBase32
{
    public const string Alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

    public const int Radix = 32;

    // Dictionary 대신 배열인 이유: 대량 발급 때 문자 하나당 한 번씩 조회한다.
    private static readonly sbyte[] ValueByChar = BuildValueTable();

    private static sbyte[] BuildValueTable()
    {
        var table = new sbyte[128];
        Array.Fill(table, (sbyte)-1);

        for (var i = 0; i < Alphabet.Length; ++i)
        {
            table[Alphabet[i]] = (sbyte)i;
        }

        // 관용 입력 규칙: 코드를 "생성"할 때는 이 글자들을 절대 쓰지 않지만, 사용자가 O를 0으로
        // 잘못 읽어 입력하는 건 흔하므로 읽기 쪽에서만 받아준다.
        table['O'] = table['0'];
        table['I'] = table['1'];
        table['L'] = table['1'];

        return table;
    }

    public static char ToChar(int value)
    {
        if (value is < 0 or >= Radix)
        {
            throw new ArgumentOutOfRangeException(nameof(value), value, "Base32 값은 0~31이어야 합니다.");
        }

        return Alphabet[value];
    }

    /// <summary>
    /// 문자를 값(0~31)으로 바꾼다. 혼동 문자는 관용적으로 받아준다
    /// (<see cref="BuildValueTable"/> 주석 참고).
    /// </summary>
    public static bool TryGetValue(char c, out int value)
    {
        var upper = char.ToUpperInvariant(c);
        if (upper >= ValueByChar.Length)
        {
            value = -1;
            return false;
        }

        var mapped = ValueByChar[upper];
        value = mapped;
        return mapped >= 0;
    }
}
