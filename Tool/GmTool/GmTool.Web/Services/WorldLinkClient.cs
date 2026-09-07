using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Net.Sockets;
using GmTool.Core.Protocol;
using GmTool.Web.Options;
using Microsoft.Extensions.Options;

namespace GmTool.Web.Services;

/// <summary>World가 돌려준 명령 처리 결과.</summary>
public sealed record ToolCommandResult(ToolResultCode ResultCode, uint AffectedCount)
{
    public bool IsSuccess => ResultCode == ToolResultCode.Ok;

    public string Describe() => ResultCode switch
    {
        ToolResultCode.Ok => $"성공 (대상 {AffectedCount}명)",
        ToolResultCode.NotAuthenticated => "실패: World가 운영툴 인증을 거부했습니다(공유 시크릿 확인)",
        ToolResultCode.BadRequest => "실패: 요청 형식이 올바르지 않습니다",
        ToolResultCode.TargetNotFound => "실패: 대상 클라이언트가 접속 중이 아닙니다",
        ToolResultCode.ZoneUnavailable => "실패: 대상이 속한 존 서버와 연결이 없습니다",
        ToolResultCode.ToolTimeout => "실패: World 응답 시간 초과",
        ToolResultCode.ToolNotConnected => "실패: WorldServer에 연결되어 있지 않습니다",
        _ => $"실패: 알 수 없는 결과 코드 {(ushort)ResultCode}",
    };
}

/// <summary>ClientListReply의 항목 하나.</summary>
public sealed record WorldClientEntry(ulong ClientSessionId, uint ZoneId);

/// <summary>
/// WorldServer의 운영툴 포트(기본 9300)에 붙어 있는 TCP 클라이언트. 연결은 <b>하나만</b> 유지하고
/// 모든 명령이 이 소켓을 다중화한다. 지켜야 할 규약 셋:
/// <list type="bullet">
///   <item>요청마다 <c>requestId</c>를 붙이고 응답이 오면 그 id로 대기 중인 TCS를 깨운다 —
///         요청/응답 순서가 뒤섞여도 짝이 어긋나지 않게.</item>
///   <item>쓰기는 <see cref="SemaphoreSlim"/>으로 직렬화한다. 프레임이 [헤더][본문] 두 번의
///         쓰기로 나가므로 겹치면 스트림 자체가 깨진다.</item>
///   <item>연결이 끊기면 대기 중인 요청을 전부 깨운다. 안 깨우면 호출자가 타임아웃까지 매달린다.</item>
/// </list>
///
/// 연결 직후 <c>ToolHello</c>로 공유 시크릿을 보내고 통과해야 <see cref="IsReady"/>가 된다.
/// </summary>
public sealed class WorldLinkClient : BackgroundService
{
    private readonly WorldLinkOptions options_;
    private readonly ILogger<WorldLinkClient> logger_;

    private readonly SemaphoreSlim writeLock_ = new(1, 1);
    private readonly ConcurrentDictionary<uint, TaskCompletionSource<WorldLinkResponse>> pending_ = new();

    private int nextRequestId_;
    private volatile TcpClient? client_;
    private volatile NetworkStream? stream_;
    private volatile bool authenticated_;

    public WorldLinkClient(IOptions<WorldLinkOptions> options, ILogger<WorldLinkClient> logger)
    {
        options_ = options.Value;
        logger_ = logger;
    }

    /// <summary>연결 + 인증까지 끝나 명령을 보낼 수 있는 상태인가.</summary>
    public bool IsReady => authenticated_ && client_ is { Connected: true };

    /// <summary>UI 표시용 연결 대상.</summary>
    public string Endpoint => $"{options_.Host}:{options_.Port}";

    /// <summary>마지막 연결 실패 사유(UI에 그대로 보여준다).</summary>
    public string? LastError { get; private set; }

    // 공개 명령 API

