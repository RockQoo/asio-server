using System.Collections.Concurrent;
using System.Net.Sockets;
using VisualClient.Protocol;

namespace VisualClient.Net;

/// <summary>수신한 패킷 하나. 페이로드는 이미 이 패킷 몫만 잘라낸 복사본이다.</summary>
public readonly record struct InboundPacket(PacketId Id, byte[] Payload);

/// <summary>링크 상태. 화면 좌상단에 그대로 표시한다.</summary>
public enum LinkState
{
    Disconnected,
    Connecting,
    Connected,
    Failed,
}

/// <summary>
/// GatewayServer(기본 9000)와의 TCP 연결 하나. <c>[bodySize:2][id:2][본문]</c> 프레이밍을
/// 풀어서 패킷 단위로 큐에 넣어준다.
///
/// <para>
/// <b>스레드 규약이 서버의 NETWORK/LB 분리와 같은 모양이다.</b> 수신은 별도 Task에서 돌면서
/// 바이트를 프레이밍만 해 <see cref="ConcurrentQueue{T}"/>에 넣고, 게임 상태는 전혀 건드리지
/// 않는다. 실제 상태 변경은 게임 스레드가 <see cref="Drain"/>으로 큐를 비우면서 한다 — 이렇게
/// 하면 <c>WorldModel</c>은 락이 하나도 필요 없다(서버의 <c>ZoneInstance</c>가 BASIC 스레드
/// 전용이라 락이 없는 것과 같은 이유).
/// </para>
///
/// <para>
/// 송신은 어느 스레드에서 불러도 되게 <see cref="SemaphoreSlim"/>으로 직렬화한다. 게임
/// 스레드에서만 부르고 있지만, 한 번에 한 쓰기만 나가는 것을 타입 차원에서 보장해두지 않으면
/// 프레임이 섞여 프로토콜이 깨진다(서버 <c>Network::Session</c>이 <c>strand_</c>로 같은 일을 한다).
/// </para>
/// </summary>
public sealed class GameLink : IDisposable
{
    /// <summary>C++ <c>Packet::PacketHeader</c>: bodySize(2) + id(2). 리틀엔디언 고정.</summary>
    private const int HeaderSize = 4;

    /// <summary>C++ <c>Packet::PacketHeader::MaxBodySize()</c>와 같아야 한다.</summary>
    private const int MaxBodySize = 8192;

    private readonly ConcurrentQueue<InboundPacket> inbound_ = new();
    private readonly SemaphoreSlim sendLock_ = new(1, 1);
    private readonly CancellationTokenSource shutdown_ = new();

    private TcpClient? client_;
    private NetworkStream? stream_;
    private volatile LinkState state_ = LinkState.Disconnected;
    private volatile string lastError_ = string.Empty;

    public LinkState State => state_;

    public string LastError => lastError_;

    public string Host { get; }

    public int Port { get; }

    public GameLink(string host, int port)
    {
        Host = host;
        Port = port;
    }

    /// <summary>
    /// 접속하고, 성공하면 수신 루프를 백그라운드로 띄운다. 실패는 예외가 아니라
    /// <see cref="State"/>/<see cref="LastError"/>로 남긴다 — 서버가 안 떠 있는 상태로도 창은
    /// 정상적으로 열려서 "무엇이 안 됐는지"를 화면에 보여주는 게 이 도구의 목적이다.
    /// </summary>
    public async Task ConnectAsync()
    {
        state_ = LinkState.Connecting;
        try
        {
            client_ = new TcpClient();
            // 이동 패킷이 프레임마다 나가므로 40ms씩 뭉치면 화면이 눈에 띄게 늦게 따라온다.
            client_.NoDelay = true;
            await client_.ConnectAsync(Host, Port, shutdown_.Token).ConfigureAwait(false);
            stream_ = client_.GetStream();
            state_ = LinkState.Connected;
            _ = Task.Run(ReceiveLoopAsync);
        }
        catch (Exception ex)
        {
            state_ = LinkState.Failed;
            lastError_ = ex.Message;
        }
    }

    /// <summary>
    /// 게임 스레드에서 프레임마다 부른다. 큐에 쌓인 패킷을 전부 꺼내 순서대로 돌려준다.
    /// </summary>
    public IEnumerable<InboundPacket> Drain()
    {
        while (inbound_.TryDequeue(out var packet))
        {
            yield return packet;
        }
    }

