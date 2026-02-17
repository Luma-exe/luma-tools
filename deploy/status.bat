@echo off
:: Quick status check for Luma Tools services

echo.
echo   Luma Tools - Service Status
echo   ────────────────────────────

echo.
echo   [Backend]
C:\nssm\nssm.exe status LumaTools 2>nul || echo   Not installed

echo.
echo   [Caddy HTTPS]
C:\nssm\nssm.exe status LumaToolsCaddy 2>nul || echo   Not installed

echo.
pause
