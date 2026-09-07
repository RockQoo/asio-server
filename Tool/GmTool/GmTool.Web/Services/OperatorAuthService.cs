using System.Security.Cryptography;
using System.Text;
using GmTool.Web.Models;
using GmTool.Web.Options;
using GmTool.Web.Repositories;
using Microsoft.Extensions.Options;

namespace GmTool.Web.Services;

/// <summary>로그인 결과.</summary>
public sealed record LoginResult(bool Succeeded, OperatorAccount? Account, string? ApiToken, string Message)
{
    public static LoginResult Fail(string message) => new(false, null, null, message);
}

/// <summary>
/// 운영자 로그인/토큰 발급. 인증 경로가 둘이다 — 웹 UI는 쿠키, HTTP API는 HMAC-SHA256 서명
/// 토큰. 토큰은 서명만으로 위조 검증이 되지만 강제 로그아웃·중복 로그인 차단을 하려면 "지금
/// 살아있는 토큰" 목록이 필요해서 DB에도 남긴다.
///
/// <b>실패 메시지를 세분화하지 말 것.</b> 아이디 없음과 비밀번호 오류를 구분하면 "이 아이디는
/// 존재한다"를 공짜로 알려주는 셈이다(계정 열거).
/// </summary>
public sealed class OperatorAuthService
{
    private const string GenericFailureMessage = "로그인 정보가 일치하지 않습니다.";

    private readonly OperatorRepository operators_;
    private readonly AuthOptions options_;
    private readonly ILogger<OperatorAuthService> logger_;

    public OperatorAuthService(
        OperatorRepository operators, IOptions<AuthOptions> options, ILogger<OperatorAuthService> logger)
    {
        operators_ = operators;
        options_ = options.Value;
        logger_ = logger;
    }

    public async Task<LoginResult> LoginAsync(
        string loginId, string password, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(loginId) || string.IsNullOrEmpty(password))
        {
            return LoginResult.Fail(GenericFailureMessage);
        }

        var account = await operators_.FindByLoginIdAsync(loginId, cancellationToken).ConfigureAwait(false);
        if (account is null)
        {
            // 존재하지 않는 계정도 비밀번호 검증과 비슷한 시간을 쓰게 한다. 바로 반환하면
            // 응답 시간만으로 "이 아이디는 없다"를 알 수 있다.
            _ = PasswordHasher.Verify(password, PasswordHasher.Hash("dummy"));
            logger_.LogWarning("로그인 실패(존재하지 않는 계정): {LoginId}", loginId);
            return LoginResult.Fail(GenericFailureMessage);
        }

        if (!account.IsActive)
        {
            logger_.LogWarning("로그인 실패(비활성 계정): {LoginId}", loginId);
            return LoginResult.Fail(GenericFailureMessage);
        }

        if (!PasswordHasher.Verify(password, account.PasswordHash))
        {
            logger_.LogWarning("로그인 실패(비밀번호 불일치): {LoginId}", loginId);
            return LoginResult.Fail(GenericFailureMessage);
        }

        // 반복 횟수 기준이 올라갔으면 지금 이 순간(평문을 아는 유일한 시점)에 재해싱한다.
        if (PasswordHasher.NeedsRehash(account.PasswordHash))
        {
            var upgraded = PasswordHasher.Hash(password);
            await operators_.UpdatePasswordHashAsync(account.OperatorId, upgraded, cancellationToken)
                .ConfigureAwait(false);
            logger_.LogInformation("비밀번호 해시를 현재 기준으로 갱신했습니다: {LoginId}", loginId);
        }

        // 중복 로그인 차단: 새로 로그인하면 이전 토큰을 전부 회수한다.
        await operators_.RevokeAllTokensAsync(account.OperatorId, cancellationToken).ConfigureAwait(false);

        var issuedAt = DateTime.UtcNow;
        var expiresAt = issuedAt.AddHours(options_.TokenLifetimeHours);
        var token = CreateToken(account.OperatorId, issuedAt);

