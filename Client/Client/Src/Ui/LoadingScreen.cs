using Microsoft.Xna.Framework;
using Client.Model;

namespace Client.Ui;

/// <summary>
/// <see cref="GamePhase.Loading"/> 화면. 로그인은 통과했고 서버가 존을 배정해주기를 기다린다.
///
/// <para>
/// <b>지금은 기다릴 것이 존 배정 하나뿐이라 대개 한순간에 지나간다.</b> 그래도 화면을 따로
/// 둔 이유는 두 가지다 — ① 존이 아직 World에 안 붙어 있으면 여기서 멈추는데, 그때 "로그인은
/// 됐는데 화면이 검다"가 아니라 무엇을 기다리는지가 보여야 한다. ② 나중에 캐릭터 정보나
/// 인벤토리를 미리 받게 되면 진행 항목이 여기 줄로 늘어난다.
/// </para>
/// </summary>
public sealed class LoadingScreen
{
    private const double DotLapSeconds = 1.4;

    public void Draw(Painter painter, WorldModel world, Rectangle screen, double totalSeconds)
    {
        var panel = new Rectangle(
            screen.Center.X - 220,
            screen.Center.Y - 70,
            440,
            (painter.Font.LineHeight * 2) + (painter.SmallFont.LineHeight * 2) + 64);

        painter.Panel(panel, "접속 중");

        var x = panel.X + 20;
        var y = panel.Y + painter.Font.LineHeight + 18;

        var welcome = world.PlayerName.Length > 0
            ? $"{world.PlayerName} 님, 환영합니다."
            : "로그인에 성공했습니다.";
        painter.Text(painter.Font.Ellipsize(welcome, panel.Width - 40), new Vector2(x, y),
                     new Color(205, 218, 238));
        y += painter.Font.LineHeight + 10;

        // 점 세 개가 차례로 붙는 고전적인 표시. 존 배정은 보통 즉시라 이게 오래 보이면
        // 존 서버가 안 떠 있다는 신호다.
        var dots = new string('.', 1 + (int)(totalSeconds / DotLapSeconds % 3));
        painter.Text($"존 배정을 기다리는 중{dots}", new Vector2(x, y), new Color(235, 205, 120));
        y += painter.Font.LineHeight + 12;

        painter.SmallText($"계정 playerId: {world.AccountPlayerId}", new Vector2(x, y),
                          new Color(130, 145, 172));
        y += painter.SmallFont.LineHeight + 4;

        painter.SmallText("이 화면이 계속 보이면 ZoneServer가 World에 붙지 않은 것입니다.",
                          new Vector2(x, y), new Color(110, 124, 148));
    }
}
