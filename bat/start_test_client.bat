@echo off
REM TestClient(대화형 REPL)를 실행한다. echo/move/chat/mail 명령을 직접 입력하는 도구라
REM 새 창을 띄우지 않고 이 콘솔에서 그대로 돌린다. Gateway(127.0.0.1:9000)로 접속한다.
REM
REM 사용법: start_test_client.bat [Debug^|Release] [-attach]
REM   -attach : 새 창으로 띄우고 PID를 출력한다. 클라이언트 쪽에 중단점을 걸 때 쓴다.
REM             서버에만 붙을 거라면 기본 모드로 충분하다(서버는 idle이라 언제든 attach 가능).
chcp 65001 >nul
setlocal

set "CONFIG=Debug"
set "ATTACH="
for %%A in (%*) do (
    if /i "%%~A"=="-attach" (set "ATTACH=1") else (set "CONFIG=%%~A")
)

for %%I in ("%~dp0..\bin\x64\%CONFIG%") do set "BIN=%%~fI"

if not exist "%BIN%\TestClient.exe" (
    echo [start_test_client] "%BIN%" 에 TestClient.exe 가 없습니다. asio-server.slnx 를 먼저 빌드하세요.
    pause
    exit /b 1
)

REM .\ 접두사 이유는 start_server_all.bat 주석 참고.
if not defined ATTACH (
    cd /d "%BIN%"
    .\TestClient.exe
    goto :eof
)

start "asio TestClient (%CONFIG%)" cmd /k "cd /d "%BIN%" && .\TestClient.exe"
ping -n 3 127.0.0.1 >nul

echo.
echo ============================================================
echo  Visual Studio 연결용 PID  (디버그 ^> 프로세스에 연결, Ctrl+Alt+P)
echo ============================================================
set "ANY="
for /f "tokens=2 delims=," %%P in ('tasklist /fi "imagename eq TestClient.exe" /fo csv /nh 2^>nul') do (
    echo   TestClient.exe   PID %%~P
    set "ANY=1"
)
if not defined ANY echo   ^(기동되지 않았습니다 -- 새로 열린 창의 오류 메시지를 확인하세요^)
echo ============================================================
echo.
echo  VS에서 붙고 중단점을 건 다음, 새로 열린 창에 명령을 입력하세요.
echo.
pause

endlocal
