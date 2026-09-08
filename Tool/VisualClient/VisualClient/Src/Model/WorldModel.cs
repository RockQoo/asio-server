using VisualClient.Net;
using VisualClient.Protocol;

namespace VisualClient.Model;

/// <summary>
/// 존 안의 플레이어 한 명. 서버는 x/y만 보내주고 <b>방향(dir) 필드는 아예 없다</b> —
/// 화면의 작대기 방향은 이전 좌표에서 새 좌표로의 변화량으로 클라이언트가 만들어낸 값이다.
/// </summary>
public sealed class RemotePlayer
{
    public uint PlayerId { get; init; }

    public float X { get; private set; }

    public float Y { get; private set; }

    /// <summary>마지막으로 실제 이동한 방향(라디안). 이동량이 0이면 직전 값을 유지한다.</summary>
    public float DirRadians { get; private set; }

    /// <summary>마지막 <c>Z2CMoveNotify</c> 수신 시각. 오래된 플레이어를 흐리게 그리는 데 쓴다.</summary>
    public double LastSeenSeconds { get; private set; }

    public void ApplyMove(float x, float y, double nowSeconds)
    {
        var dx = x - X;
        var dy = y - Y;

        // 아주 작은 변화로 방향이 튀는 걸 막는다. 제자리 이동(같은 좌표 재전송)이면 방향을
        // 그대로 두는 게 화면상 자연스럽다.
        if (dx * dx + dy * dy > 1e-6f)
        {
            DirRadians = MathF.Atan2(dy, dx);
        }

        X = x;
        Y = y;
        LastSeenSeconds = nowSeconds;
    }
}

/// <summary>우편함의 우편 한 통. 서버 <c>Mail::MailInfo</c>와 필드가 1:1로 같다.</summary>
public sealed record MailEntry(uint MailId, string Title, string Body, long SendUt, long EndUt);

/// <summary>채팅/시스템 로그 한 줄.</summary>
public sealed record ChatLine(string Text, ChatLineKind Kind);

public enum ChatLineKind
{
    /// <summary>다른 플레이어의 발언.</summary>
    Other,

    /// <summary>내 발언(서버를 왕복해 돌아온 것).</summary>
    Mine,

    /// <summary>운영툴 공지(<c>W2CNotice</c>).</summary>
    Notice,

    /// <summary>클라이언트가 만든 안내/에러.</summary>
    System,
}

/// <summary>
/// 이 클라이언트가 보는 세계의 전부. <b>락이 하나도 없다</b> — 이 객체는 게임 스레드에서만
/// 접근되고, 소켓 수신 스레드는 <c>GameLink</c>의 큐에 바이트를 넣기만 한다. 서버의
/// <c>ZoneInstance</c>가 BASIC 스레드 전용이라 락이 없는 것과 같은 구조다.
/// </summary>
public sealed class WorldModel
{
    /// <summary>채팅 로그 보관 줄 수. 넘으면 오래된 것부터 버린다.</summary>
    private const int MaxChatLines = 200;

    private readonly Dictionary<uint, RemotePlayer> players_ = [];
    private readonly List<MailEntry> mails_ = [];
    private readonly List<ChatLine> chatLines_ = [];

    /// <summary>내 playerId. <c>Z2CEnterZoneNotify</c>를 받기 전에는 0이다.</summary>
    public uint MyPlayerId { get; private set; }

    /// <summary>서버가 배정한 현재 zoneId. 핸드오프될 때마다 갱신된다.</summary>
    public uint MyZoneId { get; private set; }

    public bool HasEnteredZone { get; private set; }

    /// <summary>내가 서버에 보낸 마지막 목표 좌표. 서버 확정 좌표는 <see cref="Players"/>에 있다.</summary>
    public float RequestedX { get; private set; }

    public float RequestedY { get; private set; }

    /// <summary>마지막 Echo 왕복 시간(ms). 아직 못 받았으면 null.</summary>
    public double? RttMs { get; private set; }

    /// <summary>존 이동으로 우편함이 초기화됐다는 사실을 화면에 띄우기 위한 플래그.</summary>
    public bool MailboxResetByZoneChange { get; private set; }

