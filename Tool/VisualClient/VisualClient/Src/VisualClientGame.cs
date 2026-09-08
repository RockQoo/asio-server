using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Graphics;
using Microsoft.Xna.Framework.Input;
using VisualClient.Model;
using VisualClient.Net;
using VisualClient.Protocol;
using VisualClient.Text;
using VisualClient.Ui;

namespace VisualClient;

/// <summary>
/// 이 클라이언트의 실행 옵션. 명령줄 인자로만 받는다 — 설정 파일을 두면 "지금 어디에 붙어
/// 있는지"가 화면과 파일 두 군데로 갈린다.
/// </summary>
public sealed record ClientOptions(string GatewayHost, int GatewayPort, string GmToolBaseUrl)
{
    /// <summary>
    /// 기본값. Gateway 9000은 <c>ProtocolClient</c>의 기본 포트와 같고, 5080은
    /// <c>bat/start_gmtool.bat</c>이 <c>--urls</c>로 지정하는 운영툴 포트다.
    ///
    /// <para>
    /// GmTool.Web의 <c>launchSettings.json</c>에 적힌 5270이 아니라 5080인 이유: 그 bat이
    /// <c>--no-launch-profile</c>로 프로필을 무시하고 5080을 명시한다. 저장소의 기동 스크립트가
    /// 사실상의 관례이므로 그쪽에 맞춘다 — <c>dotnet run</c>을 직접 돌려 5270으로 떴다면
    /// 실행 인자로 넘기면 된다.
    /// </para>
    /// </summary>
    public static ClientOptions Default => new("127.0.0.1", 9000, "http://127.0.0.1:5080");
}

/// <summary>
/// 존/채팅/우편/쿠폰을 한 화면에서 확인하는 시각 클라이언트.
///
/// <para>
/// <b>Update와 Draw의 역할 분담</b>: 네트워크 수신 반영과 이동 입력은 <see cref="Update"/>에서,
/// UI(패널·버튼·입력칸)는 <see cref="Draw"/>에서 처리한다. UI를 Draw에 둔 이유는 위젯이 즉시
/// 모드(immediate mode)라서 그리는 그 자리에서 클릭 판정까지 함께 하기 때문이다 — 레이아웃을
/// 두 번 계산해 Update와 Draw로 나눠 두면 두 계산이 어긋나는 순간 "보이는 버튼과 눌리는
/// 버튼이 다른" 버그가 된다.
/// </para>
/// </summary>
public sealed class VisualClientGame : Game
{
    /// <summary>이동 속도(월드 단위/초). 존 폭이 10이라 이 속도면 경계까지 3초쯤 걸린다.</summary>
    private const float MoveSpeed = 3.0f;

    /// <summary>이동 패킷 전송 주기(초). 프레임마다 보내면 서버 쪽 브로드캐스트가 과해진다.</summary>
    private const double MoveSendInterval = 0.05;

    /// <summary>Echo 자동 전송 주기(초).</summary>
    private const double EchoInterval = 1.0;

    /// <summary>
    /// 가만히 있어도 좌표를 다시 알리는 주기(초).
    ///
    /// <para>
    /// <b>왜 필요한가</b>: 존 브로드캐스트는 <em>그 패킷을 처리하는 순간 존에 있는 플레이어</em>
    /// 에게만 간다(<c>ZoneInstance::BroadcastToZone</c>이 그때의 <c>players_</c>로 대상 목록을
    /// 만든다). 그런데 프로토콜에 <b>"입장 시 기존 플레이어 목록"을 주는 패킷이 없어서</b>,
    /// 나중에 들어온 창은 이미 있던 플레이어가 <em>움직일 때까지</em> 그 존재를 알 수 없다.
    /// 창을 두 개 띄워 놓고 한쪽을 가만히 두면 다른 쪽 화면에서 아예 안 보이는 셈이다.
    /// </para>
    ///
    /// <para>
    /// 그래서 이동이 없어도 이 주기로 현재 좌표를 다시 보낸다 — 새로 들어온 창도 1초 안에
    /// 나를 발견한다. 서버를 고치지 않고 클라이언트에서 메우는 부분이라 명시해 둔다(제대로
    /// 하려면 입장 응답에 존 안의 플레이어 스냅샷이 실려야 한다).
    /// </para>
    /// </summary>
    private const double PositionHeartbeatInterval = 1.0;

