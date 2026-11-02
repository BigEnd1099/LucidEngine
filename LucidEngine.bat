@echo off
cd /d "%~dp0"
title Lucid Engine
if not exist "dist\index.html" (
    echo [Lucid Engine] Budowanie pakietu...
    call npm run build
)
start "" "node_modules\electron\dist\electron.exe" .
