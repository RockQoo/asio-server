namespace GmTool.Core.Protocol;

/// <summary>
/// 운영툴이 쓰는 패킷 타입. C++ <c>Protocol::PacketId</c>(<c>Shared/Protocol/Src/PacketId.h</c>)와
/// 이름·값이 1:1로 같아야 한다.
///
/// <para>
/// 이름 앞 3글자는 "보내는 노드 2 받는 노드"이고, 방향마다 1000 단위로 번호 대역이 잘려 있다.
/// C++ 쪽 enum에는 클라이언트/서버 내부 링크 대역도 함께 들어 있지만, <b>운영툴은 자기 대역
/// (T2W 8000번대 / W2T 9000번대)만 선언한다</b> -- 운영툴이 알아야 할 패킷이 그게 전부이기
/// 때문이다. 공지/우편은 결국 클라이언트 패킷으로 존에 주입되지만 그 변환은 전적으로 World의
/// <c>Tool::ToolProcessor</c>가 하므로, 운영툴은 어떤 클라이언트 패킷으로 바뀌는지 모른다.
/// </para>
///
/// <para>
/// T2W 요청 계열은 페이로드 맨 앞에 항상 requestId(uint32)를 둔다 -- 소켓 하나에 여러 요청이
/// 동시에 흘러다니므로 응답을 어느 요청의 답인지 짝지을 키가 필요하다.
/// </para>
/// </summary>
public enum PacketId : ushort
{
    /// <summary>T2W: 공유 시크릿 인증. 통과 전에는 다른 패킷이 모두 거부된다.</summary>
    T2WToolHello = 8001,

    /// <summary>T2W: 접속 중 전체 클라이언트에게 공지 브로드캐스트.</summary>
    T2WNoticeRequest = 8002,

    /// <summary>T2W: 특정 클라이언트 또는 접속 중 전체에게 우편 발송.</summary>
    T2WMailSendRequest = 8003,

    /// <summary>T2W: 특정 클라이언트의 우편 1건 삭제.</summary>
    T2WMailDeleteRequest = 8004,

    /// <summary>T2W: 운영툴이 로컬에서 생성한 쿠폰 번호 묶음(청크) 적재 요청.</summary>
    T2WCouponChunkPush = 8005,

    /// <summary>T2W: 지금 접속 중인 클라이언트 목록 조회.</summary>
    T2WClientListRequest = 8006,

    /// <summary>W2T: 인증 결과.</summary>
    W2TToolHelloAck = 9001,

    /// <summary>W2T: <see cref="T2WClientListRequest"/>의 응답.</summary>
    W2TClientListReply = 9002,

    /// <summary>W2T: 공지/우편/쿠폰 요청의 처리 결과.</summary>
    W2TToolCommandAck = 9003,
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
