using Client.Net;
using Client.Protocol;

namespace Client.Model;

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

/// <summary>우편함의 우편 한 통. 서버 <c>Mail::Info</c>와 필드가 1:1로 같다.</summary>
public sealed record MailEntry(long MailId, string Title, string Body, long SendUt, long EndUt);

/// <summary>채팅/시스템 로그 한 줄.</summary>
public sealed record ChatLine(string Text, ChatLineKind Kind);

/// <summary>
/// <c>W2CLogin</c> 한 통을 그대로 담은 결과. <c>ErrorCode</c>가 <c>Success</c>면 곧이어 존
/// 배정(<c>Z2CEnterZoneNotify</c>)이 온다.
/// </summary>
public sealed record LoginOutcome(int ErrorCode, long PlayerId, string PlayerName)
{
    public bool Succeeded => ErrorCode == (int)Protocol.ErrorCode.Success;
}

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
/// <c>Instance</c>가 BASIC 스레드 전용이라 락이 없는 것과 같은 구조다.
/// </summary>
public sealed class WorldModel
{
    /// <summary>채팅 로그 보관 줄 수. 넘으면 오래된 것부터 버린다.</summary>
    private const int MaxChatLines = 200;

    /// <summary>화살 연출이 대상까지 날아가는 시간(초). 서버 판정과 무관한 값이다.</summary>
    private const double ArrowFlightSeconds = 0.25;

    private readonly Dictionary<uint, RemotePlayer> players_ = [];
    private readonly Dictionary<uint, CombatUnit> units_ = [];
    private readonly List<ArrowEffect> arrows_ = [];
    private readonly List<DamagePopup> damagePopups_ = [];
    private readonly List<MailEntry> mails_ = [];
    private readonly List<ChatLine> chatLines_ = [];

    /// <summary>
    /// 마지막 <c>W2CLogin</c> 응답. 아직 안 왔으면 null이라, 화면이 "대기 중"과 "결과 도착"을
    /// 이 값 하나로 가른다.
    /// </summary>
    public LoginOutcome? Login { get; private set; }

    /// <summary>
    /// 로그인으로 확정된 DB의 player_id(int64). <see cref="MySessionId"/>와 <b>다른 값이다</b> —
    /// 그쪽은 존이 브로드캐스트에 "누가" 보냈는지를 싣는 세션 키이고, 이건 계정의 영속 키다.
    /// <c>W2CLogin</c>과 <c>Z2CEnterZoneNotify</c> 둘 다에 실려 오며 같은 값이어야 한다.
    /// </summary>
    public long AccountPlayerId { get; private set; }

    /// <summary>로그인한 계정 이름. 서버가 확정해 돌려준 값이다.</summary>
    public string PlayerName { get; private set; } = string.Empty;

    /// <summary>
    /// 내 세션 id. <c>Z2CEnterZoneNotify</c>를 받기 전에는 0이다.
    /// <c>Z2CMoveNotify</c>/<c>Z2CChatNotify</c>의 발신자 키와 같은 값이라, 그 통지 중
    /// 내 것을 가려내는 데 쓴다. <b>계정 키가 아니다</b>(그건 <see cref="AccountPlayerId"/>).
    /// </summary>
    public uint MySessionId { get; private set; }

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

    /// <summary>
    /// 지금 고른 공격 대상의 unitId. 0이면 없다. <b>UI 상태지만 여기 둔다</b> — 존 뷰(대상 링)와
    /// HUD(대상 HP)와 입력(공격 전송)이 같은 값을 봐야 해서, 어느 한쪽이 들고 있으면 나머지
    /// 둘에 전달 인자가 생긴다.
    /// </summary>
    public uint TargetUnitId { get; set; }

    /// <summary>지금 쥐고 있는 무기. 서버에는 이 상태가 없고 요청마다 실려 나간다.</summary>
    public AttackKind Weapon { get; set; } = AttackKind.Melee;