    /// <summary>접속 중인 전체 클라이언트에게 공지를 브로드캐스트한다.</summary>
    public Task<ToolCommandResult> SendNoticeAsync(string message, CancellationToken cancellationToken = default)
    {
        return SendCommandAsync(PacketId.T2WNoticeRequest, (writer, requestId) =>
        {
            writer.WriteUInt32(requestId);
            writer.WriteString(message);
        }, cancellationToken);
    }

    /// <summary>
    /// 우편을 발송한다. <paramref name="clientSessionId"/>가 null이면 접속 중 전체가 대상이다.
    /// </summary>
    public Task<ToolCommandResult> SendMailAsync(
        ulong? clientSessionId, string title, string body, long durationSec,
        CancellationToken cancellationToken = default)
    {
        return SendCommandAsync(PacketId.T2WMailSendRequest, (writer, requestId) =>
        {
            writer.WriteUInt32(requestId);
            writer.WriteUInt8(clientSessionId is null
                ? ToolLinkProtocol.MailTargetAllOnline
                : ToolLinkProtocol.MailTargetSingle);
            writer.WriteUInt64(clientSessionId ?? 0UL);
            writer.WriteString(title);
            writer.WriteString(body);
            writer.WriteInt64(durationSec);
        }, cancellationToken);
    }

    /// <summary>특정 클라이언트의 우편 1건을 삭제한다.</summary>
    public Task<ToolCommandResult> DeleteMailAsync(
        ulong clientSessionId, uint mailId, CancellationToken cancellationToken = default)
    {
        return SendCommandAsync(PacketId.T2WMailDeleteRequest, (writer, requestId) =>
        {
            // ToolMailDeleteRequestPacket: requestId(u32) + clientSessionId(u64) + mailId(u32)
            writer.WriteUInt32(requestId);
            writer.WriteUInt64(clientSessionId);
            writer.WriteUInt32(mailId);
        }, cancellationToken);
    }

    /// <summary>
    /// 생성된 쿠폰 코드 묶음을 World로 넘긴다. 한 패킷에 담을 수 있는 개수가
    /// <see cref="ToolLinkProtocol.CouponWireChunkSize"/>로 제한되므로, 큰 청크는 여기서 다시
    /// 잘라 여러 패킷으로 보낸다(DB 청크와 전송 청크는 서로 다른 제약이다).
    /// </summary>
    public async Task<ToolCommandResult> PushCouponChunkAsync(
        string campaignCode, IReadOnlyList<string> couponCodes, long chunkSeq,
        CancellationToken cancellationToken = default)
    {
        uint totalPushed = 0;
        var subChunkSeq = chunkSeq * 1000;

        for (var offset = 0; offset < couponCodes.Count; offset += ToolLinkProtocol.CouponWireChunkSize)
        {
            var take = Math.Min(ToolLinkProtocol.CouponWireChunkSize, couponCodes.Count - offset);
            var slice = new string[take];
            for (var i = 0; i < take; ++i)
            {
                slice[i] = couponCodes[offset + i];
            }

            var seq = ++subChunkSeq;
            var result = await SendCommandAsync(PacketId.T2WCouponChunkPush, (writer, requestId) =>
            {
                writer.WriteUInt32(requestId);
                writer.WriteString(campaignCode);
                writer.WriteUInt32((uint)seq);
                writer.WriteUInt32((uint)slice.Length);
                foreach (var code in slice)
                {
                    writer.WriteString(code);
                }
            }, cancellationToken).ConfigureAwait(false);

            if (!result.IsSuccess)
            {
                return result with { AffectedCount = totalPushed };
            }

            totalPushed += result.AffectedCount;
        }

        return new ToolCommandResult(ToolResultCode.Ok, totalPushed);
    }

