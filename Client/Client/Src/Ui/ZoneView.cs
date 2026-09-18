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
    /// 캐릭터 몸통의 월드 크기(unit). 리소스 요청서 §1의 "플레이어 몸통 0.75 × 0.75 unit".
    /// </summary>
    private const float BodyUnits = 2.5f;

    /// <summary>
    /// 몸통 프레임의 한 변(px). 2배 납품이라 192 이고, 화면 배율은 이 값에서 역산한다.
    /// </summary>
    private const float BodyFramePixels = 192.0f;

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

    /// <summary>
    /// 화면 가로에 담는 월드 폭(unit). **주변 사람이 몇 명이나 보이는지가 이 값으로 정해진다** --
    /// 좁히면 움직임은 커 보이지만 화면에 사람이 거의 안 남고, 넓히면 반대가 된다.
    /// 값을 코드에 박지 않고 실행 중에 바꾸는 이유가 그것이다(휠 / +,-).
    /// </summary>
    private float viewSpan_ = ZoneLayout.ZoneSize;

    /// <summary>
    /// 자주 쓰는 시야 몇 가지. 마지막은 월드 전체라, 캐릭터를 따라가면서 존 배치를 같이 볼 때 쓴다.
    /// </summary>
    private static readonly float[] ViewSpanPresets =
    [
        ZoneLayout.ZoneSize * 0.25f,
        ZoneLayout.ZoneSize * 0.5f,
        ZoneLayout.ZoneSize,
        ZoneLayout.WorldMaxX,
    ];

    private int presetIndex_ = 2;

    public void CycleViewSpan()
    {
        presetIndex_ = (presetIndex_ + 1) % ViewSpanPresets.Length;
        viewSpan_ = ViewSpanPresets[presetIndex_];
    }

    /// <summary>월드 전체(존 전부)를 한 화면에 보는 모드. 내 캐릭터를 따라가지 않는다.</summary>
    /// <remarks>
    /// 기본값이 켜짐인 이유: 이 클라이언트는 서버를 관찰하는 도구라, 존 배치와 인구가
    /// 한눈에 보이는 쪽이 기본이어야 한다. 캐릭터 중심은 F3 으로 켠다.
    /// </remarks>
    public bool WorldView { get; set; } = true;

    /// <summary>지금 화면 가로에 담기는 월드 폭. HUD 표시에도 쓴다.</summary>
    public float ViewSpan => WorldView ? ZoneLayout.WorldMaxX : viewSpan_;

    /// <summary>시야를 배율로 조절한다. 한 존의 1/10 ~ 월드 전체 사이로 자른다.</summary>
    public void ZoomBy(float factor) =>
        viewSpan_ = Math.Clamp(viewSpan_ * factor, ZoneLayout.ZoneSize * 0.1f, ZoneLayout.WorldMaxX);

    /// <summary>카메라 중심(월드 좌표). 입장 전에는 월드 한가운데를 본다.</summary>
    private Vector2 camera_ = new(ZoneLayout.WorldMaxX * 0.5f, ZoneLayout.WorldMaxY * 0.5f);

    /// <summary>실제로 보는 중심. 월드 보기에서는 내 위치와 무관하게 월드 한가운데다.</summary>
    private Vector2 CameraCenter => WorldView
        ? new Vector2(ZoneLayout.WorldMaxX * 0.5f, ZoneLayout.WorldMaxY * 0.5f)
        : camera_;

    private SpriteAtlas? atlas_;

    /// <summary>리소스를 못 읽었으면 null 이 들어오고, 그때는 예전처럼 도형으로 그린다.</summary>
    public void SetAtlas(SpriteAtlas? atlas) => atlas_ = atlas;

    public Rectangle Bounds { get; private set; }

    public void Layout(Rectangle bounds) => Bounds = bounds;

    /// <summary>카메라를 여기로 옮긴다. 매 프레임 내 캐릭터 위치로 부른다.</summary>
    public void SetCamera(float x, float y) => camera_ = new Vector2(x, y);

    /// <summary>월드 1 unit 이 화면 몇 픽셀인가.</summary>
    private float PixelsPerUnit => Bounds.Width / ViewSpan;

    /// <summary>지금 화면에 들어오는 월드 사각형. **여기 밖은 아예 그리지 않는다** --
    /// Painter 에 클리핑이 없어서, 밖을 그리면 HUD 위에 덧그려진다.</summary>
    private (float MinX, float MaxX, float MinY, float MaxY) VisibleWorld()
    {
        var halfWidth = ViewSpan * 0.5f;
        var halfHeight = (Bounds.Height / PixelsPerUnit) * 0.5f;
        var center = CameraCenter;
        return (center.X - halfWidth, center.X + halfWidth,
                center.Y - halfHeight, center.Y + halfHeight);
    }

    /// <summary>월드 좌표를 화면 픽셀로. y는 위아래를 뒤집는다(월드는 위로 증가).</summary>
    public Vector2 WorldToScreen(float x, float y)
    {
        var scale = PixelsPerUnit;
        var center = CameraCenter;
        return new Vector2(
            Bounds.X + (Bounds.Width * 0.5f) + ((x - center.X) * scale),
            Bounds.Y + (Bounds.Height * 0.5f) - ((y - center.Y) * scale));
    }

    /// <summary>화면 픽셀을 월드 좌표로. 존 뷰 클릭으로 이동 목표를 찍는 데 쓴다.</summary>
    public Vector2 ScreenToWorld(Point screen)
    {
        var scale = PixelsPerUnit;
        var center = CameraCenter;
        return new Vector2(
            center.X + ((screen.X - Bounds.X - (Bounds.Width * 0.5f)) / scale),
            center.Y - ((screen.Y - Bounds.Y - (Bounds.Height * 0.5f)) / scale));
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

            // 바닥 타일. 없으면 예전처럼 단색으로 채운다.
            //
            // **2×2 로 나눠 깐다.** 한 장을 존 전체로 늘리면 512 -> 670 이라 무늬가 뭉개지고,
            // 타일이라는 게 안 보인다. 타일이 seamless 라 이어 붙여도 경계가 안 드러난다
            // (납품 검수에서 이음매 색차 1~2/255 확인).
            //
            // **샘플러를 LinearWrap 으로 바꾸지 않는다.** 그러려면 SpriteBatch.Begin 블록을
            // 따로 열어야 하고, 그러면 이 화면이 배치 두 개로 갈린다. 복사본을 직접 까는 쪽이
            // 존 4개 × 4장 = 16 드로우라 더 싸다.
            if (atlas_ is not null && atlas_.TryGetTile((int)zoneId, out var tile))
            {
                // 한 장이 덮는 월드 크기를 고정해 둔다 -- 존이 커져도 무늬가 늘어나지 않고,
                // 칸이 작아져서 화면 밖 칸을 통째로 건너뛰기도 쉬워진다.
                var tilesPerZone = Math.Max(2, (int)(ZoneLayout.ZoneSize / 10.0f));
                var cellWidth = rect.Width / (float)tilesPerZone;
                var cellHeight = rect.Height / (float)tilesPerZone;
                var tint = isMine ? new Color(190, 235, 210) : new Color(150, 160, 185);

                for (var ty = 0; ty < tilesPerZone; ++ty)
                {
                    for (var tx = 0; tx < tilesPerZone; ++tx)
                    {
                        // 마지막 칸은 남는 픽셀까지 덮어 존 경계에 한 줄이 비지 않게 한다.
                        var x = rect.X + (int)(tx * cellWidth);
                        var y = rect.Y + (int)(ty * cellHeight);
                        var w = (tx == tilesPerZone - 1) ? (rect.Right - x) : (int)MathF.Ceiling(cellWidth);
                        var h = (ty == tilesPerZone - 1) ? (rect.Bottom - y) : (int)MathF.Ceiling(cellHeight);
                        DrawTileClipped(painter, tile, new Rectangle(x, y, w, h), tint);
                    }
                }
            }
            else
            {
                painter.FillRect(Rectangle.Intersect(rect, Bounds),
                                 isMine ? new Color(24, 40, 34) : new Color(20, 24, 34));
            }

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

        // 그리드와 같은 이유로 보이는 범위만 그린다.
        var view = VisibleWorld();

        for (var column = 1; column < ZoneLayout.ZonesPerRow; ++column)
        {
            var x = column * ZoneLayout.ZoneSize;
            if (x < view.MinX || x > view.MaxX)
            {
                continue;
            }

            var top = MathF.Min(view.MaxY, ZoneLayout.WorldMaxY);
            var bottom = MathF.Max(view.MinY, 0.0f);
            painter.Line(WorldToScreen(x, top), WorldToScreen(x, bottom), threadColor, 2.0f);
            painter.SmallText("핸드오프 경계 (같은 프로세스, 스레드만 다름)",
                              WorldToScreen(x, bottom) + new Vector2(6, -18), threadColor);
        }

        for (var row = 1; row < ZoneLayout.ZoneRows; ++row)
        {
            var y = row * ZoneLayout.ZoneSize;
            if (y < view.MinY || y > view.MaxY)
            {
                continue;
            }

            var left = MathF.Max(view.MinX, 0.0f);
            var right = MathF.Min(view.MaxX, ZoneLayout.WorldMaxX);
            painter.Line(WorldToScreen(left, y), WorldToScreen(right, y), processColor, 2.0f);
            painter.SmallText("핸드오프 경계 (프로세스를 넘는다)",
                              WorldToScreen(left, y) + new Vector2(6, 4), processColor);
        }
    }

    /// <summary>
    /// 타일 한 장을 그리되 **화면 밖은 잘라낸다.** Painter 에 클리핑이 없어서 자르지 않으면
    /// HUD 위에 덧그려지고, 목적지만 자르면 그림이 늘어난다 -- 자른 비율만큼 원본도 자른다.
    /// </summary>
    private void DrawTileClipped(Painter painter, Microsoft.Xna.Framework.Graphics.Texture2D tile,
                                 Rectangle dest, Color tint)
    {
        var clipped = Rectangle.Intersect(dest, Bounds);
        if (clipped.Width <= 0 || clipped.Height <= 0)
        {
            return;
        }

        var source = new Rectangle(
            (int)((clipped.Left - dest.Left) / (float)dest.Width * tile.Width),
            (int)((clipped.Top - dest.Top) / (float)dest.Height * tile.Height),
            (int)(clipped.Width / (float)dest.Width * tile.Width),
            (int)(clipped.Height / (float)dest.Height * tile.Height));

        if (source.Width <= 0 || source.Height <= 0)
        {
            return;
        }

        painter.SpriteBatch.Draw(tile, clipped, source, tint);
    }

    private void DrawGrid(Painter painter)
    {
        var gridColor = new Color(255, 255, 255, 14);

        // **보이는 범위만 그린다.** 카메라가 생기면서 월드 전체를 도는 루프는 대부분이
        // 화면 밖이 되고, Painter 에 클리핑이 없어서 그 선들이 HUD 위에 덧그려진다.
        var view = VisibleWorld();
        const float Step = 10.0f;

        // 존 경계는 DrawZoneBoundaries가 굵게 그리므로 여기서는 건너뛴다.
        for (var x = MathF.Ceiling(MathF.Max(view.MinX, 0.0f) / Step) * Step;
             x <= MathF.Min(view.MaxX, ZoneLayout.WorldMaxX); x += Step)
        {
            if (MathF.Abs(x % ZoneLayout.ZoneSize) < 0.001f)
            {
                continue;
            }

            painter.Line(WorldToScreen(x, MathF.Max(view.MinY, 0.0f)),
                         WorldToScreen(x, MathF.Min(view.MaxY, ZoneLayout.WorldMaxY)), gridColor);
        }

        for (var y = MathF.Ceiling(MathF.Max(view.MinY, 0.0f) / Step) * Step;
             y <= MathF.Min(view.MaxY, ZoneLayout.WorldMaxY); y += Step)
        {
            if (MathF.Abs(y % ZoneLayout.ZoneSize) < 0.001f)
            {
                continue;
            }

            painter.Line(WorldToScreen(MathF.Max(view.MinX, 0.0f), y),
                         WorldToScreen(MathF.Min(view.MaxX, ZoneLayout.WorldMaxX), y), gridColor);
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
        // **뷰 크기에서 역산한다.** 상수를 박아두면 창 크기나 줌이 바뀔 때 같이 안 따라간다
        // (실제로 처음에 0.22 를 박았다가 2.4배 작게 그리고 있었다).
        //
        //   px/unit  = 뷰 너비 / 월드 너비
        //   몸통 표시 px = 0.75 unit × px/unit
        //   배율     = 표시 px / 프레임 192px
        //
        // atlas.Scale(2배 납품)은 여기서 따로 나누지 않는다 -- 프레임 크기 192 에 이미 반영돼
        // 있어서 또 나누면 절반이 된다.
        var pixelsPerUnit = PixelsPerUnit;
        var scale = (BodyUnits * pixelsPerUnit) / BodyFramePixels;

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
