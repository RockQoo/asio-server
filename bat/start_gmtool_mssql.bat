@echo off
REM 운영툴이 쓰는 SQL Server를 Docker 컨테이너로 띄운다.
REM 이미 컨테이너가 있으면 start 만 하고, 없으면 새로 만든다.
REM
REM 왜 Docker인가: SQL Server Developer Edition 을 직접 설치해도 되지만(그 경우 이 배치는
REM 필요 없다), 설치가 수 GB에 재부팅을 요구할 수 있어서 개발용으로는 컨테이너가 가볍다.
REM VS 와 함께 깔리는 LocalDB 는 래퍼만 등록돼 있고 엔진이 없는 경우가 있어 기대하지 않는다.
REM
REM 스키마(Sql\schema.sql)는 GmTool.Web 이 기동할 때 자동으로 적용하므로 여기서는 컨테이너와
REM DB/로그인만 만든다 (Database:ApplySchemaOnStartup 설정으로 자동 적용을 끌 수 있다).
chcp 65001 >nul
setlocal

set "NAME=gmtool-mssql"
set "SA_PASSWORD=GmTool1234!"
set "SQLCMD=/opt/mssql-tools18/bin/sqlcmd"

docker version >nul 2>&1
if errorlevel 1 (
    echo [start_gmtool_mssql] Docker 데몬이 응답하지 않습니다. Docker Desktop 을 먼저 실행하세요.
    pause
    exit /b 1
)

docker ps -a --format "{{.Names}}" | findstr /X "%NAME%" >nul
if not errorlevel 1 (
    echo [start_gmtool_mssql] 기존 컨테이너 %NAME% 를 시작합니다.
    docker start %NAME%
    goto :wait
)

echo [start_gmtool_mssql] 컨테이너 %NAME% 를 새로 만듭니다. 첫 실행은 이미지 다운로드 때문에 몇 분 걸립니다.
REM MSSQL_PID=Developer: 무료이고 기능 제한이 없다. Express 는 DB 10GB / RAM 1.4GB 상한이
REM 걸려서 쿠폰 수백만 건 실측에 방해가 된다.
docker run -d --name %NAME% ^
  -e ACCEPT_EULA=Y ^
  -e MSSQL_SA_PASSWORD=%SA_PASSWORD% ^
  -e MSSQL_PID=Developer ^
  -p 1433:1433 ^
  --restart unless-stopped ^
  mcr.microsoft.com/mssql/server:2022-latest

:wait
echo [start_gmtool_mssql] 엔진이 준비될 때까지 기다립니다...
REM SQL Server 는 컨테이너가 Up 이 된 뒤에도 복구/업그레이드 단계를 거치므로, 포트가 열렸는지가
REM 아니라 실제 쿼리가 되는지로 판정해야 한다.
set /a TRIES=0
:ping
set /a TRIES+=1
docker exec %NAME% %SQLCMD% -S localhost -U sa -P "%SA_PASSWORD%" -C -Q "SELECT 1" >nul 2>&1
if not errorlevel 1 goto :provision
if %TRIES% GEQ 60 (
    echo [start_gmtool_mssql] 60회 시도했지만 엔진이 응답하지 않습니다. docker logs %NAME% 를 확인하세요.
    pause
    exit /b 1
)
timeout /t 2 /nobreak >nul
goto :ping

:provision
echo [start_gmtool_mssql] gmtool 데이터베이스와 로그인을 준비합니다.
REM CHECK_POLICY=OFF: 개발용 비밀번호가 Windows 암호 정책에 걸리지 않게 한다.
docker exec %NAME% %SQLCMD% -S localhost -U sa -P "%SA_PASSWORD%" -C -b -Q ^
  "IF DB_ID('gmtool') IS NULL CREATE DATABASE gmtool;" >nul
docker exec %NAME% %SQLCMD% -S localhost -U sa -P "%SA_PASSWORD%" -C -b -Q ^
  "IF NOT EXISTS (SELECT 1 FROM sys.server_principals WHERE name='gmtool') CREATE LOGIN gmtool WITH PASSWORD='%SA_PASSWORD%', DEFAULT_DATABASE=gmtool, CHECK_POLICY=OFF;" >nul
docker exec %NAME% %SQLCMD% -S localhost -U sa -P "%SA_PASSWORD%" -C -b -d gmtool -Q ^
  "IF NOT EXISTS (SELECT 1 FROM sys.database_principals WHERE name='gmtool') CREATE USER gmtool FOR LOGIN gmtool; ALTER ROLE db_owner ADD MEMBER gmtool;" >nul

echo.
echo [start_gmtool_mssql] 준비 완료.
echo [start_gmtool_mssql] 접속 정보: 127.0.0.1,1433 / DB gmtool / 계정 gmtool / 비밀번호 %SA_PASSWORD%
echo [start_gmtool_mssql] 조회 예:   docker exec %NAME% %SQLCMD% -S localhost -U gmtool -P "%SA_PASSWORD%" -C -d gmtool -Q "SELECT name FROM sys.tables"
echo [start_gmtool_mssql] 종료:      docker stop %NAME%
endlocal
