using Microsoft.Xna.Framework;
using Client.Model;
using Client.Protocol;

namespace Client.Ui;

/// <summary>
/// 존 격자와 플레이어를 그리는 메인 뷰.
///
/// <para>
/// 이 화면의 목적은 <b>존 경계에서 실제로 핸드오프가 일어나는지 눈으로 보는 것</b>이다.
/// 그래서 경계선을 다른 색으로 굵게 긋고, 내가 지금 어느 존에 배정돼 있는지를 존 배경색으로
/// 표시한다 — 경계를 넘는 순간 배경색이 옆 칸으로 옮겨가면 그게 핸드오프가 성공한 것이다.
/// 클라이언트는 재접속도 재인증도 하지 않는다(같은 TCP 연결을 그대로 쓴다).
/// </para>
/// </summary>
public sealed class ZoneView
{
    /// <summary>플레이어 마커 반지름(픽셀).</summary>
    private const float PlayerRadius = 11.0f;

    /// <summary>방향 작대기 길이(픽셀). 반지름보다 길어야 방향이 한눈에 보인다.</summary>
    private const float DirLength = 20.0f;

    /// <summary>이 시간(초) 이상 이동 통지가 없는 플레이어는 흐리게 그린다.</summary>
    private const double StaleAfterSeconds = 5.0;

    /// <summary>대상 클릭 판정 반경(픽셀). 마커보다 넉넉하다 — <see cref="HitTestUnit"/> 주석 참고.</summary>
    private const float TargetPickRadius = 22.0f;

    public Rectangle Bounds { get; private set; }

    public void Layout(Rectangle bounds) => Bounds = bounds;

    /// <summary>월드 좌표를 화면 픽셀로. y는 위아래를 뒤집는다(월드는 위로 증가).</summary>
    public Vector2 WorldToScreen(float x, float y)
    {
        var normalizedX = x / ZoneLayout.WorldMaxX;
        var normalizedY = y / ZoneLayout.WorldMaxY;
        return new Vector2(
            Bounds.X + (normalizedX * Bounds.Width),
            Bounds.Y + ((1.0f - normalizedY) * Bounds.Height));
    }

    /// <summary>화면 픽셀을 월드 좌표로. 존 뷰 클릭으로 이동 목표를 찍는 데 쓴다.</summary>
    public Vector2 ScreenToWorld(Point screen)
    {
        var normalizedX = (screen.X - Bounds.X) / (float)Bounds.Width;
        var normalizedY = 1.0f - ((screen.Y - Bounds.Y) / (float)Bounds.Height);
        return new Vector2(
            normalizedX * ZoneLayout.WorldMaxX,
            normalizedY * ZoneLayout.WorldMaxY);
    }

    public void Draw(Painter painter, WorldModel world, double nowSeconds)
    {
        DrawZones(painter, world);
        DrawGrid(painter);
        DrawRequestedMarker(painter, world);
        DrawUnits(painter, world, nowSeconds);
        DrawPlayersWithoutUnit(painter, world, nowSeconds);
        DrawArrows(painter, world, nowSeconds);
        DrawDamagePopups(painter, world, nowSeconds);
        painter.StrokeRect(Bounds, new Color(80, 100, 130));
    }

    /// <summary>
    /// 클릭 지점에 있는 유닛의 unitId. 없으면 0.
    ///
    /// <para>
    /// 판정 반경을 마커보다 넉넉하게 잡는다 — 이 화면은 존 4개를 한 화면에 욱여넣은 상태라
    /// 유닛이 작게 그려지고, 정확히 원 안을 찍게 하면 타겟팅이 짜증난다. 카메라가 플레이어를
    /// 따라가게 바뀌면 유닛이 커지므로 그때 이 여유를 줄이면 된다.
    /// </para>
    /// </summary>
    public uint HitTestUnit(WorldModel world, Point screen, double nowSeconds)
    {
        var bestUnitId = 0u;
        var bestDistanceSquared = float.MaxValue;
        var pick = new Vector2(screen.X, screen.Y);

        foreach (var unit in world.Units.Values)
        {
            // 내 자신은 고르지 않는다. 지금은 PvP가 없어서 골라 봐야 SameKind로 튕긴다.
            if (unit.UnitId == world.MySessionId)
            {
                continue;
            }

            var (worldX, worldY) = world.PositionOf(unit);
            var center = WorldToScreen(worldX, worldY);
            var distanceSquared = Vector2.DistanceSquared(center, pick);

            if (distanceSquared < bestDistanceSquared && distanceSquared <= TargetPickRadius * TargetPickRadius)
            {
                bestDistanceSquared = distanceSquared;
                bestUnitId = unit.UnitId;
            }
        }

        return bestUnitId;
    }

