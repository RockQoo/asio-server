using VisualClient;

// 실행 인자: [GatewayHost] [GatewayPort] [GmToolBaseUrl]
//   예) VisualClient.exe 127.0.0.1 9000 http://127.0.0.1:5080
//
// 인자를 파일이 아니라 명령줄로만 받는 이유: 이 도구는 같은 PC에서 창을 여러 개 띄워
// 존 브로드캐스트와 핸드오프를 확인하는 용도가 많고, 그때 창마다 다른 대상을 지정해야 한다.
// 설정 파일 하나를 공유하면 그게 불가능하다.
var options = ClientOptions.Default;

if (args.Length > 0)
{
    options = options with { GatewayHost = args[0] };
}

if (args.Length > 1 && int.TryParse(args[1], out var port))
{
    options = options with { GatewayPort = port };
}

if (args.Length > 2)
{
    options = options with { GmToolBaseUrl = args[2] };
}

using var game = new VisualClientGame(options);
game.Run();
