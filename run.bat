@echo off
echo.
echo   Starting Luma Tools Server...
echo.

:: Try Release first, then Debug
if exist build\Release\luma-tools.exe (
    cd build\Release
    luma-tools.exe
) else if exist build\Debug\luma-tools.exe (
    cd build\Debug
    luma-tools.exe
) else if exist build\luma-tools.exe (
    cd build
    luma-tools.exe
) else (
    echo [ERROR] luma-tools.exe not found! Run build.bat first.
    pause
)
