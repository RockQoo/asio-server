namespace VisualClient.Protocol;

/// <summary>
/// <c>Z2CTaskResult</c> 앞에 실려 오는 콘텐츠 처리 결과 코드. C++ <c>Protocol::EErrorCode</c>
/// (<c>Shared/Protocol/Src/ErrorCode.h</c>)와 같은 값이다. 콘텐츠별로 100 단위 대역을 쓴다.
/// </summary>
public enum ErrorCode
{
    Success = 0,

    /// <summary>패킷 본문 파싱 실패 / 필수 필드 누락.</summary>
    InvalidPayload = 1,

    /// <summary>대상 mailId가 우편함에 없음.</summary>
    MailNotFound = 100,

    /// <summary>배정하려는 mailId가 이미 우편함에 있음(서버 id 발급 버그 신호).</summary>
    MailAlreadyExists = 101,

    /// <summary>이 세션의 우편함 자체가 없음(입장 처리 누락 신호).</summary>
    MailBoxNotFound = 102,
}

public static class ErrorCodeText
{
    /// <summary>
    /// 화면에 찍을 한 줄 설명. 알 수 없는 값은 숫자를 그대로 보여준다 — 서버가 새 에러를
    /// 추가했는데 클라이언트가 아직 모르는 상황을 "성공"이나 빈 문자열로 뭉개면 안 된다.
    /// </summary>
    public static string Describe(int errorCode) => (ErrorCode)errorCode switch
    {
        ErrorCode.Success => "성공",
        ErrorCode.InvalidPayload => "본문 파싱 실패",
        ErrorCode.MailNotFound => "그 우편이 없습니다",
        ErrorCode.MailAlreadyExists => "우편 id 충돌(서버 버그 신호)",
        ErrorCode.MailBoxNotFound => "우편함이 없습니다(입장 처리 누락)",
        _ => $"알 수 없는 에러({errorCode})",
    };
}
