using Microsoft.Xna.Framework;
using Client.Model;
using Client.Net;

namespace Client.Ui;

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

    /// <param name="autoTour">
    /// 자동 순회가 켜져 있으면 지금까지의 존 전환 횟수, 꺼져 있으면 <c>null</c>.
    /// 횟수를 보여주는 이유: 화면을 잠깐만 봐도 핸드오프가 계속 돌고 있는지 판단할 수 있다.
    /// </param>
    /// <param name="autoTourReverse">자동 순회가 반대 방향인가. 창 여러 개를 구분하는 데 쓴다.</param>
    public void DrawStatusBar(Painter painter, GameLink link, WorldModel world, Rectangle bounds,
                              int? autoTour = null, bool autoTourReverse = false)
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
            x = DrawField(painter, "playerId", world.MySessionId.ToString(), Color.White, x, y);

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

        // 자동 순회는 켜져 있을 때만 자리를 쓴다 -- 평소엔 상태줄을 좁히지 않는다.
        if (autoTour is { } tour)
        {
            var direction = autoTourReverse ? "반시계" : "시계";
            x = DrawField(painter, "자동 순회", $"{direction}  존 전환 {tour}회",
                          new Color(150, 230, 180), x, y);
        }

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

    /// <summary>
    /// 화면 아래 가운데의 전투 패널 — 내 HP/MP, 든 무기, 고른 대상.
    ///
    /// <para>
    /// 상태 줄(위쪽)에 끼워 넣지 않고 따로 둔 이유: 위쪽은 "서버와 통신이 되고 있는가"를 보는
    /// 자리이고 여기는 "지금 싸움이 어떻게 되고 있는가"를 보는 자리다. 둘이 섞이면 서버가
    /// 이상한 것인지 내가 죽은 것인지 한눈에 안 갈린다.
    /// </para>
    /// </summary>
    public void DrawCombatBar(Painter painter, WorldModel world, Rectangle screen, double nowSeconds)
    {
        if (world.MyUnit is not { } me)
        {
            return;
        }

        const int Width = 280;
        const int BarHeight = 12;

        var rect = new Rectangle(screen.Center.X - (Width / 2), screen.Bottom - 132, Width,
                                 (BarHeight * 2) + painter.SmallFont.LineHeight + 22);
        painter.FillRect(rect, new Color(12, 16, 24, 225));
        painter.StrokeRect(rect, new Color(60, 76, 100));

        var x = rect.X + 8;
        var barWidth = Width - 16;

        DrawStatBar(painter, new Rectangle(x, rect.Y + 6, barWidth, BarHeight), me.Hp, me.MaxHp,
                    CombatArt.HealthColor(me.MaxHp > 0 ? me.Hp / (float)me.MaxHp : 0.0f), "HP");
        DrawStatBar(painter, new Rectangle(x, rect.Y + 8 + BarHeight, barWidth, BarHeight), me.Mp, me.MaxMp,
                    new Color(110, 160, 235), "MP");

        var weaponText = world.Weapon == AttackKind.Melee ? "근접 (1)" : "원거리 (2)";
        var targetText = world.TargetUnitId != 0 && world.Units.TryGetValue(world.TargetUnitId, out var target)
            ? $"대상 {target.UnitId}  {target.Hp}/{target.MaxHp}"
            : "대상 없음 (Tab 또는 클릭)";

        painter.SmallText($"{weaponText}   ·   {targetText}   ·   Space 공격",
                          new Vector2(x, rect.Y + 12 + (BarHeight * 2)), new Color(150, 166, 194));

        DrawAttackFailureToast(painter, world, screen, nowSeconds);
    }

    private static void DrawStatBar(Painter painter, Rectangle bounds, int value, int max, Color fill,
                                    string label)
    {
        painter.FillRect(bounds, new Color(24, 30, 42));

        if (max > 0 && value > 0)
        {
            var width = (int)(bounds.Width * Math.Clamp(value / (float)max, 0.0f, 1.0f));
            painter.FillRect(new Rectangle(bounds.X, bounds.Y, width, bounds.Height), fill);
        }

        var text = $"{label} {value}/{max}";
        painter.SmallText(text, new Vector2(bounds.X + 6, bounds.Y - 1), new Color(235, 240, 250));
    }

    /// <summary>
    /// 공격이 왜 안 먹혔는지. <b>서버가 실패해도 반드시 응답을 주기 때문에</b> 띄울 수 있는
    /// 것이고, 그 규약이 없으면 화면은 "아무 일도 안 일어남"만 보여준다.
    /// </summary>
    private static void DrawAttackFailureToast(Painter painter, WorldModel world, Rectangle screen,
                                               double nowSeconds)
    {
        const double DurationSeconds = 1.6;

        if (world.LastAttackFailure is not { } reason)
        {
            return;
        }

        var age = nowSeconds - world.LastAttackFailureAtSeconds;
        if (age > DurationSeconds)
        {
            return;
        }

        var fade = (float)Math.Clamp(DurationSeconds - age, 0.0, 1.0);
        var size = painter.SmallFont.Measure(reason);
        var position = new Vector2(screen.Center.X - (size.X * 0.5f), screen.Bottom - 152);
        painter.SmallText(reason, position, new Color(240, 160, 150) * fade);
    }

    public void DrawHelpBar(Painter painter, Rectangle bounds, string fontName)
    {
        painter.FillRect(bounds, new Color(12, 15, 22, 245));
        painter.FillRect(new Rectangle(bounds.X, bounds.Y, bounds.Width, 1), new Color(50, 62, 84));

        const string Help =
            "WASD 이동  ·  빈 곳 클릭 순간 이동  ·  유닛 클릭/Tab 타겟  ·  Space 공격  ·  1/2 무기  ·  Enter 채팅  ·  F1 핑  ·  F2 자동 순회";
        painter.SmallText(Help, new Vector2(bounds.X + 12, bounds.Y + 5), new Color(130, 144, 168));

        var fontText = $"글꼴: {fontName}";
        var width = painter.SmallFont.Measure(fontText).X;
        painter.SmallText(fontText, new Vector2(bounds.Right - width - 12, bounds.Y + 5),
                          new Color(90, 102, 124));
    }
}
