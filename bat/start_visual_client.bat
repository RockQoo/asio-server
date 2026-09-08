@echo off
REM VisualClient(MonoGame 창 클라이언트)를 실행한다. Gateway(127.0.0.1:9000)로 접속하고,
REM 쿠폰 등록만 운영툴(127.0.0.1:5080 -- start_gmtool.bat 의 기본 포트)의 HTTP API로 나간다.
REM
REM C++ 솔루션이 아니라 별도 .NET 솔루션(Tool\VisualClient\VisualClient.slnx)이라 bin\x64 가
REM 아닌 자기 obj/bin 경로로 빌드된다 -- 그래서 여기서는 dotnet run 으로 띄운다.
REM
REM 사용법: start_visual_client.bat [개수] [Debug^|Release] [auto]   (기본값: 1 Debug)
REM   개수를 2 이상으로 주면 창을 그만큼 띄운다. 존 브로드캐스트(다른 플레이어가 보이는지)와
REM   핸드오프를 확인하려면 최소 2개가 필요하다 -- 창 하나로는 "나 혼자" 상태라 브로드캐스트가
REM   왔는지 안 왔는지 구분되지 않는다.
REM
REM   세 번째 인자에 auto 를 주면 자동 순회(원을 그리며 존 경계를 계속 넘음)로 띄운다.
REM   창 번호가 짝수인 창은 반대 방향으로 돌린다 -- 서로 반대로 돌면 한 바퀴에 두 번 만나고
REM   갈라져서, 같은 존에 있을 때 보이고 다른 존으로 나가면 사라지는 걸 규칙적으로 볼 수 있다.
REM   같은 방향으로만 돌리면 위상 차이가 유지돼 계속 안 마주칠 수도 있다.
REM   (실행 중에는 F2 로 켜고 끌 수 있다.)
chcp 65001 >nul
setlocal

set "COUNT=%~1"
if "%COUNT%"=="" set "COUNT=1"

set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=Debug"

set "AUTO="
if /i "%~3"=="auto" set "AUTO=1"

for %%I in ("%~dp0..\Tool\VisualClient\VisualClient") do set "PROJ=%%~fI"

if not exist "%PROJ%\VisualClient.csproj" (
    echo [start_visual_client] "%PROJ%" 에 VisualClient.csproj 가 없습니다.
    pause
    exit /b 1
)

where dotnet >nul 2>nul
if errorlevel 1 (
    echo [start_visual_client] dotnet SDK 를 찾지 못했습니다. .NET 10 SDK 를 설치하세요.
    pause
    exit /b 1
)

echo [start_visual_client] %CONFIG% 구성으로 빌드합니다...
dotnet build "%PROJ%\VisualClient.csproj" -c %CONFIG% --nologo -v q
if errorlevel 1 (
    echo [start_visual_client] 빌드 실패.
    pause
    exit /b 1
)

echo [start_visual_client] 창 %COUNT%개를 띄웁니다. 서버가 안 떠 있으면 창 위쪽에 "접속 실패"가 표시됩니다.
echo.

REM 창 번호의 홀짝으로 순회 방향을 갈라야 해서 지연 확장(!VAR!)이 필요하다.
setlocal enabledelayedexpansion
for /l %%N in (1,1,%COUNT%) do (
    set "ARGS="
    if defined AUTO (
        set /a "REV=%%N %% 2"
        if "!REV!"=="0" (set "ARGS=-- --auto-rev") else (set "ARGS=-- --auto")
    )
    start "asio VisualClient %%N (%CONFIG%)" cmd /c "cd /d "%PROJ%" && dotnet run --no-build -c %CONFIG% !ARGS!"
    ping -n 2 127.0.0.1 >nul
)
endlocal

echo  조작: WASD/방향키 이동, 존 뷰 클릭으로 순간 이동, Enter 채팅, F1 Echo 핑, F2 자동 순회, Esc 패널 닫기
echo.
echo  쿠폰 기능을 쓰려면 운영툴도 함께 띄워야 합니다(SQL Server 필요):
echo    bat\start_gmtool_mssql.bat  그리고  bat\start_gmtool.bat
echo  운영툴에서 캠페인을 만들고(보상 종류 = 우편) 쿠폰을 발급한 뒤, 코드 하나를 쿠폰 패널에 넣으세요.
echo  캠페인 코드에는 I/L/O/U 를 쓸 수 없습니다(Crockford Base32 에서 제외된 문자입니다).
echo.
echo  다른 포트로 붙으려면 인자로 넘기면 됩니다:
echo    dotnet run --project Tool\VisualClient\VisualClient -- 127.0.0.1 9000 http://127.0.0.1:5270
echo.

endlocal