    /// <summary>마지막 공격 실패 사유. 화면 중앙 아래 토스트로 잠깐 띄운다.</summary>
    public string? LastAttackFailure { get; private set; }

    public double LastAttackFailureAtSeconds { get; private set; }

    public IReadOnlyDictionary<uint, RemotePlayer> Players => players_;

    public IReadOnlyDictionary<uint, CombatUnit> Units => units_;

    public IReadOnlyList<ArrowEffect> Arrows => arrows_;

    public IReadOnlyList<DamagePopup> DamagePopups => damagePopups_;

    /// <summary>내 전투 유닛. 존에 들어가기 전이나 등장 통지 전에는 null이다.</summary>
    public CombatUnit? MyUnit => MySessionId != 0 && units_.TryGetValue(MySessionId, out var unit) ? unit : null;

    public IReadOnlyList<MailEntry> Mails => mails_;

    public IReadOnlyList<ChatLine> ChatLines => chatLines_;

    public RemotePlayer? Me => MySessionId != 0 && players_.TryGetValue(MySessionId, out var me) ? me : null;

    public void SetRequestedPosition(float x, float y)
    {
        RequestedX = x;
        RequestedY = y;
    }

    public void AddSystemLine(string text) => AddChatLine(new ChatLine(text, ChatLineKind.System));

    /// <summary>
    /// <c>C2WLogin</c>을 보내기 직전에 부른다. 직전 결과를 지워서, 다시 시도했을 때 옛 응답이
    /// 새 응답으로 오해되지 않게 한다.
    /// </summary>
    public void BeginLogin() => Login = null;

    /// <summary>
    /// 지정 시각 이후로 소식이 없는 다른 플레이어를 목록에서 지운다.
    ///
    /// <para>
    /// 프로토콜에 <b>퇴장 통지가 없어서</b> 필요한 처리다 — 서버는 <c>W2ZLeaveZone</c>로
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
            if (playerId != MySessionId && player.LastSeenSeconds < cutoffSeconds)
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

            case PacketId.W2CLogin:
                ApplyLogin(packet.Payload);
                break;

            case PacketId.Z2CUnitSpawn:
                ApplyUnitSpawn(packet.Payload);
                break;

            case PacketId.Z2CUnitDespawn:
                ApplyUnitDespawn(packet.Payload);
                break;

            case PacketId.Z2CUnitStateSync:
                ApplyUnitStateSync(packet.Payload);
                break;

            case PacketId.Z2CUnitDead:
                ApplyUnitDead(packet.Payload, nowSeconds);
                break;

            case PacketId.Z2CAttackResult:
                ApplyAttackResult(packet.Payload, nowSeconds);
                break;

            case PacketId.Z2CUnitAttackNotify:
                ApplyUnitAttackNotify(packet.Payload, nowSeconds);
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
        // EnterZoneNotifyPacket: playerId(int64) + clientSessionId(uint64) + zoneId(uint32)
        //
        // **playerId와 sessionId가 따로 온다.** 예전에는 playerId 하나뿐이었고 그 값이
        // 세션 id를 uint32로 자른 것이라 둘을 겸했는데, 서버가 진짜 계정 키를 싣게 되면서
        // 갈라졌다. 브로드캐스트(Move/Chat)의 발신자 키는 여전히 세션 id다.
        var reader = new BinaryPacketReader(payload);
        if (!reader.TryReadInt64(out var playerId)
            || !reader.TryReadUInt64(out var clientSessionId)
            || !reader.TryReadUInt32(out var zoneId))
        {
            return;
        }

        var isHandoff = HasEnteredZone && zoneId != MyZoneId;

        AccountPlayerId = playerId;
        MySessionId = (uint)clientSessionId;
        MyZoneId = zoneId;
        HasEnteredZone = true;

