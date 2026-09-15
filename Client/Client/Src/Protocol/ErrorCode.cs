namespace Client.Protocol;

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

    /// <summary>아이디/비밀번호가 비었거나 길이 제한을 넘음.</summary>
    LoginInvalidInput = 300,

    /// <summary>계정은 있는데 비밀번호가 다름. <b>계정이 없는 것은 실패가 아니다</b>(자동 가입).</summary>
    LoginWrongPassword = 301,

    /// <summary>DB 조회/생성이 실패했다. 재시도하면 될 수 있다.</summary>
    LoginDbFailure = 302,

    /// <summary>이미 로그인한 연결이 또 보냄.</summary>
    LoginAlreadyAuthenticated = 303,

    /// <summary>인증은 됐는데 입장시킬 존이 아직 World에 붙지 않음.</summary>
    LoginNoZoneAvailable = 304,

    /// <summary>내 유닛이 존에 없음(입장 처리 누락 신호).</summary>
    CombatUnitNotFound = 400,

    /// <summary>대상이 이 존에 없음. 이미 퇴장했거나 화면이 낡았다.</summary>
    CombatNoTarget = 401,

    /// <summary>내가 죽은 채로 때리려 함.</summary>
    CombatSelfDead = 402,

    /// <summary>이미 죽은 대상.</summary>
    CombatTargetDead = 403,

    /// <summary>같은 진영(플레이어끼리). 지금은 PvP가 없다.</summary>
    CombatSameKind = 404,

    /// <summary>사거리 밖.</summary>
    CombatOutOfRange = 405,

    /// <summary>쿨다운이 안 돌았음.</summary>
    CombatOnCooldown = 406,

    /// <summary>MP 부족. 서버는 검증을 다 통과한 뒤에만 소모하므로 아무것도 안 깎였다.</summary>
    CombatNotEnoughMp = 407,
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
        ErrorCode.LoginInvalidInput => "아이디와 비밀번호를 확인하세요(비었거나 너무 깁니다)",
        ErrorCode.LoginWrongPassword => "비밀번호가 다릅니다",
        ErrorCode.LoginDbFailure => "서버가 DB에 접근하지 못했습니다. 잠시 후 다시 시도하세요",
        ErrorCode.LoginAlreadyAuthenticated => "이미 로그인된 연결입니다",
        ErrorCode.LoginNoZoneAvailable => "입장할 존 서버가 아직 준비되지 않았습니다",
        ErrorCode.CombatUnitNotFound => "내 유닛이 존에 없습니다(입장 처리 누락)",
        ErrorCode.CombatNoTarget => "대상이 없습니다",
        ErrorCode.CombatSelfDead => "죽어 있어 공격할 수 없습니다",
        ErrorCode.CombatTargetDead => "이미 쓰러진 대상입니다",
        ErrorCode.CombatSameKind => "같은 편은 공격할 수 없습니다",
        ErrorCode.CombatOutOfRange => "사거리 밖입니다",
        ErrorCode.CombatOnCooldown => "아직 재사용 대기 중입니다",
        ErrorCode.CombatNotEnoughMp => "MP가 부족합니다",
        _ => $"알 수 없는 에러({errorCode})",
    };
}
