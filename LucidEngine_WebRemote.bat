@echo off
setlocal
cd /d "%~dp0"
title Lucid Engine - Remote Web/Electron Studio

echo ========================================================
echo  LUCID ENGINE - ZDALNE STUDIO WEB / ELECTRON (OPCJONALNE)
echo ========================================================
echo [INFO] Uruchamianie wbudowanego klienta zdalnego...
if not exist "dist\index.html" (
    echo [INFO] Budowanie pakietu webowego...
    call npm run build
)
call npm start
