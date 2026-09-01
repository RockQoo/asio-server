@echo off
REM TestClient is an interactive REPL (echo/move/chat/mail commands), so run it in this
REM same console instead of spawning a new window. Connects to Gateway (127.0.0.1:9000).
setlocal
set "BIN=%~dp0..\bin\x64\Debug"

if not exist "%BIN%\TestClient.exe" (
    echo [client.bat] No executable found under "%BIN%". Build asio-server.slnx first.
    pause
    exit /b 1
)

cd /d "%BIN%"
TestClient.exe

endlocal