    /// <summary>
    /// 이 시간(초) 이상 소식이 없는 플레이어를 목록에서 지운다.
    ///
    /// <para>
    /// 퇴장 통지도 프로토콜에 없다 — 서버는 <c>W2ZLeaveZoneNotify</c>로 자기 상태에서만
    /// 지우고 남은 사람들에게 알리지 않는다. 위 하트비트가 있으니 "소식이 끊긴 것"으로
    /// 퇴장을 추정할 수 있고, 안 지우면 접속을 끊은 창이 화면에 영원히 남는다.
    /// 하트비트 주기의 몇 배로 잡아 일시적인 지연을 퇴장으로 오해하지 않게 한다.
    /// </para>
    /// </summary>
    private const double PlayerForgetAfterSeconds = 6.0;

    private readonly GraphicsDeviceManager graphics_;
    private readonly ClientOptions options_;
    private readonly InputState input_ = new();
    private readonly WorldModel world_ = new();
    private readonly ZoneView zoneView_ = new();
    private readonly ChatPanel chatPanel_ = new();
    private readonly MailPanel mailPanel_ = new();
    private readonly Hud hud_ = new();
    private readonly GameLink link_;
    private readonly CouponClient coupons_;
    private readonly CouponPanel couponPanel_;

    private SpriteBatch? spriteBatch_;
    private GlyphAtlas? font_;
    private GlyphAtlas? smallFont_;
    private Painter? painter_;

    /// <summary>내 목표 좌표. 서버가 확정해 되돌려주는 좌표와 별개로 클라이언트가 들고 있다.</summary>
    private float localX_ = 5.0f;
    private float localY_ = 5.0f;

    /// <summary>이번 프레임의 시각(초). Update 시작에서 한 번 갱신한다.</summary>
    private double nowSeconds_;

    private double moveSentAtSeconds_ = double.NegativeInfinity;
    private double echoSentAtSeconds_ = double.NegativeInfinity;
    private bool moveDirty_;

    /// <summary>입장/핸드오프 직후 좌표를 알렸는지. <see cref="AnnouncePositionOnZoneChange"/> 참고.</summary>
    private bool announcedZone_;
    private uint announcedZoneId_;

    public VisualClientGame(ClientOptions options)
    {
        options_ = options;
        link_ = new GameLink(options.GatewayHost, options.GatewayPort);
        coupons_ = new CouponClient(options.GmToolBaseUrl);
        couponPanel_ = new CouponPanel(coupons_);

        graphics_ = new GraphicsDeviceManager(this)
        {
            PreferredBackBufferWidth = 1360,
            PreferredBackBufferHeight = 860,
        };

        Content.RootDirectory = "Content";
        IsMouseVisible = true;
        Window.AllowUserResizing = true;
        Window.Title = $"asio-server VisualClient — {options.GatewayHost}:{options.GatewayPort}";

        // 타이핑은 키 코드가 아니라 OS가 확정한 문자로 받는다(InputState 주석 참고).
        Window.TextInput += (_, args) => input_.OnTextInput(args.Character);
    }

    protected override void Initialize()
    {
        world_.SetRequestedPosition(localX_, localY_);
        world_.AddSystemLine($"{options_.GatewayHost}:{options_.GatewayPort} 접속을 시도합니다...");
        world_.AddSystemLine($"쿠폰 등록은 {options_.GmToolBaseUrl} (GmTool.Web)로 나갑니다.");

        // 접속을 기다리지 않고 창을 먼저 띄운다 — 서버가 안 떠 있을 때 "창이 안 열린다"가
        // 아니라 "접속 실패"가 화면에 보이는 쪽이 진단에 쓸모 있다.
        _ = link_.ConnectAsync();

        base.Initialize();
    }

    protected override void LoadContent()
    {
        spriteBatch_ = new SpriteBatch(GraphicsDevice);

        // 콘텐츠 파이프라인을 쓰지 않고 시스템 글꼴을 런타임에 굽는다(GlyphAtlas 주석 참고).
        font_ = new GlyphAtlas(GraphicsDevice, "맑은 고딕", 15.0f);
        smallFont_ = new GlyphAtlas(GraphicsDevice, "맑은 고딕", 12.5f);
        painter_ = new Painter(GraphicsDevice, spriteBatch_, font_, smallFont_);
    }