    private void DrawZones(Painter painter, WorldModel world)
    {
        for (var zoneId = ZoneLayout.FirstZoneId; zoneId <= ZoneLayout.LastZoneId; ++zoneId)
        {
            var topLeft = WorldToScreen(ZoneLayout.MinXOf(zoneId), ZoneLayout.MaxYOf(zoneId));
            var bottomRight = WorldToScreen(ZoneLayout.MaxXOf(zoneId), ZoneLayout.MinYOf(zoneId));
            var rect = new Rectangle(
                (int)topLeft.X, (int)topLeft.Y,
                (int)(bottomRight.X - topLeft.X), (int)(bottomRight.Y - topLeft.Y));

            var isMine = world.HasEnteredZone && world.MyZoneId == zoneId;
            painter.FillRect(rect, isMine ? new Color(24, 40, 34) : new Color(20, 24, 34));

            var label = $"Zone {zoneId}";
            painter.Text(label, new Vector2(rect.X + 10, rect.Y + 8),
                         isMine ? new Color(150, 230, 180) : new Color(110, 122, 145));

            // 담당 사각형과 어느 프로세스가 호스팅하는지를 같이 적는다 -- 세로 경계를 넘는 것이
            // 프로세스를 넘는 핸드오프라는 걸 화면에서 바로 읽을 수 있어야 한다.
            var (_, row) = ZoneLayout.CellOf(zoneId);
            var hostLabel = $"x:[{ZoneLayout.MinXOf(zoneId):0}, {ZoneLayout.MaxXOf(zoneId):0})  "
                            + $"y:[{ZoneLayout.MinYOf(zoneId):0}, {ZoneLayout.MaxYOf(zoneId):0})  ·  "
                            + $"ZoneServer #{row + 1}";
            painter.SmallText(hostLabel, new Vector2(rect.X + 10, rect.Y + 8 + painter.Font.LineHeight + 2),
                              new Color(96, 106, 128));

            if (isMine)
            {
                painter.SmallText("내가 배정된 존 (이 존의 BASIC 스레드가 내 상태를 소유한다)",
                                  new Vector2(rect.X + 10, rect.Y + 8 + painter.Font.LineHeight
                                                           + painter.SmallFont.LineHeight + 4),
                                  new Color(96, 150, 118));
            }
        }

        DrawZoneBoundaries(painter);
    }

    /// <summary>
    /// 존 사이 경계선. 이 선을 넘는 이동이 곧 핸드오프 요청(Z2WZoneTransfer)이다.
    ///
    /// <para>
    /// 존 사각형마다 그리지 않고 따로 그리는 이유: 격자가 되면서 한 경계선이 존 두 개에
    /// 공유되기 때문이다. 존별로 그리면 같은 선을 두 번 긋게 되고, 굵기가 있는 선이라
    /// 겹친 부분만 진해 보인다.
    /// </para>
    ///
    /// <para>
    /// <b>세로선과 가로선의 색을 다르게 한다.</b> 가로 이동(존 1↔2)은 같은 프로세스 안의
    /// BASIC 스레드 간 이동이고, 세로 이동(존 1↔3)은 프로세스(TCP 링크)를 넘는다 — 눈으로
    /// 구분되지 않으면 "무엇을 확인했는지"가 흐려진다.
    /// </para>
    /// </summary>
    private void DrawZoneBoundaries(Painter painter)
    {
        var threadColor = new Color(210, 150, 70);
        var processColor = new Color(120, 180, 235);

        for (var column = 1; column < ZoneLayout.ZonesPerRow; ++column)
        {
            var x = column * ZoneLayout.ZoneSize;
            painter.Line(WorldToScreen(x, ZoneLayout.WorldMaxY), WorldToScreen(x, 0.0f), threadColor, 2.0f);
            painter.SmallText("핸드오프 경계 (같은 프로세스, 스레드만 다름)",
                              WorldToScreen(x, 0.0f) + new Vector2(6, -18), threadColor);
        }

        for (var row = 1; row < ZoneLayout.ZoneRows; ++row)
        {
            var y = row * ZoneLayout.ZoneSize;
            painter.Line(WorldToScreen(0.0f, y), WorldToScreen(ZoneLayout.WorldMaxX, y), processColor, 2.0f);
            painter.SmallText("핸드오프 경계 (프로세스를 넘는다)",
                              WorldToScreen(0.0f, y) + new Vector2(6, 4), processColor);
        }
    }

