using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Input;
using Client.Net;

namespace Client.Ui;

/// <summary>접속 버튼이 눌렸을 때 호출부가 받아가는 입력값.</summary>
public readonly record struct LoginRequest(string PlayerName, string Password);

/// <summary>
/// 첫 화면(<see cref="GamePhase.Login"/>)의 ID/PW 입력 + 응답 대기 팝업.
///
/// <para>
/// <b>서버 상태와 무관하게 뜬다.</b> 접속이 안 돼 있으면 접속 버튼만 잠기고 화면은 그대로다 —
/// 창이 안 열리는 것보다 "무엇이 안 됐는지"가 화면에 보이는 쪽이 진단에 쓸모 있다는
/// <c>ClientGame.Initialize</c>의 판단을 그대로 따른다.
/// </para>
///
/// <para>
/// <b>왜 팝업이 모달인가</b>: 응답을 기다리는 동안 접속 버튼을 다시 누르면 <c>C2WLogin</c>이
/// 두 번 나가고, 두 번째는 서버가 <c>LoginAlreadyAuthenticated</c>로 튕긴다. 그러면 먼저 온
/// 성공 뒤에 실패가 도착해 화면이 에러로 덮인다 — 입력 자체를 막는 편이 확실하다.
/// </para>
/// </summary>
public sealed class LoginScreen
{
    /// <summary>응답을 이만큼 못 받으면 실패로 돌린다(초). 팝업이 영원히 도는 것을 막는다.</summary>
    private const double ResponseTimeoutSeconds = 10.0;

    /// <summary>대기 표시(뱅글뱅글)의 점 개수와 한 바퀴 시간(초).</summary>
    private const int SpinnerDotCount = 10;
    private const double SpinnerLapSeconds = 1.1;

    private readonly TextField name_ = new("아이디 (예: tester1)", 32);
    private readonly TextField password_ = new("비밀번호 (예: 0000)", 32) { Masked = true };

    private bool waiting_;
    private double waitStartedAtSeconds_;
    private string? errorMessage_;

    private Rectangle closeButtonBounds_;

    /// <summary>응답 대기 중인가. 호출부는 이 동안 <c>C2WLogin</c>을 다시 보내면 안 된다.</summary>
    public bool IsWaiting => waiting_;

    /// <summary>입력칸 중 하나라도 포커스를 갖고 있는가.</summary>
    public bool HasFocusedField => name_.HasFocus || password_.HasFocus;

    /// <summary>로그인 화면에 처음 들어올 때 아이디 칸에 포커스를 준다.</summary>
    public void FocusName() => name_.HasFocus = true;

    /// <summary>미리 채워둘 값(실행 인자로 받은 계정). 타이핑 없이 바로 접속을 누를 수 있다.</summary>
    public void Prefill(string playerName, string password)
    {
        name_.SetValue(playerName);
        password_.SetValue(password);
    }

    /// <summary>서버가 실패로 답했다. 대기를 끝내고 에러 팝업으로 바꾼다.</summary>
    public void ShowError(string message)
    {
        waiting_ = false;
        errorMessage_ = message;
    }

    /// <summary>
    /// 화면을 그리고 입력을 처리한다. 접속이 요청되면 그 값을 돌려준다(그 즉시 대기 상태로
    /// 들어가므로, 호출부는 받은 프레임에 패킷 하나만 보내면 된다).
    /// </summary>
    public LoginRequest? UpdateAndDraw(Painter painter, InputState input, GameLink link,
                                       Rectangle screen, double totalSeconds)
    {
        DrawBackdrop(painter, screen, totalSeconds);

        var panel = new Rectangle(
            screen.Center.X - 230,
            Math.Max(40, screen.Center.Y - 150),
            460,
            (painter.Font.LineHeight * 2) + (painter.SmallFont.LineHeight * 4) + 168);

        painter.Panel(panel, "로그인");

        var connected = link.State == LinkState.Connected;
        var request = DrawForm(painter, input, link, panel, connected, totalSeconds);

        // 팝업은 폼 위에 그린다 — 아래 위젯이 이미 이번 프레임 클릭을 소비했더라도, 팝업이
        // 떠 있는 동안에는 그 결과를 무시하므로(DrawForm의 blocked) 순서가 문제되지 않는다.
        DrawPopup(painter, input, screen, totalSeconds);

        return request;
    }

