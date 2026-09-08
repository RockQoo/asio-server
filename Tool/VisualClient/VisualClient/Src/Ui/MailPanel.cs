using Microsoft.Xna.Framework;
using VisualClient.Model;

namespace VisualClient.Ui;

public enum MailAction
{
    None,
    Add,
    Delete,
}

/// <summary>이번 프레임에 사용자가 요청한 우편 동작. 실제 전송은 소켓을 든 쪽이 한다.</summary>
public readonly record struct MailRequest(
    MailAction Action, string Title, string Body, long DurationSec, uint MailId)
{
    public static readonly MailRequest None = new(MailAction.None, string.Empty, string.Empty, 0, 0);
}

/// <summary>
/// 우측 하단 우편 아이콘과, 클릭하면 열리는 우편함 패널.
///
/// <para>
/// 목록은 <b>서버에서 조회해 온 게 아니다</b> — 우편 목록 조회 패킷이 프로토콜에 없다. 대신
/// 입장 시점에 서버 우편함도 비어 있으므로(<c>MailRegistry::Add</c>가 빈 <c>MailModel</c>을
/// 만든다), <c>Z2CTaskResult</c>로 내려오는 Added/Removed 태스크만 순서대로 적용하면 서버와
/// 같은 상태가 유지된다 — 이게 Unit-of-Work 스트림을 클라이언트에도 그대로 흘려주는 이유다.
/// </para>
/// </summary>
public sealed class MailPanel
{
    private const int MaxTitleLength = 40;
    private const int MaxBodyLength = 120;

    private readonly TextField title_ = new("제목", MaxTitleLength);
    private readonly TextField body_ = new("본문", MaxBodyLength);
    private readonly TextField duration_ = new("유효기간(초)", 6);

    private int scrollOffset_;

    public bool IsOpen { get; private set; }

    /// <summary>
    /// 이 패널의 입력칸 중 하나가 포커스를 갖고 있는가. 포커스가 있으면 WASD가 이동이 아니라
    /// 타이핑으로 가야 하므로, 메인 루프가 이동 처리를 건너뛸 판단에 쓴다.
    /// </summary>
    public bool HasFocusedField => title_.HasFocus || body_.HasFocus || duration_.HasFocus;

    /// <summary>우편 아이콘 버튼의 위치. 존 뷰 클릭과 겹치지 않게 하려고 밖에서도 본다.</summary>
    public Rectangle IconBounds { get; private set; }

    public Rectangle PanelBounds { get; private set; }

    public MailPanel()
    {
        // 만료 삭제가 도는 걸 짧은 시간에 확인할 수 있게 기본값을 짧게 잡는다. 서버는
        // Mail::MailExpiryService가 1초 주기로 훑는다.
        duration_.SetValue("30");
    }

    public void Layout(Rectangle iconBounds, Rectangle panelBounds)
    {
        IconBounds = iconBounds;
        PanelBounds = panelBounds;
    }

    public void Close() => IsOpen = false;

    public MailRequest UpdateAndDraw(Painter painter, InputState input, WorldModel world,
                                     double totalSeconds, long nowUt)
    {
        DrawIcon(painter, input, world);

        if (!IsOpen)
        {
            return MailRequest.None;
        }

        var rect = PanelBounds;
        painter.Panel(rect, $"우편함 ({world.Mails.Count}통)   C2ZMailAdd / C2ZMailDel → Z2CTaskResult");

        var headerHeight = painter.Font.LineHeight + 8;
        var y = rect.Y + headerHeight + 6;

        if (world.MailboxResetByZoneChange)
        {
            // 서버가 핸드오프 때 우편함을 버리는 건 구현이 덜 된 부분이라, 화면에서 이유를
            // 밝혀두지 않으면 "우편이 사라지는 버그"로 보인다.
            var noticeRect = new Rectangle(rect.X + 8, y, rect.Width - 16, painter.SmallFont.LineHeight + 8);
            painter.FillRect(noticeRect, new Color(64, 42, 16, 220));
            painter.SmallText("존 이동 시 서버가 우편함을 버린다(존 간 이관 미구현) — 목록도 같이 비웠습니다.",
                              new Vector2(noticeRect.X + 6, noticeRect.Y + 4), new Color(240, 200, 130));
            y = noticeRect.Bottom + 6;
        }

        var composeHeight = ((painter.Font.LineHeight + 10) * 2) + 8;
        var listRect = new Rectangle(rect.X + 8, y, rect.Width - 16, rect.Bottom - y - composeHeight - 16);

        var deleteId = DrawList(painter, input, world, listRect, nowUt);

        var composeRect = new Rectangle(rect.X + 8, listRect.Bottom + 8, rect.Width - 16, composeHeight);
        var added = DrawCompose(painter, input, composeRect, totalSeconds, out var request);

        if (deleteId != 0)
        {
            return new MailRequest(MailAction.Delete, string.Empty, string.Empty, 0, deleteId);
        }

        return added ? request : MailRequest.None;
    }

