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
public sealed record ClientOptions(
    string GatewayHost,
    int GatewayPort,
    string GmToolBaseUrl,
    bool AutoTour = false,
    bool AutoTourReverse = false)
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

    /// <summary>
    /// 자동 순회(<see cref="ClientOptions.AutoTour"/>) 한 바퀴에 걸리는 시간(초).
    ///
    /// <para>
    /// <b>왜 원인가</b>: 존이 2×2 격자라(존 1,2 / 존 3,4) 한 바퀴 돌면 네 존을 순서대로 지나며
    /// <b>가로 경계와 세로 경계를 각각 두 번씩</b> 넘는다. 가로 경계는 같은 프로세스 안의 스레드
    /// 간 이동이고 세로 경계는 프로세스를 넘는 핸드오프라, 원 하나로 두 경로를 다 밟는다.
    /// 직선 왕복으로는 한 종류만 넘게 되어 반쪽만 확인된다.
    /// </para>
    ///
    /// <para>
    /// 한 바퀴를 이 정도로 잡은 이유: 너무 빠르면 경계에서 핸드오프가 초당 여러 번 일어나
    /// 로그를 읽을 수 없고(경계에 히스테리시스가 없다), 너무 느리면 지켜보는 시간이 길어진다.
    /// </para>
    /// </summary>
    private const double AutoTourLapSeconds = 22.0;

    /// <summary>
    /// 자동 순회 반지름의 여유(월드 단위). 벽에 붙지 않게 안쪽으로 넣는다.
    ///
    /// <para>
    /// 이 값이 존 한 변의 절반(5)보다 <b>작아야</b> 원이 네 존을 다 지난다 — 여유가 너무 크면
    /// 원이 월드 중앙(네 존이 만나는 점) 근처로 쪼그라들어 경계를 아슬아슬하게만 넘는다.
    /// </para>
    /// </summary>
    private const float AutoTourMargin = 1.5f;

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

    /// <summary>자동 순회 중인가. 옵션으로 켜고 F2로 끄거나 다시 켤 수 있다.</summary>
    private bool autoTour_;

    /// <summary>
    /// 자동 순회 타원의 시작 위상(라디안).
    ///
    /// <para>
    /// 창마다 다르게 잡는 이유: 여러 창을 자동 순회로 띄우면 위상이 같을 때 전부 같은 좌표로
    /// 겹쳐 움직여서 "다른 플레이어가 보이는지"(브로드캐스트)를 확인할 수 없다. 위상을 흩어
    /// 놓으면 서로 다른 존에 있다가 만나는 장면까지 그대로 보인다.
    /// </para>
    /// </summary>
    private readonly double autoTourPhase_ = Random.Shared.NextDouble() * Math.Tau;

    /// <summary>
    /// 자동 순회 방향. <c>-1</c>이면 반대로 돈다(<c>--auto-rev</c>).
    ///
    /// <para>
    /// 창 두 개를 서로 반대로 돌리면 <b>같은 존에서 만나고 다시 갈라지는 장면</b>이 한 바퀴에
    /// 두 번 생긴다 — 브로드캐스트(다른 플레이어가 보이는지)와 존 이탈(안 보이게 되는지)을
    /// 둘 다 규칙적으로 확인할 수 있다. 같은 방향으로만 돌면 위상 차이가 그대로 유지돼서
    /// 서로 마주치지 않을 수도 있다.
    /// </para>
    /// </summary>
    private readonly double autoTourDirection_;

    /// <summary>자동 순회가 마지막으로 통과한 zoneId. 순회가 존을 실제로 다 도는지 세는 데 쓴다.</summary>
    private uint autoTourLastZoneId_;

    /// <summary>자동 순회 중 zoneId가 바뀐 횟수. HUD에 띄워 핸드오프가 도는지 한눈에 보게 한다.</summary>
    private int autoTourZoneChanges_;

    public VisualClientGame(ClientOptions options)
    {
        options_ = options;
        autoTour_ = options.AutoTour;
        autoTourDirection_ = options.AutoTourReverse ? -1.0 : 1.0;
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

        // F2로 자동 순회를 켜고 끈다. 자동으로 도는 동안 직접 몰아보고 싶은 순간이 있어서
        // 실행 인자로만 정하지 않고 실행 중에도 바꿀 수 있게 뒀다.
        if (input_.IsKeyPressed(Keys.F2) && !IsTyping)
        {
            autoTour_ = !autoTour_;
            world_.AddSystemLine(autoTour_
                ? "자동 순회를 켰습니다. 타원으로 돌면서 존 경계를 계속 넘습니다. (F2로 끄기)"
                : "자동 순회를 껐습니다. WASD로 직접 이동합니다. (F2로 켜기)");
        }
    }

    /// <summary>
    /// 자동 순회: 월드 가운데를 중심으로 타원을 돈다.
    ///
    /// <para>
    /// 목적은 <b>사람이 지켜보지 않아도 존 1~4를 계속 왕복하게 만드는 것</b>이다. 존 경계에
    /// 히스테리시스가 없어서 경계 위에서 미세하게 흔들면 초당 여러 번 핸드오프가 일어나는데,
    /// 타원 운동은 경계를 <em>한 방향으로 통과</em>하므로 그 문제 없이 핸드오프만 깔끔하게
    /// 반복된다.
    /// </para>
    /// </summary>
    private void UpdateAutoTour()
    {
        var angle = autoTourPhase_ + (autoTourDirection_ * nowSeconds_ / AutoTourLapSeconds * Math.Tau);

        var centerX = ZoneLayout.WorldMaxX * 0.5f;
        var centerY = ZoneLayout.WorldMaxY * 0.5f;
        var radiusX = centerX - AutoTourMargin;
        var radiusY = centerY - AutoTourMargin;

        SetLocalPosition(
            centerX + (radiusX * (float)Math.Cos(angle)),
            centerY + (radiusY * (float)Math.Sin(angle)));

        // 존이 바뀐 횟수를 세어 둔다. 화면을 잠깐 봐도 "돌고 있는지"가 숫자로 드러난다.
        if (world_.HasEnteredZone && world_.MyZoneId != autoTourLastZoneId_)
        {
            if (autoTourLastZoneId_ != 0)
            {
                ++autoTourZoneChanges_;
            }
            autoTourLastZoneId_ = world_.MyZoneId;
        }
    }

    /// <summary>입력칸 중 하나라도 포커스를 갖고 있는가. 그러면 WASD는 타이핑이다.</summary>
    private bool IsTyping =>
        chatPanel_.InputHasFocus || mailPanel_.HasFocusedField || couponPanel_.HasFocusedField;

    private void HandleMovement(GameTime gameTime)
    {
        // 자동 순회가 켜져 있으면 좌표를 계산해서 넣는다 -- 키 입력과 섞으면 서로 좌표를
        // 덮어써서 어느 쪽이 움직인 건지 알 수 없다.
        if (autoTour_)
        {
            UpdateAutoTour();
        }
        else if (!IsTyping)
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
        hud_.DrawStatusBar(painter, link_, world_, new Rectangle(0, 0, screen.Width, 38),
                           autoTour_ ? autoTourZoneChanges_ : null,
                           options_.AutoTourReverse);
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
