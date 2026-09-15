using Microsoft.Xna.Framework;
using Client.Model;

namespace Client.Ui;

/// <summary>
/// 전투에 나오는 것들을 <b>도형으로</b> 그리는 곳. 플레이어/몬스터 몸통, 칼, 활, 화살, 투구,
/// HP 바가 전부 여기 모여 있다.
///
/// <para>
/// <b>왜 한 파일에 모았나</b>: 이 클라이언트는 콘텐츠 파이프라인(<c>.mgcb</c>)을 쓰지 않고
/// <see cref="Painter"/>의 선·원·사각형으로만 그린다(한글 글꼴조차 런타임에 굽는다). 나중에
/// 스프라이트 아틀라스를 받으면 <b>이 파일의 함수 본문만 텍스처 그리기로 바뀌고</b> 호출부
/// (좌표 계산·회전 각도·연출 타이밍)는 그대로 남는다. 함수 이름을 아틀라스의 프레임 이름
/// (<c>player_body</c>, <c>weapon_sword</c> …)과 1:1로 맞춰둔 것도 같은 이유다.
/// </para>
///
/// <para>
/// <b>방향은 전부 코드가 만든다.</b> 서버에는 방향 값이 없고, 몸통은 언제나 정면이며
/// 무기만 각도대로 돈다 — 그래서 방향별 프레임이 필요 없다.
/// </para>
/// </summary>
public static class CombatArt
{
    /// <summary>근접 휘두르기 연출 길이(초).</summary>
    public const double SwingSeconds = 0.18;

    /// <summary>원거리 시위 당기기 연출 길이(초).</summary>
    public const double DrawSeconds = 0.12;

    /// <summary>피격 표시가 남는 시간(초).</summary>
    public const double HitFlashSeconds = 0.2;

    /// <summary>휘두르는 각도 범위(라디안). 뒤로 젖혔다가 앞으로 넘긴다.</summary>
    private const float SwingArcRadians = 1.75f;

    private static readonly Color PlayerBody = new(120, 220, 160);
    private static readonly Color MonsterBody = new(224, 128, 112);
    private static readonly Color DeadBody = new(86, 92, 104);
    private static readonly Color Outline = new(18, 22, 30);
    private static readonly Color Steel = new(206, 216, 232);
    private static readonly Color Wood = new(166, 128, 78);

    /// <summary>
    /// 몸통. 스케치대로 "원 + 눈 두 개"이고, 몬스터는 색과 눈 모양으로 갈린다 —
    /// <b>적과 아군을 한눈에 가르는 것이 이 그림의 유일한 일이다.</b>
    /// </summary>
    public static void DrawBody(Painter painter, Vector2 center, float radius, UnitKind kind,
                                bool isDead, bool isMine, double nowSeconds, double hitAtSeconds)
    {
        var body = kind == UnitKind.Monster ? MonsterBody : PlayerBody;
        if (isDead)
        {
            body = DeadBody;
        }
        else if (nowSeconds - hitAtSeconds < HitFlashSeconds)
        {
            // 맞은 직후 잠깐 하얗게 뜬다. 데미지 숫자만으로는 "누가 맞았는지"가 화면에서
            // 흐려서, 몸통 자체가 반응해야 타격이 읽힌다.
            var flash = 1.0f - (float)((nowSeconds - hitAtSeconds) / HitFlashSeconds);
            body = Color.Lerp(body, Color.White, flash * 0.75f);
        }

        painter.FillCircle(center, radius, body);

        if (isDead)
        {
            DrawDeadMark(painter, center, radius);
            return;
        }

        DrawEyes(painter, center, radius, kind);

        if (isMine)
        {
            // 내 캐릭터에만 점을 찍는다. 색만으로 구분하면 여러 창을 띄웠을 때 헷갈린다.
            painter.FillCircle(center, radius * 0.32f, new Color(16, 28, 22));
        }
    }

    private static void DrawEyes(Painter painter, Vector2 center, float radius, UnitKind kind)
    {
        var offsetX = radius * 0.34f;
        var offsetY = radius * 0.18f;
        var eyeSize = Math.Max(2, (int)(radius * 0.22f));

        // 플레이어는 동그란 눈, 몬스터는 각진 눈 — 스케치의 삼각 눈을 사각형으로 옮겼다.
        if (kind == UnitKind.Monster)
        {
            painter.FillRect(new Rectangle((int)(center.X - offsetX - eyeSize), (int)(center.Y - offsetY),
                                           eyeSize * 2, eyeSize), Outline);
            painter.FillRect(new Rectangle((int)(center.X + offsetX - eyeSize), (int)(center.Y - offsetY),
                                           eyeSize * 2, eyeSize), Outline);
            return;
        }

        painter.FillCircle(center + new Vector2(-offsetX, -offsetY), eyeSize * 0.8f, Outline);
        painter.FillCircle(center + new Vector2(offsetX, -offsetY), eyeSize * 0.8f, Outline);
    }

