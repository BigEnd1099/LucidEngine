@echo off
setlocal
cd /d "%~dp0"
title Lucid Engine - RT Debugger

where node >nul 2>nul || (echo [ERROR] Node.js is required.& pause & exit /b 1)
if not exist "node_modules\ws" (
  echo [ERROR] npm dependency 'ws' is missing. Run npm install first.
  pause
  exit /b 1
)

set "PORT=9000"
set "DURATION=10000"
set "MODE=none"
if not "%~1"=="" set "PORT=%~1"
if not "%~2"=="" set "DURATION=%~2"
if not "%~3"=="" set "MODE=%~3"

echo ========================================================
echo  LUCID ENGINE - RT DEBUGGER
echo ========================================================
echo  Endpoint: 127.0.0.1:%PORT%
echo  Capture per case: %DURATION% ms
echo  Debug view: %MODE%
echo.
echo  Start the engine first and load AtriumBenchmark for
echo  comparable measurements.
echo.
node tools\run_rt_debugger.mjs %PORT% %DURATION% %MODE%
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (echo [RTDEBUG] Completed successfully.) else (echo [RTDEBUG] Failed with exit code %RC%.)
echo Reports are in rt-debug-results.
pause
exit /b %RC%
