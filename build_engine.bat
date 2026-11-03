@echo off
setlocal enabledelayedexpansion

chcp 65001 >nul
cd /d "%~dp0"
title Lucid Engine - Budowanie engine.exe (C++ / Vulkan / Jolt)

echo ========================================================
echo       LUCID ENGINE - KOMPILATOR SILNIKA C++
echo ========================================================

if exist "C:\Program Files\CMake\bin\cmake.exe" set "PATH=C:\Program Files\CMake\bin;%PATH%"

where cmake >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo [BLAD] Narzedzie 'cmake' nie zostalo odnalezione w sciezce systemowej PATH!
    echo Upewnij sie, ze CMake oraz kompilator C++ [Visual Studio / MSVC] sa zainstalowane.
    echo ========================================================
    pause
    exit /b 1
)

powershell -Command "Stop-Process -Name engine -Force -ErrorAction SilentlyContinue" >nul 2>nul
taskkill /F /IM engine.exe >nul 2>nul

set CONFIG=Release
set DO_CLEAN=0
set DO_RUN=0

:parse_args
if "%~1"=="" goto after_args
if /i "%~1"=="debug" set CONFIG=Debug
if /i "%~1"=="release" set CONFIG=Release
if /i "%~1"=="clean" set DO_CLEAN=1
if /i "%~1"=="--clean" set DO_CLEAN=1
if /i "%~1"=="run" set DO_RUN=1
if /i "%~1"=="--run" set DO_RUN=1
shift
goto parse_args
:after_args

if "!DO_CLEAN!"=="1" (
    echo [INFO] Czyszczenie folderu engine/build...
    if exist "engine\build" rmdir /s /q "engine\build"
)

if not exist "engine\build\CMakeCache.txt" (
    echo [INFO] Inicjalizacja konfiguracji CMake dla silnika Lucid Engine...
    cmake -B engine/build -S engine
    if !ERRORLEVEL! neq 0 (
        echo.
        echo [BLAD] Konfiguracja CMake zakonczyla sie niepowodzeniem [kod: !ERRORLEVEL!].
        pause
        exit /b !ERRORLEVEL!
    )
)

echo.
echo [INFO] Kompilacja silnika Lucid Engine [Tryb: !CONFIG!, Wielowatkowosc: wlaczona]...
cmake --build engine/build --config !CONFIG! --parallel
if !ERRORLEVEL! neq 0 (
    echo.
    echo ========================================================
    echo  [BLAD] Kompilacja silnika nie powiodla sie! [kod: !ERRORLEVEL!]
    echo ========================================================
    pause
    exit /b !ERRORLEVEL!
)

set EXE_PATH=engine\build\!CONFIG!\engine.exe
if not exist "!EXE_PATH!" (
    set EXE_PATH=engine\build\engine.exe
)

echo.
echo ========================================================
echo  [SUKCES] engine.exe zostal pomyslnie zbudowany!
echo ========================================================
echo Plik wykonywalny: "%~dp0!EXE_PATH!"

if "!DO_RUN!"=="1" (
    echo [INFO] Automatyczne uruchamianie silnika [!EXE_PATH!] z powloka 1:1 Studio...
    start "" "%~dp0!EXE_PATH!" --studio
    exit /b 0
)

echo.
echo Gotowe. Nacisnij dowolny klawisz, aby zamknac to okno...
pause >nul
exit /b 0
