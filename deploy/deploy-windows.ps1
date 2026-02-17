#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Luma Tools - Windows VPS Deployment Script
    Installs everything, builds the project, sets up HTTPS with Caddy,
    and configures auto-start on boot.

.DESCRIPTION
    Run this on your Windows VPS (Server or Desktop) via PowerShell as Admin:
      1. Open PowerShell as Administrator
      2. cd to the luma-tools folder
      3. Run: .\deploy\deploy-windows.ps1

    What it does:
      - Installs: Python, yt-dlp, ffmpeg, CMake, Visual Studio Build Tools, Caddy
      - Builds the C++ server
      - Sets up Caddy as reverse proxy (HTTPS + auto Let's Encrypt)
      - Creates Windows Services for both (auto-start on boot)

    Works on both Windows Server (no winget) and Windows 10/11 (with winget).
#>

param(
    [string]$Domain = "tools.lumaplayground.com",
    [int]$BackendPort = 8080,
    [string]$InstallDir = "C:\luma-tools"
)

# Use Continue so individual command failures don't kill the whole script.
# We handle errors manually with try/catch where needed.
$ErrorActionPreference = "Continue"
Set-StrictMode -Version Latest

# Force TLS 1.2 for downloads
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

Write-Host ""
Write-Host "  LUMA TOOLS" -ForegroundColor Cyan
Write-Host "  ==========" -ForegroundColor Cyan
Write-Host "  VPS Deployment -- $Domain" -ForegroundColor DarkCyan
Write-Host ""

# --- Helper -------------------------------------------------------------------

function Write-Step($num, $text) {
    Write-Host ""
    Write-Host "  [$num] $text" -ForegroundColor Yellow
    Write-Host "  $('-' * 60)" -ForegroundColor DarkGray
}

function Test-CommandExists($cmd) {
    return [bool](Get-Command $cmd -ErrorAction SilentlyContinue)
}

function Refresh-Path {
    $env:Path = [System.Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [System.Environment]::GetEnvironmentVariable('Path', 'User')
}

# Find a real python.exe by scanning common install directories.
# This avoids the Windows Store alias (python.exe in WindowsApps) that
# opens a "Select an app" dialog instead of running Python.
function Find-RealPython {
    $searchPaths = @(
        "C:\Python3*\python.exe",
        "C:\Program Files\Python*\python.exe",
        "C:\Program Files (x86)\Python*\python.exe",
        "$env:LocalAppData\Programs\Python\Python*\python.exe",
        "$env:ProgramFiles\Python*\python.exe"
    )
    foreach ($pattern in $searchPaths) {
        $found = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue | Select-Object -Last 1
        if ($found) { return $found.FullName }
    }
    # Also check if py.exe launcher exists (not a Store alias)
    $pyLauncher = "C:\Windows\py.exe"
    if (Test-Path $pyLauncher) { return $pyLauncher }
    return $null
}

# Test if winget actually works (not just exists on PATH)
$hasWinget = $false
if (Test-CommandExists "winget") {
    try {
        $wgTest = & winget --version 2>$null
        if ($wgTest -and $wgTest -match '\d') { $hasWinget = $true }
    } catch {}
}
if ($hasWinget) {
    Write-Host "  [INFO] winget detected -- using winget where possible" -ForegroundColor DarkGray
} else {
    Write-Host "  [INFO] winget unavailable -- using direct downloads" -ForegroundColor DarkGray
}

# --- Step 1: Install Python ---------------------------------------------------

Write-Step "1/8" "Installing Python..."

# Try to find real Python (not the Store alias)
$pythonExe = Find-RealPython
if ($pythonExe) {
    $pyVer = & $pythonExe --version 2>$null
    Write-Host "    Python found: $pyVer ($pythonExe)" -ForegroundColor Green
} else {
    Write-Host "    Python not found, installing..." -ForegroundColor Cyan
    $installOk = $false

    if ($hasWinget) {
        Write-Host "    Trying winget..." -ForegroundColor Cyan
        try {
            & winget install Python.Python.3.12 --accept-package-agreements --accept-source-agreements --silent 2>$null
            if ($LASTEXITCODE -eq 0) { $installOk = $true }
        } catch {}
    }

    if (-not $installOk) {
        Write-Host "    Downloading Python 3.12 installer..." -ForegroundColor Cyan
        $pyInstaller = "$env:TEMP\python-3.12.exe"
        Invoke-WebRequest -Uri "https://www.python.org/ftp/python/3.12.8/python-3.12.8-amd64.exe" -OutFile $pyInstaller -UseBasicParsing
        Write-Host "    Installing Python (silent, with PATH)..." -ForegroundColor Cyan
        Start-Process -FilePath $pyInstaller -ArgumentList "/quiet InstallAllUsers=1 PrependPath=1 Include_pip=1" -Wait -NoNewWindow
        Remove-Item $pyInstaller -Force -ErrorAction SilentlyContinue
    }

    Refresh-Path
    $pythonExe = Find-RealPython
    if ($pythonExe) {
        Write-Host "    Python installed: $pythonExe" -ForegroundColor Green
    } else {
        Write-Host "    WARNING: Python install may need a shell restart" -ForegroundColor Yellow
    }
}

# Ensure Python's folder and Scripts folder are on the system PATH
if ($pythonExe) {
    $pyDir = Split-Path $pythonExe
    $pyScriptsDir = Join-Path $pyDir "Scripts"
    $machinePath = [System.Environment]::GetEnvironmentVariable('Path', 'Machine')
    $pathChanged = $false
    if ($machinePath -notlike "*$pyDir*") {
        $machinePath = "$machinePath;$pyDir"
        $pathChanged = $true
    }
    if ((Test-Path $pyScriptsDir) -and $machinePath -notlike "*$pyScriptsDir*") {
        $machinePath = "$machinePath;$pyScriptsDir"
        $pathChanged = $true
    }
    if ($pathChanged) {
        [System.Environment]::SetEnvironmentVariable('Path', $machinePath, 'Machine')
        Refresh-Path
        Write-Host "    Added Python to system PATH" -ForegroundColor Green
    }
}

# --- Step 2: Install CMake ----------------------------------------------------

Write-Step "2/8" "Installing CMake..."

Refresh-Path
$hasCMake = (Test-CommandExists "cmake") -or (Test-Path "C:\Program Files\CMake\bin\cmake.exe")
if ($hasCMake) {
    $cmakeVer = $null
    try { $cmakeVer = & cmake --version 2>$null | Select-Object -First 1 } catch {}
    Write-Host "    CMake already installed: $cmakeVer" -ForegroundColor Green
} else {
    $installOk = $false
    if ($hasWinget) {
        Write-Host "    Trying winget..." -ForegroundColor Cyan
        try {
            & winget install Kitware.CMake --accept-package-agreements --accept-source-agreements --silent 2>$null
            if ($LASTEXITCODE -eq 0) { $installOk = $true }
        } catch {}
    }
    if (-not $installOk) {
        Write-Host "    Downloading CMake installer..." -ForegroundColor Cyan
        $cmakeInstaller = "$env:TEMP\cmake-installer.msi"
        Invoke-WebRequest -Uri "https://github.com/Kitware/CMake/releases/download/v3.31.5/cmake-3.31.5-windows-x86_64.msi" -OutFile $cmakeInstaller -UseBasicParsing
        Write-Host "    Installing CMake..." -ForegroundColor Cyan
        Start-Process -FilePath "msiexec.exe" -ArgumentList "/i `"$cmakeInstaller`" /quiet ADD_CMAKE_TO_PATH=System" -Wait -NoNewWindow
        Remove-Item $cmakeInstaller -Force -ErrorAction SilentlyContinue
        Write-Host "    CMake installed" -ForegroundColor Green
    }
    Refresh-Path
}

# --- Step 3: Install FFmpeg ---------------------------------------------------

Write-Step "3/8" "Installing FFmpeg..."

$ffmpegDir = "C:\ffmpeg"
$ffmpegExe = "$ffmpegDir\bin\ffmpeg.exe"
$hasFFmpeg = (Test-CommandExists "ffmpeg") -or (Test-Path $ffmpegExe)

if ($hasFFmpeg) {
    Write-Host "    FFmpeg already installed" -ForegroundColor Green
} else {
    $installOk = $false
    if ($hasWinget) {
        Write-Host "    Trying winget..." -ForegroundColor Cyan
        try {
            & winget install Gyan.FFmpeg --accept-package-agreements --accept-source-agreements --silent 2>$null
            if ($LASTEXITCODE -eq 0) { $installOk = $true }
        } catch {}
    }
    if (-not $installOk) {
        Write-Host "    Downloading FFmpeg..." -ForegroundColor Cyan
        $ffmpegZip = "$env:TEMP\ffmpeg.zip"
        Invoke-WebRequest -Uri "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip" -OutFile $ffmpegZip -UseBasicParsing
        Write-Host "    Extracting FFmpeg to $ffmpegDir..." -ForegroundColor Cyan
        New-Item -ItemType Directory -Path $ffmpegDir -Force | Out-Null
        Expand-Archive -Path $ffmpegZip -DestinationPath "$env:TEMP\ffmpeg-extract" -Force
        # The zip contains a subfolder like ffmpeg-7.1-essentials_build
        $extracted = Get-ChildItem "$env:TEMP\ffmpeg-extract" -Directory | Select-Object -First 1
        if ($extracted) {
            Copy-Item -Path "$($extracted.FullName)\*" -Destination $ffmpegDir -Recurse -Force
        }
        Remove-Item $ffmpegZip -Force -ErrorAction SilentlyContinue
        Remove-Item "$env:TEMP\ffmpeg-extract" -Recurse -Force -ErrorAction SilentlyContinue

        # Add to system PATH
        $machinePath = [System.Environment]::GetEnvironmentVariable('Path', 'Machine')
        if ($machinePath -notlike "*$ffmpegDir\bin*") {
            [System.Environment]::SetEnvironmentVariable('Path', "$machinePath;$ffmpegDir\bin", 'Machine')
        }
        Write-Host "    FFmpeg installed" -ForegroundColor Green
    }
    Refresh-Path
}

# --- Step 4: Install Visual Studio Build Tools --------------------------------

Write-Step "4/8" "Checking C++ Build Tools..."

$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$hasMSVC = $false
if (Test-Path $vsWhere) {
    $result = & $vsWhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($result) { $hasMSVC = $true }
}

if (-not $hasMSVC) {
    Write-Host "    Downloading Visual Studio Build Tools (this takes a while)..." -ForegroundColor Cyan
    $vsInstaller = "$env:TEMP\vs_buildtools.exe"
    Invoke-WebRequest -Uri "https://aka.ms/vs/17/release/vs_buildtools.exe" -OutFile $vsInstaller -UseBasicParsing
    Write-Host "    Installing C++ Build Tools..." -ForegroundColor Cyan
    Start-Process -FilePath $vsInstaller -ArgumentList "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended" -Wait -NoNewWindow
    Remove-Item $vsInstaller -Force -ErrorAction SilentlyContinue
    Write-Host "    Build Tools installed. You may need to restart this script." -ForegroundColor Yellow
} else {
    Write-Host "    C++ Build Tools found" -ForegroundColor Green
}

# --- Step 5: Install yt-dlp ---------------------------------------------------

Write-Step "5/8" "Installing yt-dlp..."

Refresh-Path

# Re-discover python in case PATH changed
if (-not $pythonExe) { $pythonExe = Find-RealPython }

# Try pip install using the real python we found
$pipOk = $false
if ($pythonExe) {
    try {
        & $pythonExe -m pip install --upgrade yt-dlp 2>$null
        if ($LASTEXITCODE -eq 0) { $pipOk = $true }
    } catch {}
}

if (-not $pipOk) {
    Write-Host "    pip install failed, trying direct download..." -ForegroundColor Yellow
    # Download yt-dlp.exe directly
    $ytdlpTarget = "C:\yt-dlp\yt-dlp.exe"
    New-Item -ItemType Directory -Path "C:\yt-dlp" -Force | Out-Null
    try {
        Invoke-WebRequest -Uri "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe" -OutFile $ytdlpTarget -UseBasicParsing
        $machinePath = [System.Environment]::GetEnvironmentVariable('Path', 'Machine')
        if ($machinePath -notlike "*C:\yt-dlp*") {
            [System.Environment]::SetEnvironmentVariable('Path', "$machinePath;C:\yt-dlp", 'Machine')
        }
        Write-Host "    yt-dlp.exe downloaded to C:\yt-dlp\" -ForegroundColor Green
    } catch {
        Write-Host "    WARNING: Could not download yt-dlp" -ForegroundColor Yellow
    }
}

Refresh-Path

# Add Python Scripts to system PATH if not there
if ($pythonExe) {
    $pyScripts = $null
    try { $pyScripts = & $pythonExe -c "import sysconfig; print(sysconfig.get_path('scripts'))" 2>$null } catch {}
    if ($pyScripts -and (Test-Path $pyScripts)) {
        $machinePath = [System.Environment]::GetEnvironmentVariable('Path', 'Machine')
        if ($machinePath -notlike "*$pyScripts*") {
            [System.Environment]::SetEnvironmentVariable('Path', "$machinePath;$pyScripts", 'Machine')
            $env:Path += ";$pyScripts"
            Write-Host "    Added $pyScripts to system PATH" -ForegroundColor Green
        }
    }
}

# Verify
Refresh-Path
$ytdlpVer = $null
try { $ytdlpVer = & yt-dlp --version 2>$null } catch {}
if ($ytdlpVer) {
    Write-Host "    yt-dlp v$ytdlpVer" -ForegroundColor Green
} else {
    Write-Host "    WARNING: yt-dlp not on PATH, server will auto-locate it" -ForegroundColor Yellow
}

# --- Step 6: Copy project files and build -------------------------------------

Write-Step "6/8" "Copying project to $InstallDir and building..."

$scriptDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)

if ($scriptDir -ne $InstallDir) {
    if (-not (Test-Path $InstallDir)) {
        New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    }
    Copy-Item -Path "$scriptDir\*" -Destination $InstallDir -Recurse -Force -Exclude @("build", ".git", "downloads")
    Write-Host "    Copied to $InstallDir" -ForegroundColor Green
} else {
    Write-Host "    Already in install directory" -ForegroundColor Green
}

Set-Location $InstallDir

# Build
Refresh-Path

if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}

Push-Location build

# Find cmake
$cmakeExe = "cmake"
if (-not (Test-CommandExists "cmake")) {
    # Common locations
    $cmakePaths = @(
        "C:\Program Files\CMake\bin\cmake.exe",
        "C:\Program Files (x86)\CMake\bin\cmake.exe"
    )
    foreach ($p in $cmakePaths) {
        if (Test-Path $p) { $cmakeExe = $p; break }
    }
}

& $cmakeExe .. -DCMAKE_BUILD_TYPE=Release
& $cmakeExe --build . --config Release
Pop-Location

if (Test-Path "build\Release\luma-tools.exe") {
    Write-Host "    Build successful!" -ForegroundColor Green
} else {
    Write-Host "    ERROR: Build failed!" -ForegroundColor Red
    exit 1
}

# --- Step 7: Install Caddy + configure firewall -------------------------------

Write-Step "7/8" "Setting up Caddy (HTTPS reverse proxy) and firewall..."

$caddyDir = "C:\caddy"
$caddyExe = "$caddyDir\caddy.exe"

if (-not (Test-Path $caddyExe)) {
    New-Item -ItemType Directory -Path $caddyDir -Force | Out-Null
    
    Write-Host "    Downloading Caddy..." -ForegroundColor Cyan
    $caddyUrl = "https://caddyserver.com/api/download?os=windows&arch=amd64"
    Invoke-WebRequest -Uri $caddyUrl -OutFile $caddyExe -UseBasicParsing
    Write-Host "    Caddy downloaded" -ForegroundColor Green
} else {
    Write-Host "    Caddy already installed" -ForegroundColor Green
}

# Create Caddyfile
$caddyfile = @"
$Domain {
    reverse_proxy localhost:$BackendPort
    encode gzip
    
    header {
        X-Content-Type-Options nosniff
        X-Frame-Options DENY
        Referrer-Policy strict-origin-when-cross-origin
    }
}
"@

Set-Content -Path "$caddyDir\Caddyfile" -Value $caddyfile
Write-Host "    Caddyfile created for $Domain" -ForegroundColor Green

# Firewall
$rules = @(
    @{ Name = "Luma Tools HTTP";  Port = 80;   Protocol = "TCP" },
    @{ Name = "Luma Tools HTTPS"; Port = 443;  Protocol = "TCP" }
)

foreach ($rule in $rules) {
    $existing = Get-NetFirewallRule -DisplayName $rule.Name -ErrorAction SilentlyContinue
    if (-not $existing) {
        New-NetFirewallRule -DisplayName $rule.Name -Direction Inbound -Protocol $rule.Protocol -LocalPort $rule.Port -Action Allow | Out-Null
        Write-Host "    Opened port $($rule.Port) ($($rule.Name))" -ForegroundColor Green
    } else {
        Write-Host "    Port $($rule.Port) already open" -ForegroundColor Green
    }
}

# --- Step 8: Create Windows Services -----------------------------------------

Write-Step "8/8" "Creating Windows services (auto-start on boot)..."

# Install NSSM (Non-Sucking Service Manager) for service management
$nssmDir = "C:\nssm"
$nssmExe = "$nssmDir\nssm.exe"

if (-not (Test-Path $nssmExe)) {
    New-Item -ItemType Directory -Path $nssmDir -Force | Out-Null
    Write-Host "    Downloading NSSM..." -ForegroundColor Cyan
    $nssmUrl = "https://nssm.cc/release/nssm-2.24.zip"
    $nssmZip = "$nssmDir\nssm.zip"
    Invoke-WebRequest -Uri $nssmUrl -OutFile $nssmZip -UseBasicParsing
    Expand-Archive -Path $nssmZip -DestinationPath $nssmDir -Force
    # Find the 64-bit exe
    $found = Get-ChildItem -Path $nssmDir -Recurse -Filter "nssm.exe" | Where-Object { $_.DirectoryName -like "*win64*" } | Select-Object -First 1
    if ($found) {
        Copy-Item $found.FullName $nssmExe -Force
    }
    Remove-Item $nssmZip -Force
    Write-Host "    NSSM installed" -ForegroundColor Green
}

# Stop existing services if they exist
& $nssmExe stop "LumaTools" 2>$null
& $nssmExe stop "LumaToolsCaddy" 2>$null
Start-Sleep -Seconds 2

# Refresh PATH one final time to capture everything installed above
Refresh-Path

# Create Luma Tools backend service
& $nssmExe remove "LumaTools" confirm 2>$null
& $nssmExe install "LumaTools" "$InstallDir\build\Release\luma-tools.exe"
& $nssmExe set "LumaTools" AppDirectory "$InstallDir\build\Release"
& $nssmExe set "LumaTools" Description "Luma Tools - Universal Media Downloader Backend"
& $nssmExe set "LumaTools" Start SERVICE_AUTO_START
& $nssmExe set "LumaTools" AppStdout "$InstallDir\logs\backend.log"
& $nssmExe set "LumaTools" AppStderr "$InstallDir\logs\backend-error.log"
& $nssmExe set "LumaTools" AppRotateFiles 1
& $nssmExe set "LumaTools" AppEnvironmentExtra "PATH=$env:Path"

# Create Caddy service
& $nssmExe remove "LumaToolsCaddy" confirm 2>$null
& $nssmExe install "LumaToolsCaddy" "$caddyExe" "run --config $caddyDir\Caddyfile"
& $nssmExe set "LumaToolsCaddy" AppDirectory "$caddyDir"
& $nssmExe set "LumaToolsCaddy" Description "Luma Tools - Caddy HTTPS Reverse Proxy"
& $nssmExe set "LumaToolsCaddy" Start SERVICE_AUTO_START
& $nssmExe set "LumaToolsCaddy" AppStdout "$InstallDir\logs\caddy.log"
& $nssmExe set "LumaToolsCaddy" AppStderr "$InstallDir\logs\caddy-error.log"

# Create logs directory
New-Item -ItemType Directory -Path "$InstallDir\logs" -Force | Out-Null

# Start services
& $nssmExe start "LumaTools"
Start-Sleep -Seconds 2
& $nssmExe start "LumaToolsCaddy"

Write-Host "    Services created and started!" -ForegroundColor Green

# --- Done ---------------------------------------------------------------------

Write-Host ""
Write-Host "  ========================================================" -ForegroundColor Green
Write-Host "   DEPLOYMENT COMPLETE!" -ForegroundColor Green
Write-Host "  ========================================================" -ForegroundColor Green
Write-Host ""
Write-Host "  Your site: https://$Domain" -ForegroundColor Cyan
Write-Host "  Backend:   http://localhost:$BackendPort" -ForegroundColor DarkCyan
Write-Host ""
Write-Host "  IMPORTANT: Make sure your domain's DNS" -ForegroundColor Yellow
Write-Host "  A record points to this server's IP address!" -ForegroundColor Yellow
Write-Host ""
Write-Host "  Manage services:" -ForegroundColor DarkGray
Write-Host "    $nssmExe restart LumaTools" -ForegroundColor DarkGray
Write-Host "    $nssmExe restart LumaToolsCaddy" -ForegroundColor DarkGray
Write-Host "    $nssmExe status LumaTools" -ForegroundColor DarkGray
Write-Host ""
Write-Host "  Logs: $InstallDir\logs\" -ForegroundColor DarkGray
Write-Host ""
