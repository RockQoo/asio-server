@echo off
REM 게임 DB(asio_game)를 준비한다 -- 컨테이너 확인 + DB/로그인 생성 + 스키마/시드 적용.
REM
REM 운영툴과 **같은 SQL Server 컨테이너(asio-server-mssql)를 쓰되 DB만 따로** 만든다. 컨테이너를
REM 하나 더 띄우면 개발 머신에서 SQL Server가 두 벌 돌아 메모리만 먹는다. 스키마를 섞지 않는
REM 이유는 Sql\schema.sql 헤더 주석 참고.
REM
REM 컨테이너가 없으면 bat\start_mssql.bat 을 먼저 실행하라고 안내만 한다 -- 컨테이너
REM 생성 로직을 두 배치에 복사해두면 한쪽만 고쳤을 때 조용히 갈라진다.
REM
REM 사용법: setup_game_db.bat            (스키마 + 시드)
REM         setup_game_db.bat schema     (스키마만, 시드 생략)
chcp 65001 >nul
setlocal

set "NAME=asio-server-mssql"
set "SA_PASSWORD=GmTool1234!"
set "SQLCMD=/opt/mssql-tools18/bin/sqlcmd"
set "DBNAME=asio_game"
set "DBUSER=asio_game"

set "SKIP_SEED="
if /i "%~1"=="schema" set "SKIP_SEED=1"

for %%I in ("%~dp0..") do set "ROOT=%%~fI"

docker version >nul 2>&1
if errorlevel 1 (
    echo [setup_game_db] Docker 데몬이 응답하지 않습니다. Docker Desktop 을 먼저 실행하세요.
    pause
    exit /b 1
)

docker ps --format "{{.Names}}" | findstr /X "%NAME%" >nul
if errorlevel 1 (
    echo [setup_game_db] 컨테이너 %NAME% 가 실행 중이 아닙니다.
    echo [setup_game_db] 먼저 bat\start_mssql.bat 을 실행하세요.
    pause
    exit /b 1
)

echo [setup_game_db] %DBNAME% 데이터베이스와 로그인을 준비합니다.
REM CHECK_POLICY=OFF: 개발용 비밀번호가 Windows 암호 정책에 걸리지 않게 한다.
docker exec %NAME% %SQLCMD% -S localhost -U sa -P "%SA_PASSWORD%" -C -b -Q ^
  "IF DB_ID('%DBNAME%') IS NULL CREATE DATABASE %DBNAME%;" >nul
if errorlevel 1 goto :fail

docker exec %NAME% %SQLCMD% -S localhost -U sa -P "%SA_PASSWORD%" -C -b -Q ^
  "IF NOT EXISTS (SELECT 1 FROM sys.server_principals WHERE name='%DBUSER%') CREATE LOGIN %DBUSER% WITH PASSWORD='%SA_PASSWORD%', DEFAULT_DATABASE=%DBNAME%, CHECK_POLICY=OFF;" >nul
if errorlevel 1 goto :fail

docker exec %NAME% %SQLCMD% -S localhost -U sa -P "%SA_PASSWORD%" -C -b -d %DBNAME% -Q ^
  "IF NOT EXISTS (SELECT 1 FROM sys.database_principals WHERE name='%DBUSER%') CREATE USER %DBUSER% FOR LOGIN %DBUSER%; ALTER ROLE db_owner ADD MEMBER %DBUSER%;" >nul
if errorlevel 1 goto :fail

REM 스크립트를 컨테이너 안으로 복사해서 실행한다. 호스트의 sqlcmd 설치를 요구하지 않기 위해서다
REM (start_mssql.bat 도 같은 이유로 docker exec 만 쓴다).
echo [setup_game_db] 스키마를 적용합니다.
docker cp "%ROOT%\Sql\schema.sql" %NAME%:/tmp/schema.sql >nul
if errorlevel 1 goto :fail
docker exec %NAME% %SQLCMD% -S localhost -U %DBUSER% -P "%SA_PASSWORD%" -C -b -d %DBNAME% -i /tmp/schema.sql
if errorlevel 1 goto :fail

if defined SKIP_SEED (
    echo [setup_game_db] 시드는 건너뜁니다 ^(schema 인자^).
    goto :done
)

echo [setup_game_db] 시드를 적용합니다. 계정 2만 건이라 수십 초 걸릴 수 있습니다.
docker cp "%ROOT%\Sql\seed.sql" %NAME%:/tmp/seed.sql >nul
if errorlevel 1 goto :fail
docker exec %NAME% %SQLCMD% -S localhost -U %DBUSER% -P "%SA_PASSWORD%" -C -b -d %DBNAME% -i /tmp/seed.sql
if errorlevel 1 goto :fail

:done
echo.
echo [setup_game_db] 준비 완료.
echo [setup_game_db] 접속 정보: 127.0.0.1,1433 / DB %DBNAME% / 계정 %DBUSER% / 비밀번호 %SA_PASSWORD%
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