    /// <summary>지금 접속 중인 클라이언트 목록을 조회한다.</summary>
    public async Task<IReadOnlyList<WorldClientEntry>> GetClientListAsync(CancellationToken cancellationToken = default)
    {
        if (!IsReady)
        {
            return [];
        }

        var requestId = NextRequestId();
        var completion = RegisterPending(requestId);

        try
        {
            var writer = new BinaryPacketWriter();
            writer.WriteUInt32(requestId);
            await SendFrameAsync(PacketId.T2WClientListRequest, writer, cancellationToken).ConfigureAwait(false);

            var response = await WaitForResponseAsync(requestId, completion, cancellationToken).ConfigureAwait(false);
            return response.Clients ?? [];
        }
        catch (Exception ex)
        {
            logger_.LogWarning(ex, "클라이언트 목록 조회 실패");
            pending_.TryRemove(requestId, out _);
            return [];
        }
    }

    // 연결 유지 루프

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        while (!stoppingToken.IsCancellationRequested)
        {
            try
            {
                await ConnectAndPumpAsync(stoppingToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
            {
                break;
            }
            catch (Exception ex)
            {
                LastError = ex.Message;
                logger_.LogWarning("WorldServer 연결 실패/끊김: {Message}", ex.Message);
            }
            finally
            {
                Teardown();
            }

            if (stoppingToken.IsCancellationRequested)
            {
                break;
            }

            await Task.Delay(TimeSpan.FromSeconds(options_.ReconnectDelaySeconds), stoppingToken)
                .ConfigureAwait(false);
        }
    }

    private async Task ConnectAndPumpAsync(CancellationToken stoppingToken)
    {
        var client = new TcpClient();
        // Nagle 알고리즘을 끈다. 운영 명령은 작고 드문 패킷이라, 묶어 보내려고 기다리는
        // 40ms가 그대로 UI 응답 지연으로 보인다.
        client.NoDelay = true;

        await client.ConnectAsync(options_.Host, options_.Port, stoppingToken).ConfigureAwait(false);

        client_ = client;
        stream_ = client.GetStream();
        LastError = null;
        logger_.LogInformation("WorldServer 운영툴 포트에 연결됨: {Endpoint}", Endpoint);

        // 읽기 루프를 먼저 띄운 뒤에 Hello를 보낸다 -- 반대로 하면 Hello 응답이 도착했을 때
        // 그걸 읽어줄 루프가 아직 없어서 응답을 놓친다.
        var readLoop = ReadLoopAsync(stream_, stoppingToken);

        var helloOk = await SendHelloAsync(stoppingToken).ConfigureAwait(false);
        if (!helloOk)
        {
            LastError = "World가 운영툴 인증을 거부했습니다(공유 시크릿/프로토콜 버전 확인).";
            logger_.LogError("{Message}", LastError);
            // World가 연결을 닫으므로 읽기 루프도 곧 끝난다.
        }

        await readLoop.ConfigureAwait(false);
    }

    private async Task<bool> SendHelloAsync(CancellationToken cancellationToken)
    {
        var requestId = NextRequestId();
        var completion = RegisterPending(requestId);

        var writer = new BinaryPacketWriter();
        writer.WriteUInt32(requestId);
        writer.WriteUInt32(ToolLinkProtocol.ProtocolVersion);
        writer.WriteString(options_.SharedSecret);
        writer.WriteString(Environment.MachineName);

        await SendFrameAsync(PacketId.T2WToolHello, writer, cancellationToken).ConfigureAwait(false);

        var response = await WaitForResponseAsync(requestId, completion, cancellationToken).ConfigureAwait(false);
        var accepted = response.Command is { ResultCode: ToolResultCode.Ok };
        authenticated_ = accepted;

        if (accepted)
        {
            logger_.LogInformation("WorldServer 운영툴 인증 성공");
        }

        return accepted;
    }

    private async Task ReadLoopAsync(NetworkStream stream, CancellationToken stoppingToken)
    {
        var header = new byte[ToolLinkProtocol.HeaderSize];
        var body = new byte[ToolLinkProtocol.MaxBodySize];

        while (!stoppingToken.IsCancellationRequested)
        {
            await ReadExactAsync(stream, header.AsMemory(0, ToolLinkProtocol.HeaderSize), stoppingToken)
                .ConfigureAwait(false);

            var bodySize = BinaryPrimitives.ReadUInt16LittleEndian(header);
            var packetId = (PacketId)BinaryPrimitives.ReadUInt16LittleEndian(header.AsSpan(2));

            if (bodySize > ToolLinkProtocol.MaxBodySize)
            {
                throw new InvalidDataException($"본문 크기가 상한을 넘었습니다: {bodySize}바이트");
            }

            if (bodySize > 0)
            {
                await ReadExactAsync(stream, body.AsMemory(0, bodySize), stoppingToken).ConfigureAwait(false);
            }

            HandlePacket(packetId, body.AsSpan(0, bodySize));
        }
    }

    private void HandlePacket(PacketId packetId, ReadOnlySpan<byte> payload)
    {
        switch (packetId)
        {
            case PacketId.W2TToolHelloAck:
            {
                // ToolHelloAckPacket: requestId(u32) + accepted(u8) + protocolVersion(u32)
                var reader = new BinaryPacketReader(payload);
                if (!reader.TryReadUInt32(out var requestId) || !reader.TryReadUInt8(out var accepted))
                {
                    return;
                }

                Complete(requestId, new WorldLinkResponse(
                    new ToolCommandResult(
                        accepted != 0 ? ToolResultCode.Ok : ToolResultCode.NotAuthenticated, 0),
                    null));
                break;
            }

            case PacketId.W2TToolCommandAck:
            {
                // ToolCommandAckPacket: requestId(u32) + resultCode(u16) + affectedCount(u32)
                var reader = new BinaryPacketReader(payload);
                if (!reader.TryReadUInt32(out var requestId)
                    || !reader.TryReadUInt16(out var resultCode)
                    || !reader.TryReadUInt32(out var affectedCount))
                {
                    return;
                }

                Complete(requestId, new WorldLinkResponse(
                    new ToolCommandResult((ToolResultCode)resultCode, affectedCount), null));
                break;
            }

            case PacketId.W2TClientListReply:
            {
                // requestId(u32) + count(u32) + count * { clientSessionId(u64) + zoneId(u32) }
                var reader = new BinaryPacketReader(payload);
                if (!reader.TryReadUInt32(out var requestId) || !reader.TryReadUInt32(out var count))
                {
                    return;
                }

                var entries = new List<WorldClientEntry>((int)Math.Min(count, 4096));
                for (var i = 0u; i < count; ++i)
                {
                    if (!reader.TryReadUInt64(out var sessionId) || !reader.TryReadUInt32(out var zoneId))
                    {
                        break;
                    }

                    entries.Add(new WorldClientEntry(sessionId, zoneId));
                }

                Complete(requestId, new WorldLinkResponse(null, entries));
                break;
            }

            default:
                logger_.LogDebug("처리하지 않는 패킷 수신: {PacketId}", packetId);
                break;
        }
    }

    // 내부 유틸

    private async Task<ToolCommandResult> SendCommandAsync(
        PacketId packetId,
        Action<BinaryPacketWriter, uint> writeBody,
        CancellationToken cancellationToken)
    {
        if (!IsReady)
        {
            return new ToolCommandResult(ToolResultCode.ToolNotConnected, 0);
        }

        var requestId = NextRequestId();
        var completion = RegisterPending(requestId);

        try
        {
            var writer = new BinaryPacketWriter();
            writeBody(writer, requestId);
            await SendFrameAsync(packetId, writer, cancellationToken).ConfigureAwait(false);

            var response = await WaitForResponseAsync(requestId, completion, cancellationToken).ConfigureAwait(false);
            return response.Command ?? new ToolCommandResult(ToolResultCode.BadRequest, 0);
        }
        catch (TimeoutException)
        {
            pending_.TryRemove(requestId, out _);
            return new ToolCommandResult(ToolResultCode.ToolTimeout, 0);
        }
        catch (Exception ex)
        {
            pending_.TryRemove(requestId, out _);
            logger_.LogWarning(ex, "World 명령 전송 실패: {PacketId}", packetId);
            return new ToolCommandResult(ToolResultCode.ToolNotConnected, 0);
        }
    }

    private async Task SendFrameAsync(
        PacketId packetId, BinaryPacketWriter body, CancellationToken cancellationToken)
    {
        if (body.Length > ToolLinkProtocol.MaxBodySize)
        {
            throw new InvalidOperationException(
                $"패킷 본문이 상한을 넘었습니다: {body.Length}바이트 (상한 {ToolLinkProtocol.MaxBodySize})");
        }

        var stream = stream_ ?? throw new InvalidOperationException("WorldServer에 연결되어 있지 않습니다.");

        // [헤더][본문]을 한 번의 WriteAsync로 내보내야 다른 요청과 섞이지 않는다. 락은 그
        // 위에서 한 번 더 보장한다(두 요청이 동시에 이 함수에 들어오는 경우).
        var frame = new byte[ToolLinkProtocol.HeaderSize + body.Length];
        BinaryPrimitives.WriteUInt16LittleEndian(frame, (ushort)body.Length);
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(2), (ushort)packetId);
        body.WrittenSpan.CopyTo(frame.AsSpan(ToolLinkProtocol.HeaderSize));

        await writeLock_.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await stream.WriteAsync(frame, cancellationToken).ConfigureAwait(false);
            await stream.FlushAsync(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            writeLock_.Release();
        }
    }

