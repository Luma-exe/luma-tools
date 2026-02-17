@echo off
echo ============================================
echo   LUMA TOOLS - Build Script (Windows)
echo ============================================
echo.

:: Check for CMake
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake not found! Please install CMake.
    echo Download: https://cmake.org/download/
    pause
    exit /b 1
)

:: Check for yt-dlp
where yt-dlp >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [WARNING] yt-dlp not found! Installing via pip...
    pip install yt-dlp
    if %ERRORLEVEL% neq 0 (
        echo [ERROR] Failed to install yt-dlp.
        echo Install manually: pip install yt-dlp
        echo Or download from: https://github.com/yt-dlp/yt-dlp/releases
    )
)

:: Check for ffmpeg
where ffmpeg >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [WARNING] ffmpeg not found! Some conversions may fail.
    echo Download: https://ffmpeg.org/download.html
)

:: Create build directory
if not exist build mkdir build
cd build

:: Configure with CMake
echo.
echo [1/2] Configuring with CMake...
cmake .. -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake configuration failed!
    pause
    exit /b 1
)

:: Build
echo.
echo [2/2] Building...
cmake --build . --config Release
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo.
echo ============================================
echo   Build complete!
echo   Run: build\Release\luma-tools.exe
echo   Or:  run.bat
echo ============================================
pause