    private void DrawGrid(Painter painter)
    {
        var gridColor = new Color(255, 255, 255, 14);

        // 존 경계는 DrawZoneBoundaries가 굵게 그리므로 여기서는 건너뛴다.
        for (var x = 1.0f; x < ZoneLayout.WorldMaxX; x += 1.0f)
        {
            if (MathF.Abs(x % ZoneLayout.ZoneSize) < 0.001f)
            {
                continue;
            }

            painter.Line(WorldToScreen(x, 0.0f), WorldToScreen(x, ZoneLayout.WorldMaxY), gridColor);
        }

        for (var y = 1.0f; y < ZoneLayout.WorldMaxY; y += 1.0f)
        {
            if (MathF.Abs(y % ZoneLayout.ZoneSize) < 0.001f)
            {
                continue;
            }

            painter.Line(WorldToScreen(0.0f, y), WorldToScreen(ZoneLayout.WorldMaxX, y), gridColor);
        }
    }

    /// <summary>
    /// 내가 서버에 보낸 목표 좌표. 서버가 확정해 되돌려준 위치(<see cref="DrawPlayers"/>)와
    /// 어긋나 있으면 그 사이가 곧 왕복 지연이고, 경계를 넘었을 때는 핸드오프가 도는 동안
    /// 잠깐 벌어진다 — 두 개를 같이 보여주는 게 이 뷰의 핵심이다.
    /// </summary>
    private void DrawRequestedMarker(Painter painter, WorldModel world)
    {
        if (!world.HasEnteredZone)
        {
            return;
        }

        var center = WorldToScreen(world.RequestedX, world.RequestedY);
        painter.FillCircle(center, 4.0f, new Color(240, 200, 120, 160));
    }

    /// <summary>
    /// 전투 유닛(플레이어 + 몬스터). <b>몸통은 정면 한 장이고 무기만 방향대로 돈다</b> —
    /// 서버에 방향 값이 없어서 그 각도는 클라이언트가 만들어낸 값이다(이동 방향, 공격 중에는
    /// 대상 방향).
    /// </summary>
    private void DrawUnits(Painter painter, WorldModel world, double nowSeconds)
    {
        // 선택한 대상의 링을 먼저 깐다 — 유닛보다 뒤에 있어야 몸통을 가리지 않는다.
        if (world.TargetUnitId != 0 && world.Units.TryGetValue(world.TargetUnitId, out var target))
        {
            var (targetX, targetY) = world.PositionOf(target);
            CombatArt.DrawTargetRing(painter, WorldToScreen(targetX, targetY), PlayerRadius, nowSeconds);
        }

        foreach (var unit in world.Units.Values)
        {
            var (worldX, worldY) = world.PositionOf(unit);
            var center = WorldToScreen(worldX, worldY);
            var isMine = unit.UnitId == world.MySessionId;

            CombatArt.DrawBody(painter, center, PlayerRadius, unit.Kind, unit.IsDead, isMine,
                               nowSeconds, unit.HitAtSeconds);

            if (!unit.IsDead)
            {
                // 몬스터만 투구를 쓴다. 스탯에 DEF가 있다는 것을 그림으로 말해주는 자리다.
                if (unit.Kind == UnitKind.Monster)
                {
                    CombatArt.DrawHelmet(painter, center, PlayerRadius);
                }

                DrawUnitWeapon(painter, world, unit, center, nowSeconds);
            }

            CombatArt.DrawHealthBar(painter, center, PlayerRadius, unit);
            DrawUnitLabel(painter, world, unit, center, nowSeconds);
        }
    }

