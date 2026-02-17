@echo off
:: Restart both Luma Tools services

echo   Restarting Luma Tools services...
C:\nssm\nssm.exe restart LumaTools
C:\nssm\nssm.exe restart LumaToolsCaddy
echo   Done!
pause