    private async Task<WorldLinkResponse> WaitForResponseAsync(
        uint requestId, TaskCompletionSource<WorldLinkResponse> completion, CancellationToken cancellationToken)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(options_.RequestTimeoutSeconds));

        try
        {
            return await completion.Task.WaitAsync(timeout.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new TimeoutException($"World 응답 시간 초과 (requestId={requestId})");
        }
        finally
        {
            pending_.TryRemove(requestId, out _);
        }
    }

    private uint NextRequestId()
    {
        // 0은 "응답을 짝지을 수 없는 요청"의 의미로 남겨둔다(World가 파싱 실패 시 0으로 답한다).
        var next = (uint)Interlocked.Increment(ref nextRequestId_);
        return next == 0 ? (uint)Interlocked.Increment(ref nextRequestId_) : next;
    }

    private TaskCompletionSource<WorldLinkResponse> RegisterPending(uint requestId)
    {
        var completion = new TaskCompletionSource<WorldLinkResponse>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        pending_[requestId] = completion;
        return completion;
    }

    private void Complete(uint requestId, WorldLinkResponse response)
    {
        if (pending_.TryRemove(requestId, out var completion))
        {
            completion.TrySetResult(response);
        }
    }

    private static async Task ReadExactAsync(NetworkStream stream, Memory<byte> buffer, CancellationToken cancellationToken)
    {
        var read = 0;
        while (read < buffer.Length)
        {
            var got = await stream.ReadAsync(buffer[read..], cancellationToken).ConfigureAwait(false);
            if (got == 0)
            {
                throw new EndOfStreamException("WorldServer가 연결을 닫았습니다.");
            }

            read += got;
        }
    }

    private void Teardown()
    {
        authenticated_ = false;

        // 대기 중인 요청을 전부 깨운다. 안 그러면 호출자가 타임아웃까지 그대로 매달린다.
        foreach (var requestId in pending_.Keys.ToArray())
        {
            if (pending_.TryRemove(requestId, out var completion))
            {
                completion.TrySetResult(new WorldLinkResponse(
                    new ToolCommandResult(ToolResultCode.ToolNotConnected, 0), null));
            }
        }

        try
        {
            stream_?.Dispose();
            client_?.Dispose();
        }
        catch (Exception ex)
        {
            logger_.LogDebug(ex, "WorldServer 연결 정리 중 예외(무시)");
        }

        stream_ = null;
        client_ = null;
    }

    public override void Dispose()
    {
        Teardown();
        writeLock_.Dispose();
        base.Dispose();
    }

    private sealed record WorldLinkResponse(ToolCommandResult? Command, IReadOnlyList<WorldClientEntry>? Clients);
}