    private void DrawUnitWeapon(Painter painter, WorldModel world, CombatUnit unit, Vector2 center,
                                double nowSeconds)
    {
        var weapon = unit.Kind == UnitKind.Monster ? AttackKind.Melee : unit.LastAttackKind;
        var swingSeconds = weapon == AttackKind.Melee ? CombatArt.SwingSeconds : CombatArt.DrawSeconds;
        var elapsed = nowSeconds - unit.AttackAtSeconds;
        var swingProgress = elapsed >= swingSeconds ? 1.0f : (float)(elapsed / swingSeconds);

        // 공격 중이면 대상 쪽, 아니면 걸어가는 쪽을 본다. 내가 지금 든 무기는 서버가 모르는
        // 클라이언트 상태라, 아직 한 번도 안 때린 내 캐릭터에는 그 선택을 그대로 보여준다.
        var facing = unit.WeaponRadians;
        if (swingProgress >= 1.0f && world.Players.TryGetValue(unit.UnitId, out var player))
        {
            facing = player.DirRadians;
        }

        if (unit.UnitId == world.MySessionId && swingProgress >= 1.0f)
        {
            weapon = world.Weapon;
        }

        CombatArt.DrawWeapon(painter, center, PlayerRadius, weapon, facing, swingProgress);
    }

    private void DrawUnitLabel(Painter painter, WorldModel world, CombatUnit unit, Vector2 center,
                               double nowSeconds)
    {
        var isMine = unit.UnitId == world.MySessionId;
        var isStale = world.Players.TryGetValue(unit.UnitId, out var player)
                      && !isMine
                      && nowSeconds - player.LastSeenSeconds > StaleAfterSeconds;

        var label = unit.Kind switch
        {
            UnitKind.Monster => $"몬스터 {unit.Hp}/{unit.MaxHp}",
            _ => isMine ? $"나 ({unit.UnitId})" : unit.UnitId.ToString(),
        };

        var labelWidth = painter.SmallFont.Measure(label).X;
        painter.SmallText(label,
                          new Vector2(center.X - (labelWidth * 0.5f), center.Y + PlayerRadius + 3),
                          isStale ? new Color(120, 130, 145) : new Color(210, 220, 235));
    }

    /// <summary>
    /// 유닛 정보가 아직 안 온 플레이어. 존에 막 들어왔을 때 이동 통지가 등장 통지보다 먼저
    /// 도착할 수 있고, 그 사이 화면에서 사라지면 "핸드오프가 됐나"를 눈으로 쫓을 수 없다 —
    /// <b>이 화면의 존재 이유가 그것이라</b> 전투 정보 없이도 일단 그린다.
    /// </summary>
    private void DrawPlayersWithoutUnit(Painter painter, WorldModel world, double nowSeconds)
    {
        foreach (var player in world.Players.Values)
        {
            if (world.Units.ContainsKey(player.PlayerId))
            {
                continue;
            }

            var isStale = nowSeconds - player.LastSeenSeconds > StaleAfterSeconds;
            var center = WorldToScreen(player.X, player.Y);
            painter.FillCircle(center, PlayerRadius, isStale ? new Color(90, 100, 120) : new Color(120, 170, 240));

            var tip = center + new Vector2(
                MathF.Cos(player.DirRadians) * DirLength,
                -MathF.Sin(player.DirRadians) * DirLength);
            painter.Line(center, tip, new Color(220, 230, 245), 2.5f);
        }
    }

    private void DrawArrows(Painter painter, WorldModel world, double nowSeconds)
    {
        foreach (var arrow in world.Arrows)
        {
            var progress = arrow.Progress(nowSeconds);
            var position = WorldToScreen(
                arrow.FromX + ((arrow.ToX - arrow.FromX) * progress),
                arrow.FromY + ((arrow.ToY - arrow.FromY) * progress));

            var from = WorldToScreen(arrow.FromX, arrow.FromY);
            var to = WorldToScreen(arrow.ToX, arrow.ToY);
            CombatArt.DrawArrow(painter, position, MathF.Atan2(-(to.Y - from.Y), to.X - from.X));
        }
    }

    private void DrawDamagePopups(Painter painter, WorldModel world, double nowSeconds)
    {
        foreach (var popup in world.DamagePopups)
        {
            var progress = popup.Progress(nowSeconds);
            var center = WorldToScreen(popup.X, popup.Y);

            // 위로 떠오르면서 사라진다. 겹쳐도 시간차로 갈라져 보인다.
            var position = new Vector2(center.X, center.Y - PlayerRadius - 14 - (progress * 22.0f));
            var alpha = (byte)(255 * (1.0f - (progress * progress)));
            var color = popup.IsMine
                ? new Color((byte)255, (byte)236, (byte)150, alpha)
                : new Color((byte)255, (byte)150, (byte)140, alpha);

            var text = popup.Damage.ToString();
            var width = painter.Font.Measure(text).X;
            painter.Text(text, new Vector2(position.X - (width * 0.5f), position.Y), color);
        }
    }
}
