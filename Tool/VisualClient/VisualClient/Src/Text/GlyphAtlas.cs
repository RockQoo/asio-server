using System.Runtime.Versioning;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

// System.Drawing과 Microsoft.Xna.Framework에는 Color/Rectangle/Point가 같은 이름으로 둘 다
// 있다. `using System.Drawing;`을 그냥 열면 그 세 개가 전부 모호해져서 컴파일이 안 되므로,
// GDI+ 타입은 전부 Gdi 접두사 별칭으로만 쓴다 -- 이 파일에서 접두사 없는 Color/Rectangle은
// 항상 XNA 쪽이다.
using GdiBitmap = System.Drawing.Bitmap;
using GdiBrushes = System.Drawing.Brushes;
using GdiColor = System.Drawing.Color;
using GdiFont = System.Drawing.Font;
using GdiFontFamily = System.Drawing.FontFamily;
using GdiFontStyle = System.Drawing.FontStyle;
using GdiGraphics = System.Drawing.Graphics;
using GdiGraphicsUnit = System.Drawing.GraphicsUnit;
using GdiPixelFormat = System.Drawing.Imaging.PixelFormat;
using GdiPointF = System.Drawing.PointF;
using GdiStringFormat = System.Drawing.StringFormat;
using GdiStringFormatFlags = System.Drawing.StringFormatFlags;
using GdiTextRenderingHint = System.Drawing.Text.TextRenderingHint;

namespace VisualClient.Text;

/// <summary>
/// 필요한 글리프만 그때그때 GDI+로 구워서 텍스처 아틀라스에 채워 넣는 폰트.
///
/// <para>
/// <b>왜 MonoGame의 SpriteFont를 안 쓰는가</b>: SpriteFont는 콘텐츠 빌드 시점에 문자 목록을
/// 확정해야 한다. 한글을 넣으려면 <c>CharacterRegions</c>에 U+AC00~U+D7A3(11172자)을 통째로
/// 선언해야 하고, 그러면 텍스처가 수천만 픽셀로 불어나 빌드도 로딩도 감당이 안 된다. 실제로
/// 쓰이는 글자는 채팅에 등장한 몇십~몇백 자뿐이라 "처음 만난 글자만 구워서 캐시"가 훨씬 싸다.
/// 덤으로 <c>.mgcb</c> 콘텐츠 파이프라인 자체를 프로젝트에서 뺄 수 있다.
/// </para>
///
/// <para>
/// <b>알파 채널 규약</b>: 아틀라스에는 흰 글리프를 <c>(a, a, a, a)</c>로, 즉 미리 곱해진
/// (premultiplied) 형태로 넣는다. MonoGame의 기본 <c>BlendState.AlphaBlend</c>가 미리 곱해진
/// 텍스처를 전제하기 때문이다. 흰색이라 R=G=B=255이므로 알파를 네 채널에 그대로 복사하면 그게
/// 곧 미리 곱해진 값이 된다 — 불투명한 색으로 tint하면 결과도 올바르게 미리 곱해진 값이 나온다.
/// </para>
///
/// <para>
/// GDI+(System.Drawing)를 쓰므로 <b>Windows 전용</b>이다. 서버 자체가 MSVC/x64 Windows 전용
/// 빌드라 제약이 되지 않는다(그래서 TargetFramework도 <c>net10.0-windows</c>다).
/// </para>
/// </summary>
[SupportedOSPlatform("windows")]
public sealed class GlyphAtlas : IDisposable
{
    /// <summary>아틀라스 한 장의 크기. 한글 수백 자까지는 한 장으로 충분하다.</summary>
    private const int AtlasSize = 1024;

    /// <summary>글리프 사이 여백. 없으면 이웃 글리프가 선형 보간에 섞여 들어온다.</summary>
    private const int Padding = 1;

    private readonly GdiFont font_;
    private readonly GdiBitmap scratch_;
    private readonly GdiGraphics scratchGraphics_;
    private readonly GdiStringFormat format_;
    private readonly Texture2D atlas_;
    private readonly Dictionary<char, Glyph> glyphs_ = [];

    private int cursorX_;
    private int cursorY_;
    private bool atlasFull_;

    /// <summary>한 줄의 높이(픽셀). 줄 간격에 그대로 쓴다.</summary>
    public int LineHeight { get; }

