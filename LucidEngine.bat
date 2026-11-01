@echo off
cd /d "%~dp0"
title Lucid Engine 3.8
if not exist "dist\index.html" (
    echo [Lucid Engine] Budowanie pakietu...
    call npm run build
)
start "" "node_modules\electron\dist\electron.exe" .
