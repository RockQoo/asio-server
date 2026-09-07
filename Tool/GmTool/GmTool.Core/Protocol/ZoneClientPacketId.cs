namespace GmTool.Core.Protocol;

/// <summary>
/// 게임 클라이언트 프로토콜의 패킷 타입. C++ <c>Zone::PacketId</c>와 값이 같아야 한다.
///
/// 운영툴의 우편/공지는 운영 전용 패킷이 아니라 <b>이 클라이언트 패킷을 그대로 존에 주입</b>하는
/// 방식이다 — 존/우편/UnitOfWork/DB 경로가 평소 클라이언트 요청과 완전히 같게 흐르도록,
/// 운영툴 전용 우회로를 만들지 않는 것이 목적이다.
///
/// 값을 실제로 채우는 쪽은 World의 ToolProcessor이고(MailSendRequest → MailAdd 등) 운영툴은
/// 의미 단위 요청만 보낸다. 그래서 이 enum은 지금 참조용이고, 쓰이는 곳이 없어 보이는 게 정상이다.
/// </summary>
public enum ZoneClientPacketId : ushort
{
    Echo = 1,
    Chat = 2,
    Move = 3,
    EnterZoneNotify = 4,
    MailAdd = 5,
    MailDel = 6,
    Notice = 7,
    MailAddAck = 8,
    MailDelAck = 9,
}