    /// <summary>존 격자를 흐리게 깐 배경. 빈 화면보다 "이 클라이언트가 뭘 보여줄지"가 드러난다.</summary>
    private static void DrawBackdrop(Painter painter, Rectangle screen, double totalSeconds)
    {
        // 아주 느리게 도는 점 몇 개. 정지 화면이면 프레임이 도는지조차 알 수 없다.
        for (var i = 0; i < 24; ++i)
        {
            var angle = (totalSeconds * 0.05) + (i * Math.Tau / 24.0);
            var radius = 120.0 + (i * 9.0);
            var center = new Vector2(
                screen.Center.X + (float)(Math.Cos(angle) * radius * 1.6),
                screen.Center.Y + (float)(Math.Sin(angle) * radius * 0.7));
            painter.FillCircle(center, 2.0f, new Color(26, 34, 50));
        }
    }

    private LoginRequest? DrawForm(Painter painter, InputState input, GameLink link, Rectangle panel,
                                   bool connected, double totalSeconds)
    {
        // 팝업이 떠 있는 동안에는 폼이 입력을 받지 않는다(모달).
        var blocked = waiting_ || errorMessage_ is not null;

        var x = panel.X + 20;
        var width = panel.Width - 40;
        var y = panel.Y + painter.Font.LineHeight + 22;
        var rowHeight = painter.Font.LineHeight + 12;

        painter.SmallText("아이디", new Vector2(x, y), new Color(130, 145, 172));
        y += painter.SmallFont.LineHeight + 4;
        name_.Draw(painter, blocked ? InputState.Blocked : input, new Rectangle(x, y, width, rowHeight), totalSeconds);
        y += rowHeight + 12;

        painter.SmallText("비밀번호", new Vector2(x, y), new Color(130, 145, 172));
        y += painter.SmallFont.LineHeight + 4;
        password_.Draw(painter, blocked ? InputState.Blocked : input, new Rectangle(x, y, width, rowHeight), totalSeconds);
        y += rowHeight + 16;

        var submitted = false;
        if (!blocked)
        {
            // Enter: 아이디 칸에서는 비밀번호로 넘어가고, 비밀번호 칸에서는 바로 접속한다.
            if (name_.Update(input))
            {
                name_.HasFocus = false;
                password_.HasFocus = true;
            }

            if (password_.Update(input))
            {
                submitted = true;
            }

            // Tab으로도 칸을 옮긴다. TextField가 Tab을 문자로 넣지 않으므로 여기서 처리한다.
            if (input.IsKeyPressed(Keys.Tab))
            {
                (name_.HasFocus, password_.HasFocus) = (password_.HasFocus, name_.HasFocus);
            }
        }

        var canSubmit = connected && !blocked
                        && name_.Value.Length > 0 && password_.Value.Length > 0;

        if (Widgets.Button(painter, blocked ? InputState.Blocked : input,
                           new Rectangle(x, y, width, painter.Font.LineHeight + 16),
                           connected ? "접속" : "서버에 연결되어 있지 않습니다", canSubmit))
        {
            submitted = true;
        }

        y += painter.Font.LineHeight + 16 + 12;

        var (hint, hintColor) = link.State switch
        {
            LinkState.Connected => ($"{link.Host}:{link.Port} 연결됨", new Color(140, 225, 170)),
            LinkState.Connecting => ($"{link.Host}:{link.Port} 접속 중...", new Color(235, 205, 120)),
            _ => ($"{link.Host}:{link.Port} 접속 실패 — 서버를 먼저 띄우세요", new Color(240, 130, 120)),
        };
        painter.SmallText(painter.SmallFont.Ellipsize(hint, width), new Vector2(x, y), hintColor);
        y += painter.SmallFont.LineHeight + 6;

        painter.SmallText(
            painter.SmallFont.Ellipsize("계정이 없으면 서버가 그 자리에서 만듭니다(자동 가입).", width),
            new Vector2(x, y), new Color(110, 124, 148));

        if (!submitted || !canSubmit)
        {
            return null;
        }

        waiting_ = true;
        waitStartedAtSeconds_ = totalSeconds;
        return new LoginRequest(name_.Value, password_.Value);
    }