    protected override void Update(GameTime gameTime)
    {
        input_.BeginFrame();

        // 이번 프레임의 시각. 이동 전송 주기/Echo 주기/플레이어 노후 판정이 모두 같은 기준을
        // 써야 하므로 프레임 시작에 한 번만 읽어 필드에 담는다.
        nowSeconds_ = gameTime.TotalGameTime.TotalSeconds;

        // 소켓 수신 스레드가 큐에 넣어둔 패킷을 여기서 상태에 반영한다. 게임 상태를 만지는
        // 스레드가 이 스레드 하나뿐이라 WorldModel에 락이 없다.
        foreach (var packet in link_.Drain())
        {
            world_.Apply(packet, nowSeconds_);
        }

        AnnouncePositionOnZoneChange();
        HandleGlobalKeys();
        HandleMovement(gameTime);
        HandleEcho();
        world_.ForgetStalePlayers(nowSeconds_ - PlayerForgetAfterSeconds);

        base.Update(gameTime);
    }

    /// <summary>
    /// 존에 배정된 직후 이동 패킷을 한 번 보낸다.
    ///
    /// <para>
    /// 이게 없으면 <b>키를 누를 때까지 내 캐릭터가 화면에 안 그려진다</b>. 화면의 플레이어
    /// 원은 서버가 <c>Z2CMoveNotify</c>로 확정해 되돌려준 좌표만 쓰는데(그게 이 클라이언트가
    /// "서버를 실제로 거쳤는지"를 보여주는 방식이다), <c>Z2CEnterZoneNotify</c>에는 좌표가
    /// 없어서 입장만으로는 확정 좌표를 받을 길이 없다. 그래서 입장 직후 한 번 좌표를 알려
    /// 서버가 브로드캐스트하게 만든다 — 같은 존의 다른 창에도 내가 나타난다.
    /// </para>
    ///
    /// <para>
    /// 핸드오프(zoneId 변경)에도 같은 처리가 필요하다. 전입한 존은 브로드캐스트 대상 목록을
    /// 새로 갖고 있어서, 다시 알려주지 않으면 그 존의 다른 플레이어들에게 내가 안 보인다.
    /// </para>
    /// </summary>
    private void AnnouncePositionOnZoneChange()
    {
        if (!world_.HasEnteredZone || (announcedZone_ && world_.MyZoneId == announcedZoneId_))
        {
            return;
        }

        announcedZone_ = true;
        announcedZoneId_ = world_.MyZoneId;
        moveDirty_ = true;
    }

    private void HandleGlobalKeys()
    {
        // Esc는 종료가 아니라 "입력/패널 닫기"다. 채팅을 치다 Esc를 눌렀는데 프로그램이
        // 꺼지면 도구로 쓸 수가 없다(MonoGame 템플릿 기본 동작을 의도적으로 뺐다).
        if (input_.IsKeyPressed(Keys.Escape))
        {
            if (mailPanel_.IsOpen || couponPanel_.IsOpen)
            {
                mailPanel_.Close();
                couponPanel_.Close();
            }
        }

        if (input_.IsKeyPressed(Keys.Enter) && !IsTyping)
        {
            chatPanel_.FocusInput();
        }

        if (input_.IsKeyPressed(Keys.F1))
        {
            SendEcho();
        }
    }

    /// <summary>입력칸 중 하나라도 포커스를 갖고 있는가. 그러면 WASD는 타이핑이다.</summary>
    private bool IsTyping =>
        chatPanel_.InputHasFocus || mailPanel_.HasFocusedField || couponPanel_.HasFocusedField;

    private void HandleMovement(GameTime gameTime)
    {
        if (!IsTyping)
        {
            var direction = Vector2.Zero;
            if (input_.IsKeyDown(Keys.A) || input_.IsKeyDown(Keys.Left))
            {
                direction.X -= 1.0f;
            }

            if (input_.IsKeyDown(Keys.D) || input_.IsKeyDown(Keys.Right))
            {
                direction.X += 1.0f;
            }

            if (input_.IsKeyDown(Keys.W) || input_.IsKeyDown(Keys.Up))
            {
                direction.Y += 1.0f;
            }

            if (input_.IsKeyDown(Keys.S) || input_.IsKeyDown(Keys.Down))
            {
                direction.Y -= 1.0f;
            }

            if (direction != Vector2.Zero)
            {
                direction.Normalize();
                var delta = MoveSpeed * (float)gameTime.ElapsedGameTime.TotalSeconds;
                SetLocalPosition(localX_ + (direction.X * delta), localY_ + (direction.Y * delta));
            }
        }

        // 이동이 없어도 주기적으로 좌표를 다시 알린다(PositionHeartbeatInterval 주석 참고).
        if (world_.HasEnteredZone && nowSeconds_ - moveSentAtSeconds_ >= PositionHeartbeatInterval)
        {
            moveDirty_ = true;
        }

        if (moveDirty_ && nowSeconds_ - moveSentAtSeconds_ >= MoveSendInterval)
        {
            SendMove();
            moveSentAtSeconds_ = nowSeconds_;
            moveDirty_ = false;
        }
    }