    /// <summary>
    /// 패킷 하나를 보낸다. 실패는 삼키고 <see cref="LastError"/>에만 남긴다 — 전송 실패는
    /// 곧 연결이 끊긴 것이고, 그 사실은 수신 루프가 상태로 이미 알려준다.
    /// </summary>
    public void Send(PacketId id, ReadOnlySpan<byte> payload)
    {
        if (state_ != LinkState.Connected)
        {
            return;
        }

        if (payload.Length > MaxBodySize)
        {
            lastError_ = $"본문이 상한을 넘어 보내지 않았습니다: {payload.Length}바이트 (상한 {MaxBodySize})";
            return;
        }

        // 헤더와 본문을 한 배열로 합쳐서 한 번에 쓴다. 두 번 나눠 쓰면 그 사이에 다른 Send가
        // 끼어들어 프레임이 섞일 여지가 생긴다.
        var writer = new BinaryPacketWriter(HeaderSize + payload.Length);
        writer.WriteUInt16((ushort)payload.Length);
        writer.WriteUInt16((ushort)id);
        writer.WriteBytes(payload);

        _ = SendFrameAsync(writer.ToArray());
    }

    /// <summary>가변 길이 페이로드를 라이터로 만들어 보내는 편의 오버로드.</summary>
    public void Send(PacketId id, BinaryPacketWriter payload) => Send(id, payload.WrittenSpan);

    private async Task SendFrameAsync(byte[] frame)
    {
        await sendLock_.WaitAsync(shutdown_.Token).ConfigureAwait(false);
        try
        {
            if (stream_ is { } stream)
            {
                await stream.WriteAsync(frame, shutdown_.Token).ConfigureAwait(false);
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            lastError_ = ex.Message;
        }
        finally
        {
            sendLock_.Release();
        }
    }

    private async Task ReceiveLoopAsync()
    {
        // 서버 ProtocolClient의 PacketBuffer와 같은 역할. TCP는 경계를 보장하지 않으므로
        // "헤더가 다 왔는지 → 본문이 다 왔는지"를 매번 확인하며 꺼낸다.
        var pending = new List<byte>(capacity: 8192);
        var chunk = new byte[8192];

        try
        {
            while (!shutdown_.IsCancellationRequested && stream_ is { } stream)
            {
                var read = await stream.ReadAsync(chunk, shutdown_.Token).ConfigureAwait(false);
                if (read <= 0)
                {
                    lastError_ = "서버가 연결을 닫았습니다.";
                    break;
                }

                pending.AddRange(chunk[..read]);
                ExtractPackets(pending);
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            lastError_ = ex.Message;
        }

        state_ = LinkState.Disconnected;
    }

    private void ExtractPackets(List<byte> pending)
    {
        var offset = 0;
        while (pending.Count - offset >= HeaderSize)
        {
            var span = CollectionsMarshalSpan(pending)[offset..];
            var reader = new BinaryPacketReader(span);
            reader.TryReadUInt16(out var bodySize);
            reader.TryReadUInt16(out var rawId);

            if (bodySize > MaxBodySize)
            {
                // 조작이거나 프레이밍이 어긋난 것이다. 이 지점부터는 어디가 패킷 경계인지 알 수
                // 없으니 계속 읽어봐야 쓰레기만 나온다 — 버퍼를 비우고 연결을 끝낸다.
                lastError_ = $"bodySize가 상한을 넘었습니다: {bodySize} — 프레이밍이 깨졌습니다.";
                pending.Clear();
                shutdown_.Cancel();
                return;
            }

            var total = HeaderSize + bodySize;
            if (pending.Count - offset < total)
            {
                break;
            }

            var payload = new byte[bodySize];
            pending.CopyTo(offset + HeaderSize, payload, 0, bodySize);
            inbound_.Enqueue(new InboundPacket((PacketId)rawId, payload));
            offset += total;
        }

        if (offset > 0)
        {
            pending.RemoveRange(0, offset);
        }
    }

    /// <summary>
    /// <see cref="List{T}"/>의 내부 배열을 스팬으로 본다. 헤더 4바이트를 읽기 위해 매번
    /// <c>ToArray()</c>로 복사하지 않으려는 것뿐이다.
    /// </summary>
    private static ReadOnlySpan<byte> CollectionsMarshalSpan(List<byte> list) =>
        System.Runtime.InteropServices.CollectionsMarshal.AsSpan(list);

    public void Dispose()
    {
        shutdown_.Cancel();
        stream_?.Dispose();
        client_?.Dispose();
        sendLock_.Dispose();
        shutdown_.Dispose();
    }
}