    /// <summary>마지막으로 받은 운영툴 공지. 화면 상단 토스트에 잠깐 띄운다.</summary>
    public string? LastNotice { get; private set; }

    /// <summary>그 공지를 받은 시각. 토스트를 언제 걷을지 정하는 데만 쓴다.</summary>
    public double LastNoticeAtSeconds { get; private set; }

    public IReadOnlyDictionary<uint, RemotePlayer> Players => players_;

    public IReadOnlyList<MailEntry> Mails => mails_;

    public IReadOnlyList<ChatLine> ChatLines => chatLines_;

    public RemotePlayer? Me => MyPlayerId != 0 && players_.TryGetValue(MyPlayerId, out var me) ? me : null;

    public void SetRequestedPosition(float x, float y)
    {
        RequestedX = x;
        RequestedY = y;
    }

    public void AddSystemLine(string text) => AddChatLine(new ChatLine(text, ChatLineKind.System));

    /// <summary>
    /// 지정 시각 이후로 소식이 없는 다른 플레이어를 목록에서 지운다.
    ///
    /// <para>
    /// 프로토콜에 <b>퇴장 통지가 없어서</b> 필요한 처리다 — 서버는 <c>W2ZLeaveZoneNotify</c>로
    /// 자기 상태에서만 지우고 같은 존의 남은 사람들에게 알리지 않는다. 안 지우면 접속을 끊은
    /// 클라이언트가 화면에 영원히 남는다. 내 자신은 지우지 않는다(내 소식이 끊긴 건 퇴장이
    /// 아니라 연결 문제이고, 그건 상태 줄이 따로 보여준다).
    /// </para>
    /// </summary>
    public void ForgetStalePlayers(double cutoffSeconds)
    {
        // 순회 중 삭제할 수 없으므로 대상을 먼저 모은다. 존 하나의 인구가 화면에 그릴 만한
        // 규모라 이 방식으로 충분하다.
        List<uint>? stale = null;
        foreach (var (playerId, player) in players_)
        {
            if (playerId != MyPlayerId && player.LastSeenSeconds < cutoffSeconds)
            {
                (stale ??= []).Add(playerId);
            }
        }

        if (stale is null)
        {
            return;
        }

        foreach (var playerId in stale)
        {
            players_.Remove(playerId);
        }
    }

    /// <summary>
    /// 수신 패킷 하나를 상태에 적용한다. 파싱 실패는 그 패킷만 버린다 — 신뢰할 수 없는
    /// 바이트라 한 패킷의 오류로 루프가 죽으면 안 된다(<c>BinaryPacketReader</c>가 예외를
    /// 던지지 않는 이유와 같다).
    /// </summary>
    public void Apply(InboundPacket packet, double nowSeconds)
    {
        switch (packet.Id)
        {
            case PacketId.Z2CEnterZoneNotify:
                ApplyEnterZone(packet.Payload);
                break;

            case PacketId.Z2CMoveNotify:
                ApplyMoveNotify(packet.Payload, nowSeconds);
                break;

            case PacketId.Z2CChatNotify:
                ApplyChatNotify(packet.Payload);
                break;

            case PacketId.Z2CEchoAck:
                ApplyEchoAck(packet.Payload, nowSeconds);
                break;

            case PacketId.Z2CTaskResult:
                ApplyTaskResult(packet.Payload);
                break;

            case PacketId.W2CNotice:
                ApplyNotice(packet.Payload, nowSeconds);
                break;

            default:
                // 등록하지 않은 id는 조용히 버리지 않고 남긴다 — 서버가 새 패킷을 보내기
                // 시작했는데 클라이언트가 아직 모르는 상황이 눈에 띄어야 한다.
                AddSystemLine($"처리하지 않는 패킷 id={(ushort)packet.Id} ({packet.Payload.Length}바이트)");
                break;
        }
    }

