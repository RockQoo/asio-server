@echo off
REM Launch order: WorldServer -> ZoneServer(0,1) -> GatewayServer, each in its own console window.
REM Zone/Gateway use Connector with retry, so slight startup ordering gaps are fine.
setlocal
set "BIN=%~dp0..\bin\x64\Debug"

if not exist "%BIN%\WorldServer.exe" (
    echo [server.bat] No executables found under "%BIN%". Build asio-server.slnx first.
    pause
    exit /b 1
)

start "WorldServer" cmd /k "cd /d "%BIN%" && WorldServer.exe"
timeout /t 1 /nobreak >nul

start "ZoneServer (0,1)" cmd /k "cd /d "%BIN%" && ZoneServer.exe 0,1"
timeout /t 1 /nobreak >nul

start "GatewayServer" cmd /k "cd /d "%BIN%" && GatewayServer.exe"

endlocal