    /// <summary>봉투 모양 아이콘. 안 읽은 우편 수를 뱃지로 얹는다.</summary>
    private void DrawIcon(Painter painter, InputState input, WorldModel world)
    {
        var rect = IconBounds;
        var hovered = rect.Contains(input.MousePosition);

        painter.FillRect(rect, hovered ? new Color(48, 68, 104) : new Color(30, 40, 60, 235));
        painter.StrokeRect(rect, IsOpen ? new Color(150, 190, 240) : new Color(96, 128, 176));

        // 봉투: 사각형 + 뚜껑을 이루는 두 선.
        var envelope = new Rectangle(rect.X + 12, rect.Y + 14, rect.Width - 24, rect.Height - 30);
        painter.FillRect(envelope, new Color(226, 232, 244));
        painter.StrokeRect(envelope, new Color(120, 132, 152));
        painter.Line(new Vector2(envelope.Left, envelope.Top),
                     new Vector2(envelope.Center.X, envelope.Center.Y),
                     new Color(120, 132, 152), 1.5f);
        painter.Line(new Vector2(envelope.Right, envelope.Top),
                     new Vector2(envelope.Center.X, envelope.Center.Y),
                     new Color(120, 132, 152), 1.5f);

        var count = world.Mails.Count;
        if (count > 0)
        {
            var badge = new Vector2(rect.Right - 12, rect.Y + 12);
            painter.FillCircle(badge, 10.0f, new Color(220, 80, 80));
            var text = count > 99 ? "99+" : count.ToString();
            var size = painter.SmallFont.Measure(text);
            painter.SmallText(text, new Vector2(badge.X - (size.X * 0.5f), badge.Y - (size.Y * 0.5f)),
                              Color.White);
        }

        var label = "우편";
        var labelWidth = painter.SmallFont.Measure(label).X;
        painter.SmallText(label,
                          new Vector2(rect.Center.X - (labelWidth * 0.5f),
                                      rect.Bottom - painter.SmallFont.LineHeight - 2),
                          new Color(200, 214, 236));

        if (hovered && input.MouseLeftPressed)
        {
            IsOpen = !IsOpen;
        }
    }

