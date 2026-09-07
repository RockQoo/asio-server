using GmTool.Web.Data;
using GmTool.Web.Models;
using SqlKata.Execution;

namespace GmTool.Web.Repositories;

/// <summary>
/// <c>gm_command_log</c> 접근. 운영툴에서 나간 게임 서버 명령을 전부 남긴다 — 우편/공지는
/// 실제 플레이어에게 영향을 주는 행위라, "누가 언제 무엇을 보냈는지"가 남지 않으면 사고가
/// 났을 때 원인을 추적할 수 없다.
/// </summary>
public sealed class CommandLogRepository
{
    private const string Table = "gm_command_log";

    private readonly SqlServerConnectionFactory factory_;

    public CommandLogRepository(SqlServerConnectionFactory factory) => factory_ = factory;

    public async Task WriteAsync(GmCommandLogEntry entry, CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        await db.Query(Table).InsertAsync(new
        {
            operator_id = SqlNum.Of(entry.OperatorId),
            command_kind = entry.CommandKind,
            target_kind = entry.TargetKind,
            client_session_id = SqlNum.Of(entry.ClientSessionId),
            mail_id = SqlNum.Of(entry.MailId),
            title = Truncate(entry.Title, 128),
            body = Truncate(entry.Body, 1024),
            duration_sec = entry.DurationSec,
            result_code = SqlNum.Of(entry.ResultCode),
            affected_count = SqlNum.Of(entry.AffectedCount),
            error_message = Truncate(entry.ErrorMessage, 255),
            requested_at = DateTime.UtcNow,
        }, cancellationToken: cancellationToken).ConfigureAwait(false);
    }

    public async Task<IReadOnlyList<GmCommandLogEntry>> ListRecentAsync(
        int limit = 30, CancellationToken cancellationToken = default)
    {
        using var db = await factory_.CreateQueryFactoryAsync(cancellationToken).ConfigureAwait(false);
        var rows = await db.Query(Table)
            .OrderByDesc("command_id")
            .Limit(limit)
            .GetAsync<dynamic>(cancellationToken: cancellationToken)
            .ConfigureAwait(false);

        return rows.Select(row =>
        {
            var dict = (IDictionary<string, object?>)row;
            return new GmCommandLogEntry
            {
                CommandId = Convert.ToUInt64(dict["command_id"]),
                OperatorId = Convert.ToUInt64(dict["operator_id"]),
                CommandKind = (string)dict["command_kind"]!,
                TargetKind = Convert.ToByte(dict["target_kind"]),
                ClientSessionId = Convert.ToUInt64(dict["client_session_id"]),
                MailId = Convert.ToUInt32(dict["mail_id"]),
                Title = (string)dict["title"]!,
                Body = (string)dict["body"]!,
                DurationSec = Convert.ToInt64(dict["duration_sec"]),
                ResultCode = Convert.ToUInt16(dict["result_code"]),
                AffectedCount = Convert.ToUInt32(dict["affected_count"]),
                ErrorMessage = (string)dict["error_message"]!,
                RequestedAt = Convert.ToDateTime(dict["requested_at"]),
            };
        }).ToList();
    }

    // 컬럼 길이를 넘는 값 때문에 감사 기록 자체를 못 남기는 게 제일 나쁜 결과라, 넘치면 자른다.
    private static string Truncate(string? value, int maxLength)
    {
        if (string.IsNullOrEmpty(value))
        {
            return string.Empty;
        }

        return value.Length <= maxLength ? value : value[..maxLength];
    }
}
