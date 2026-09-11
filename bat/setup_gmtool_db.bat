@echo off
REM 운영툴 DB(gmtool)와 로그인을 준비한다 -- DB 생성 + 로그인 생성 + db_owner 부여.
REM
REM 스키마(Tool\GmTool\Sql\schema.sql)는 GmTool.Web 이 기동할 때 자동 적용하므로 여기서
REM 적용하지 않는다(Database:ApplySchemaOnStartup 설정으로 끌 수 있다).
REM 게임 DB(asio_game)는 bat\setup_game_db.bat 이 따로 담당한다 -- 스키마를 섞지 않는
REM 이유는 Sqlplayers.sql 헤더 주석 참고.
REM
REM **엔진이 어디서 도는지 신경 쓰지 않는다.** 호스트의 sqlcmd 로 127.0.0.1,1433 에 붙으므로
REM 로컬에 설치한 SQL Server 든 포트를 뚫어둔 컨테이너든 그대로 동작한다.
REM
REM 사용법: setup_gmtool_db.bat
REM 환경 변수: ASIO_SERVER_DB_SERVER   (기본 127.0.0.1,1433)
REM            ASIO_SERVER_SA_PASSWORD (기본 0000)
chcp 65001 >nul
setlocal

set "DBNAME=gmtool"
set "DBUSER=gmtool"
set "DBPASSWORD=0000"

if "%ASIO_SERVER_DB_SERVER%"=="" (set "SERVER=127.0.0.1,1433") else (set "SERVER=%ASIO_SERVER_DB_SERVER%")
if "%ASIO_SERVER_SA_PASSWORD%"=="" (set "SAPW=0000") else (set "SAPW=%ASIO_SERVER_SA_PASSWORD%")

REM sqlcmd 는 SQL Server / SSMS / ODBC Client SDK 중 아무거나 깔려 있으면 PATH 에 들어온다.
where sqlcmd >nul 2>&1
if errorlevel 1 (
    echo [setup_gmtool_db] sqlcmd 를 찾지 못했습니다.
    echo [setup_gmtool_db] SQL Server 또는 SSMS 를 설치하면 같이 깔립니다.
    pause
    exit /b 1
)

REM -C: 자체 서명 인증서를 신뢰한다(개발용). -b: 오류를 errorlevel 로 돌려준다.
set "SQL=sqlcmd -S %SERVER% -U sa -P %SAPW% -C -b -f 65001"

%SQL% -Q "SELECT 1" >nul 2>&1
if errorlevel 1 (
    echo [setup_gmtool_db] %SERVER% 에 연결하지 못했습니다.
    echo [setup_gmtool_db] 엔진이 떠 있는지, sa 비밀번호가 %SAPW% 가 맞는지 확인하세요.
    pause
    exit /b 1
)

echo [setup_gmtool_db] %DBNAME% 데이터베이스와 로그인을 준비합니다.
%SQL% -Q "IF DB_ID('%DBNAME%') IS NULL CREATE DATABASE %DBNAME%;" >nul
if errorlevel 1 goto :fail

REM CHECK_POLICY=OFF: 개발용 비밀번호(0000)가 Windows 암호 정책에 걸리지 않게 한다.
%SQL% -Q "IF NOT EXISTS (SELECT 1 FROM sys.server_principals WHERE name='%DBUSER%') CREATE LOGIN %DBUSER% WITH PASSWORD='%DBPASSWORD%', DEFAULT_DATABASE=%DBNAME%, CHECK_POLICY=OFF;" >nul
if errorlevel 1 goto :fail

%SQL% -d %DBNAME% -Q "IF NOT EXISTS (SELECT 1 FROM sys.database_principals WHERE name='%DBUSER%') CREATE USER %DBUSER% FOR LOGIN %DBUSER%; ALTER ROLE db_owner ADD MEMBER %DBUSER%;" >nul
if errorlevel 1 goto :fail

echo.
echo [setup_gmtool_db] 준비 완료.
echo [setup_gmtool_db] 접속 정보: %SERVER% / DB %DBNAME% / 계정 %DBUSER% / 비밀번호 %DBPASSWORD%
echo [setup_gmtool_db] 다음: bat\start_gmtool.bat 으로 운영툴을 띄우면 스키마가 자동 적용됩니다.
endlocal
exit /b 0

:fail
echo.
echo [setup_gmtool_db] 실패했습니다. 위 오류 메시지를 확인하세요.
pause
endlocal
exit /b 1
