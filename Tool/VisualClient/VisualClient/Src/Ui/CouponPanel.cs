using Microsoft.Xna.Framework;
using VisualClient.Model;
using VisualClient.Net;

namespace VisualClient.Ui;

/// <summary>
/// 쿠폰 등록 패널. 우측 하단 우편 아이콘 옆 버튼으로 열고 닫는다.
///
/// <para>
/// <b>이 패널만 소켓이 아니라 HTTP를 쓴다.</b> 쿠폰 등록은 클라이언트 패킷 대역에 없고,
/// 쿠폰 체계 전체가 GmTool.Web + SQL Server에 있다(<see cref="CouponClient"/> 주석 참고).
/// 등록이 성공하면 보상은 이 응답이 아니라 <b>우편함</b>에 도착한다 — GmTool이 World에
/// 우편 발송을 지시하고, 그게 존을 거쳐 <c>Z2CTaskResult</c>로 소켓으로 들어온다. 그래서
/// 등록 성공 문구와 우편 도착 사이에 짧은 시차가 있는 게 정상이다.
/// </para>
/// </summary>
public sealed class CouponPanel
{
    /// <summary>쿠폰 번호는 하이픈 포함 29자(25자 + 하이픈 4개). 여유를 둔다.</summary>
    private const int MaxCodeLength = 40;

    private readonly CouponClient coupons_;
    private readonly TextField code_ = new("쿠폰 번호 (하이픈/소문자 상관없음)", MaxCodeLength);

    /// <summary>진행 중인 등록 요청. 게임 스레드를 막지 않기 위해 Task를 들고 폴링한다.</summary>
    private Task<CouponRedeemResponse>? pending_;

    private string status_ = string.Empty;
    private bool statusIsError_;

    public bool IsOpen { get; private set; }

    /// <summary>쿠폰 번호 입력칸이 포커스를 갖고 있는가(MailPanel.HasFocusedField와 같은 이유).</summary>
    public bool HasFocusedField => code_.HasFocus;

    public Rectangle ToggleBounds { get; private set; }

    public Rectangle PanelBounds { get; private set; }

    public CouponPanel(CouponClient coupons)
    {
        coupons_ = coupons;
    }

    public void Layout(Rectangle toggleBounds, Rectangle panelBounds)
    {
        ToggleBounds = toggleBounds;
        PanelBounds = panelBounds;
    }

    public void Close() => IsOpen = false;

    public void UpdateAndDraw(Painter painter, InputState input, WorldModel world, double totalSeconds)
    {
        if (Widgets.Button(painter, input, ToggleBounds, "쿠폰"))
        {
            IsOpen = !IsOpen;
        }

        PollPending();

        if (!IsOpen)
        {
            return;
        }

        var rect = PanelBounds;
        painter.Panel(rect, "쿠폰 등록  (GmTool.Web HTTP → 보상은 우편으로 도착)");

        var headerHeight = painter.Font.LineHeight + 8;
        var fieldHeight = painter.Font.LineHeight + 10;
        var y = rect.Y + headerHeight + 8;

        painter.SmallText($"POST {coupons_.BaseUrl}/api/coupon/redeem",
                          new Vector2(rect.X + 10, y), new Color(120, 132, 155));
        y += painter.SmallFont.LineHeight + 6;

        var sessionText = world.MyPlayerId == 0
            ? "clientSessionId: (존 입장 전 — 보상을 받을 대상이 없습니다)"
            : $"clientSessionId: {world.MyPlayerId}  (보상 우편을 받을 대상)";
        painter.SmallText(sessionText, new Vector2(rect.X + 10, y),
                          world.MyPlayerId == 0 ? new Color(220, 150, 110) : new Color(140, 200, 165));
        y += painter.SmallFont.LineHeight + 10;

        var isBusy = pending_ is not null;
        var codeRect = new Rectangle(rect.X + 10, y, rect.Width - 130, fieldHeight);
        var submitRect = new Rectangle(codeRect.Right + 8, y, rect.Right - codeRect.Right - 18, fieldHeight);

        var submitted = code_.Update(input);
        code_.Draw(painter, input, codeRect, totalSeconds);
        code_.HandleEscape(input);

        var canSubmit = !isBusy && code_.Value.Trim().Length > 0;
        var clicked = Widgets.Button(painter, input, submitRect, isBusy ? "등록 중..." : "등록", canSubmit);

        y = codeRect.Bottom + 10;
        if (status_.Length > 0)
        {
            foreach (var line in WrapStatus(painter, status_, rect.Width - 20))
            {
                painter.SmallText(line, new Vector2(rect.X + 10, y),
                                  statusIsError_ ? new Color(240, 140, 130) : new Color(150, 225, 175));
                y += painter.SmallFont.LineHeight + 2;
            }
        }

        if (canSubmit && (clicked || submitted))
        {
            Submit(world);
        }
    }

    private void Submit(WorldModel world)
    {
        var code = code_.Value.Trim();
        code_.Clear();
        status_ = "등록 요청을 보냈습니다...";
        statusIsError_ = false;

        // playerId를 그대로 clientSessionId로 넘긴다 — 서버의 playerId는 clientSessionId의
        // 하위 32비트이고 SessionId는 1부터 증가하는 카운터라 상위 32비트가 0이다.
        ulong? sessionId = world.MyPlayerId == 0 ? null : world.MyPlayerId;
        pending_ = coupons_.RedeemAsync(code, sessionId);
    }

    private void PollPending()
    {
        if (pending_ is not { IsCompleted: true } task)
        {
            return;
        }

        pending_ = null;

        // RedeemAsync는 통신 실패까지 결과로 감싸서 돌려주므로 여기서 예외는 나지 않는다.
        // 그래도 Result를 그냥 읽지 않는 이유: 앞으로 취소를 넣게 되면 조용히 죽는다.
        if (task.IsFaulted)
        {
            status_ = $"등록 실패: {task.Exception?.GetBaseException().Message}";
            statusIsError_ = true;
            return;
        }

        var response = task.Result;
        statusIsError_ = !response.Succeeded;
        status_ = response.Succeeded
            ? $"{response.Message ?? "등록 성공"} — 보상 우편이 곧 우편함에 도착합니다."
            : response.Message ?? "유효하지 않은 쿠폰입니다.";
    }

    /// <summary>
    /// 상태 문구를 패널 폭에 맞춰 단어 단위로 접는다. <c>GlyphAtlas</c>가 줄바꿈을 모르기
    /// 때문에 접는 건 이쪽 책임이다.
    /// </summary>
    private static List<string> WrapStatus(Painter painter, string text, float maxWidth)
    {
        var lines = new List<string>();
        var current = string.Empty;

        foreach (var word in text.Split(' '))
        {
            var candidate = current.Length == 0 ? word : $"{current} {word}";
            if (painter.SmallFont.Measure(candidate).X <= maxWidth)
            {
                current = candidate;
                continue;
            }

            if (current.Length > 0)
            {
                lines.Add(current);
            }

            current = word;
        }

        if (current.Length > 0)
        {
            lines.Add(current);
        }

        return lines;
    }
}