    private void ApplyEnterZone(byte[] payload)
    {
        // EnterZoneNotifyPacket: playerId(uint32) + zoneId(uint32)
        var reader = new BinaryPacketReader(payload);
        if (!reader.TryReadUInt32(out var playerId) || !reader.TryReadUInt32(out var zoneId))
        {
            return;
        }

        var isHandoff = HasEnteredZone && zoneId != MyZoneId;

        MyPlayerId = playerId;
        MyZoneId = zoneId;
        HasEnteredZone = true;

        if (isHandoff)
        {
            // 존 경계를 넘으면 서버가 이전 존의 우편함을 통째로 버린다
            // (ZoneInstance::HandleMove의 mailRegistry_.Remove -> 새 존에서 빈 MailModel 재생성).
            // 클라이언트가 목록을 그대로 들고 있으면 서버에 없는 우편을 보여주게 되고,
            // 삭제를 눌러도 MailNotFound만 돌아온다 — 서버와 같은 상태로 맞춰 비운다.
            mails_.Clear();
            MailboxResetByZoneChange = true;
            AddChatLine(new ChatLine(
                $"존 {zoneId}(으)로 이동했습니다. 서버가 우편함을 초기화하므로 목록을 비웠습니다.",
                ChatLineKind.System));

            // 다른 존의 플레이어는 브로드캐스트가 오지 않으므로 화면에서 사라져야 한다.
            players_.Clear();
        }
        else
        {
            AddChatLine(new ChatLine($"존 {zoneId}에 입장했습니다. (playerId={playerId})", ChatLineKind.System));
        }
    }

    private void ApplyMoveNotify(byte[] payload, double nowSeconds)
    {
        // moverId(uint32) + MovePacket{ x(float), y(float) }
        var reader = new BinaryPacketReader(payload);
        if (!reader.TryReadUInt32(out var moverId)
            || !reader.TryReadSingle(out var x)
            || !reader.TryReadSingle(out var y))
        {
            return;
        }

        if (!players_.TryGetValue(moverId, out var player))
        {
            player = new RemotePlayer { PlayerId = moverId };
            players_[moverId] = player;
        }

        player.ApplyMove(x, y, nowSeconds);
    }

    private void ApplyChatNotify(byte[] payload)
    {
        // senderId(uint32) + 메시지(길이 접두 UTF-8)
        var reader = new BinaryPacketReader(payload);
        if (!reader.TryReadUInt32(out var senderId) || !reader.TryReadString(out var message))
        {
            return;
        }

        var isMine = senderId == MyPlayerId;
        AddChatLine(new ChatLine(
            isMine ? $"나: {message}" : $"{senderId}: {message}",
            isMine ? ChatLineKind.Mine : ChatLineKind.Other));
    }

    private void ApplyEchoAck(byte[] payload, double nowSeconds)
    {
        // Z2CEchoAck의 본문은 보낸 바이트 그대로다(길이 접두가 없다) — 보낼 때 심어둔
        // 전송 시각(double 8바이트)을 그대로 되읽어 왕복 시간을 구한다.
        var reader = new BinaryPacketReader(payload);
        if (!reader.TryReadInt64(out var sentAtTicks))
        {
            return;
        }

        var sentAtSeconds = BitConverter.Int64BitsToDouble(sentAtTicks);
        RttMs = Math.Max(0.0, (nowSeconds - sentAtSeconds) * 1000.0);
    }

    private void ApplyNotice(byte[] payload, double nowSeconds)
    {
        var reader = new BinaryPacketReader(payload);
        if (!reader.TryReadString(out var message))
        {
            return;
        }

        LastNotice = message;
        LastNoticeAtSeconds = nowSeconds;
        AddChatLine(new ChatLine($"[공지] {message}", ChatLineKind.Notice));
    }

