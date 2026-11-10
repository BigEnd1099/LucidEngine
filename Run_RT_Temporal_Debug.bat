@echo off
setlocal
cd /d "%~dp0"
title Lucid Engine - RT Temporal Debug

set "PORT=9000"
set "DURATION=15000"
if not "%~1"=="" set "PORT=%~1"
if not "%~2"=="" set "DURATION=%~2"

echo ========================================================
echo  LUCID ENGINE - RT TEMPORAL DEBUG VIEW
echo ========================================================
echo  This enables the renderer History Length debug view.
echo  It is a visual inspection mode, not a per-pixel GPU
echo  histogram/readback report.
echo.
call Run_RT_Debugger.bat %PORT% %DURATION% history
exit /b %ERRORLEVEL%
