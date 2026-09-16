using System.Text.Json;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;

namespace Client.Ui;

/// <summary>아틀라스 안의 프레임 하나. 좌표와 피벗(그리기 기준점)을 갖는다.</summary>
public readonly record struct SpriteFrame(Rectangle Source, Vector2 Pivot);

/// <summary>
/// 캐릭터·장비·이펙트 스프라이트가 들어 있는 아틀라스 한 장.
///
/// <para>
/// <b>콘텐츠 파이프라인(.mgcb)을 쓰지 않는다</b> — PNG를 런타임에 <see cref="Texture2D.FromStream"/>로
/// 읽는다. 한글 글리프를 GDI+로 굽는 <c>GlyphAtlas</c>와 같은 이유이고, 덕분에 빌드마다
/// mgcb가 끼어들지 않는다. 대가는 시작할 때 파일을 읽는 것뿐이다(1MB 미만).
/// </para>
///
/// <para>
/// <b>알파 규약이 다르다.</b> PNG는 보통 스트레이트 알파인데 이 클라이언트는
/// 미리 곱해진 알파(<see cref="BlendState.AlphaBlend"/>)로 그린다. 그래서 읽은 직후
/// 한 번 곱해준다 — 이걸 빼면 반투명 가장자리에 검은 테두리가 생긴다.
/// </para>
/// </summary>
public sealed class SpriteAtlas : IDisposable
{
    private readonly Texture2D texture_;
    private readonly Dictionary<string, SpriteFrame> frames_;

    /// <summary>몸통 안에서 머리 중심의 위치. 머리 장비를 여기 맞춘다.</summary>
    public Vector2 HeadAnchor { get; }

    /// <summary>몸통 안에서 무기를 쥐는 위치. 무기 피벗을 여기 맞춘다.</summary>
    public Vector2 WeaponSocket { get; }

    /// <summary>아틀라스가 그려진 배율. 화면에 원래 크기로 그리려면 이 값으로 나눈다.</summary>
    public float Scale { get; }

    private SpriteAtlas(Texture2D texture, Dictionary<string, SpriteFrame> frames,
                        Vector2 headAnchor, Vector2 weaponSocket, float scale)
    {
        texture_ = texture;
        frames_ = frames;
        HeadAnchor = headAnchor;
        WeaponSocket = weaponSocket;
        Scale = scale;
    }

    /// <summary>
    /// 아틀라스를 읽는다. 파일이 없거나 형식이 깨졌으면 <c>null</c>을 돌려준다 —
    /// <b>리소스가 없다고 클라이언트가 죽으면 안 된다.</b> 그럴 때는 예전처럼 도형으로 그린다
    /// (<see cref="ZoneView"/>가 그 분기를 갖고 있다).
    /// </summary>
    public static SpriteAtlas? TryLoad(GraphicsDevice device, string assetDirectory)
    {
        try
        {
            var jsonPath = Path.Combine(assetDirectory, "atlas_game.json");
            if (!File.Exists(jsonPath))
            {
                return null;
            }

            using var document = JsonDocument.Parse(File.ReadAllText(jsonPath));
            var root = document.RootElement;

            var textureName = root.TryGetProperty("texture", out var textureElement)
                ? (textureElement.GetString() ?? "atlas_game.png")
                : "atlas_game.png";
            var texturePath = Path.Combine(assetDirectory, textureName);
            if (!File.Exists(texturePath))
            {
                return null;
            }

            using var stream = File.OpenRead(texturePath);
            var texture = Texture2D.FromStream(device, stream);
            PremultiplyAlpha(texture);

            var frames = new Dictionary<string, SpriteFrame>();
            foreach (var property in root.GetProperty("frames").EnumerateObject())
            {
                var f = property.Value;
                var source = new Rectangle(
                    f.GetProperty("x").GetInt32(), f.GetProperty("y").GetInt32(),
                    f.GetProperty("w").GetInt32(), f.GetProperty("h").GetInt32());
                var pivot = new Vector2(f.GetProperty("px").GetInt32(), f.GetProperty("py").GetInt32());
                frames[property.Name] = new SpriteFrame(source, pivot);
            }

            var head = ReadAnchor(root, "head", new Vector2(96, 72));
            var weapon = ReadAnchor(root, "weapon_socket", new Vector2(130, 115));
            var scale = root.TryGetProperty("scale", out var scaleElement) ? scaleElement.GetSingle() : 1.0f;

            return new SpriteAtlas(texture, frames, head, weapon, scale);
        }
        catch (Exception)
        {
            // 리소스 파싱 실패는 게임을 못 하게 할 이유가 아니다 — 도형 렌더링으로 돌아간다.
            return null;
        }
    }

    private static Vector2 ReadAnchor(JsonElement root, string name, Vector2 fallback)
    {
        if (!root.TryGetProperty("anchors", out var anchors) || !anchors.TryGetProperty(name, out var anchor))
        {
            return fallback;
        }
        return new Vector2(anchor.GetProperty("x").GetInt32(), anchor.GetProperty("y").GetInt32());
    }

    private static void PremultiplyAlpha(Texture2D texture)
    {
        var pixels = new Color[texture.Width * texture.Height];
        texture.GetData(pixels);
        for (var i = 0; i < pixels.Length; ++i)
        {
            var p = pixels[i];
            pixels[i] = Color.FromNonPremultiplied(p.R, p.G, p.B, p.A);
        }
        texture.SetData(pixels);
    }

    public bool Has(string frameName) => frames_.ContainsKey(frameName);

    /// <summary>
    /// 프레임 하나를 그린다. <paramref name="position"/>은 <b>프레임의 피벗이 놓일 화면 좌표</b>다 —
    /// 몸통은 캐릭터 발밑 기준점, 무기는 소켓 위치가 되도록 호출부가 맞춘다.
    /// </summary>
    public void Draw(Painter painter, string frameName, Vector2 position, float scale,
                     float rotation = 0.0f, Color? tint = null)
    {
        if (!frames_.TryGetValue(frameName, out var frame))
        {
            return;
        }

        painter.SpriteBatch.Draw(texture_, position, frame.Source, tint ?? Color.White,
                                 rotation, frame.Pivot, scale, SpriteEffects.None, 0.0f);
    }

    public void Dispose() => texture_.Dispose();
}
