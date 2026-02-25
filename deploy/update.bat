@echo off
:: Luma Tools - Quick update script (HTML/JS/CSS changes, no rebuild needed)
:: Run from C:\luma-tools as Administrator
:: Usage: update.bat

cd /d C:\luma-tools

echo [1] Pulling latest changes...
git pull origin main
if %errorlevel% neq 0 (
    echo [ERROR] git pull failed
    exit /b 1
)

echo [2] Syncing public files to Release build dir...
xcopy /y /s /e public\* build\Release\public\
if %errorlevel% neq 0 (
    echo [ERROR] xcopy failed
    exit /b 1
)

echo.
echo Done! Public files updated without a rebuild.
echo (No service restart needed - files are served live)
