using GmTool.Web.Models;
using GmTool.Web.Repositories;

namespace GmTool.Web.Services;

/// <summary>
/// 운영 명령(공지/우편 발송/우편 삭제)을 WorldServer로 보내고 그 결과를 감사 로그로 남긴다.
///
/// <see cref="WorldLinkClient"/>를 화면에서 직접 부르지 않고 이 계층을 하나 끼운 이유는
/// "보내고 나서 반드시 기록한다"를 한 곳에서 강제하기 위해서다. 화면마다 로그를 남기게 하면
/// 새 화면을 만들 때 빠뜨리기 쉽고, 감사 기록은 빠진 줄이 하나만 있어도 신뢰를 잃는다.
/// </summary>
public sealed class GameCommandService
{
    private readonly WorldLinkClient worldLink_;
    private readonly CommandLogRepository commandLog_;
    private readonly ILogger<GameCommandService> logger_;

    public GameCommandService(
        WorldLinkClient worldLink, CommandLogRepository commandLog, ILogger<GameCommandService> logger)
    {
        worldLink_ = worldLink;
        commandLog_ = commandLog;
        logger_ = logger;
    }

    public bool IsWorldConnected => worldLink_.IsReady;

    public string WorldEndpoint => worldLink_.Endpoint;

    public string? WorldLastError => worldLink_.LastError;

    public Task<IReadOnlyList<WorldClientEntry>> GetOnlineClientsAsync(CancellationToken cancellationToken = default)
        => worldLink_.GetClientListAsync(cancellationToken);

    public async Task<ToolCommandResult> SendNoticeAsync(
        ulong operatorId, string message, CancellationToken cancellationToken = default)
    {
        var result = await worldLink_.SendNoticeAsync(message, cancellationToken).ConfigureAwait(false);

        await WriteLogAsync(new GmCommandLogEntry
        {
            OperatorId = operatorId,
            CommandKind = "Notice",
            TargetKind = 0,
            Body = message,
            ResultCode = (ushort)result.ResultCode,
            AffectedCount = result.AffectedCount,
        }, cancellationToken).ConfigureAwait(false);

        return result;
    }

    /// <param name="clientSessionId">null이면 접속 중인 전체가 대상.</param>
    public async Task<ToolCommandResult> SendMailAsync(
        ulong operatorId, ulong? clientSessionId, string title, string body, long durationSec,
        CancellationToken cancellationToken = default)
    {
        var result = await worldLink_
            .SendMailAsync(clientSessionId, title, body, durationSec, cancellationToken)
            .ConfigureAwait(false);

        await WriteLogAsync(new GmCommandLogEntry
        {
            OperatorId = operatorId,
            CommandKind = "MailSend",
            TargetKind = clientSessionId is null ? (byte)0 : (byte)1,
            ClientSessionId = clientSessionId ?? 0,
            Title = title,
            Body = body,
            DurationSec = durationSec,
            ResultCode = (ushort)result.ResultCode,
            AffectedCount = result.AffectedCount,
        }, cancellationToken).ConfigureAwait(false);

        return result;
    }

    public async Task<ToolCommandResult> DeleteMailAsync(
        ulong operatorId, ulong clientSessionId, uint mailId, CancellationToken cancellationToken = default)
    {
        var result = await worldLink_.DeleteMailAsync(clientSessionId, mailId, cancellationToken)
            .ConfigureAwait(false);

        await WriteLogAsync(new GmCommandLogEntry
        {
            OperatorId = operatorId,
            CommandKind = "MailDelete",
            TargetKind = 1,
            ClientSessionId = clientSessionId,
            MailId = mailId,
            ResultCode = (ushort)result.ResultCode,
            AffectedCount = result.AffectedCount,
        }, cancellationToken).ConfigureAwait(false);

        return result;
    }

    public Task<IReadOnlyList<GmCommandLogEntry>> GetRecentLogsAsync(
        int limit = 30, CancellationToken cancellationToken = default)
        => commandLog_.ListRecentAsync(limit, cancellationToken);

    // 감사 로그 기록이 실패해도 이미 나간 명령을 되돌릴 수는 없다. 여기서 예외를 올려
    // 화면에 "실패"로 보여주면 운영자가 같은 명령을 다시 보내 우편이 두 번 나가는, 더 나쁜
    // 결과가 된다. 그래서 로그로만 남기고 삼킨다.
    private async Task WriteLogAsync(GmCommandLogEntry entry, CancellationToken cancellationToken)
    {
        try
        {
            await commandLog_.WriteAsync(entry, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            logger_.LogError(ex, "명령 감사 로그 기록 실패: {Kind}", entry.CommandKind);
        }
    }
}
