namespace GmTool.Web.Models;

/// <summary>운영자 계정. <c>gm_operator</c> 한 행.</summary>
public sealed class OperatorAccount
{
    public ulong OperatorId { get; init; }
    public string LoginId { get; init; } = string.Empty;
    public string DisplayName { get; init; } = string.Empty;
    public string PasswordHash { get; init; } = string.Empty;
    public string Role { get; init; } = "Operator";
    public bool IsActive { get; init; } = true;
    public DateTime? LastLoginAt { get; init; }
    public DateTime CreatedAt { get; init; }
}

/// <summary>쿠폰 캠페인(= 쿠폰 번호의 네임스페이스). <c>coupon_campaign</c> 한 행.</summary>
public sealed class CouponCampaign
{
    public ulong CampaignId { get; init; }
    public string CampaignCode { get; init; } = string.Empty;
    public string CampaignName { get; init; } = string.Empty;
    public string CouponTable { get; init; } = string.Empty;

    public string RewardKind { get; init; } = "Mail";
    public string RewardTitle { get; init; } = string.Empty;
    public string RewardBody { get; init; } = string.Empty;
    public long RewardDurationSec { get; init; } = 604800;

    /// <summary>0 = 무제한, 1 = 1회용, N = N회까지.</summary>
    public int MaxUseCount { get; init; } = 1;

    public DateTime? ValidFrom { get; init; }
    public DateTime? ValidTo { get; init; }

    /// <summary>발급일로부터 N일. 0이면 상대 유효 기간을 쓰지 않는다.</summary>
    public int ValidDaysAfterIssue { get; init; }

    public string Status { get; init; } = "Draft";
    public ulong IssuedCount { get; init; }
    public ulong CreatedBy { get; init; }
    public DateTime CreatedAt { get; init; }
}

/// <summary>대량 발급 배치 한 건. <c>coupon_issue_batch</c> 한 행.</summary>
public sealed class CouponIssueBatch
{
    public ulong BatchId { get; init; }
    public ulong CampaignId { get; init; }
    public string CampaignCode { get; init; } = string.Empty;
    public ulong RequestedCount { get; init; }
    public ulong GeneratedCount { get; init; }
    public ulong InsertedCount { get; init; }
    public ulong DuplicateCount { get; init; }
    public string SpoolPath { get; init; } = string.Empty;
    public string Status { get; init; } = "Running";
    public string ErrorMessage { get; init; } = string.Empty;
    public long ElapsedMs { get; init; }
    public ulong CreatedBy { get; init; }
    public DateTime CreatedAt { get; init; }
    public DateTime? FinishedAt { get; init; }
}

/// <summary>쿠폰 한 장. 캠페인 전용 테이블의 한 행.</summary>
public sealed class CouponRecord
{
    public string CouponCode { get; init; } = string.Empty;
    public byte Status { get; init; }
    public uint UseCount { get; init; }
    public DateTime IssuedAt { get; init; }
    public DateTime? ExpiresAt { get; init; }
    public DateTime? FirstUsedAt { get; init; }
    public DateTime? LastUsedAt { get; init; }
    public ulong? UsedBySessionId { get; init; }
}

/// <summary>운영툴이 게임 서버로 보낸 명령의 감사 기록. <c>gm_command_log</c> 한 행.</summary>
public sealed class GmCommandLogEntry
{
    public ulong CommandId { get; init; }
    public ulong OperatorId { get; init; }
    public string CommandKind { get; init; } = string.Empty;
    public byte TargetKind { get; init; }
    public ulong ClientSessionId { get; init; }
    public uint MailId { get; init; }
    public string Title { get; init; } = string.Empty;
    public string Body { get; init; } = string.Empty;
    public long DurationSec { get; init; }
    public ushort ResultCode { get; init; }
    public uint AffectedCount { get; init; }
    public string ErrorMessage { get; init; } = string.Empty;
    public DateTime RequestedAt { get; init; }
}
