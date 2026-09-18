using Client;
using System.Runtime.InteropServices;

// **DPI 인식을 먼저 켠다.** 켜지 않으면 Windows 가 창을 가상화해서, 아래에서 요청한
// 백버퍼 크기가 실제 픽셀의 절반짜리 창으로 나온다(4K 에 배율 200%면 1800 요청이 906 이 된다).
// GraphicsDeviceManager 를 만들기 전에 불러야 효과가 있다.
if (OperatingSystem.IsWindows())
{
    _ = DpiSupport.SetProcessDpiAwarenessContext(DpiSupport.DpiAwarenessContextPerMonitorAwareV2);
}

// 실행 인자: [GatewayHost] [GatewayPort] [GmToolBaseUrl]
//            (+ 어디든 --auto / --auto-rev / --id=<아이디> / --pw=<비밀번호>)
//   예) Client.exe 127.0.0.1 9000 http://127.0.0.1:5080
//   예) Client.exe --auto            (자동 순회로 시작, 나머지는 기본값)
//   예) Client.exe --auto-rev        (자동 순회를 반대 방향으로 -- 창 두 개를 서로
//                                           반대로 돌리면 존에서 만나고 갈라지는 게 보인다)
//   예) Client.exe --id=tester2      (로그인 화면의 아이디 칸을 미리 채운다)
//
// --id/--pw는 로그인 화면을 **채워둘 뿐 자동 로그인이 아니다** -- 접속은 사람이 눌러야 한다.
// 창을 여러 개 띄워 서로 다른 계정으로 붙일 때 매번 타이핑하지 않으려는 것이고, 계정이 없으면
// 서버가 그 자리에서 만들어주므로(자동 가입) 시드에 없는 이름을 줘도 된다.
//
// 인자를 파일이 아니라 명령줄로만 받는 이유: 이 도구는 같은 PC에서 창을 여러 개 띄워
// 존 브로드캐스트와 핸드오프를 확인하는 용도가 많고, 그때 창마다 다른 대상을 지정해야 한다.
// 설정 파일 하나를 공유하면 그게 불가능하다.
//
// -- 로 시작하는 옵션은 위치 인자와 섞이지 않게 먼저 걷어낸다 -- 순서를 외우지 않고 아무
// 자리에나 붙일 수 있어야 여러 창을 띄우는 명령을 손으로 조립할 때 편하다(자동 순회 자체는
// F2 로 실행 중에도 켜고 끌 수 있다).
var reverse = args.Any(a => string.Equals(a, "--auto-rev", StringComparison.OrdinalIgnoreCase));
var autoTour = reverse
               || args.Any(a => string.Equals(a, "--auto", StringComparison.OrdinalIgnoreCase));
var positional = args.Where(a => !a.StartsWith("--", StringComparison.Ordinal)).ToArray();

var options = ClientOptions.Default with { AutoTour = autoTour, AutoTourReverse = reverse };

// --key=value 형태만 값을 갖는다. 위치 인자와 섞이지 않게 하려고 붙여 쓰는 형식으로 통일했다.
static string? ValueOf(string[] args, string key) =>
    args.FirstOrDefault(a => a.StartsWith(key, StringComparison.OrdinalIgnoreCase))?[key.Length..];

if (ValueOf(args, "--id=") is { Length: > 0 } loginName)
{
    options = options with { LoginName = loginName };
}

if (ValueOf(args, "--pw=") is { } loginPassword)
{
    options = options with { LoginPassword = loginPassword };
}

if (positional.Length > 0)
{
    options = options with { GatewayHost = positional[0] };
}

if (positional.Length > 1 && int.TryParse(positional[1], out var port))
{
    options = options with { GatewayPort = port };
}

if (positional.Length > 2)
{
    options = options with { GmToolBaseUrl = positional[2] };
}

using var game = new ClientGame(options);
game.Run();

// DPI 인식을 켜는 Win32 진입점. .NET 에 이걸 켜는 표준 API 가 없어서 직접 부른다.
static class DpiSupport
{
    // PerMonitorAwareV2 의 상수값. 모니터마다 배율이 다른 환경에서도 창을 옮길 때 다시 맞춘다.
    internal static readonly nint DpiAwarenessContextPerMonitorAwareV2 = -4;

    // LibraryImport 가 아니라 DllImport 인 이유: LibraryImport 는 생성 코드가 unsafe 라
    // 프로젝트에 AllowUnsafeBlocks 를 켜야 한다. 기동에 한 번 부르는 함수 하나 때문에
    // 프로젝트 전체의 unsafe 를 켜지 않는다.
    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    internal static extern bool SetProcessDpiAwarenessContext(nint value);
}
