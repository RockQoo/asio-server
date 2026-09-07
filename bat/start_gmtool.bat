@echo off
REM 운영툴(GmTool.Web)을 띄운다. WorldServer가 먼저 떠 있어야 우편/공지 명령이 나간다
REM (안 떠 있어도 웹은 뜨고, 화면 우상단에 "World 연결 끊김"으로 표시된다).
REM
REM 사용법: start_gmtool.bat [포트]   (기본값: 5080)
REM
REM 선행 조건: SQL Server가 필요하다. Docker로 띄우려면 bat\start_gmtool_mssql.bat 를 먼저 실행할 것.
REM (DB가 없어도 웹은 뜨지만 계정 조회를 못 해서 로그인이 안 된다.)
chcp 65001 >nul
setlocal

set "PORT=%~1"
if "%PORT%"=="" set "PORT=5080"

for %%I in ("%~dp0..\Tool\GmTool\GmTool.Web") do set "WEBDIR=%%~fI"

if not exist "%WEBDIR%\GmTool.Web.csproj" (
    echo [start_gmtool] "%WEBDIR%" 에서 GmTool.Web.csproj 를 찾지 못했습니다.
    pause
    exit /b 1
)

echo [start_gmtool] http://127.0.0.1:%PORT% 로 운영툴을 기동합니다.
echo [start_gmtool] 초기 계정: admin / admin1234!  (첫 로그인 후 변경 권장)
echo.

REM ASPNETCORE_ENVIRONMENT=Development: HTTPS 리다이렉트와 예외 페이지 숨김을 끈다.
REM 로컬 운영툴은 http로만 접근하므로 이게 편하다.
set ASPNETCORE_ENVIRONMENT=Development

start "asio GmTool (:%PORT%)" cmd /k "cd /d "%WEBDIR%" && dotnet run --no-launch-profile --urls http://127.0.0.1:%PORT%"

echo [start_gmtool] 기동 중입니다. 첫 실행은 NuGet 복원 때문에 조금 더 걸립니다.
endlocal