        await operators_.StoreTokenAsync(account.OperatorId, token, issuedAt, expiresAt, cancellationToken)
            .ConfigureAwait(false);
        await operators_.TouchLastLoginAsync(account.OperatorId, cancellationToken).ConfigureAwait(false);

        logger_.LogInformation("로그인 성공: {LoginId} (id={OperatorId})", loginId, account.OperatorId);
        return new LoginResult(true, account, token, "로그인되었습니다.");
    }

    public Task LogoutAsync(ulong operatorId, CancellationToken cancellationToken = default)
        => operators_.RevokeAllTokensAsync(operatorId, cancellationToken);

    /// <summary>API 토큰을 검증하고 운영자 계정을 돌려준다.</summary>
    public async Task<OperatorAccount?> ValidateTokenAsync(
        string? token, CancellationToken cancellationToken = default)
    {
        if (string.IsNullOrWhiteSpace(token))
        {
            return null;
        }

        // 1차: 서명 검증(DB 없이). 형식이 깨졌거나 위조된 토큰은 여기서 걸러져 DB 조회를
        // 아예 하지 않는다 — 무작위 토큰을 던지는 공격에 DB 커넥션을 소모하지 않기 위함이다.
        if (!VerifyTokenSignature(token, out var operatorId))
        {
            return null;
        }

        // 2차: 살아있는 토큰인지(회수/만료되지 않았는지) DB 확인.
        var owner = await operators_.FindActiveTokenOwnerAsync(token, cancellationToken).ConfigureAwait(false);
        if (owner is null || owner.Value != operatorId)
        {
            return null;
        }

        return await operators_.FindByIdAsync(operatorId, cancellationToken).ConfigureAwait(false);
    }

    // 토큰 형식: base64url(operatorId "." issuedUnixSeconds "." nonce) "." base64url(HMAC)

    private string CreateToken(ulong operatorId, DateTime issuedAt)
    {
        var nonce = Convert.ToHexString(RandomNumberGenerator.GetBytes(8));
        var payload = $"{operatorId}.{new DateTimeOffset(issuedAt, TimeSpan.Zero).ToUnixTimeSeconds()}.{nonce}";
        var signature = ComputeSignature(payload);

        return $"{Base64UrlEncode(Encoding.UTF8.GetBytes(payload))}.{Base64UrlEncode(signature)}";
    }

    private bool VerifyTokenSignature(string token, out ulong operatorId)
    {
        operatorId = 0;

        var separator = token.LastIndexOf('.');
        if (separator <= 0 || separator == token.Length - 1)
        {
            return false;
        }

        byte[] payloadBytes;
        byte[] signature;
        try
        {
            payloadBytes = Base64UrlDecode(token[..separator]);
            signature = Base64UrlDecode(token[(separator + 1)..]);
        }
        catch (FormatException)
        {
            return false;
        }

        var payload = Encoding.UTF8.GetString(payloadBytes);
        var expected = ComputeSignature(payload);

        if (!CryptographicOperations.FixedTimeEquals(signature, expected))
        {
            return false;
        }

        var parts = payload.Split('.');
        return parts.Length == 3 && ulong.TryParse(parts[0], out operatorId);
    }

    private byte[] ComputeSignature(string payload)
    {
        var key = Encoding.UTF8.GetBytes(options_.TokenSigningKey);
        return HMACSHA256.HashData(key, Encoding.UTF8.GetBytes(payload));
    }

    private static string Base64UrlEncode(byte[] data)
        => Convert.ToBase64String(data).TrimEnd('=').Replace('+', '-').Replace('/', '_');

    private static byte[] Base64UrlDecode(string text)
    {
        var padded = text.Replace('-', '+').Replace('_', '/');
        padded += (padded.Length % 4) switch { 2 => "==", 3 => "=", _ => string.Empty };
        return Convert.FromBase64String(padded);
    }
}
