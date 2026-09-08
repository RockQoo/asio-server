namespace VisualClient.Protocol;

/// <summary>
/// 이 클라이언트가 쓰는 패킷 타입. C++ <c>Protocol::PacketId</c>
/// (<c>Shared/Protocol/Src/PacketId.h</c>)와 이름·값이 1:1로 같아야 한다.
///
/// <para>
/// 이름 앞 3글자는 "보내는 노드 2 받는 노드"이고, 방향마다 1000 단위로 번호 대역이 잘려 있다.
/// C++ enum에는 서버 내부 링크(G2W/W2G/W2Z/Z2W)와 운영툴(T2W/W2T) 대역도 함께 들어 있지만,
/// <b>이쪽은 클라이언트 대면 대역만 선언한다</b> — C2Z(요청) / Z2C(존 응답·통지) / W2C(World
/// 직접 브로드캐스트) 셋이 클라이언트가 볼 수 있는 전부다. 릴레이 봉투
/// (<c>ClientEnvelopeHeader</c>)는 GatewayServer가 벗겨서 넘겨주므로 클라이언트에 도달하는
/// 바이트는 항상 <c>PacketHeader</c> + 순수 페이로드다.
/// </para>
/// </summary>
public enum PacketId : ushort
{
    /// <summary>C2Z: 상태 확인용. 존의 LB 스레드가 게임 로직을 거치지 않고 즉시 되돌려준다.</summary>
    C2ZEcho = 1,

    /// <summary>C2Z: 길이 접두 문자열. 같은 존에 브로드캐스트된다(본인 포함).</summary>
    C2ZChat = 2,

    /// <summary>C2Z: <c>MovePacket</c>(x, y). 목표 좌표가 존 경계를 넘으면 핸드오프가 일어난다.</summary>
    C2ZMove = 3,

    /// <summary>C2Z: title/body/durationSec. 결과는 <see cref="Z2CTaskResult"/>로 돌아온다.</summary>
    C2ZMailAdd = 4,

    /// <summary>C2Z: mailId(uint32). 결과는 <see cref="Z2CTaskResult"/>로 돌아온다.</summary>
    C2ZMailDel = 5,

    /// <summary>Z2C: <see cref="C2ZEcho"/>의 응답. 본문은 보낸 바이트 그대로(길이 접두 없음).</summary>
    Z2CEchoAck = 1001,

    /// <summary>Z2C: senderId(uint32) + 메시지(길이 접두). 존 내부 브로드캐스트.</summary>
    Z2CChatNotify = 1002,

    /// <summary>Z2C: moverId(uint32) + <c>MovePacket</c>. 존 내부 브로드캐스트.</summary>
    Z2CMoveNotify = 1003,

    /// <summary>Z2C: 존 배정 통지. 신규 입장과 핸드오프 전입이 같은 패킷으로 온다.</summary>
    Z2CEnterZoneNotify = 1004,

    /// <summary>
    /// Z2C: errorCode(int32) + requestPacketId(uint16) + UnitOfWork 태스크 스트림.
    /// requestPacketId가 0이면 요청 없이 서버가 만든 변경(우편 만료 삭제, 운영툴 우편 발송).
    /// </summary>
    Z2CTaskResult = 1007,

    /// <summary>W2C: World가 존을 거치지 않고 접속 중 전체에 직접 뿌리는 공지(길이 접두 문자열).</summary>
    W2CNotice = 3001,
}
