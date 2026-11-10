@echo off
setlocal
cd /d "%~dp0"
title Lucid Engine - Render Benchmark

set "EXE=engine\build\Release\engine.exe"
if not exist "%EXE%" set "EXE=engine\build\engine.exe"
if not exist "%EXE%" (
  echo [ERROR] engine.exe not found. Building Release...
  call build_engine.bat release
  if errorlevel 1 exit /b 1
)
if not exist "%EXE%" (
  echo [ERROR] Release executable still missing: %EXE%
  pause
  exit /b 1
)
where node >nul 2>nul || (echo [ERROR] Node.js is required.& pause & exit /b 1)
if not exist "node_modules\ws" (
  echo [ERROR] npm dependency 'ws' is missing. Run npm install first.
  pause
  exit /b 1
)

set "LUCID_BENCH_WARMUP_MS=1500"
set "LUCID_BENCH_CAPTURE_MS=2000"
if not "%~1"=="" set "LUCID_BENCH_CAPTURE_MS=%~1"
if not "%~2"=="" set "LUCID_BENCH_WARMUP_MS=%~2"

echo [BENCH] Warmup: %LUCID_BENCH_WARMUP_MS% ms / Capture: %LUCID_BENCH_CAPTURE_MS% ms
node tools\run_render_benchmark.mjs "%EXE%"
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (echo [BENCH] Completed successfully.) else (echo [BENCH] Failed with exit code %RC%.)
echo Reports are in benchmark-results.
pause
exit /b %RC%