        if (isHandoff)
        {
            // 존 경계를 넘으면 서버가 이전 존의 우편함을 통째로 버린다
            // (Instance::HandleMove의 mailRegistry_.Remove -> 새 존에서 빈 Model 재생성).
            // 클라이언트가 목록을 그대로 들고 있으면 서버에 없는 우편을 보여주게 되고,
            // 삭제를 눌러도 MailNotFound만 돌아온다 — 서버와 같은 상태로 맞춰 비운다.
            mails_.Clear();
            MailboxResetByZoneChange = true;
            AddChatLine(new ChatLine(
                $"존 {zoneId}(으)로 이동했습니다. 서버가 우편함을 초기화하므로 목록을 비웠습니다.",
                ChatLineKind.System));

            // 다른 존의 플레이어는 브로드캐스트가 오지 않으므로 화면에서 사라져야 한다.
            players_.Clear();

            // 유닛도 같이 비운다. 유닛 id는 **존 안에서만 유효**해서 옆 존의 몬스터 id와
            // 겹칠 수 있고, 새 존이 입장 직후 자기 목록을 통째로 보내준다. 안 비우면 옆 존의
            // 몬스터가 화면에 남아 있다가 새 목록과 id가 부딪힌다.
            //
            // **경계를 넘으면 내 HP가 최대치로 돌아간다.** 존 서버는 DB를 만지지 않고, 세로
            // 핸드오프에서는 프로세스가 바뀌어 유닛이 새로 만들어지기 때문이다 — HP를 이어
            // 받게 하려면 World가 전투 스탯을 날라야 하는데 그러면 "World는 전투를 모른다"가
            // 깨진다(docs/design/combat-lane.md).
            units_.Clear();
            arrows_.Clear();
            damagePopups_.Clear();
            TargetUnitId = 0;
        }
        else
        {
            AddChatLine(new ChatLine($"존 {zoneId}에 입장했습니다. (playerId={playerId})", ChatLineKind.System));
        }
    }

    /// <summary>
    /// <c>W2CLogin</c>: errorCode(int32) + playerId(int64) + playerName(길이 접두).
    ///
    /// <para>
    /// 파싱에 실패하면 <see cref="Login"/>을 그대로 두지 않고 <c>InvalidPayload</c>로 채운다 —
    /// null로 남기면 화면이 영원히 "대기 중"이라 타임아웃이 돌 때까지 아무 설명이 없다.
    /// </para>
    /// </summary>
    private void ApplyLogin(byte[] payload)
    {
        var reader = new BinaryPacketReader(payload);
        if (!reader.TryReadInt32(out var errorCode)
            || !reader.TryReadInt64(out var playerId)
            || !reader.TryReadString(out var playerName))
        {
            Login = new LoginOutcome((int)ErrorCode.InvalidPayload, 0, string.Empty);
            return;
        }

        Login = new LoginOutcome(errorCode, playerId, playerName);

        if (!Login.Succeeded)
        {
            AddSystemLine($"[로그인 실패] {ErrorCodeText.Describe(errorCode)}");
            return;
        }

        AccountPlayerId = playerId;
        PlayerName = playerName;
        AddSystemLine($"[로그인] {playerName} (playerId={playerId})");
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

        var isMine = senderId == MySessionId;
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
    /// <c>Z2CTaskResult</c>: errorCode(int32) + requestPacketId(uint16) + requestId(int64)
    /// + [태스크 스트림].
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

        // requestId는 성공/실패 어느 쪽이든 실린다 -- 값 자체는 쓰지 않지만 건너뛰지 않으면
        // 그 뒤 태스크 스트림을 8바이트 밀려서 읽는다.
        if (!reader.TryReadInt32(out var errorCode)
            || !reader.TryReadUInt16(out var requestPacketId)
            || !reader.TryReadInt64(out _))
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
        if (!reader.TryReadInt64(out var mailId)
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

    /// <summary>
    /// <c>Z2CUnitSpawn</c>: count(uint16) + count개의 항목. 입장할 때는 존 전체가, 리스폰할
    /// 때는 한 기가 온다. 이미 있는 유닛이면 값을 덮어쓴다 — 리스폰이 같은 패킷으로 오므로
    /// "새로 넣기"만 하면 죽은 상태가 화면에 남는다.
    /// </summary>
    private void ApplyUnitSpawn(byte[] payload)
    {
        var reader = new BinaryPacketReader(payload);
        if (!reader.TryReadUInt16(out var count))
        {
            return;
        }

        for (var index = 0; index < count; ++index)
        {
            if (!reader.TryReadUInt32(out var unitId)
                || !reader.TryReadUInt8(out var kind)
                || !reader.TryReadSingle(out var x)
                || !reader.TryReadSingle(out var y)
                || !reader.TryReadInt32(out var hp)
                || !reader.TryReadInt32(out var maxHp)
                || !reader.TryReadInt32(out var mp)
                || !reader.TryReadInt32(out var maxMp))
            {
                return;
            }

            if (!units_.TryGetValue(unitId, out var unit))
            {
                unit = new CombatUnit { UnitId = unitId, Kind = (UnitKind)kind };
                units_[unitId] = unit;
            }

            unit.X = x;
            unit.Y = y;
            unit.Hp = hp;
            unit.MaxHp = maxHp;
            unit.Mp = mp;
            unit.MaxMp = maxMp;
            unit.DeadAtSeconds = double.NegativeInfinity;
        }
    }

    private void ApplyUnitDespawn(byte[] payload)
    {
        var reader = new BinaryPacketReader(payload);
        if (!reader.TryReadUInt32(out var unitId))
        {
            return;
        }

        units_.Remove(unitId);

        if (TargetUnitId == unitId)
        {
            TargetUnitId = 0;
        }
    }

    /// <summary>
    /// <c>Z2CUnitStateSync</c>: 틱 끝에 한 번, 그 틱에 값이 바뀐 유닛만. <b>모르는 unitId는
    /// 버린다</b> — 등장 통지보다 먼저 도착할 일은 없지만, 오면 그건 순서가 어긋난 것이라
    /// 빈 유닛을 만들어 두면 좌표 없는 HP 바가 화면에 뜬다.
    /// </summary>
    private void ApplyUnitStateSync(byte[] payload)
    {
        var reader = new BinaryPacketReader(payload);
        if (!reader.TryReadUInt16(out var count))
        {
            return;
        }

        for (var index = 0; index < count; ++index)
        {
            if (!reader.TryReadUInt32(out var unitId)
                || !reader.TryReadInt32(out var hp)
                || !reader.TryReadInt32(out var mp))
            {
                return;
            }

            if (units_.TryGetValue(unitId, out var unit))
            {
                unit.Hp = hp;
                unit.Mp = mp;
            }
        }
    }

    private void ApplyUnitDead(byte[] payload, double nowSeconds)
    {
        var reader = new BinaryPacketReader(payload);
        if (!reader.TryReadUInt32(out var unitId) || !reader.TryReadUInt32(out _))
        {
            return;
        }

        if (!units_.TryGetValue(unitId, out var unit))
        {
            return;
        }

        // 사망 통지는 틱을 기다리지 않고 즉시 오므로, HP 동기화(0)가 아직 안 왔을 수 있다.
        // 여기서 0으로 맞춰야 쓰러지는 연출과 HP 바가 어긋나지 않는다.
        unit.Hp = 0;
        unit.DeadAtSeconds = nowSeconds;

        if (unitId == MySessionId)
        {
            AddSystemLine("쓰러졌습니다. 잠시 뒤 다시 일어납니다.");
        }
    }

    /// <summary>
    /// <c>Z2CAttackResult</c>: 내가 보낸 공격의 결과. <b>실패해도 온다.</b>
    /// 성공이면 내 유닛의 공격 연출을 시작하고, 실패면 사유를 토스트로 띄운다.
    /// </summary>
    private void ApplyAttackResult(byte[] payload, double nowSeconds)
    {
        var reader = new BinaryPacketReader(payload);
        if (!reader.TryReadInt32(out var errorCode)
            || !reader.TryReadUInt32(out var targetUnitId)
            || !reader.TryReadUInt8(out var attackKind)
            || !reader.TryReadInt32(out var damage)
            || !reader.TryReadInt32(out var targetHp))
        {
            return;
        }

        if (errorCode != (int)ErrorCode.Success)
        {
            LastAttackFailure = ErrorCodeText.Describe(errorCode);
            LastAttackFailureAtSeconds = nowSeconds;
            return;
        }

        // 서버가 알려준 HP를 바로 반영한다. 틱 끝의 상태 동기화를 기다리면 최대 한 틱만큼
        // 내 화면이 남의 화면보다 늦다.
        if (units_.TryGetValue(targetUnitId, out var target))
        {
            target.Hp = targetHp;
        }

        PlayAttack(MySessionId, targetUnitId, (AttackKind)attackKind, damage, nowSeconds, isMine: true);
    }

    /// <summary>
    /// <c>Z2CUnitAttackNotify</c>: 남의 공격. 시전자는 자기 <c>Z2CAttackResult</c>로 같은
    /// 연출을 그리므로 이 패킷을 받지 않는다.
    /// </summary>
    private void ApplyUnitAttackNotify(byte[] payload, double nowSeconds)
    {
        var reader = new BinaryPacketReader(payload);
        if (!reader.TryReadUInt32(out var attackerUnitId)
            || !reader.TryReadUInt32(out var targetUnitId)
            || !reader.TryReadUInt8(out var attackKind)
            || !reader.TryReadInt32(out var damage))
        {
            return;
        }

        PlayAttack(attackerUnitId, targetUnitId, (AttackKind)attackKind, damage, nowSeconds, isMine: false);
    }

    /// <summary>
    /// 공격 한 번의 연출을 시작한다 — 무기를 대상 쪽으로 돌리고, 원거리면 화살을 띄우고,
    /// 데미지 숫자를 올린다. <b>연출 길이는 서버 판정과 무관하다</b>(서버는 이미 즉발로
    /// 확정했다). 그래서 이 함수의 시간 값들은 전부 클라이언트가 정한다.
    /// </summary>
    private void PlayAttack(uint attackerUnitId, uint targetUnitId, AttackKind attackKind,
                            int damage, double nowSeconds, bool isMine)
    {
        if (!units_.TryGetValue(attackerUnitId, out var attacker)
            || !units_.TryGetValue(targetUnitId, out var target))
        {
            return;
        }

        var (attackerX, attackerY) = PositionOf(attacker);
        var (targetX, targetY) = PositionOf(target);

        attacker.AttackAtSeconds = nowSeconds;
        attacker.LastAttackKind = attackKind;
        attacker.WeaponRadians = MathF.Atan2(targetY - attackerY, targetX - attackerX);

        target.HitAtSeconds = nowSeconds;

        if (attackKind == AttackKind.Ranged)
        {
            arrows_.Add(new ArrowEffect(attackerX, attackerY, targetX, targetY,
                                        nowSeconds, ArrowFlightSeconds));
        }

        if (damage > 0)
        {
            damagePopups_.Add(new DamagePopup(targetX, targetY, damage, isMine, nowSeconds));
        }
    }

    /// <summary>
    /// 유닛의 화면 좌표. <b>플레이어는 이동 통지 쪽이 권위</b>이고, 그 정보가 아직 없으면
    /// 등장 통지에 실려 온 좌표로 버틴다.
    /// </summary>
    public (float X, float Y) PositionOf(CombatUnit unit)
    {
        if (unit.Kind == UnitKind.Player && players_.TryGetValue(unit.UnitId, out var player))
        {
            return (player.X, player.Y);
        }

        return (unit.X, unit.Y);
    }

    /// <summary>수명이 다한 연출을 걷어낸다. 게임 스레드가 매 프레임 한 번 부른다.</summary>
    public void UpdateEffects(double nowSeconds)
    {
        arrows_.RemoveAll(arrow => nowSeconds >= arrow.EndSeconds);
        damagePopups_.RemoveAll(popup => nowSeconds >= popup.EndSeconds);
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
