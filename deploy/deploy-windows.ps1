#Requires -RunAsAdministrator
<#
.SYNOPSIS
    Luma Tools — Windows VPS Deployment Script
    Installs everything, builds the project, sets up HTTPS with Caddy,
    and configures auto-start on boot.

.DESCRIPTION
    Run this on your Windows 11 VPS via Remote Desktop:
      1. Open PowerShell as Administrator
      2. cd to the luma-tools folder
      3. Run: .\deploy\deploy-windows.ps1

    What it does:
      - Installs: Python, yt-dlp, ffmpeg, CMake, Visual Studio Build Tools, Caddy
      - Builds the C++ server
      - Sets up Caddy as reverse proxy (HTTPS + auto Let's Encrypt)
      - Creates Windows Services for both (auto-start on boot)
#>

param(
    [string]$Domain = "tools.lumaplayground.com",
    [int]$BackendPort = 8080,
    [string]$InstallDir = "C:\luma-tools"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Write-Host ""
Write-Host "  ╦  ╦ ╦╔╦╗╔═╗  ╔╦╗╔═╗╔═╗╦  ╔═╗" -ForegroundColor Cyan
Write-Host "  ║  ║ ║║║║╠═╣   ║ ║ ║║ ║║  ╚═╗" -ForegroundColor Cyan
Write-Host "  ╩═╝╚═╝╩ ╩╩ ╩   ╩ ╚═╝╚═╝╩═╝╚═╝" -ForegroundColor Cyan
Write-Host "    VPS Deployment — $Domain" -ForegroundColor DarkCyan
Write-Host ""

# ─── Helper ──────────────────────────────────────────────────────────────────

function Write-Step($num, $text) {
    Write-Host ""
    Write-Host "  [$num] $text" -ForegroundColor Yellow
    Write-Host "  $('─' * 60)" -ForegroundColor DarkGray
}

function Test-CommandExists($cmd) {
    return [bool](Get-Command $cmd -ErrorAction SilentlyContinue)
}

# ─── Step 1: Install winget packages ────────────────────────────────────────

Write-Step "1/8" "Installing dependencies via winget..."

$packages = @(
    @{ Id = "Python.Python.3.12";       Name = "Python 3.12" },
    @{ Id = "Kitware.CMake";            Name = "CMake" },
    @{ Id = "Gyan.FFmpeg";              Name = "FFmpeg" }
)

foreach ($pkg in $packages) {
    Write-Host "    Checking $($pkg.Name)..." -NoNewline
    $installed = winget list --id $pkg.Id 2>$null | Select-String $pkg.Id
    if ($installed) {
        Write-Host " already installed" -ForegroundColor Green
    } else {
        Write-Host " installing..." -ForegroundColor Cyan
        winget install $pkg.Id --accept-package-agreements --accept-source-agreements --silent
    }
}

# Refresh PATH
$env:Path = [System.Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [System.Environment]::GetEnvironmentVariable('Path', 'User')

# ─── Step 2: Install Visual Studio Build Tools ──────────────────────────────

Write-Step "2/8" "Checking C++ Build Tools..."

$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$hasMSVC = $false
if (Test-Path $vsWhere) {
    $result = & $vsWhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($result) { $hasMSVC = $true }
}

if (-not $hasMSVC) {
    Write-Host "    Installing Visual Studio Build Tools (this takes a while)..." -ForegroundColor Cyan
    winget install Microsoft.VisualStudio.2022.BuildTools --accept-package-agreements --accept-source-agreements --silent --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    Write-Host "    Build Tools installed. You may need to restart this script." -ForegroundColor Yellow
} else {
    Write-Host "    C++ Build Tools found" -ForegroundColor Green
}

# ─── Step 3: Install yt-dlp ─────────────────────────────────────────────────

Write-Step "3/8" "Installing yt-dlp..."

py -m pip install --upgrade yt-dlp 2>$null
if (-not $?) {
    python -m pip install --upgrade yt-dlp 2>$null
}

# Refresh PATH again
$env:Path = [System.Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [System.Environment]::GetEnvironmentVariable('Path', 'User')

# Add Python Scripts to system PATH if not there
$pyScripts = (py -c "import sysconfig; print(sysconfig.get_path('scripts'))" 2>$null)
if ($pyScripts -and (Test-Path $pyScripts)) {
    $machinePath = [System.Environment]::GetEnvironmentVariable('Path', 'Machine')
    if ($machinePath -notlike "*$pyScripts*") {
        [System.Environment]::SetEnvironmentVariable('Path', "$machinePath;$pyScripts", 'Machine')
        $env:Path += ";$pyScripts"
        Write-Host "    Added $pyScripts to system PATH" -ForegroundColor Green
    }
}

# Verify
$ytdlpVer = & yt-dlp --version 2>$null
if ($ytdlpVer) {
    Write-Host "    yt-dlp v$ytdlpVer" -ForegroundColor Green
} else {
    Write-Host "    WARNING: yt-dlp not on PATH, server will auto-locate it" -ForegroundColor Yellow
}

# ─── Step 4: Copy project files ─────────────────────────────────────────────

Write-Step "4/8" "Copying project to $InstallDir..."

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

# ─── Step 5: Build the C++ server ───────────────────────────────────────────

Write-Step "5/8" "Building C++ server..."

if (-not (Test-Path "build")) {
    New-Item -ItemType Directory -Path "build" | Out-Null
}

Push-Location build
& cmake .. -DCMAKE_BUILD_TYPE=Release
& cmake --build . --config Release
Pop-Location

if (Test-Path "build\Release\luma-tools.exe") {
    Write-Host "    Build successful!" -ForegroundColor Green
} else {
    Write-Host "    ERROR: Build failed!" -ForegroundColor Red
    exit 1
}

# ─── Step 6: Install Caddy (HTTPS reverse proxy) ────────────────────────────

Write-Step "6/8" "Setting up Caddy (HTTPS reverse proxy)..."

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

# ─── Step 7: Open firewall ports ────────────────────────────────────────────

Write-Step "7/8" "Configuring firewall..."

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

# ─── Step 8: Create Windows Services ────────────────────────────────────────

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

# ─── Done ────────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "  ════════════════════════════════════════════════════" -ForegroundColor Green
Write-Host "   DEPLOYMENT COMPLETE!" -ForegroundColor Green
Write-Host "  ════════════════════════════════════════════════════" -ForegroundColor Green
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
