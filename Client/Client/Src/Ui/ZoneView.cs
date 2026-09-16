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

    /// <summary>
    /// 몸통 스프라이트를 화면에 그릴 때의 배율. 아틀라스 프레임이 192px 인데 플레이어 마커는
    /// 지름 22px 수준이라, 아틀라스의 scale(2)로 나눈 뒤 다시 줄인다.
    /// </summary>
    private const float SpriteScale = 0.22f;

    /// <summary>
    /// 몸통 종류. <b>서버에는 외형 정보가 없다</b> — playerId 로 고르는 임시 규칙이라,
    /// 같은 사람은 항상 같은 모습으로 보이고 서버는 아무것도 몰라도 된다.
    /// 외형이 실제 데이터가 되면 이 배열이 아니라 서버가 보낸 값으로 고른다.
    /// </summary>
    private static readonly string[] BodyFrames =
        ["player_body", "player_body_viking", "player_body_bald"];

    private static readonly string[] WeaponFrames =
        ["weapon_sword", "weapon_axe", "weapon_club", "weapon_bow"];

    private static readonly string[] HeadgearFrames =
        ["armor_helmet", "armor_hat", "armor_helmet_copper", "armor_hat_blue"];

    private SpriteAtlas? atlas_;

    /// <summary>리소스를 못 읽었으면 null 이 들어오고, 그때는 예전처럼 도형으로 그린다.</summary>
    public void SetAtlas(SpriteAtlas? atlas) => atlas_ = atlas;

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
        DrawPlayers(painter, world, nowSeconds);
        painter.StrokeRect(Bounds, new Color(80, 100, 130));
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

    private void DrawPlayers(Painter painter, WorldModel world, double nowSeconds)
    {
        foreach (var player in world.Players.Values)
        {
            var isMe = player.PlayerId == world.MySessionId;
            var isStale = nowSeconds - player.LastSeenSeconds > StaleAfterSeconds;

            var body = isMe
                ? new Color(120, 220, 160)
                : isStale ? new Color(90, 100, 120) : new Color(120, 170, 240);

            var center = WorldToScreen(player.X, player.Y);
            if (atlas_ is not null)
            {
                DrawSpritePlayer(painter, atlas_, player, center, isMe, isStale);
            }
            else
            {
                painter.FillCircle(center, PlayerRadius, body);
            }

            if (atlas_ is null)
            {
                // 방향 작대기. 서버에는 dir 필드가 없어서, 직전 좌표에서 새 좌표로의 변화량으로
                // 클라이언트가 만들어낸 값이다(RemotePlayer.ApplyMove 참고).
                var tip = center + new Vector2(
                    MathF.Cos(player.DirRadians) * DirLength,
                    -MathF.Sin(player.DirRadians) * DirLength);
                painter.Line(center, tip, isMe ? Color.White : new Color(220, 230, 245), 2.5f);

                if (isMe)
                {
                    painter.FillCircle(center, 4.0f, new Color(20, 30, 26));
                }
            }

            var label = isMe ? $"나 ({player.PlayerId})" : player.PlayerId.ToString();
            var labelWidth = painter.SmallFont.Measure(label).X;
            painter.SmallText(label,
                              new Vector2(center.X - (labelWidth * 0.5f), center.Y + PlayerRadius + 3),
                              isStale ? new Color(120, 130, 145) : new Color(210, 220, 235));
        }
    }

    /// <summary>
    /// 몸통 → 무기 → 머리 장비 순서로 겹쳐 그린다(아틀라스 README 의 레이어 순서).
    ///
    /// <para>
    /// 피벗을 맞추는 방식이 층마다 다르다 — 몸통은 자기 피벗이 곧 캐릭터 좌표이고,
    /// 무기는 <b>무기 피벗을 몸통의 무기 소켓에</b>, 머리 장비는 <b>자기 피벗을 몸통의 머리
    /// 중심에</b> 맞춘다. 앵커는 아틀라스 원본 픽셀 기준이라 화면 배율을 곱해서 옮긴다.
    /// </para>
    ///
    /// <para>
    /// <b>무기만 공격 방향으로 돈다.</b> 몸통을 같이 돌리면 머리가 뒤집히고, 이 아틀라스의
    /// 몸통은 정면 한 방향뿐이다.
    /// </para>
    /// </summary>
    private void DrawSpritePlayer(Painter painter, SpriteAtlas atlas, RemotePlayer player,
                                  Vector2 center, bool isMe, bool isStale)
    {
        var scale = SpriteScale / atlas.Scale;

        // 흐려진 플레이어는 어둡게, 나는 살짝 밝게 -- 도형일 때 색으로 하던 구분을 유지한다.
        var tint = isStale ? new Color(140, 150, 165) : Color.White;

        var key = (int)(player.PlayerId % 997);
        var bodyFrame = BodyFrames[key % BodyFrames.Length];
        var weaponFrame = WeaponFrames[(key / 3) % WeaponFrames.Length];
        var headFrame = HeadgearFrames[(key / 7) % HeadgearFrames.Length];

        atlas.Draw(painter, bodyFrame, center, scale, 0.0f, tint);

        // 몸통 피벗에서 소켓까지의 거리를 화면 배율로 옮긴다. 아틀라스 앵커는 프레임 좌상단
        // 기준이고 피벗은 (96,96)이라 그 차이가 곧 오프셋이다.
        var bodyPivot = new Vector2(96.0f, 96.0f);
        var weaponOffset = (atlas.WeaponSocket - bodyPivot) * scale;
        var headOffset = (atlas.HeadAnchor - bodyPivot) * scale;

        // y 는 화면이 아래로 증가하고 월드 방향은 위로 증가라 부호를 뒤집는다.
        atlas.Draw(painter, weaponFrame, center + weaponOffset, scale, -player.DirRadians, tint);
        atlas.Draw(painter, headFrame, center + headOffset, scale, 0.0f, tint);

        if (isMe)
        {
            // 내 캐릭터임을 알리는 고리. 아틀라스에 있는 것을 쓴다.
            atlas.Draw(painter, "ui_target_ring", center, scale * 1.15f, 0.0f, new Color(120, 220, 160));
        }
    }
}