    /// <summary>
    /// <c>Z2CTaskResult</c>: errorCode(int32) + requestPacketId(uint16) + [태스크 스트림].
    /// 스트림 포맷은 <c>ownerId(8) + taskCount(2) + taskCount개의 {kind(2) + len(4) + payload}</c>.
    ///
    /// <para>
    /// 길이 프리픽스가 있는 덕에 <b>모르는 kind는 통째로 건너뛸 수 있다</b> — 서버가 새 콘텐츠
    /// 태스크를 추가해도 이 클라이언트가 죽지 않고 나머지 태스크를 계속 적용한다.
    /// </para>
    /// </summary>
    private void ApplyTaskResult(byte[] payload)
    {
        var reader = new BinaryPacketReader(payload);
        if (!reader.TryReadInt32(out var errorCode) || !reader.TryReadUInt16(out var requestPacketId))
        {
            return;
        }

        var requestName = requestPacketId == 0
            ? "서버발 변경"
            : ((PacketId)requestPacketId).ToString();

        if (errorCode != (int)ErrorCode.Success)
        {
            // 실패면 서버가 이미 자기 메모리를 롤백했고 스트림도 비어 있다 — 알리기만 한다.
            AddChatLine(new ChatLine(
                $"[실패] {requestName}: {ErrorCodeText.Describe(errorCode)}", ChatLineKind.System));
            return;
        }

        if (reader.AtEnd)
        {
            return;
        }

        if (!reader.TryReadUInt64(out _) || !reader.TryReadUInt16(out var taskCount))
        {
            return;
        }

        for (var i = 0; i < taskCount; ++i)
        {
            if (!reader.TryReadUInt16(out var taskKind) || !reader.TryReadUInt32(out var length))
            {
                return;
            }

            if (reader.Remaining < length)
            {
                return;
            }

            var taskPayload = new byte[length];
            for (var b = 0; b < length; ++b)
            {
                if (!reader.TryReadUInt8(out taskPayload[b]))
                {
                    return;
                }
            }

            ApplyTask(taskKind, taskPayload, requestPacketId);
        }
    }

    private void ApplyTask(ushort taskKind, byte[] taskPayload, ushort requestPacketId)
    {
        switch (TaskKind.CategoryOf(taskKind))
        {
            case TaskCategory.Mail:
                ApplyMailTask((MailTask)TaskKind.SubTaskOf(taskKind), taskPayload, requestPacketId);
                break;

            default:
                AddSystemLine($"적용 규칙이 없는 태스크 kind=0x{taskKind:X4} ({taskPayload.Length}바이트) — 건너뜁니다.");
                break;
        }
    }

    private void ApplyMailTask(MailTask subTask, byte[] taskPayload, ushort requestPacketId)
    {
        // Mail 태스크 페이로드는 추가/삭제가 같은 포맷이다(원본 전체) — 서버가 삭제 태스크에도
        // 지워진 내용을 통째로 싣는 이유는 실패 시 되살리기(롤백)에 그게 필요하기 때문이다.
        var reader = new BinaryPacketReader(taskPayload);
        if (!reader.TryReadUInt32(out var mailId)
            || !reader.TryReadString(out var title)
            || !reader.TryReadString(out var body)
            || !reader.TryReadInt64(out var sendUt)
            || !reader.TryReadInt64(out var endUt))
        {
            return;
        }

        switch (subTask)
        {
            case MailTask.Added:
                mails_.RemoveAll(mail => mail.MailId == mailId);
                mails_.Add(new MailEntry(mailId, title, body, sendUt, endUt));
                mails_.Sort((left, right) => left.MailId.CompareTo(right.MailId));
                AddChatLine(new ChatLine(
                    requestPacketId == 0
                        ? $"[우편] 서버가 우편을 보냈습니다: {title} (#{mailId})"
                        : $"[우편] 추가됨: {title} (#{mailId})",
                    ChatLineKind.System));
                break;

            case MailTask.Removed:
                mails_.RemoveAll(mail => mail.MailId == mailId);
                AddChatLine(new ChatLine(
                    requestPacketId == 0
                        ? $"[우편] 유효기간이 지나 서버가 삭제했습니다: {title} (#{mailId})"
                        : $"[우편] 삭제됨: {title} (#{mailId})",
                    ChatLineKind.System));
                break;

            default:
                AddSystemLine($"적용 규칙이 없는 Mail 태스크 subTask={(byte)subTask} — 건너뜁니다.");
                break;
        }
    }

    private void AddChatLine(ChatLine line)
    {
        chatLines_.Add(line);
        if (chatLines_.Count > MaxChatLines)
        {
            chatLines_.RemoveRange(0, chatLines_.Count - MaxChatLines);
        }
    }
}
