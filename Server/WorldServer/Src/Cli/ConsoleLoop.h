#pragma once

#include <atomic>

namespace World
{
    class App;

    // 콘솔에서 `notice <메시지>` / `quit` 을 받는 REPL. **별도 스레드에서 돈다**(main 참고).
    //
    // World 가 클라이언트 레지스트리를 직접 들고 있어서 Zone 을 거치지 않고 전체에 브로드캐스트할
    // 수 있다는 것을 손으로 확인하는 용도다. 운영툴(GmTool)의 공지 기능과 같은 경로를 탄다.
    void RunConsoleLoop(App& app, std::atomic<bool>& running);
}
