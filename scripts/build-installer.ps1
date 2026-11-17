# Build the Windows installer (EatAppSetup-x64.exe) from the one-click output.
# Requires Inno Setup 6: https://jrsoftware.org/isdl.php
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1
#   powershell -ExecutionPolicy Bypass -File .\scripts\build-installer.ps1 -Version 0.3.0 -SkipBuild

param(
    [string]$Version = "0.3.0",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

if (-not $SkipBuild -and -not (Test-Path "dist\eat-app-windows-x64\eat-app.exe")) {
    Write-Host "==> 运行一键构建" -ForegroundColor Cyan
    & powershell -ExecutionPolicy Bypass -File ".\scripts\build-windows-oneclick.ps1"
    if ($LASTEXITCODE -ne 0) {
        throw "一键构建失败（exit=$LASTEXITCODE）"
    }
}

$candidates = @(
    (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
    (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe"),
    (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
)
$iscc = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
    $command = Get-Command iscc -ErrorAction SilentlyContinue
    if ($command) { $iscc = $command.Source }
}
if (-not $iscc) {
    throw "未找到 Inno Setup 6（ISCC.exe）。安装后重试：https://jrsoftware.org/isdl.php"
}
if (-not (Test-Path "dist\eat-app-windows-x64\eat-app.exe")) {
    throw "缺少 dist\eat-app-windows-x64\eat-app.exe，请先运行 build-windows-oneclick.ps1"
}

Write-Host "==> 生成安装包" -ForegroundColor Cyan
& $iscc "/DMyAppVersion=$Version" "packaging\eat-app.iss"
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup 编译失败（exit=$LASTEXITCODE）"
}

$installer = Join-Path $repo "dist\EatAppSetup-x64.exe"
if (-not (Test-Path $installer)) {
    throw "没有找到安装包：$installer"
}
Write-Host ""
Write-Host "安装包已生成：" -ForegroundColor Green
Write-Host "  $installer"
Write-Host ""
Write-Host "把安装包发给 Windows 用户；安装时会创建桌面快捷方式。" -ForegroundColor Green
