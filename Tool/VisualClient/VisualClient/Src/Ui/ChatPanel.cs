using Microsoft.Xna.Framework;
using VisualClient.Model;

namespace VisualClient.Ui;

/// <summary>
/// 좌측 하단 채팅 패널. 서버가 되돌려준 <c>Z2CChatNotify</c>만 표시한다 — 보낸 즉시 화면에
/// 찍는(로컬 에코) 방식이 아니라 <b>왕복해서 돌아온 것만</b> 보여준다. 그래야 "서버를 실제로
/// 거쳤는지"가 화면에 그대로 드러난다.
/// </summary>
public sealed class ChatPanel
{
    /// <summary>C++ <c>Packet::PacketHeader::MaxBodySize()</c>를 넘지 않도록 넉넉히 잡은 상한.</summary>
    private const int MaxMessageLength = 200;

    /// <summary>
    /// 한글 전송 확인용 프리셋. MonoGame이 IME 조합을 받지 못해 입력칸으로는 한글을 못 치므로,
    /// UTF-8 길이 접두 문자열이 제대로 왕복하는지는 이 버튼들로 확인한다(한글 한 글자가
    /// 3바이트라, 서버가 길이를 문자 수로 착각하고 있으면 여기서 바로 깨져 보인다).
    /// </summary>
    private static readonly string[] Presets = ["안녕하세요", "존 이동 테스트", "우편 확인했습니다", "가나다라마 12345"];

    private readonly TextField input_ = new("메시지 입력 (Enter로 전송, 한글은 아래 프리셋 사용)", MaxMessageLength);

    /// <summary>아래에서 몇 줄 위로 올려 봤는지. 0이면 최신 줄이 맨 아래에 붙는다.</summary>
    private int scrollOffset_;

    public Rectangle Bounds { get; private set; }

    public bool InputHasFocus => input_.HasFocus;

    public void Layout(Rectangle bounds) => Bounds = bounds;

    public void FocusInput() => input_.HasFocus = true;

    /// <summary>
    /// 패널을 갱신하고 그린다. 보낼 메시지가 확정되면 그 문자열을 돌려준다(없으면 null) —
    /// 실제 전송은 소켓을 들고 있는 쪽이 하도록 여기서는 문자열만 넘긴다.
    /// </summary>
    public string? UpdateAndDraw(Painter painter, InputState input, WorldModel world, double totalSeconds)
    {
        painter.Panel(Bounds, "채팅  (C2ZChat → Z2CChatNotify 존 브로드캐스트)");

        var headerHeight = painter.Font.LineHeight + 8;
        var inputHeight = painter.Font.LineHeight + 10;
        var presetHeight = painter.Font.LineHeight + 10;

        var logRect = new Rectangle(
            Bounds.X + 8,
            Bounds.Y + headerHeight + 6,
            Bounds.Width - 16,
            Bounds.Height - headerHeight - inputHeight - presetHeight - 24);

        HandleScroll(input, world, logRect, painter.SmallFont.LineHeight);
        DrawLog(painter, world, logRect);

        var presetRect = new Rectangle(Bounds.X + 8, logRect.Bottom + 4, Bounds.Width - 16, presetHeight);
        var preset = DrawPresets(painter, input, presetRect);

        var inputRect = new Rectangle(Bounds.X + 8, presetRect.Bottom + 4, Bounds.Width - 16, inputHeight);
        var submitted = input_.Update(input);
        input_.Draw(painter, input, inputRect, totalSeconds);
        input_.HandleEscape(input);

        if (preset is not null)
        {
            return preset;
        }

        if (!submitted)
        {
            return null;
        }

        var message = input_.Value.Trim();
        input_.Clear();
        return message.Length == 0 ? null : message;
    }

    private void HandleScroll(InputState input, WorldModel world, Rectangle logRect, int lineHeight)
    {
        if (!logRect.Contains(input.MousePosition))
        {
            return;
        }

        var visibleLines = Math.Max(1, logRect.Height / lineHeight);
        var maxOffset = Math.Max(0, world.ChatLines.Count - visibleLines);

        // 휠 한 칸이 120이다. 세 줄씩 움직이는 게 손 감각에 맞는다.
        scrollOffset_ = Math.Clamp(scrollOffset_ + (input.WheelDelta / 120 * 3), 0, maxOffset);
    }

    private void DrawLog(Painter painter, WorldModel world, Rectangle logRect)
    {
        painter.FillRect(logRect, new Color(8, 10, 16, 200));

        var lineHeight = painter.SmallFont.LineHeight;
        var visibleLines = Math.Max(1, logRect.Height / lineHeight);
        var lines = world.ChatLines;

        // 아래에서 위로 채운다 — 최신 줄이 항상 맨 아래에 붙어 있어야 읽기 편하다.
        var lastIndex = lines.Count - 1 - scrollOffset_;
        for (var slot = 0; slot < visibleLines; ++slot)
        {
            var index = lastIndex - slot;
            if (index < 0)
            {
                break;
            }

            var line = lines[index];
            var y = logRect.Bottom - ((slot + 1) * lineHeight) - 2;
            painter.SmallFont.DrawString(
                painter.SpriteBatch,
                painter.SmallFont.Ellipsize(line.Text, logRect.Width - 10),
                new Vector2(logRect.X + 5, y),
                ColorOf(line.Kind));
        }

        if (scrollOffset_ > 0)
        {
            painter.SmallText($"▲ {scrollOffset_}줄 위 (휠로 최신으로)",
                              new Vector2(logRect.X + 5, logRect.Y + 2), new Color(200, 180, 120));
        }
    }

    private static Color ColorOf(ChatLineKind kind) => kind switch
    {
        ChatLineKind.Mine => new Color(150, 230, 180),
        ChatLineKind.Notice => new Color(255, 205, 110),
        ChatLineKind.System => new Color(140, 152, 175),
        _ => new Color(205, 218, 238),
    };

    private static string? DrawPresets(Painter painter, InputState input, Rectangle rect)
    {
        var gap = 4;
        var width = (rect.Width - (gap * (Presets.Length - 1))) / Presets.Length;
        string? clicked = null;

        for (var i = 0; i < Presets.Length; ++i)
        {
            var buttonRect = new Rectangle(rect.X + (i * (width + gap)), rect.Y, width, rect.Height);
            if (Widgets.Button(painter, input, buttonRect, Presets[i]))
            {
                clicked = Presets[i];
            }
        }

        return clicked;
    }
}
