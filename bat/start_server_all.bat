@echo off
REM 서버 3종을 각각 별도 콘솔 창으로 띄운다.
REM 기동 순서: WorldServer -> ZoneServer(0,1) -> GatewayServer.
REM Zone/Gateway는 Connector가 재시도를 하므로 순서가 조금 어긋나도 문제없다.
REM
REM 사용법: start_server_all.bat [Debug^|Release]   (기본값: Debug)
REM   VS attach 디버깅은 Debug 빌드를 쓸 것. Release는 최적화 때문에 중단점이 어긋난다.
REM   README 5절의 처리량/지연 수치를 재현할 때만 Release를 쓴다(Debug가 약 5배 느림).
chcp 65001 >nul
setlocal

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"

REM bat\..\ 를 남기지 않도록 절대 경로로 정규화한다.
for %%I in ("%~dp0..\bin\x64\%CONFIG%") do set "BIN=%%~fI"

if not exist "%BIN%\WorldServer.exe" (
    echo [start_server_all] "%BIN%" 에 실행 파일이 없습니다. asio-server.slnx 를 먼저 빌드하세요.
    echo [start_server_all] 참고: start_server_all.bat Release  로 최적화 빌드를 띄울 수 있습니다.
    pause
    exit /b 1
)

echo [start_server_all] %CONFIG% 바이너리를 "%BIN%" 에서 실행합니다.
echo.

REM 실행 파일 앞의 .\ 는 생략하면 안 된다 -- 현재 디렉터리를 실행 검색 경로에서 제외하는
REM 설정(NoDefaultCurrentDirectoryInExePath)이 걸린 PC에서는 "명령을 찾을 수 없음"이 된다.
REM 창 제목의 asio 접두사는 stop_server_all.bat 이 창을 닫을 때 쓰는 식별자다.
start "asio WorldServer (%CONFIG%)"    cmd /k "cd /d "%BIN%" && .\WorldServer.exe"
ping -n 2 127.0.0.1 >nul

start "asio ZoneServer 0,1 (%CONFIG%)" cmd /k "cd /d "%BIN%" && .\ZoneServer.exe 0,1"
ping -n 2 127.0.0.1 >nul

start "asio GatewayServer (%CONFIG%)"  cmd /k "cd /d "%BIN%" && .\GatewayServer.exe"

REM 프로세스가 목록에 올라올 때까지 기다린 뒤 PID를 뽑는다.
ping -n 3 127.0.0.1 >nul

echo.
echo ============================================================
echo  Visual Studio 연결용 PID  (디버그 ^> 프로세스에 연결, Ctrl+Alt+P)
echo ============================================================
set "ANY="
for %%E in (WorldServer.exe ZoneServer.exe GatewayServer.exe) do (
    for /f "tokens=2 delims=," %%P in ('tasklist /fi "imagename eq %%E" /fo csv /nh 2^>nul') do (
        echo   %%~E   PID %%~P
        set "ANY=1"
    )
)
if not defined ANY echo   ^(기동된 프로세스가 없습니다 -- 각 창의 오류 메시지를 확인하세요^)
echo ============================================================
echo.
echo  세 프로세스 모두에 붙이려면 연결 대화상자에서 Ctrl 로 다중 선택하세요.
echo  종료: stop_server_all.bat
echo.

endlocal
