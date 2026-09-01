@echo off
setlocal EnableExtensions
color 07
cd /d "%~dp0"

set "PYTHONUTF8=1"
set "PIPELINE=.claude\skills\review-pipeline\scripts\review_pipeline.py"
where python >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python was not found on PATH.
    pause
    exit /b 1
)
if not exist "%PIPELINE%" (
    echo [ERROR] Pipeline entry point not found: %PIPELINE%
    pause
    exit /b 1
)

echo ============================================================
echo  ASIO Server Code Review Pipeline
echo ============================================================
echo  Git range: BASE..HEAD
echo  BASE = comparison baseline, excluded from the change range.
echo  HEAD = reviewed target, included in the change range.
echo.

:ask_base
set "BASE_COMMIT="
set /p "BASE_COMMIT=Paste BASE SHA-1: "
if not defined BASE_COMMIT (
    echo [ERROR] BASE SHA-1 is required.
    goto ask_base
)

set "HEAD_COMMIT="
set /p "HEAD_COMMIT=Paste HEAD SHA-1 [default: HEAD]: "
if not defined HEAD_COMMIT set "HEAD_COMMIT=HEAD"

echo.
echo [1/2] Checking %BASE_COMMIT%..%HEAD_COMMIT% ...
python "%PIPELINE%" --base "%BASE_COMMIT%" --head "%HEAD_COMMIT%" --dry-run
if errorlevel 1 (
    color 0C
    echo.
    echo ============================================================
    echo  REVIEW PIPELINE FAILED
    echo  Preflight failed. No review was started.
    echo ============================================================
    pause
    exit /b 1
)

echo.
choice /C YN /N /M "[2/2] Run Codex review and Claude validation? [Y/N]: "
if errorlevel 2 (
    echo Cancelled. No review was started.
    pause
    exit /b 0
)

echo.
python "%PIPELINE%" --base "%BASE_COMMIT%" --head "%HEAD_COMMIT%"
set "PIPELINE_EXIT=%ERRORLEVEL%"
echo.
if not "%PIPELINE_EXIT%"=="0" (
    color 0C
    echo ============================================================
    echo  REVIEW PIPELINE FAILED
    echo  Exit code: %PIPELINE_EXIT%
    echo  Check docs\code-review\logs for details.
    echo ============================================================
) else (
    color 0A
    echo ============================================================
    echo  REVIEW PIPELINE COMPLETE
    echo  Final report: docs\code-review\index.html
    echo  Log directory: docs\code-review\logs
    echo ============================================================
)
pause
exit /b %PIPELINE_EXIT%