    private static void DrawDeadMark(Painter painter, Vector2 center, float radius)
    {
        var size = radius * 0.45f;
        var color = new Color(30, 34, 42);
        painter.Line(center + new Vector2(-size, -size), center + new Vector2(size, size), color, 2.0f);
        painter.Line(center + new Vector2(-size, size), center + new Vector2(size, -size), color, 2.0f);
    }

    /// <summary>
    /// 투구. <b>DEF가 있다는 표시</b>다 — 스탯에 방어력만 있으므로 방어구도 하나면 충분하다.
    /// 머리 위를 덮는 반원이라 몸통 프레임의 머리 위치에 맞춰 얹는다.
    /// </summary>
    public static void DrawHelmet(Painter painter, Vector2 center, float radius)
    {
        var color = new Color(150, 158, 176);
        var top = center.Y - (radius * 0.35f);

        // 반원을 선분 8개로 근사한다. 원판 텍스처를 반만 쓸 방법이 없어서(잘라내기가 없다)
        // 위쪽 호만 직접 긋는다.
        const int Segments = 8;
        var previous = Vector2.Zero;
        for (var index = 0; index <= Segments; ++index)
        {
            var angle = MathF.PI * index / Segments;
            var point = new Vector2(
                center.X - (MathF.Cos(angle) * radius * 0.95f),
                top - (MathF.Sin(angle) * radius * 0.55f));

            if (index > 0)
            {
                painter.Line(previous, point, color, 3.0f);
            }

            previous = point;
        }

        painter.Line(new Vector2(center.X - (radius * 0.95f), top),
                     new Vector2(center.X + (radius * 0.95f), top), color, 2.5f);
    }

    /// <summary>
    /// 무기. 몸통 옆에 붙여 <paramref name="radians"/> 방향으로 돌린다.
    ///
    /// <para>
    /// <paramref name="swingProgress"/>는 0~1이고 1이면 연출이 끝난 상태다. 근접은 이 값으로
    /// 각도를 -50°에서 +50°까지 넘기고, 원거리는 활을 당겼다 놓는 거리로 쓴다 —
    /// <b>프레임 애니메이션이 없는 이유</b>가 이것이다(서버가 즉발이라 연출 길이가 판정과
    /// 무관해야 하고, 그러려면 시간 곡선을 코드가 쥐고 있어야 한다).
    /// </para>
    /// </summary>
    public static void DrawWeapon(Painter painter, Vector2 center, float radius, AttackKind weapon,
                                  float radians, float swingProgress)
    {
        if (weapon == AttackKind.Melee)
        {
            DrawSword(painter, center, radius, radians, swingProgress);
            return;
        }

        DrawBow(painter, center, radius, radians, swingProgress);
    }

    private static void DrawSword(Painter painter, Vector2 center, float radius, float radians,
                                  float swingProgress)
    {
        // 휘두르는 중이면 각도를 뒤에서 앞으로 넘긴다. 다 끝났으면 살짝 아래로 내린 기본 자세.
        var offset = swingProgress < 1.0f
            ? (-SwingArcRadians * 0.5f) + (SwingArcRadians * Ease(swingProgress))
            : 0.45f;

        var angle = radians + offset;
        var direction = new Vector2(MathF.Cos(angle), -MathF.Sin(angle));
        var normal = new Vector2(-direction.Y, direction.X);

        var grip = center + (direction * radius * 0.55f);
        var tip = grip + (direction * radius * 1.9f);

        painter.Line(grip - (direction * radius * 0.35f), grip, Wood, 3.5f);          // 손잡이
        painter.Line(grip - (normal * radius * 0.42f), grip + (normal * radius * 0.42f), Steel, 2.5f);  // 코등이
        painter.Line(grip, tip, Steel, 3.0f);                                          // 날

        if (swingProgress < 1.0f)
        {
            DrawSwingArc(painter, center, radius, radians, swingProgress);
        }
    }

