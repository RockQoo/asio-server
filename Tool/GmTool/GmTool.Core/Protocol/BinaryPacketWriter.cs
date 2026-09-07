using System.Buffers.Binary;
using System.Text;

namespace GmTool.Core.Protocol;

/// <summary>
/// C++ <c>Packet::BinaryWriter</c>와 바이트 단위로 호환되는 페이로드 라이터.
///
/// 규약(양쪽이 반드시 같아야 하는 부분):
/// <list type="bullet">
///   <item>정수는 전부 리틀엔디언. C++ 쪽은 구조체를 <c>memcpy</c>로 그대로 쓰므로 x64에서
///         리틀엔디언이 되고, 이쪽은 그 표현을 명시적으로 재현한다.</item>
///   <item>문자열은 길이(<c>ushort</c>) + UTF-8 바이트. C++ <c>WriteString</c>이
///         <c>text.size()</c>를 그대로 쓰므로 이 길이는 "문자 수"가 아니라 "바이트 수"다 —
///         한글 한 글자가 3바이트라 이걸 문자 수로 착각하면 그대로 깨진다.</item>
///   <item>구조체는 <c>#pragma pack(1)</c>이므로 패딩이 없다. 이쪽에서 필드를 선언 순서대로
///         하나씩 쓰면 같은 레이아웃이 된다.</item>
/// </list>
///
/// 구조체(struct)에 대응하는 전용 타입을 두지 않고 필드를 순서대로 쓰게 만든 이유: 패킷이
/// 몇 개뿐이고 대부분 가변 길이(문자열 포함)라, C# 구조체 + Marshal 레이아웃을 맞추는 쪽이
/// 오히려 실수하기 쉽다(<c>ushort</c> 뒤에 자동 정렬이 끼어들지 않는지 매번 확인해야 한다).
/// </summary>
public sealed class BinaryPacketWriter
{
    private byte[] buffer_;
    private int length_;

    public BinaryPacketWriter(int initialCapacity = 256)
    {
        buffer_ = new byte[Math.Max(initialCapacity, 16)];
        length_ = 0;
    }

    public int Length => length_;

    public ReadOnlySpan<byte> WrittenSpan => buffer_.AsSpan(0, length_);

    public byte[] ToArray() => buffer_.AsSpan(0, length_).ToArray();

    public void WriteUInt8(byte value)
    {
        Reserve(1);
        buffer_[length_] = value;
        length_ += 1;
    }

    public void WriteUInt16(ushort value)
    {
        Reserve(sizeof(ushort));
        BinaryPrimitives.WriteUInt16LittleEndian(buffer_.AsSpan(length_), value);
        length_ += sizeof(ushort);
    }

    public void WriteUInt32(uint value)
    {
        Reserve(sizeof(uint));
        BinaryPrimitives.WriteUInt32LittleEndian(buffer_.AsSpan(length_), value);
        length_ += sizeof(uint);
    }

    public void WriteUInt64(ulong value)
    {
        Reserve(sizeof(ulong));
        BinaryPrimitives.WriteUInt64LittleEndian(buffer_.AsSpan(length_), value);
        length_ += sizeof(ulong);
    }

    public void WriteInt64(long value)
    {
        Reserve(sizeof(long));
        BinaryPrimitives.WriteInt64LittleEndian(buffer_.AsSpan(length_), value);
        length_ += sizeof(long);
    }

    public void WriteSingle(float value)
    {
        Reserve(sizeof(float));
        BinaryPrimitives.WriteSingleLittleEndian(buffer_.AsSpan(length_), value);
        length_ += sizeof(float);
    }

    /// <summary>
    /// 길이(<c>ushort</c>) + UTF-8 바이트로 쓴다. UTF-8 바이트 수가 <c>ushort</c> 범위를 넘으면
    /// 조용히 잘라내는 대신 예외를 던진다 — 잘라내면 한글이 중간에서 끊겨 받는 쪽에서
    /// 깨진 문자열이 되고, 원인을 찾기 어려운 버그가 된다.
    /// </summary>
    public void WriteString(string? value)
    {
        var text = value ?? string.Empty;
        var byteCount = Encoding.UTF8.GetByteCount(text);
        if (byteCount > ushort.MaxValue)
        {
            throw new ArgumentException(
                $"문자열이 너무 깁니다: {byteCount}바이트 (상한 {ushort.MaxValue}바이트)", nameof(value));
        }

        WriteUInt16((ushort)byteCount);
        Reserve(byteCount);
        Encoding.UTF8.GetBytes(text, buffer_.AsSpan(length_, byteCount));
        length_ += byteCount;
    }

    public void WriteBytes(ReadOnlySpan<byte> data)
    {
        Reserve(data.Length);
        data.CopyTo(buffer_.AsSpan(length_));
        length_ += data.Length;
    }

    private void Reserve(int extra)
    {
        var required = length_ + extra;
        if (required <= buffer_.Length)
        {
            return;
        }

        var capacity = buffer_.Length;
        while (capacity < required)
        {
            capacity *= 2;
        }

        Array.Resize(ref buffer_, capacity);
    }
}
