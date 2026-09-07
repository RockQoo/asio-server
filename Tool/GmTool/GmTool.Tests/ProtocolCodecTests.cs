using System.Buffers.Binary;
using System.Text;
using GmTool.Core.Protocol;

namespace GmTool.Tests;

/// <summary>
/// C++ <c>Packet::BinaryWriter</c>/<c>BinaryReader</c>와의 와이어 호환성 검증.
///
/// 여기서 고정하려는 것은 <b>바이트 레이아웃</b>이다. 리틀엔디언인지, 문자열 길이가 문자
/// 수인지 바이트 수인지 같은 것들은 한쪽만 바뀌면 컴파일은 되지만 런타임에 조용히 깨진다.
/// 특히 한글은 UTF-8에서 3바이트라, 길이를 문자 수로 착각하면 그 뒤 필드가 전부 밀린다.
/// </summary>
public class BinaryPacketCodecTests
{
    [Fact]
    public void 정수는_리틀엔디언으로_기록된다()
    {
        var writer = new BinaryPacketWriter();
        writer.WriteUInt32(0x11223344);

        var bytes = writer.ToArray();
        Assert.Equal(new byte[] { 0x44, 0x33, 0x22, 0x11 }, bytes);
    }

    [Fact]
    public void 문자열은_바이트_길이_접두사를_쓴다()
    {
        // "가"는 UTF-8에서 3바이트다. 길이 필드에 1(문자 수)이 아니라 3(바이트 수)이 들어가야
        // C++ 쪽 BinaryReader::ReadString과 맞는다.
        var writer = new BinaryPacketWriter();
        writer.WriteString("가");

        var bytes = writer.ToArray();
        Assert.Equal(3, BinaryPrimitives.ReadUInt16LittleEndian(bytes));
        Assert.Equal(2 + 3, bytes.Length);
        Assert.Equal(Encoding.UTF8.GetBytes("가"), bytes[2..]);
    }

    [Fact]
    public void 한글_문자열이_왕복한다()
    {
        const string text = "운영툴에서 발송한 공지입니다 (테스트)";

        var writer = new BinaryPacketWriter();
        writer.WriteString(text);

        var reader = new BinaryPacketReader(writer.WrittenSpan);
        Assert.True(reader.TryReadString(out var decoded));
        Assert.Equal(text, decoded);
        Assert.True(reader.AtEnd);
    }

    [Fact]
    public void MailSendRequest_페이로드가_필드_순서대로_왕복한다()
    {
        // ToolProcessor::HandleMailSendRequest가 읽는 순서:
        //   requestId(u32) + targetKind(u8) + clientSessionId(u64)
        //     + String(title) + String(body) + durationSec(i64)
        var writer = new BinaryPacketWriter();
        writer.WriteUInt32(7);
        writer.WriteUInt8(ToolLinkProtocol.MailTargetSingle);
        writer.WriteUInt64(1234567890123UL);
        writer.WriteString("운영자 우편");
        writer.WriteString("본문 내용");
        writer.WriteInt64(604800);

        var reader = new BinaryPacketReader(writer.WrittenSpan);
        Assert.True(reader.TryReadUInt32(out var requestId));
        Assert.True(reader.TryReadUInt8(out var targetKind));
        Assert.True(reader.TryReadUInt64(out var sessionId));
        Assert.True(reader.TryReadString(out var title));
        Assert.True(reader.TryReadString(out var body));
        Assert.True(reader.TryReadInt64(out var duration));

        Assert.Equal(7u, requestId);
        Assert.Equal(ToolLinkProtocol.MailTargetSingle, targetKind);
        Assert.Equal(1234567890123UL, sessionId);
        Assert.Equal("운영자 우편", title);
        Assert.Equal("본문 내용", body);
        Assert.Equal(604800, duration);
        Assert.True(reader.AtEnd);
    }

    [Fact]
    public void 바이트가_부족하면_예외_대신_false를_돌려준다()
    {
        // 네트워크에서 온 신뢰할 수 없는 바이트를 다루므로, 잘못된 패킷 하나가 수신 루프를
        // 죽이지 않아야 한다(C++ BinaryReader와 같은 규약).
        var reader = new BinaryPacketReader(new byte[] { 0x01, 0x02 });
        Assert.False(reader.TryReadUInt32(out _));
        Assert.False(reader.TryReadUInt64(out _));
    }

    [Fact]
    public void 길이_접두사가_실제_바이트보다_크면_읽기에_실패한다()
    {
        // 조작된 길이 필드(100바이트라고 주장하지만 실제로는 2바이트뿐).
        var buffer = new byte[4];
        BinaryPrimitives.WriteUInt16LittleEndian(buffer, 100);

        var reader = new BinaryPacketReader(buffer);
        Assert.False(reader.TryReadString(out var text));
        Assert.Equal(string.Empty, text);
    }