    /// <summary>휘두른 자취. 남은 진행도만큼 옅어진다.</summary>
    private static void DrawSwingArc(Painter painter, Vector2 center, float radius, float radians,
                                     float swingProgress)
    {
        var alpha = (byte)(140 * (1.0f - swingProgress));
        var color = new Color((byte)235, (byte)240, (byte)255, alpha);

        const int Segments = 6;
        var previous = Vector2.Zero;
        for (var index = 0; index <= Segments; ++index)
        {
            var trail = (-SwingArcRadians * 0.5f) + (SwingArcRadians * Ease(swingProgress) * index / Segments);
            var angle = radians + trail;
            var point = center + (new Vector2(MathF.Cos(angle), -MathF.Sin(angle)) * radius * 2.3f);

            if (index > 0)
            {
                painter.Line(previous, point, color, 2.0f);
            }

            previous = point;
        }
    }

    private static void DrawBow(Painter painter, Vector2 center, float radius, float radians,
                                float drawProgress)
    {
        var direction = new Vector2(MathF.Cos(radians), -MathF.Sin(radians));
        var normal = new Vector2(-direction.Y, direction.X);
        var grip = center + (direction * radius * 0.9f);

        // 활대: 앞으로 볼록한 호를 선분 6개로.
        const int Segments = 6;
        var previous = Vector2.Zero;
        for (var index = 0; index <= Segments; ++index)
        {
            var t = (index / (float)Segments * 2.0f) - 1.0f;      // -1 ~ 1
            var point = grip + (normal * t * radius * 1.15f)
                        + (direction * (1.0f - (t * t)) * radius * 0.5f);

            if (index > 0)
            {
                painter.Line(previous, point, Wood, 3.0f);
            }

            previous = point;
        }

        // 시위: 쏘는 순간 뒤로 당겨졌다가 제자리로 돌아온다.
        var pull = drawProgress < 1.0f ? (1.0f - Ease(drawProgress)) * radius * 0.7f : 0.0f;
        var top = grip + (normal * radius * 1.15f);
        var bottom = grip - (normal * radius * 1.15f);
        var nock = grip - (direction * pull);

        painter.Line(top, nock, Steel, 1.5f);
        painter.Line(nock, bottom, Steel, 1.5f);
    }

    /// <summary>날아가는 화살 하나. 촉이 진행 방향을 향한다.</summary>
    public static void DrawArrow(Painter painter, Vector2 position, float radians)
    {
        var direction = new Vector2(MathF.Cos(radians), -MathF.Sin(radians));
        var normal = new Vector2(-direction.Y, direction.X);

        var tail = position - (direction * 9.0f);
        painter.Line(tail, position, Wood, 2.0f);
        painter.Line(position, position - (direction * 5.0f) + (normal * 3.5f), Steel, 2.0f);
        painter.Line(position, position - (direction * 5.0f) - (normal * 3.5f), Steel, 2.0f);
    }

    /// <summary>
    /// 유닛 머리 위 HP 바. <b>그림이 아니라 사각형으로 그린다</b> — 길이가 매 틱 바뀌는
    /// 물건이라 그림으로 하면 늘어난 픽셀이 보인다.
    /// </summary>
    public static void DrawHealthBar(Painter painter, Vector2 center, float radius, CombatUnit unit)
    {
        const int Width = 34;
        const int Height = 4;

        var x = (int)(center.X - (Width * 0.5f));
        var y = (int)(center.Y - radius - Height - 6);

        painter.FillRect(new Rectangle(x - 1, y - 1, Width + 2, Height + 2), new Color(10, 12, 18, 210));

        if (unit.MaxHp <= 0)
        {
            return;
        }

        var ratio = Math.Clamp(unit.Hp / (float)unit.MaxHp, 0.0f, 1.0f);
        var fill = (int)(Width * ratio);
        if (fill > 0)
        {
            painter.FillRect(new Rectangle(x, y, fill, Height), HealthColor(ratio));
        }
    }

    /// <summary>체력이 줄수록 초록 → 노랑 → 빨강.</summary>
    public static Color HealthColor(float ratio) => ratio switch
    {
        > 0.6f => new Color(120, 210, 130),
        > 0.3f => new Color(230, 200, 110),
        _ => new Color(232, 110, 100),
    };

    /// <summary>선택한 대상 발밑의 링.</summary>
    public static void DrawTargetRing(Painter painter, Vector2 center, float radius, double nowSeconds)
    {
        // 살짝 맥동시킨다 — 정지한 링은 배경 격자에 묻힌다.
        var pulse = 1.0f + (0.12f * MathF.Sin((float)nowSeconds * 6.0f));
        painter.FillCircle(center, radius * 1.55f * pulse, new Color(250, 210, 110, 60));
        painter.FillCircle(center, radius * 1.2f * pulse, new Color(14, 18, 26, 0));
    }

    /// <summary>시작은 빠르고 끝은 느리게. 휘두르기가 기계적으로 보이지 않게 한다.</summary>
    private static float Ease(float t) => 1.0f - ((1.0f - t) * (1.0f - t));
}
