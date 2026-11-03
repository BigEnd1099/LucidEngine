@echo off
setlocal
cd /d "%~dp0"
title Lucid Engine - Native Vulkan 1.3 Studio

if not exist "engine\build\Release\engine.exe" (
    echo ========================================================
    echo  [INFO] Brak pliku engine.exe. Rozpoczynanie kompilacji...
    echo ========================================================
    call build_engine.bat release
    if not exist "engine\build\Release\engine.exe" (
        echo [BLAD] Nie udalo sie zbudowac engine.exe!
        pause
        exit /b 1
    )
)

echo ========================================================
echo  LUCID ENGINE 5.8 - NATYWNE STUDIO VULKAN 1.3
echo ========================================================
echo  * Prezentacja bezposrednia: Vulkan 1.3 Swapchain (0ms opoznienia)
echo  * Edytor: Dear ImGui Native Studio (F1/F2/F3/F4)
echo  * Sterowanie kamera: Prawy Przycisk Myszy + WASD / QE
echo  * Fizyka: Jolt Physics Realtime
echo  * Skrypty: QuickJS Live Hot-Reload
echo  * Port zdalny: 9000 (Opcjonalny background JSON-RPC / MCP)
echo ========================================================
echo.
echo [INFO] Uruchamianie natywnego silnika...
start "" "engine\build\Release\engine.exe"
exit /b 0