    private void DrawPopup(Painter painter, InputState input, Rectangle screen, double totalSeconds)
    {
        if (!waiting_ && errorMessage_ is null)
        {
            closeButtonBounds_ = Rectangle.Empty;
            return;
        }

        // 응답이 영영 안 오는 경우(서버가 DB에서 막혔거나 연결이 조용히 끊김)를 실패로 돌린다.
        if (waiting_ && totalSeconds - waitStartedAtSeconds_ > ResponseTimeoutSeconds)
        {
            ShowError($"서버 응답이 {ResponseTimeoutSeconds:0}초 동안 없습니다. 다시 시도하세요.");
        }

        painter.FillRect(screen, new Color(0, 0, 0, 150));

        var isError = errorMessage_ is not null;
        var message = errorMessage_ ?? "로그인 중입니다...";
        var messageWidth = Math.Min(screen.Width - 80, 420);

        var height = painter.Font.LineHeight + 48 + (isError ? painter.Font.LineHeight + 28 : 56);
        var popup = new Rectangle(
            screen.Center.X - ((messageWidth + 56) / 2),
            screen.Center.Y - (height / 2),
            messageWidth + 56,
            height);

        painter.Panel(popup, isError ? "로그인 실패" : "잠시만 기다려 주세요");

        var contentY = popup.Y + painter.Font.LineHeight + 20;

        if (!isError)
        {
            DrawSpinner(painter, new Vector2(popup.Center.X, contentY + 18), totalSeconds);
            contentY += 46;
        }

        var fitted = painter.Font.Ellipsize(message, messageWidth);
        painter.Text(fitted,
                     new Vector2(popup.Center.X - (painter.Font.Measure(fitted).X * 0.5f), contentY),
                     isError ? new Color(245, 175, 165) : new Color(205, 218, 238));

        if (!isError)
        {
            closeButtonBounds_ = Rectangle.Empty;
            return;
        }

        closeButtonBounds_ = new Rectangle(
            popup.Center.X - 50,
            popup.Bottom - painter.Font.LineHeight - 24,
            100,
            painter.Font.LineHeight + 14);

        if (Widgets.Button(painter, input, closeButtonBounds_, "닫기"))
        {
            errorMessage_ = null;
            password_.Clear();
            password_.HasFocus = true;
        }
    }

    /// <summary>대기 표시. 점을 원형으로 늘어놓고 밝기를 한 바퀴 돌린다.</summary>
    private static void DrawSpinner(Painter painter, Vector2 center, double totalSeconds)
    {
        var phase = totalSeconds / SpinnerLapSeconds % 1.0;

        for (var i = 0; i < SpinnerDotCount; ++i)
        {
            var angle = i * Math.Tau / SpinnerDotCount;

            // 이 점이 "머리"에서 얼마나 뒤처져 있는지(0~1). 머리가 가장 밝고 꼬리로 갈수록 어둡다.
            var lag = (i / (double)SpinnerDotCount) - phase;
            lag -= Math.Floor(lag);
            var brightness = (float)(1.0 - lag);

            painter.FillCircle(
                center + new Vector2((float)Math.Cos(angle) * 18.0f, (float)Math.Sin(angle) * 18.0f),
                2.0f + (brightness * 1.6f),
                new Color(120, 170, 235) * (0.25f + (brightness * 0.75f)));
        }
    }
}