    private void SetLocalPosition(float x, float y)
    {
        // 존이 있는 구간 안으로만 제한한다. WorldMaxX를 넘어서면 그 좌표를 담당하는 존이
        // 아예 없어서, 서버는 로컬 상태를 먼저 지운 뒤 라우팅 대상을 못 찾고 그대로 끝낸다
        // (ZoneInstance::HandleMove -> RequestZoneTransfer). 화면에서는 캐릭터가 사라진 것처럼
        // 보이는데 원인이 클라이언트에 없어서 헷갈리므로, 애초에 못 나가게 막는다.
        const float Epsilon = 0.001f;
        localX_ = Math.Clamp(x, 0.0f, ZoneLayout.WorldMaxX - Epsilon);
        localY_ = Math.Clamp(y, 0.0f, ZoneLayout.WorldMaxY - Epsilon);
        world_.SetRequestedPosition(localX_, localY_);
        moveDirty_ = true;
    }

    private void HandleEcho()
    {
        if (nowSeconds_ - echoSentAtSeconds_ >= EchoInterval)
        {
            SendEcho();
        }
    }

    protected override void Draw(GameTime gameTime)
    {
        GraphicsDevice.Clear(new Color(10, 12, 18));

        if (painter_ is not { } painter || spriteBatch_ is not { } spriteBatch || font_ is not { } font)
        {
            base.Draw(gameTime);
            return;
        }

        var totalSeconds = gameTime.TotalGameTime.TotalSeconds;
        var nowUt = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
        var screen = GraphicsDevice.Viewport.Bounds;

        Layout(painter, screen);

        spriteBatch.Begin();

        zoneView_.Draw(painter, world_, totalSeconds);
        hud_.DrawStatusBar(painter, link_, world_, new Rectangle(0, 0, screen.Width, 38));
        hud_.DrawNoticeToast(painter, world_, screen, totalSeconds);
        hud_.DrawHelpBar(painter,
                         new Rectangle(0, screen.Height - 24, screen.Width, 24),
                         font.FontName);

        // 존 뷰 클릭으로 순간 이동. 패널을 그리기 **전에** 판정해야 하지만, 패널 위 클릭이
        // 존 뷰로 새는 걸 막아야 하므로 열려 있는 패널 영역을 먼저 제외한다.
        HandleZoneClick();

        var chatMessage = chatPanel_.UpdateAndDraw(painter, input_, world_, totalSeconds);
        if (chatMessage is not null)
        {
            SendChat(chatMessage);
        }

        var mailRequest = mailPanel_.UpdateAndDraw(painter, input_, world_, totalSeconds, nowUt);
        HandleMailRequest(mailRequest);

        couponPanel_.UpdateAndDraw(painter, input_, world_, totalSeconds);

        spriteBatch.End();

        input_.EndFrame();
        base.Draw(gameTime);
    }

