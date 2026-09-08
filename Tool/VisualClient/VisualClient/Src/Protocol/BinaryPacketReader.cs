using System.Buffers.Binary;
using System.Text;

namespace VisualClient.Protocol;

/// <summary>
/// C++ <c>Packet::BinaryReader</c>와 짝을 이루는 리더. 소유하지 않는 스팬 위를 커서로 읽고,
/// 읽을 바이트가 부족하면 예외 대신 <c>false</c>를 돌려준다(C++ 쪽과 동일한 규약).
///
/// 예외를 안 던지는 이유: 이 리더가 처리하는 건 네트워크에서 온 신뢰할 수 없는 바이트다.
/// 잘못된 길이 하나로 예외가 올라가면 그 위 소켓 수신 루프가 죽는데, 실제로 필요한 동작은
/// "이 패킷 하나만 버리고 계속 받기"이므로 반환값으로 처리하는 편이 맞다.
/// <para>
/// <b>GmTool.Core의 같은 이름 파일과 의도적으로 같은 내용이다.</b> 두 도구는 서로 다른
/// 링크(운영툴은 T2W/W2T, 이쪽은 C2Z/Z2C)를 쓰고 솔루션도 따로라 프로젝트 참조로 묶지
/// 않았다 — 공유하는 표면이 이 코덱 2개뿐이어서, 참조로 엮어 GmTool 빌드에 이 클라이언트를
/// 끌고 들어오는 값이 복사 비용보다 크지 않다. <b>와이어 포맷을 바꾸면 양쪽을 같이 고쳐야
/// 한다.</b>
/// </para>
/// </summary>
public ref struct BinaryPacketReader
{
    private readonly ReadOnlySpan<byte> data_;
    private int position_;

    public BinaryPacketReader(ReadOnlySpan<byte> data)
    {
        data_ = data;
        position_ = 0;
    }

    public int Remaining => data_.Length - position_;

    public bool AtEnd => position_ >= data_.Length;

    public bool TryReadUInt8(out byte value)
    {
        if (Remaining < sizeof(byte))
        {
            value = 0;
            return false;
        }

        value = data_[position_];
        position_ += sizeof(byte);
        return true;
    }

    public bool TryReadUInt16(out ushort value)
    {
        if (Remaining < sizeof(ushort))
        {
            value = 0;
            return false;
        }

        value = BinaryPrimitives.ReadUInt16LittleEndian(data_[position_..]);
        position_ += sizeof(ushort);
        return true;
    }

    public bool TryReadUInt32(out uint value)
    {
        if (Remaining < sizeof(uint))
        {
            value = 0;
            return false;
        }

        value = BinaryPrimitives.ReadUInt32LittleEndian(data_[position_..]);
        position_ += sizeof(uint);
        return true;
    }

    public bool TryReadInt32(out int value)
    {
        if (Remaining < sizeof(int))
        {
            value = 0;
            return false;
        }

        value = BinaryPrimitives.ReadInt32LittleEndian(data_[position_..]);
        position_ += sizeof(int);
        return true;
    }

    /// <summary>C++ <c>float</c>(<c>Zone::MovePacket</c>의 x/y)와 짝. IEEE754 4바이트 리틀엔디언.</summary>
    public bool TryReadSingle(out float value)
    {
        if (Remaining < sizeof(float))
        {
            value = 0.0f;
            return false;
        }

        value = BinaryPrimitives.ReadSingleLittleEndian(data_[position_..]);
        position_ += sizeof(float);
        return true;
    }

    public bool TryReadUInt64(out ulong value)
    {
        if (Remaining < sizeof(ulong))
        {
            value = 0;
            return false;
        }

        value = BinaryPrimitives.ReadUInt64LittleEndian(data_[position_..]);
        position_ += sizeof(ulong);
        return true;
    }

    public bool TryReadInt64(out long value)
    {
        if (Remaining < sizeof(long))
        {
            value = 0;
            return false;
        }

        value = BinaryPrimitives.ReadInt64LittleEndian(data_[position_..]);
        position_ += sizeof(long);
        return true;
    }

    /// <summary>길이(<c>ushort</c>) + UTF-8 바이트를 읽는다. 길이는 바이트 수다(문자 수 아님).</summary>
    public bool TryReadString(out string value)
    {
        if (!TryReadUInt16(out var byteCount))
        {
            value = string.Empty;
            return false;
        }

        if (Remaining < byteCount)
        {
            value = string.Empty;
            return false;
        }

        value = Encoding.UTF8.GetString(data_.Slice(position_, byteCount));
        position_ += byteCount;
        return true;
    }
}
