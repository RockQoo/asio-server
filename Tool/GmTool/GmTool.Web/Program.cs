using GmTool.Web.Components;
using GmTool.Web.Data;
using GmTool.Web.Endpoints;
using GmTool.Web.Options;
using GmTool.Web.Repositories;
using GmTool.Web.Services;
using Microsoft.AspNetCore.Authentication.Cookies;
using Serilog;

var builder = WebApplication.CreateBuilder(args);

// 로깅: "누가 무엇을 보냈는가"가 곧 감사 기록이라 콘솔만으로는 부족해 파일로도 남긴다.
builder.Host.UseSerilog((context, services, configuration) => configuration
    .ReadFrom.Configuration(context.Configuration)
    .ReadFrom.Services(services)
    .Enrich.FromLogContext()
    .WriteTo.Console()
    .WriteTo.File("logs/gmtool-.log", rollingInterval: RollingInterval.Day, retainedFileCountLimit: 14));

builder.Services.Configure<DatabaseOptions>(builder.Configuration.GetSection(DatabaseOptions.SectionName));
builder.Services.Configure<WorldLinkOptions>(builder.Configuration.GetSection(WorldLinkOptions.SectionName));
builder.Services.Configure<AuthOptions>(builder.Configuration.GetSection(AuthOptions.SectionName));
builder.Services.Configure<CouponOptions>(builder.Configuration.GetSection(CouponOptions.SectionName));

// 리포지토리를 싱글턴으로 두는 이유: 상태가 없고 연결은 호출마다 팩토리에서 새로 받는다.
// 스코프드로 두면 Blazor 회로 수명에 묶여 백그라운드 작업에서 스코프를 따로 만들어야 한다.
builder.Services.AddSingleton<SqlServerConnectionFactory>();
builder.Services.AddSingleton<OperatorRepository>();
builder.Services.AddSingleton<CommandLogRepository>();
builder.Services.AddSingleton<CouponCampaignRepository>();
builder.Services.AddSingleton<CouponRepository>();

builder.Services.AddSingleton<OperatorAuthService>();
builder.Services.AddSingleton<GameCommandService>();
builder.Services.AddSingleton<CouponIssueService>();
builder.Services.AddSingleton<CouponRedeemService>();
builder.Services.AddSingleton<DatabaseInitializer>();

// WorldLinkClient는 싱글턴이면서 백그라운드 재연결 루프도 돌린다. AddHostedService<T>()만
// 쓰면 DI가 별도 인스턴스를 하나 더 만들기 때문에 이렇게 등록해야 한다.
builder.Services.AddSingleton<WorldLinkClient>();
builder.Services.AddHostedService(sp => sp.GetRequiredService<WorldLinkClient>());

// 인증: 웹 UI는 쿠키, HTTP API는 HMAC 토큰(OperatorAuthService에서 직접 검증)
builder.Services
    .AddAuthentication(CookieAuthenticationDefaults.AuthenticationScheme)
    .AddCookie(options =>
    {
        options.LoginPath = "/login";
        options.LogoutPath = "/logout";
        options.AccessDeniedPath = "/login";
        options.ExpireTimeSpan = TimeSpan.FromHours(12);
        options.SlidingExpiration = true;
        options.Cookie.Name = "GmTool.Auth";
        options.Cookie.HttpOnly = true;
        options.Cookie.SameSite = SameSiteMode.Lax;
    });

builder.Services.AddAuthorization();
builder.Services.AddCascadingAuthenticationState();
builder.Services.AddHttpContextAccessor();

builder.Services.AddRazorComponents()
    .AddInteractiveServerComponents();

var app = builder.Build();

// DB가 안 떠 있어도 웹은 뜨게 한다 -- 예외를 올리면 프로세스가 죽어서 "왜 안 뜨는지"를
// 화면에서 확인할 수도 없다. 대신 에러 로그를 크게 남긴다.
try
{
    using var scope = app.Services.CreateScope();
    await scope.ServiceProvider.GetRequiredService<DatabaseInitializer>().InitializeAsync();
}
catch (Exception ex)
{
    app.Logger.LogError(ex, "DB 초기화 실패 — SQL Server 연결 설정(Database:ConnectionString)을 확인하세요.");
}

if (!app.Environment.IsDevelopment())
{
    app.UseExceptionHandler("/Error", createScopeForErrors: true);
    app.UseHsts();
    app.UseHttpsRedirection();
}

app.UseStatusCodePagesWithReExecute("/not-found", createScopeForStatusCodePages: true);

app.UseAuthentication();
app.UseAuthorization();
app.UseAntiforgery();

app.MapStaticAssets();
app.MapGmToolApi();
app.MapRazorComponents<App>()
    .AddInteractiveServerRenderMode();

app.Run();
