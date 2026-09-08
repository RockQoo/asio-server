using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Input;

namespace VisualClient.Ui;

/// <summary>
/// 사각형 하나 + 라벨로 된 버튼. MonoGame에는 UI 위젯이 없어서 직접 만든다.
///
/// <para>
/// 상태를 안 들고 있는 이유: 위치가 매 프레임 레이아웃에서 계산돼 나오고 hover/pressed는
/// 전부 이번 프레임 입력에서 바로 구할 수 있다. 굳이 인스턴스로 들고 있으면 화면 크기가
/// 바뀔 때마다 위치를 다시 밀어 넣어줘야 한다.
/// </para>
/// </summary>
public static class Widgets
{
    /// <summary>버튼을 그리고, 이번 프레임에 클릭됐는지 돌려준다.</summary>
    public static bool Button(Painter painter, InputState input, Rectangle rect, string label,
                              bool enabled = true)
    {
        var hovered = enabled && rect.Contains(input.MousePosition);
        var pressed = hovered && input.MouseLeftDown;

        var background = !enabled
            ? new Color(30, 34, 42, 220)
            : pressed
                ? new Color(64, 96, 148)
                : hovered
                    ? new Color(48, 68, 104)
                    : new Color(32, 44, 66, 235);

        painter.FillRect(rect, background);
        painter.StrokeRect(rect, enabled ? new Color(96, 128, 176) : new Color(60, 66, 78));

        // 버튼 폭은 레이아웃이 정하고 라벨 길이는 내용이 정하므로, 넘치는 라벨은 줄임표로
        // 잘라낸다 — 그냥 그리면 옆 버튼 위로 글자가 넘어간다.
        var fitted = painter.Font.Ellipsize(label, rect.Width - 8);
        var labelSize = painter.Font.Measure(fitted);
        var labelPosition = new Vector2(
            rect.X + ((rect.Width - labelSize.X) * 0.5f),
            rect.Y + ((rect.Height - labelSize.Y) * 0.5f));
        painter.Text(fitted, labelPosition, enabled ? Color.White : new Color(120, 126, 136));

        return hovered && input.MouseLeftPressed;
    }
}

/// <summary>
/// 한 줄 텍스트 입력칸.
///
/// <para>
/// <b>한글 입력(IME)은 동작하지 않는다.</b> MonoGame의 <c>TextInput</c>은 OS가 완성한
/// 조합 문자를 받아오지 않아서, 한글을 치면 아무것도 들어오지 않거나 자모가 깨져 들어온다.
/// 여기 붙는 IME 조합창을 직접 띄우려면 Win32 <c>ImmGetContext</c> 계열을 후킹해야 하는데,
/// 이 도구의 목적(서버 기능 왕복 확인)에 비해 비용이 크다. 대신 <b>한글은 미리 만들어둔
/// 프리셋 버튼</b>으로 보내고, 이 입력칸은 ASCII 타이핑만 받는다 — 서버가 UTF-8 바이트 길이
/// 접두 문자열을 쓰므로 한글 왕복 자체는 프리셋으로 충분히 검증된다.
/// </para>
/// </summary>
public sealed class TextField
{
    private readonly int maxLength_;

    public string Value { get; private set; } = string.Empty;

    public bool HasFocus { get; set; }

    /// <summary>비어 있을 때 흐리게 보여줄 안내 문구.</summary>
    public string Placeholder { get; }

    public TextField(string placeholder, int maxLength)
    {
        Placeholder = placeholder;
        maxLength_ = maxLength;
    }

    public void Clear() => Value = string.Empty;

    public void SetValue(string value) =>
        Value = value.Length > maxLength_ ? value[..maxLength_] : value;

    /// <summary>
    /// 포커스가 있을 때 이번 프레임의 타이핑을 반영한다. Enter로 확정됐으면 true를 돌려준다
    /// (확정된 값은 호출부가 <see cref="Value"/>에서 읽고 <see cref="Clear"/>한다).
    /// </summary>
    public bool Update(InputState input)
    {
        if (!HasFocus)
        {
            return false;
        }

        var submitted = false;
        foreach (var ch in input.TypedCharacters)
        {
            switch (ch)
            {
                case '\b':
                    if (Value.Length > 0)
                    {
                        Value = Value[..^1];
                    }
                    break;

                case '\r':
                case '\n':
                    submitted = true;
                    break;

                case '\t':
                case (char)27:
                    // Tab/Esc는 문자로 넣지 않는다. Esc의 포커스 해제는 호출부가 키로 처리한다.
                    break;

                default:
                    if (!char.IsControl(ch) && Value.Length < maxLength_)
                    {
                        Value += ch;
                    }
                    break;
            }
        }

        return submitted;
    }

    /// <summary>
    /// 입력칸을 그린다. 클릭하면 포커스를 가져간다(클릭 결과를 반환하지 않고 직접 반영하는
    /// 이유: 포커스는 이 위젯의 상태라서 호출부가 매번 옮겨줄 일이 아니다).
    /// </summary>
    public void Draw(Painter painter, InputState input, Rectangle rect, double totalSeconds)
    {
        if (input.MouseLeftPressed)
        {
            HasFocus = rect.Contains(input.MousePosition);
        }

        painter.FillRect(rect, HasFocus ? new Color(22, 30, 44) : new Color(18, 22, 32));
        painter.StrokeRect(rect, HasFocus ? new Color(120, 170, 230) : new Color(60, 74, 96));

        var textPosition = new Vector2(rect.X + 6, rect.Y + ((rect.Height - painter.Font.LineHeight) * 0.5f));
        var innerWidth = rect.Width - 12;

        if (Value.Length == 0)
        {
            painter.Text(painter.Font.Ellipsize(Placeholder, innerWidth), textPosition,
                         new Color(96, 104, 120));
        }
        else
        {
            painter.Text(painter.Font.Ellipsize(Value, innerWidth), textPosition, Color.White);
        }

        if (!HasFocus)
        {
            return;
        }

        // 커서는 0.5초 주기로 깜빡인다. 포커스가 어디 있는지 색만으로는 잘 안 보인다.
        if ((int)(totalSeconds * 2.0) % 2 == 0)
        {
            var caretX = rect.X + 6 + MathF.Min(painter.Font.Measure(Value).X, innerWidth);
            painter.FillRect(
                new Rectangle((int)caretX, rect.Y + 4, 1, rect.Height - 8), new Color(200, 220, 255));
        }
    }

    /// <summary>Esc로 포커스를 놓는 처리. 여러 입력칸이 같은 규칙을 쓰도록 여기 둔다.</summary>
    public void HandleEscape(InputState input)
    {
        if (HasFocus && input.IsKeyPressed(Keys.Escape))
        {
            HasFocus = false;
        }
    }
}