    [Fact]
    public void 프로토콜_상수가_Cpp쪽과_같은_값이다()
    {
        // Packet::PacketHeader는 bodySize(2) + id(2) = 4바이트이고,
        // MaxBodySize()는 8192다. 한쪽만 바뀌면 큰 패킷이 조용히 끊긴다.
        Assert.Equal(4, ToolLinkProtocol.HeaderSize);
        Assert.Equal(8192, ToolLinkProtocol.MaxBodySize);
        Assert.Equal(1u, ToolLinkProtocol.ProtocolVersion);
    }

    [Fact]
    public void 쿠폰_전송_청크가_패킷_본문_상한을_넘지_않는다()
    {
        // 쿠폰 코드 하나가 27바이트(길이 2 + 25자)이고, 앞에 requestId/캠페인/시퀀스/개수가
        // 붙는다. 이 계산이 깨지면 받는 쪽 PacketBuffer가 PacketTooLarge로 연결을 끊는다.
        var writer = new BinaryPacketWriter();
        writer.WriteUInt32(1);
        writer.WriteString("X7Q4M");
        writer.WriteUInt32(1);
        writer.WriteUInt32(ToolLinkProtocol.CouponWireChunkSize);

        for (var i = 0; i < ToolLinkProtocol.CouponWireChunkSize; ++i)
        {
            writer.WriteString("X7Q4M0123456789ABCDEFGHJ");  // 24자 + 여유
            writer.WriteUInt8(0);
        }

        Assert.True(writer.Length <= ToolLinkProtocol.MaxBodySize,
            $"청크 {ToolLinkProtocol.CouponWireChunkSize}개가 본문 상한을 넘는다: {writer.Length}바이트");
    }

    [Fact]
    public void 너무_긴_문자열은_잘라내지_않고_예외를_던진다()
    {
        var writer = new BinaryPacketWriter();
        var tooLong = new string('A', ushort.MaxValue + 1);

        // 조용히 잘라내면 한글이 중간에서 끊겨 받는 쪽에서 깨진 문자열이 되고,
        // 원인을 찾기 어려운 버그가 된다.
        Assert.Throws<ArgumentException>(() => writer.WriteString(tooLong));
    }

    [Fact]
    public void null_문자열은_빈_문자열로_기록된다()
    {
        var writer = new BinaryPacketWriter();
        writer.WriteString(null);

        var reader = new BinaryPacketReader(writer.WrittenSpan);
        Assert.True(reader.TryReadString(out var decoded));
        Assert.Equal(string.Empty, decoded);
    }
}

public class ToolLinkEnumTests
{
    [Theory]
    [InlineData(ToolLinkPacketId.ToolHello, 1)]
    [InlineData(ToolLinkPacketId.ToolHelloAck, 2)]
    [InlineData(ToolLinkPacketId.NoticeRequest, 3)]
    [InlineData(ToolLinkPacketId.MailSendRequest, 4)]
    [InlineData(ToolLinkPacketId.MailDeleteRequest, 5)]
    [InlineData(ToolLinkPacketId.CouponChunkPush, 6)]
    [InlineData(ToolLinkPacketId.ClientListRequest, 7)]
    [InlineData(ToolLinkPacketId.ClientListReply, 8)]
    [InlineData(ToolLinkPacketId.ToolCommandAck, 9)]
    public void 패킷_id가_Cpp쪽_ToolLinkPacketId와_같다(ToolLinkPacketId id, int expected)
    {
        Assert.Equal(expected, (int)id);
    }

    [Theory]
    [InlineData(ToolResultCode.Ok, 0)]
    [InlineData(ToolResultCode.NotAuthenticated, 1)]
    [InlineData(ToolResultCode.BadRequest, 2)]
    [InlineData(ToolResultCode.TargetNotFound, 3)]
    [InlineData(ToolResultCode.ZoneUnavailable, 4)]
    public void 결과_코드가_Cpp쪽_EToolResultCode와_같다(ToolResultCode code, int expected)
    {
        Assert.Equal(expected, (int)code);
    }

    [Fact]
    public void 툴_전용_결과_코드는_서버_코드와_겹치지_않는다()
    {
        // 서버 enum이 앞으로 늘어나도 부딪히지 않게 큰 값을 쓴다.
        Assert.True((int)ToolResultCode.ToolTimeout >= 1000);
        Assert.True((int)ToolResultCode.ToolNotConnected >= 1000);
    }

    [Theory]
    [InlineData(ZoneClientPacketId.MailAdd, 5)]
    [InlineData(ZoneClientPacketId.MailDel, 6)]
    [InlineData(ZoneClientPacketId.Notice, 7)]
    public void 클라이언트_패킷_id가_Cpp쪽_ZonePacketId와_같다(ZoneClientPacketId id, int expected)
    {
        Assert.Equal(expected, (int)id);
    }
}