    /// <summary>
    /// 화면 크기에 맞춰 패널 위치를 다시 잡는다. 창 크기 조절을 허용했으므로 매 프레임
    /// 계산한다 — 위치를 한 번만 잡아두면 창을 늘렸을 때 패널이 화면 밖에 남는다.
    /// </summary>
    private void Layout(Painter painter, Rectangle screen)
    {
        const int Margin = 12;
        var statusHeight = 38;
        var helpHeight = 24;

        var chatWidth = Math.Min(600, (screen.Width - (Margin * 3)) / 2);
        var chatHeight = (painter.SmallFont.LineHeight * 9) + (painter.Font.LineHeight * 2) + 70;
        var chatRect = new Rectangle(
            Margin,
            screen.Height - helpHeight - Margin - chatHeight,
            chatWidth,
            chatHeight);

        zoneView_.Layout(new Rectangle(
            Margin,
            statusHeight + Margin,
            screen.Width - (Margin * 2),
            chatRect.Y - statusHeight - (Margin * 2)));

        chatPanel_.Layout(chatRect);

        var iconSize = 62;
        var iconRect = new Rectangle(
            screen.Width - Margin - iconSize,
            screen.Height - helpHeight - Margin - iconSize,
            iconSize,
            iconSize);

        var couponToggleRect = new Rectangle(
            iconRect.X - 8 - 74,
            iconRect.Bottom - (painter.Font.LineHeight + 12),
            74,
            painter.Font.LineHeight + 12);

        var mailPanelWidth = Math.Min(540, screen.Width - (Margin * 2));
        var mailPanelHeight = Math.Min(420, iconRect.Y - statusHeight - (Margin * 3));
        mailPanel_.Layout(iconRect, new Rectangle(
            iconRect.Right - mailPanelWidth,
            iconRect.Y - Margin - mailPanelHeight,
            mailPanelWidth,
            mailPanelHeight));

        var couponPanelHeight = (painter.SmallFont.LineHeight * 5) + painter.Font.LineHeight + 60;
        couponPanel_.Layout(couponToggleRect, new Rectangle(
            couponToggleRect.Right - mailPanelWidth,
            couponToggleRect.Y - Margin - couponPanelHeight,
            mailPanelWidth,
            couponPanelHeight));
    }

    private void HandleZoneClick()
    {
        if (!input_.MouseLeftPressed || !zoneView_.Bounds.Contains(input_.MousePosition))
        {
            return;
        }

        // 열려 있는 패널이나 아이콘 위 클릭은 그 위젯 몫이다.
        if (mailPanel_.IconBounds.Contains(input_.MousePosition)
            || couponPanel_.ToggleBounds.Contains(input_.MousePosition)
            || (mailPanel_.IsOpen && mailPanel_.PanelBounds.Contains(input_.MousePosition))
            || (couponPanel_.IsOpen && couponPanel_.PanelBounds.Contains(input_.MousePosition))
            || chatPanel_.Bounds.Contains(input_.MousePosition))
        {
            return;
        }

        var target = zoneView_.ScreenToWorld(input_.MousePosition);
        SetLocalPosition(target.X, target.Y);
    }

    private void HandleMailRequest(MailRequest request)
    {
        switch (request.Action)
        {
            case MailAction.Add:
            {
                var writer = new BinaryPacketWriter();
                writer.WriteString(request.Title);
                writer.WriteString(request.Body);
                writer.WriteInt64(request.DurationSec);
                link_.Send(PacketId.C2ZMailAdd, writer);
                break;
            }

            case MailAction.Delete:
            {
                var writer = new BinaryPacketWriter();
                writer.WriteUInt32(request.MailId);
                link_.Send(PacketId.C2ZMailDel, writer);
                break;
            }

            case MailAction.None:
            default:
                break;
        }
    }

    private void SendMove()
    {
        // MovePacket은 #pragma pack(1) 구조체 { float x; float y; }라 길이 접두 없이 8바이트다.
        var writer = new BinaryPacketWriter(sizeof(float) * 2);
        writer.WriteSingle(localX_);
        writer.WriteSingle(localY_);
        link_.Send(PacketId.C2ZMove, writer);
    }

    private void SendChat(string message)
    {
        var writer = new BinaryPacketWriter();
        writer.WriteString(message);
        link_.Send(PacketId.C2ZChat, writer);
    }

    private void SendEcho()
    {
        // Z2CEchoAck는 본문을 그대로 되돌려주므로, 보낼 때 전송 시각을 심어두면 서버가 왕복
        // 시간을 알려주는 셈이 된다 — 클라이언트가 "몇 번째 핑을 언제 보냈는지"를 따로
        // 기억할 필요가 없다(응답이 순서대로 오지 않아도 상관없다).
        var writer = new BinaryPacketWriter(sizeof(long));
        writer.WriteInt64(BitConverter.DoubleToInt64Bits(nowSeconds_));
        link_.Send(PacketId.C2ZEcho, writer);
        echoSentAtSeconds_ = nowSeconds_;
    }

    protected override void UnloadContent()
    {
        painter_?.Dispose();
        font_?.Dispose();
        smallFont_?.Dispose();
        spriteBatch_?.Dispose();
        coupons_.Dispose();
        link_.Dispose();
        base.UnloadContent();
    }
}
