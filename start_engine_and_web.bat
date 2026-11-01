@echo off
title Lucid Engine 5.8 - Unified Studio Launcher
echo ========================================================
echo       LUCID ENGINE 5.8 - AI NATIVE UNIFIED STUDIO
echo ========================================================
echo [1/3] Uruchamianie silnika C++ w tle (Vulkan 1.3 / Jolt / QuickJS)...
powershell -Command "Stop-Process -Name engine -Force -ErrorAction SilentlyContinue"
start "" "%~dp0engine\build\Release\engine.exe" --headless

timeout /t 2 /nobreak >nul

echo [2/3] Uruchamianie serwera studia (Vite)...
start "" cmd /k "cd /d "%~dp0" && npm run dev"

timeout /t 3 /nobreak >nul

echo [3/3] Uruchamianie jednolitego okna aplikacji Lucid Engine 5.8 (1:1 Studio Window)...
if exist "%ProgramFiles%\Google\Chrome\Application\chrome.exe" (
    start "" "%ProgramFiles%\Google\Chrome\Application\chrome.exe" --app="http://localhost:3000" --window-size=1920,1080
) else (
    start http://localhost:3000
)

echo ========================================================
echo Lucid Engine 5.8 zostal pomyslnie uruchomiony!
echo Jednolite studio UI dziala w trybie natywnego okna aplikacji (1:1).
echo Backend silnika C++: port 9000
echo Studio UI:           port 3000
echo ========================================================
