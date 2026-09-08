@echo off
REM VisualClient(MonoGame 창 클라이언트)를 실행한다. Gateway(127.0.0.1:9000)로 접속하고,
REM 쿠폰 등록만 운영툴(127.0.0.1:5080 -- start_gmtool.bat 의 기본 포트)의 HTTP API로 나간다.
REM
REM C++ 솔루션이 아니라 별도 .NET 솔루션(Tool\VisualClient\VisualClient.slnx)이라 bin\x64 가
REM 아닌 자기 obj/bin 경로로 빌드된다 -- 그래서 여기서는 dotnet run 으로 띄운다.
REM
REM 사용법: start_visual_client.bat [개수] [Debug^|Release]   (기본값: 1 Debug)
REM   개수를 2 이상으로 주면 창을 그만큼 띄운다. 존 브로드캐스트(다른 플레이어가 보이는지)와
REM   핸드오프를 확인하려면 최소 2개가 필요하다 -- 창 하나로는 "나 혼자" 상태라 브로드캐스트가
REM   왔는지 안 왔는지 구분되지 않는다.
chcp 65001 >nul
setlocal

set "COUNT=%~1"
if "%COUNT%"=="" set "COUNT=1"

set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=Debug"

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

for /l %%N in (1,1,%COUNT%) do (
    start "asio VisualClient %%N (%CONFIG%)" cmd /c "cd /d "%PROJ%" && dotnet run --no-build -c %CONFIG%"
    ping -n 2 127.0.0.1 >nul
)

echo  조작: WASD/방향키 이동, 존 뷰 클릭으로 순간 이동, Enter 채팅, F1 Echo 핑, Esc 패널 닫기
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
