@echo off
REM 서버 프로세스 4개를 Windows Terminal "탭 하나"에 모아서 띄운다.
REM 기동 순서: WorldServer -> ZoneServer(1,2) -> ZoneServer(3,4) -> GatewayServer.
REM Zone/Gateway는 Connector가 재시도를 하므로 순서가 조금 어긋나도 문제없다.
REM
REM 존 배치는 2x2 격자다(zoneId는 1부터, 0은 "존 없음" 예약값):
REM     y:[10,20)   존 1   존 2      <- ZoneServer.exe 1,2
REM     y:[0,10)    존 3   존 4      <- ZoneServer.exe 3,4
REM                 x:[0,10)  x:[10,20)
REM 가로 이동(1<->2, 3<->4)은 같은 프로세스 안의 BASIC 스레드 간 이동이고,
REM 세로 이동(1<->3, 2<->4)이 프로세스(TCP 링크)를 넘는 핸드오프다.
REM 규칙은 ZoneServer main.cpp의 ParseZoneList(kZoneSize/kZonesPerRow/kZoneRows).
REM
REM 사용법: start_server_all.bat [Debug^|Release]   (기본값: Debug)
REM   VS attach 디버깅은 Debug 빌드를 쓸 것. Release는 최적화 때문에 중단점이 어긋난다.
REM   README 5절의 처리량/지연 수치를 재현할 때만 Release를 쓴다(Debug가 약 5배 느림).
REM
REM *** 서브루틴(call :label)을 쓰지 말 것 ***
REM cmd는 라벨을 찾을 때 파일을 바이트 오프셋으로 되감는데, chcp 65001과 한글 주석이 섞이면
REM 그 오프셋이 줄 중간에 떨어져 주석 조각을 명령으로 실행한다("'개'은 내부 명령이 아닙니다"
REM 같은 오류가 쏟아진다). 실제로 이 파일에서 한 번 그렇게 깨졌고, 주석 길이만 바꿔도 재현
REM 여부가 달라져서 원인을 찾기 어렵다. 그래서 반복을 감수하고 네 줄을 펼쳐 뒀다.
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

REM cmd.exe 자체에는 탭이 없다 -- 탭은 Windows Terminal(wt.exe)만 만들 수 있으므로 있으면 쓰고,
REM 없으면 예전처럼 창을 따로 띄운다(공개 포트폴리오라 wt 없는 환경에서도 그대로 돌아야 한다).
set "WT="
where wt.exe >nul 2>nul
if not errorlevel 1 set "WT=1"

REM -w 로 창 이름을 고정하는 것이 핵심이다. 이름을 주지 않으면 호출마다 새 창이 열려서 탭으로
REM 모이지 않는다. 같은 이름의 창이 이미 있으면 그 창에 탭으로 붙는다(이전 실행이 남아 있으면
REM 탭이 계속 쌓이므로, 창을 닫고 다시 띄우는 편이 깔끔하다).
set "WTWIN=asio-server"

REM 실행 명령을 통째로 따옴표로 묶어 cmd에 넘긴다 -- 인자에 콤마가 있으면(ZoneServer.exe 1,2)
REM cmd가 콤마를 구분자로 봐서 두 인자로 쪼갤 수 있고, 따옴표 안에서는 한 덩어리로 유지된다.
REM 창 제목의 "asio " 접두사는 fallback 경로에만 붙인다 -- stop_server_all.bat이 그 접두사로
REM 남은 창을 닫기 때문이다. wt 탭은 창이 하나라서 제목으로 골라 닫을 수 없고, 접두사를 붙이면
REM 정리 스크립트가 탭 4개를 통째로 날릴 수 있다.

echo [start_server_all] %CONFIG% 바이너리를 "%BIN%" 에서 실행합니다.
if defined WT (
    echo [start_server_all] Windows Terminal 창 하나에 탭 4개로 띄웁니다.
) else (
    echo [start_server_all] wt.exe 가 없어 콘솔 창을 따로 띄웁니다.
)
echo.

if defined WT (
    wt.exe -w %WTWIN% new-tab --title "WorldServer (%CONFIG%)" -d "%BIN%" cmd /k ".\WorldServer.exe"
) else (
    start "asio WorldServer (%CONFIG%)" cmd /k "cd /d "%BIN%" && .\WorldServer.exe"
)
ping -n 2 127.0.0.1 >nul

if defined WT (
    wt.exe -w %WTWIN% new-tab --title "ZoneServer 1,2 (%CONFIG%)" -d "%BIN%" cmd /k ".\ZoneServer.exe 1,2"
) else (
    start "asio ZoneServer 1,2 (%CONFIG%)" cmd /k "cd /d "%BIN%" && .\ZoneServer.exe 1,2"
)
ping -n 2 127.0.0.1 >nul

if defined WT (
    wt.exe -w %WTWIN% new-tab --title "ZoneServer 3,4 (%CONFIG%)" -d "%BIN%" cmd /k ".\ZoneServer.exe 3,4"
) else (
    start "asio ZoneServer 3,4 (%CONFIG%)" cmd /k "cd /d "%BIN%" && .\ZoneServer.exe 3,4"
)
ping -n 2 127.0.0.1 >nul

if defined WT (
    wt.exe -w %WTWIN% new-tab --title "GatewayServer (%CONFIG%)" -d "%BIN%" cmd /k ".\GatewayServer.exe"
) else (
    start "asio GatewayServer (%CONFIG%)" cmd /k "cd /d "%BIN%" && .\GatewayServer.exe"
)

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
if not defined ANY echo   ^(기동된 프로세스가 없습니다 -- 각 탭의 오류 메시지를 확인하세요^)
echo ============================================================
echo.
echo  ZoneServer.exe 가 2개 뜨는 것이 정상이다(존 1,2 담당 프로세스와 3,4 담당 프로세스).
echo  네 프로세스 모두에 붙이려면 연결 대화상자에서 Ctrl 로 다중 선택하세요.
echo  종료: stop_server_all.bat
echo.
