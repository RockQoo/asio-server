using VisualClient;

// 실행 인자: [GatewayHost] [GatewayPort] [GmToolBaseUrl]  (+ 어디든 --auto / --auto-rev)
//   예) VisualClient.exe 127.0.0.1 9000 http://127.0.0.1:5080
//   예) VisualClient.exe --auto            (자동 순회로 시작, 나머지는 기본값)
//   예) VisualClient.exe --auto-rev        (자동 순회를 반대 방향으로 -- 창 두 개를 서로
//                                           반대로 돌리면 존에서 만나고 갈라지는 게 보인다)
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

using var game = new VisualClientGame(options);
game.Run();