    /// <summary>실제로 선택된 글꼴 이름. 한글이 두부(□)로 나오면 이 값을 먼저 확인한다.</summary>
    public string FontName { get; }

    /// <summary>글리프 하나의 아틀라스 위치와 진행 폭.</summary>
    private readonly record struct Glyph(Rectangle Source, int Advance);

    public GlyphAtlas(GraphicsDevice device, string fontFamily, float pixelSize)
    {
        font_ = CreateFont(fontFamily, pixelSize);
        FontName = font_.FontFamily.Name;
        LineHeight = font_.Height;

        // 글리프 하나를 굽는 임시 캔버스. 가장 넓은 글자(전각 한글/한자)도 들어가도록
        // 폰트 크기의 2배 폭을 잡는다.
        scratch_ = new GdiBitmap((int)MathF.Ceiling(pixelSize * 2.0f) + 4, LineHeight + 4,
                                 GdiPixelFormat.Format32bppArgb);
        scratchGraphics_ = GdiGraphics.FromImage(scratch_);

        // GridFit 힌팅이 붙은 그레이스케일 안티에일리어싱. 작은 글자에서 가장 읽기 좋다.
        scratchGraphics_.TextRenderingHint = GdiTextRenderingHint.AntiAliasGridFit;

        // GenericTypographic을 측정과 그리기에 똑같이 써야 x=0이 실제 글리프 왼쪽 끝과 맞는다.
        // 기본 StringFormat은 좌우에 em의 1/6쯤 되는 여백을 몰래 넣어서 글자 간격이 벌어진다.
        format_ = new GdiStringFormat(GdiStringFormat.GenericTypographic)
        {
            FormatFlags = GdiStringFormatFlags.MeasureTrailingSpaces | GdiStringFormatFlags.NoWrap,
        };

        atlas_ = new Texture2D(device, AtlasSize, AtlasSize, mipmap: false, SurfaceFormat.Color);
    }

    /// <summary>
    /// 요청한 글꼴이 없으면 GDI+가 조용히 다른 글꼴로 대체해버려 한글이 두부(□)로 보인다.
    /// 그래서 후보를 순서대로 만들어보고 <b>실제로 그 이름이 선택됐는지 확인</b>한다.
    /// </summary>
    private static GdiFont CreateFont(string preferredFamily, float pixelSize)
    {
        string[] candidates = [preferredFamily, "맑은 고딕", "Malgun Gothic", "굴림", "Gulim", "Segoe UI"];
        foreach (var name in candidates)
        {
            if (string.IsNullOrWhiteSpace(name))
            {
                continue;
            }

            GdiFont? font = null;
            try
            {
                font = new GdiFont(name, pixelSize, GdiFontStyle.Regular, GdiGraphicsUnit.Pixel);
                if (string.Equals(font.FontFamily.Name, name, StringComparison.OrdinalIgnoreCase))
                {
                    return font;
                }
            }
            catch (ArgumentException)
            {
                // 그 글꼴이 없다는 뜻이니 다음 후보로 넘어간다.
            }

            font?.Dispose();
        }

        // 마지막 수단: 시스템 기본 sans-serif. 한글은 깨질 수 있어도 창은 열린다.
        return new GdiFont(GdiFontFamily.GenericSansSerif, pixelSize, GdiFontStyle.Regular,
                           GdiGraphicsUnit.Pixel);
    }

    /// <summary>문자열을 그렸을 때의 픽셀 크기. 패널 폭을 잡거나 가운데 정렬에 쓴다.</summary>
    public Vector2 Measure(string text)
    {
        var width = 0;
        foreach (var ch in text)
        {
            width += GetGlyph(ch).Advance;
        }

        return new Vector2(width, LineHeight);
    }

    /// <summary>
    /// 문자열을 왼쪽 위 기준으로 그린다. 줄바꿈(<c>\n</c>)은 처리하지 않는다 — 이 클라이언트의
    /// UI는 줄을 이미 나눠서 갖고 있어서, 여기서 또 나누면 줄 나누기 규칙이 두 군데로 갈린다.
    /// </summary>
    public void DrawString(SpriteBatch spriteBatch, string text, Vector2 position, Color color)
    {
        var x = position.X;
        foreach (var ch in text)
        {
            var glyph = GetGlyph(ch);
            if (glyph.Source.Width > 0)
            {
                spriteBatch.Draw(atlas_, new Vector2(x, position.Y), glyph.Source, color);
            }

            x += glyph.Advance;
        }
    }

