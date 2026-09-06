@echo off
REM start_server_all.bat 으로 띄운 서버 3종을 종료한다.
REM
REM 사용법: stop_server_all.bat [-keep]
REM   -keep 을 주면 프로세스만 죽이고 콘솔 창은 남긴다(마지막 로그를 읽을 때).
REM   기본값은 창까지 닫는다 -- cmd /k 로 띄워서 프로세스가 죽어도 창이 남기 때문이다.
chcp 65001 >nul
setlocal enabledelayedexpansion

set "KEEPWINDOW="
if /i "%~1"=="-keep" set "KEEPWINDOW=1"

set "FOUND="
echo [stop_server_all] 서버 프로세스를 종료합니다.
echo.

REM /T 로 자식 프로세스까지 함께 정리한다.
for %%E in (GatewayServer.exe ZoneServer.exe WorldServer.exe) do (
    tasklist /fi "imagename eq %%E" /nh 2>nul | find /i "%%E" >nul
    if !errorlevel! equ 0 (
        taskkill /f /t /im %%E >nul 2>&1
        if !errorlevel! equ 0 (
            echo   종료  %%E
            set "FOUND=1"
        ) else (
            echo   실패  %%E  ^(권한 부족일 수 있음 -- 관리자 권한으로 다시 시도^)
        )
    ) else (
        echo   없음  %%E
    )
)

if not defined FOUND (
    echo.
    echo [stop_server_all] 실행 중인 서버가 없었습니다.
)

if defined KEEPWINDOW (
    echo.
    echo [stop_server_all] -keep 지정: 콘솔 창은 그대로 둡니다.
    goto :done
)

REM 프로세스가 죽어도 cmd /k 셸이 남으므로 창 제목으로 찾아 닫는다.
REM start_server_all.bat 이 붙인 "asio " 접두사에만 매칭시켜 다른 창을 건드리지 않는다.
for %%T in ("asio WorldServer*" "asio ZoneServer*" "asio GatewayServer*") do (
    taskkill /f /fi "windowtitle eq %%~T" >nul 2>&1
)

:done
echo.
endlocal
