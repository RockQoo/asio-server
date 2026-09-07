using System.Security.Cryptography;

namespace GmTool.Web.Services;

/// <summary>
/// 운영자 비밀번호 해싱. PBKDF2(HMAC-SHA256) + 계정마다 다른 솔트. SHA256 한 번으로 대체하면
/// 안 된다 — 빠른 함수라 DB 유출 시 사전 공격에 그대로 뚫린다.
///
/// 저장 형식 <c>iterations.salt(base64).hash(base64)</c>에 반복 횟수를 함께 넣는 이유: 평문을
/// 모르니 전체 계정을 한 번에 재해싱할 방법이 없다. 값 안에 횟수가 있으면 각 계정이 로그인하는
/// 시점에 하나씩 올릴 수 있다(<see cref="NeedsRehash"/>).
/// </summary>
public static class PasswordHasher
{
    private const int SaltSize = 16;
    private const int HashSize = 32;

    /// <summary>현재 반복 횟수. 하드웨어가 빨라지면 올린다.</summary>
    public const int CurrentIterations = 210_000;

    public static string Hash(string password, int iterations = CurrentIterations)
    {
        ArgumentException.ThrowIfNullOrEmpty(password);

        var salt = RandomNumberGenerator.GetBytes(SaltSize);
        var hash = Rfc2898DeriveBytes.Pbkdf2(password, salt, iterations, HashAlgorithmName.SHA256, HashSize);

        return $"{iterations}.{Convert.ToBase64String(salt)}.{Convert.ToBase64String(hash)}";
    }

    public static bool Verify(string password, string storedHash)
    {
        if (string.IsNullOrEmpty(password) || string.IsNullOrEmpty(storedHash))
        {
            return false;
        }

        var parts = storedHash.Split('.');
        if (parts.Length != 3 || !int.TryParse(parts[0], out var iterations))
        {
            return false;
        }

        byte[] salt;
        byte[] expected;
        try
        {
            salt = Convert.FromBase64String(parts[1]);
            expected = Convert.FromBase64String(parts[2]);
        }
        catch (FormatException)
        {
            return false;
        }

        var actual = Rfc2898DeriveBytes.Pbkdf2(password, salt, iterations, HashAlgorithmName.SHA256, expected.Length);

        // 고정 시간 비교. 일반 == 비교는 첫 다른 바이트에서 끝나므로, 응답 시간 차이로
        // 해시를 한 바이트씩 맞춰나가는 공격이 이론적으로 가능하다.
        return CryptographicOperations.FixedTimeEquals(actual, expected);
    }

    /// <summary>저장된 해시가 지금 기준보다 약한 반복 횟수로 만들어졌는지.</summary>
    public static bool NeedsRehash(string storedHash)
    {
        var parts = storedHash.Split('.');
        return parts.Length != 3
            || !int.TryParse(parts[0], out var iterations)
            || iterations < CurrentIterations;
    }
}