    /// <summary>
    /// 최대 폭에 맞춰 잘라낸 문자열. 넘치면 끝에 줄임표를 붙인다 — 패널 밖으로 글자가
    /// 삐져나가면 그 아래 UI를 덮어버린다.
    /// </summary>
    public string Ellipsize(string text, float maxWidth)
    {
        if (Measure(text).X <= maxWidth)
        {
            return text;
        }

        const string Suffix = "...";
        var suffixWidth = Measure(Suffix).X;
        var width = 0.0f;
        var taken = 0;

        foreach (var ch in text)
        {
            var advance = GetGlyph(ch).Advance;
            if (width + advance + suffixWidth > maxWidth)
            {
                break;
            }

            width += advance;
            ++taken;
        }

        return string.Concat(text.AsSpan(0, taken), Suffix);
    }

    private Glyph GetGlyph(char ch)
    {
        if (glyphs_.TryGetValue(ch, out var cached))
        {
            return cached;
        }

        var glyph = BakeGlyph(ch);
        glyphs_[ch] = glyph;
        return glyph;
    }

    private Glyph BakeGlyph(char ch)
    {
        // 공백은 GenericTypographic으로 재면 폭이 0으로 나온다(후행 공백을 잘라내는 규칙 때문).
        // 그릴 픽셀도 없으니 진행 폭만 정해서 캐시한다.
        if (ch == ' ')
        {
            return new Glyph(Rectangle.Empty, Math.Max(1, (int)MathF.Round(font_.Size * 0.31f)));
        }

        if (char.IsControl(ch))
        {
            return new Glyph(Rectangle.Empty, 0);
        }

        var text = ch.ToString();
        var measured = scratchGraphics_.MeasureString(text, font_, GdiPointF.Empty, format_);
        var advance = Math.Max(1, (int)MathF.Ceiling(measured.Width));
        var width = Math.Min(advance + Padding, scratch_.Width);

        if (atlasFull_ || !TryReserve(width, LineHeight, out var slot))
        {
            // 아틀라스가 꽉 찼다. 여기까지 오려면 수천 자를 써야 하고, 이 도구의 용도(채팅 몇
            // 백 줄)에서는 사실상 일어나지 않는다. 새 장을 붙이는 대신 진행 폭만 돌려주고
            // 그리기를 포기한다 — 글자가 빠지는 건 눈에 보이지만 크래시는 아니다.
            atlasFull_ = true;
            return new Glyph(Rectangle.Empty, advance);
        }

        // 임시 캔버스를 투명으로 지우고 글리프 하나만 그린다.
        scratchGraphics_.Clear(GdiColor.Transparent);
        scratchGraphics_.DrawString(text, font_, GdiBrushes.White, GdiPointF.Empty, format_);

        UploadGlyph(slot);
        return new Glyph(slot, advance);
    }

    private bool TryReserve(int width, int height, out Rectangle slot)
    {
        if (cursorX_ + width > AtlasSize)
        {
            cursorX_ = 0;
            cursorY_ += height + Padding;
        }

        if (cursorY_ + height > AtlasSize)
        {
            slot = Rectangle.Empty;
            return false;
        }

        slot = new Rectangle(cursorX_, cursorY_, width, height);
        cursorX_ += width + Padding;
        return true;
    }

    private void UploadGlyph(Rectangle slot)
    {
        // GetPixel은 픽셀당 호출이라 느리지만, 글리프 하나는 20x20 남짓이고 글자마다 처음 한
        // 번만 구우므로 전체 비용이 무시할 수준이다. LockBits로 바꿀 값이 없다.
        var pixels = new Color[slot.Width * slot.Height];
        for (var y = 0; y < slot.Height; ++y)
        {
            for (var x = 0; x < slot.Width; ++x)
            {
                var alpha = scratch_.GetPixel(x, y).A;
                pixels[(y * slot.Width) + x] = new Color(alpha, alpha, alpha, alpha);
            }
        }

        atlas_.SetData(0, slot, pixels, 0, pixels.Length);
    }

    public void Dispose()
    {
        atlas_.Dispose();
        format_.Dispose();
        scratchGraphics_.Dispose();
        scratch_.Dispose();
        font_.Dispose();
    }
}
