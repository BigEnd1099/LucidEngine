@echo off
title Lucid Engine - Headless Server
echo ========================================================
echo   LUCID ENGINE - HEADLESS SERVER (MCP / AI AGENT)
echo ========================================================
echo Uruchamianie silnika w trybie bezokienkowym (--headless)...
"%~dp0engine\build\Release\engine.exe" --headless
pause
