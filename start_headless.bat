@echo off
title Lucid Engine 2.0 - Headless Server
echo ========================================================
echo   LUCID ENGINE 2.0 - HEADLESS SERVER (MCP / AI AGENT)
echo ========================================================
echo Uruchamianie silnika w trybie bezokienkowym (--headless)...
"%~dp0engine\build\Release\engine.exe" --headless
pause
