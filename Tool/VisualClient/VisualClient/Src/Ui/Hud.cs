using Microsoft.Xna.Framework;
using VisualClient.Model;
using VisualClient.Net;

namespace VisualClient.Ui;

/// <summary>
/// 화면 위쪽 상태 줄과 공지 토스트, 아래쪽 조작 안내.
///
/// <para>
/// 상태 줄에 링크 상태와 에러 메시지를 그대로 노출하는 이유: 이 도구를 쓰는 상황은 대개
/// "서버가 이상하다"를 확인하는 자리다. 창은 열렸는데 아무 반응이 없을 때, 접속이 안 된
/// 것인지 존 입장을 못 한 것인지 화면에서 바로 갈라져야 한다.
/// </para>
/// </summary>
public sealed class Hud
{
    /// <summary>공지 토스트가 화면에 남아 있는 시간(초).</summary>
    private const double NoticeDurationSeconds = 8.0;

    public void DrawStatusBar(Painter painter, GameLink link, WorldModel world, Rectangle bounds)
    {
        painter.FillRect(bounds, new Color(16, 20, 30, 245));
        painter.FillRect(new Rectangle(bounds.X, bounds.Bottom - 1, bounds.Width, 1), new Color(60, 76, 100));

        var x = bounds.X + 12.0f;
        var y = bounds.Y + ((bounds.Height - painter.Font.LineHeight) * 0.5f);

        var (stateText, stateColor) = link.State switch
        {
            LinkState.Connected => ($"연결됨 {link.Host}:{link.Port}", new Color(140, 225, 170)),
            LinkState.Connecting => ("접속 중...", new Color(235, 205, 120)),
            LinkState.Failed => ("접속 실패", new Color(240, 130, 120)),
            _ => ("연결 끊김", new Color(240, 130, 120)),
        };

        x = DrawField(painter, "Gateway", stateText, stateColor, x, y);

        if (world.HasEnteredZone)
        {
            x = DrawField(painter, "Zone", world.MyZoneId.ToString(), new Color(150, 230, 180), x, y);
            x = DrawField(painter, "playerId", world.MyPlayerId.ToString(), Color.White, x, y);

            var me = world.Me;
            var positionText = me is null
                ? "(이동 통지 대기)"
                : $"{me.X:0.00}, {me.Y:0.00}";
            x = DrawField(painter, "서버 확정 좌표", positionText, new Color(205, 218, 238), x, y);
            x = DrawField(painter, "요청 좌표",
                          $"{world.RequestedX:0.00}, {world.RequestedY:0.00}",
                          new Color(240, 200, 120), x, y);
        }
        else
        {
            x = DrawField(painter, "Zone", "입장 대기 (World가 존을 배정하면 표시됩니다)",
                          new Color(235, 205, 120), x, y);
        }

        var rttText = world.RttMs is { } rtt ? $"{rtt:0.0} ms" : "측정 중";
        x = DrawField(painter, "Echo RTT", rttText, new Color(180, 200, 235), x, y);

        // 접속 실패/끊김 사유는 잘려도 좋으니 남은 자리에 그대로 붙인다.
        if (link.LastError.Length > 0)
        {
            var remaining = bounds.Right - x - 12.0f;
            if (remaining > 60.0f)
            {
                painter.SmallText(painter.SmallFont.Ellipsize($"({link.LastError})", remaining),
                                  new Vector2(x, bounds.Y + ((bounds.Height - painter.SmallFont.LineHeight) * 0.5f)),
                                  new Color(215, 140, 130));
            }
        }
    }

    private static float DrawField(Painter painter, string label, string value, Color valueColor,
                                   float x, float y)
    {
        painter.SmallText(label, new Vector2(x, y + 2), new Color(110, 124, 148));
        x += painter.SmallFont.Measure(label).X + 6.0f;

        painter.Text(value, new Vector2(x, y), valueColor);
        x += painter.Font.Measure(value).X + 18.0f;

        return x;
    }

    /// <summary>운영툴이 보낸 공지를 화면 위쪽 가운데에 잠깐 띄운다.</summary>
    public void DrawNoticeToast(Painter painter, WorldModel world, Rectangle screen, double nowSeconds)
    {
        if (world.LastNotice is not { } notice)
        {
            return;
        }

        var age = nowSeconds - world.LastNoticeAtSeconds;
        if (age > NoticeDurationSeconds)
        {
            return;
        }

        // 마지막 1초 동안 서서히 사라지게 한다. 알파를 곱한 색이므로 미리 곱해진 규약과 맞다.
        var fade = (float)Math.Clamp(NoticeDurationSeconds - age, 0.0, 1.0);

        var text = $"[운영툴 공지] {notice}";
        var textSize = painter.Font.Measure(text);
        var rect = new Rectangle(
            (int)(screen.Center.X - ((textSize.X + 32) * 0.5f)),
            screen.Y + 46,
            (int)textSize.X + 32,
            painter.Font.LineHeight + 16);

        painter.FillRect(rect, new Color(90, 60, 16, 235) * fade);
        painter.StrokeRect(rect, new Color(230, 180, 90) * fade);
        painter.Text(text, new Vector2(rect.X + 16, rect.Y + 8), new Color(255, 225, 150) * fade);
    }

    public void DrawHelpBar(Painter painter, Rectangle bounds, string fontName)
    {
        painter.FillRect(bounds, new Color(12, 15, 22, 245));
        painter.FillRect(new Rectangle(bounds.X, bounds.Y, bounds.Width, 1), new Color(50, 62, 84));

        const string Help =
            "WASD/방향키 이동  ·  존 뷰 클릭으로 순간 이동  ·  Enter 채팅 입력  ·  F1 Echo 핑  ·  Esc 입력/패널 닫기";
        painter.SmallText(Help, new Vector2(bounds.X + 12, bounds.Y + 5), new Color(130, 144, 168));

        var fontText = $"글꼴: {fontName}";
        var width = painter.SmallFont.Measure(fontText).X;
        painter.SmallText(fontText, new Vector2(bounds.Right - width - 12, bounds.Y + 5),
                          new Color(90, 102, 124));
    }
}
