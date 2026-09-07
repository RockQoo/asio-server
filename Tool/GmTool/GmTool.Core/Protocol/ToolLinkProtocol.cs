namespace GmTool.Core.Protocol;

/// <summary>
/// 운영툴 ↔ WorldServer 링크의 와이어 프로토콜 상수. C++ 쪽
/// <c>Shared/Protocol/Src/PacketId.h</c> / <c>Server/WorldServer/Src/Packet/ToolLinkPackets.h</c>와 짝을
/// 이룬다 — <b>한쪽만 고치면 안 된다.</b>
/// </summary>
public static class ToolLinkProtocol
{
    /// <summary>
    /// C++ <c>World::kToolLinkProtocolVersion</c>과 같아야 한다. 와이어 포맷을 바꿀 때 양쪽을
    /// 같이 올리면, 어긋난 상태로 접속했을 때 이상하게 파싱되는 대신 인증에서 거부된다.
    /// </summary>
    public const uint ProtocolVersion = 1;

    /// <summary><c>Packet::PacketHeader</c>: bodySize(2) + id(2). 리틀엔디언 고정.</summary>
    public const int HeaderSize = 4;

    /// <summary>
    /// <c>Packet::PacketHeader::MaxBodySize()</c>와 동일. 넘기면 받는 쪽이 예외를 던지고 연결이
    /// 끊긴다.
    /// </summary>
    public const int MaxBodySize = 8192;

    public const byte MailTargetAllOnline = 0;

    public const byte MailTargetSingle = 1;

    /// <summary>
    /// CouponChunkPush 한 패킷의 쿠폰 개수. 코드 하나가 27바이트(길이 프리픽스 2 + 25자)라
    /// <see cref="MaxBodySize"/> 상한이 290개 남짓이고, 헤더 여유를 빼서 200으로 잡았다.
    ///
    /// <b>DB 청크 크기와 통합하지 말 것.</b> 여기는 패킷 크기 상한, 그쪽은 트랜잭션 단위라
    /// 별개 제약이다. 같은 상수로 묶으면 어느 한쪽이 반드시 비효율적이 된다.
    /// </summary>
    public const int CouponWireChunkSize = 200;
}