    /// <summary>목록을 그리고, 삭제 버튼이 눌린 mailId를 돌려준다(없으면 0).</summary>
    private uint DrawList(Painter painter, InputState input, WorldModel world, Rectangle listRect,
                          long nowUt)
    {
        painter.FillRect(listRect, new Color(8, 10, 16, 200));

        var rowHeight = painter.Font.LineHeight + 14;
        var visibleRows = Math.Max(1, listRect.Height / rowHeight);
        var maxOffset = Math.Max(0, world.Mails.Count - visibleRows);

        if (listRect.Contains(input.MousePosition))
        {
            scrollOffset_ = Math.Clamp(scrollOffset_ - (input.WheelDelta / 120), 0, maxOffset);
        }
        else
        {
            scrollOffset_ = Math.Clamp(scrollOffset_, 0, maxOffset);
        }

        if (world.Mails.Count == 0)
        {
            painter.SmallText("우편이 없습니다. 아래에서 추가하거나 쿠폰을 등록해 보세요.",
                              new Vector2(listRect.X + 8, listRect.Y + 8), new Color(120, 130, 150));
            return 0;
        }

        uint deleteId = 0;
        for (var slot = 0; slot < visibleRows; ++slot)
        {
            var index = slot + scrollOffset_;
            if (index >= world.Mails.Count)
            {
                break;
            }

            var mail = world.Mails[index];
            var rowRect = new Rectangle(listRect.X + 4, listRect.Y + 4 + (slot * rowHeight),
                                        listRect.Width - 8, rowHeight - 4);
            painter.FillRect(rowRect, (index % 2) == 0 ? new Color(18, 22, 32) : new Color(14, 18, 26));

            var deleteRect = new Rectangle(rowRect.Right - 56, rowRect.Y + 3, 52, rowRect.Height - 6);
            if (Widgets.Button(painter, input, deleteRect, "삭제"))
            {
                deleteId = mail.MailId;
            }

            var textWidth = deleteRect.X - rowRect.X - 12;
            painter.Text(painter.Font.Ellipsize($"#{mail.MailId}  {mail.Title}", textWidth),
                         new Vector2(rowRect.X + 6, rowRect.Y + 3), new Color(220, 230, 245));

            var remain = mail.EndUt - nowUt;
            var remainText = remain > 0 ? $"{remain}초 남음" : "만료 처리 대기";
            painter.SmallText(painter.SmallFont.Ellipsize($"{mail.Body}   ·   {remainText}", textWidth),
                              new Vector2(rowRect.X + 6, rowRect.Y + 3 + painter.Font.LineHeight),
                              remain > 0 ? new Color(140, 152, 175) : new Color(220, 150, 110));
        }

        if (maxOffset > 0)
        {
            painter.SmallText($"{scrollOffset_ + 1}~{Math.Min(scrollOffset_ + visibleRows, world.Mails.Count)} / {world.Mails.Count} (휠로 스크롤)",
                              new Vector2(listRect.X + 6, listRect.Bottom - painter.SmallFont.LineHeight - 2),
                              new Color(110, 122, 145));
        }

        return deleteId;
    }

    /// <summary>제목/본문/유효기간 입력 + 추가 버튼. 추가가 눌렸으면 true.</summary>
    private bool DrawCompose(Painter painter, InputState input, Rectangle rect, double totalSeconds,
                             out MailRequest request)
    {
        var fieldHeight = painter.Font.LineHeight + 10;

        var titleRect = new Rectangle(rect.X, rect.Y, rect.Width - 220, fieldHeight);
        var durationRect = new Rectangle(titleRect.Right + 6, rect.Y, 110, fieldHeight);
        var addRect = new Rectangle(durationRect.Right + 6, rect.Y, rect.Right - durationRect.Right - 6,
                                    fieldHeight);
        var bodyRect = new Rectangle(rect.X, rect.Y + fieldHeight + 8, rect.Width, fieldHeight);

        var titleSubmitted = title_.Update(input);
        var bodySubmitted = body_.Update(input);
        var durationSubmitted = duration_.Update(input);

        title_.Draw(painter, input, titleRect, totalSeconds);
        duration_.Draw(painter, input, durationRect, totalSeconds);
        body_.Draw(painter, input, bodyRect, totalSeconds);

        title_.HandleEscape(input);
        body_.HandleEscape(input);
        duration_.HandleEscape(input);

        var title = title_.Value.Trim();
        var body = body_.Value.Trim();
        var canAdd = title.Length > 0;

        var clicked = Widgets.Button(painter, input, addRect, "추가", canAdd);

        // 세 칸 어디서 Enter를 눌러도 추가로 이어지게 한다 — 입력칸을 옮겨 다니다 Enter를
        // 눌렀는데 아무 일도 안 일어나면 고장으로 보인다.
        var submitted = titleSubmitted || bodySubmitted || durationSubmitted;

        if (!canAdd || (!clicked && !submitted))
        {
            request = MailRequest.None;
            return false;
        }

        if (!long.TryParse(duration_.Value.Trim(), out var durationSec) || durationSec <= 0)
        {
            durationSec = 30;
        }

        request = new MailRequest(MailAction.Add, title, body.Length == 0 ? "(본문 없음)" : body,
                                  durationSec, 0);
        title_.Clear();
        body_.Clear();
        return true;
    }
}
