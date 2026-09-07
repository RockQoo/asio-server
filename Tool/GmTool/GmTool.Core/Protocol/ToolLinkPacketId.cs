namespace GmTool.Core.Protocol;

/// <summary>
/// 운영툴 ↔ World 패킷 타입. C++ <c>World::ToolLinkPacketId</c>와 값이 1:1로 같아야 한다.
/// </summary>
public enum ToolLinkPacketId : ushort
{
    /// <summary>T2W: 공유 시크릿 인증. 통과 전에는 다른 패킷이 모두 거부된다.</summary>
    ToolHello = 1,

    /// <summary>W2T: 인증 결과.</summary>
    ToolHelloAck = 2,

    /// <summary>T2W: 접속 중 전체 클라이언트에게 공지 브로드캐스트.</summary>
    NoticeRequest = 3,

    /// <summary>T2W: 특정 클라이언트 또는 접속 중 전체에게 우편 발송.</summary>
    MailSendRequest = 4,

    /// <summary>T2W: 특정 클라이언트의 우편 1건 삭제.</summary>
    MailDeleteRequest = 5,

    /// <summary>T2W: 운영툴이 로컬에서 생성한 쿠폰 번호 묶음(청크) 적재 요청.</summary>
    CouponChunkPush = 6,

    /// <summary>T2W: 지금 접속 중인 클라이언트 목록 조회.</summary>
    ClientListRequest = 7,

    /// <summary>W2T: <see cref="ClientListRequest"/>의 응답.</summary>
    ClientListReply = 8,

    /// <summary>W2T: 공지/우편/쿠폰 요청의 처리 결과.</summary>
    ToolCommandAck = 9,
}

/// <summary>
/// <c>ToolCommandAck</c>의 resultCode. C++ <c>World::EToolResultCode</c>와 같은 값이다.
/// </summary>
public enum ToolResultCode : ushort
{
    Ok = 0,

    /// <summary>ToolHello를 통과하지 않은 세션이 요청을 보냈다.</summary>
    NotAuthenticated = 1,

    /// <summary>페이로드 파싱 실패 또는 필수 필드 누락.</summary>
    BadRequest = 2,

    /// <summary>대상 clientSessionId가 접속 중이 아니다.</summary>
    TargetNotFound = 3,

    /// <summary>대상 플레이어가 속한 존 서버와의 연결이 없다.</summary>
    ZoneUnavailable = 4,

    /// <summary>
    /// World가 응답을 주기 전에 타임아웃/연결 끊김이 발생했다. 서버에는 없는, 운영툴 쪽에서만
    /// 쓰는 값이다(서버 enum과 겹치지 않도록 큰 값을 쓴다).
    /// </summary>
    ToolTimeout = 1000,

    /// <summary>World 링크가 아직 연결/인증되지 않았다. 이것도 운영툴 전용 값이다.</summary>
    ToolNotConnected = 1001,
}
