@echo off
REM 게임 DB(asio_game)를 준비한다 -- DB/로그인 생성 + 스키마/시드 적용.
REM
REM 운영툴과 **같은 SQL Server 인스턴스를 쓰되 DB만 따로** 만든다. 인스턴스를 하나 더 띄우면
REM 개발 머신에서 SQL Server 가 두 벌 돌아 메모리만 먹는다. 스키마를 섞지 않는 이유는
REM Sqlplayers.sql 헤더 주석 참고.
REM
REM **엔진이 어디서 도는지 신경 쓰지 않는다.** 호스트의 sqlcmd 로 127.0.0.1,1433 에 붙으므로
REM 로컬에 설치한 SQL Server 든 포트를 뚫어둔 컨테이너든 그대로 동작한다.
REM
REM 사용법: setup_game_db.bat            (스키마 + 시드)
REM         setup_game_db.bat schema     (스키마만, 시드 생략)
REM 환경 변수: ASIO_SERVER_DB_SERVER   (기본 127.0.0.1,1433)
REM            ASIO_SERVER_SA_PASSWORD (기본 0000)
chcp 65001 >nul
setlocal

set "DBNAME=asio_game"
set "DBUSER=asio_game"
set "DBPASSWORD=0000"

if "%ASIO_SERVER_DB_SERVER%"=="" (set "SERVER=127.0.0.1,1433") else (set "SERVER=%ASIO_SERVER_DB_SERVER%")
if "%ASIO_SERVER_SA_PASSWORD%"=="" (set "SAPW=0000") else (set "SAPW=%ASIO_SERVER_SA_PASSWORD%")

set "SKIP_SEED="
if /i "%~1"=="schema" set "SKIP_SEED=1"

for %%I in ("%~dp0..") do set "ROOT=%%~fI"

where sqlcmd >nul 2>&1
if errorlevel 1 (
    echo [setup_game_db] sqlcmd 를 찾지 못했습니다.
    echo [setup_game_db] SQL Server 또는 SSMS 를 설치하면 같이 깔립니다.
    pause
    exit /b 1
)

REM -C: 자체 서명 인증서를 신뢰한다(개발용). -b: 오류를 errorlevel 로 돌려준다.
set "SQL=sqlcmd -S %SERVER% -U sa -P %SAPW% -C -b -f 65001"

%SQL% -Q "SELECT 1" >nul 2>&1
if errorlevel 1 (
    echo [setup_game_db] %SERVER% 에 연결하지 못했습니다.
    echo [setup_game_db] 엔진이 떠 있는지, sa 비밀번호가 %SAPW% 가 맞는지 확인하세요.
    pause
    exit /b 1
)

echo [setup_game_db] %DBNAME% 데이터베이스와 로그인을 준비합니다.
%SQL% -Q "IF DB_ID('%DBNAME%') IS NULL CREATE DATABASE %DBNAME%;" >nul
if errorlevel 1 goto :fail

REM CHECK_POLICY=OFF: 개발용 비밀번호(0000)가 Windows 암호 정책에 걸리지 않게 한다.
%SQL% -Q "IF NOT EXISTS (SELECT 1 FROM sys.server_principals WHERE name='%DBUSER%') CREATE LOGIN %DBUSER% WITH PASSWORD='%DBPASSWORD%', DEFAULT_DATABASE=%DBNAME%, CHECK_POLICY=OFF;" >nul
if errorlevel 1 goto :fail

%SQL% -d %DBNAME% -Q "IF NOT EXISTS (SELECT 1 FROM sys.database_principals WHERE name='%DBUSER%') CREATE USER %DBUSER% FOR LOGIN %DBUSER%; ALTER ROLE db_owner ADD MEMBER %DBUSER%;" >nul
if errorlevel 1 goto :fail

REM 스키마/시드는 앱 계정(%DBUSER%)으로 적용한다 -- 그 계정 권한만으로 충분한지가 여기서 드러난다.
set "APPSQL=sqlcmd -S %SERVER% -U %DBUSER% -P %DBPASSWORD% -C -b -d %DBNAME% -f 65001"

echo [setup_game_db] 스키마를 적용합니다.
%APPSQL% -i "%ROOT%\Sql\schema.sql"
if errorlevel 1 goto :fail

if defined SKIP_SEED (
    echo [setup_game_db] 시드는 건너뜁니다 ^(schema 인자^).
    goto :done
)

echo [setup_game_db] 시드를 적용합니다. 계정 2만 건이라 수십 초 걸릴 수 있습니다.
%APPSQL% -i "%ROOT%\Sql\seed.sql"
if errorlevel 1 goto :fail

:done
echo.
echo [setup_game_db] 준비 완료.
echo [setup_game_db] 접속 정보: %SERVER% / DB %DBNAME% / 계정 %DBUSER% / 비밀번호 %DBPASSWORD%
echo [setup_game_db] 시드 계정: tester1~4, stress_00001~stress_20000  (비밀번호 0000)
echo [setup_game_db] 서버는 환경 변수 ASIO_SERVER_DB_CONN 이 있으면 그것을 우선합니다.
endlocal
exit /b 0

:fail
echo.
echo [setup_game_db] 실패했습니다. 위 오류 메시지를 확인하세요.
pause
endlocal
exit /b 1
