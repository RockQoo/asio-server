@echo off
REM Launch order: WorldServer -> ZoneServer(0,1) -> GatewayServer, each in its own console window.
REM Zone/Gateway use Connector with retry, so slight startup ordering gaps are fine.
REM
REM Usage: server.bat [Debug|Release]   (default: Debug)
REM   Release is required to reproduce the throughput/latency numbers in README section 5 --
REM   Debug builds measured about 5x slower.
setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
set "BIN=%~dp0..\bin\x64\%CONFIG%"

if not exist "%BIN%\WorldServer.exe" (
    echo [server.bat] No executables found under "%BIN%". Build asio-server.slnx first.
    echo [server.bat] Tip: server.bat Release   launches the optimized build instead.
    pause
    exit /b 1
)

echo [server.bat] Launching %CONFIG% binaries from "%BIN%".

start "WorldServer (%CONFIG%)" cmd /k "cd /d "%BIN%" && WorldServer.exe"
timeout /t 1 /nobreak >nul

start "ZoneServer 0,1 (%CONFIG%)" cmd /k "cd /d "%BIN%" && ZoneServer.exe 0,1"
timeout /t 1 /nobreak >nul

start "GatewayServer (%CONFIG%)" cmd /k "cd /d "%BIN%" && GatewayServer.exe"

endlocal
