using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using VisualClient.Text;

namespace VisualClient.Ui;

/// <summary>
/// 사각형/선/원처럼 MonoGame이 기본으로 제공하지 않는 2D 프리미티브를 스프라이트 두 장으로
/// 흉내내는 그리기 도구. 폰트까지 같이 들고 있어서 UI 코드가 텍스처를 직접 만질 일이 없다.
///
/// <para>
/// 원은 매 프레임 정점을 만드는 대신 <b>안티에일리어싱된 원판 텍스처 한 장을 미리 구워서</b>
/// 크기만 바꿔 그린다 — 이 클라이언트가 그리는 원은 플레이어 마커뿐이고 개수도 수십 개
/// 수준이라, 셰이더나 정점 버퍼를 끌어올 이유가 없다.
/// </para>
/// </summary>
public sealed class Painter : IDisposable
{
    /// <summary>미리 굽는 원판 텍스처의 반지름(픽셀). 실제로는 축소해서 쓰므로 넉넉히 잡는다.</summary>
    private const int CircleRadius = 64;

    private readonly Texture2D pixel_;
    private readonly Texture2D circle_;

    public SpriteBatch SpriteBatch { get; }

    public GlyphAtlas Font { get; }

    public GlyphAtlas SmallFont { get; }

    public Painter(GraphicsDevice device, SpriteBatch spriteBatch, GlyphAtlas font, GlyphAtlas smallFont)
    {
        SpriteBatch = spriteBatch;
        Font = font;
        SmallFont = smallFont;

        pixel_ = new Texture2D(device, 1, 1, mipmap: false, SurfaceFormat.Color);
        pixel_.SetData([Color.White]);

        circle_ = BakeCircle(device);
    }

    private static Texture2D BakeCircle(GraphicsDevice device)
    {
        const int Size = CircleRadius * 2;
        var texture = new Texture2D(device, Size, Size, mipmap: false, SurfaceFormat.Color);
        var pixels = new Color[Size * Size];

        for (var y = 0; y < Size; ++y)
        {
            for (var x = 0; x < Size; ++x)
            {
                // 픽셀 중심까지의 거리로 경계 한 픽셀만 부드럽게 깎는다. 미리 곱해진 알파
                // 규약(GlyphAtlas 주석 참고)에 맞춰 네 채널에 같은 값을 넣는다.
                var dx = x - CircleRadius + 0.5f;
                var dy = y - CircleRadius + 0.5f;
                var distance = MathF.Sqrt((dx * dx) + (dy * dy));
                var coverage = Math.Clamp(CircleRadius - distance, 0.0f, 1.0f);
                var alpha = (byte)(coverage * 255.0f);
                pixels[(y * Size) + x] = new Color(alpha, alpha, alpha, alpha);
            }
        }

        texture.SetData(pixels);
        return texture;
    }

    public void FillRect(Rectangle rect, Color color) => SpriteBatch.Draw(pixel_, rect, color);

    /// <summary>테두리만 그린다. 네 변을 얇은 사각형 네 개로 채우는 방식.</summary>
    public void StrokeRect(Rectangle rect, Color color, int thickness = 1)
    {
        FillRect(new Rectangle(rect.X, rect.Y, rect.Width, thickness), color);
        FillRect(new Rectangle(rect.X, rect.Bottom - thickness, rect.Width, thickness), color);
        FillRect(new Rectangle(rect.X, rect.Y, thickness, rect.Height), color);
        FillRect(new Rectangle(rect.Right - thickness, rect.Y, thickness, rect.Height), color);
    }

    /// <summary>임의 방향의 선분. 1x1 픽셀을 늘리고 회전시켜 그린다.</summary>
    public void Line(Vector2 from, Vector2 to, Color color, float thickness = 1.0f)
    {
        var delta = to - from;
        var length = delta.Length();
        if (length < 0.001f)
        {
            return;
        }

        SpriteBatch.Draw(
            pixel_,
            from,
            sourceRectangle: null,
            color,
            rotation: MathF.Atan2(delta.Y, delta.X),
            origin: new Vector2(0.0f, 0.5f),
            scale: new Vector2(length, thickness),
            effects: SpriteEffects.None,
            layerDepth: 0.0f);
    }

    public void FillCircle(Vector2 center, float radius, Color color)
    {
        var scale = radius / CircleRadius;
        SpriteBatch.Draw(
            circle_,
            center,
            sourceRectangle: null,
            color,
            rotation: 0.0f,
            origin: new Vector2(CircleRadius, CircleRadius),
            scale: new Vector2(scale, scale),
            effects: SpriteEffects.None,
            layerDepth: 0.0f);
    }

    public void Text(string text, Vector2 position, Color color) =>
        Font.DrawString(SpriteBatch, text, position, color);

    public void SmallText(string text, Vector2 position, Color color) =>
        SmallFont.DrawString(SpriteBatch, text, position, color);

    /// <summary>패널 배경 + 테두리. 모든 오버레이 패널이 같은 모양을 쓰도록 한 곳에 모았다.</summary>
    public void Panel(Rectangle rect, string? title = null)
    {
        FillRect(rect, new Color(14, 18, 28, 235));
        StrokeRect(rect, new Color(70, 90, 120), 1);

        if (string.IsNullOrEmpty(title))
        {
            return;
        }

        var headerHeight = Font.LineHeight + 8;
        FillRect(new Rectangle(rect.X, rect.Y, rect.Width, headerHeight), new Color(28, 38, 56, 245));
        Text(title, new Vector2(rect.X + 8, rect.Y + 4), new Color(200, 220, 255));
    }

    public void Dispose()
    {
        pixel_.Dispose();
        circle_.Dispose();
    }
}
