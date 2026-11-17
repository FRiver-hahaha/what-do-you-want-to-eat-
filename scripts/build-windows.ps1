# Windows build helper. Usage (x64 Native Tools or any shell after VS 2022 install):
#   powershell -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
#   powershell -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Gui
param(
    [switch]$Gui,
    [string]$WebView2Root = $env:WEBVIEW2_ROOT,
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Enter-Msvc {
    if (Get-Command cl.exe -ErrorAction SilentlyContinue) { return }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "Visual Studio 2022 Build Tools not found" }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64"
}

Enter-Msvc
Push-Location $root
try {
    Write-Host "==> building client_smoke.exe"
    & "$root\dist\eat-client-windows\build-client.ps1" -Out "$root\client_smoke.exe"

    if ($Gui) {
        if (-not $WebView2Root) { throw "WEBVIEW2_ROOT (or -WebView2Root) is required for -Gui" }
        Write-Host "==> configuring eat-app (WebView2)"
        cmake -S $root -B "$root\build-win" -G "Visual Studio 17 2022" -A x64 "-DWEBVIEW2_ROOT=$WebView2Root"
        cmake --build "$root\build-win" --config $Configuration --target eat-app
        Write-Host "eat-app output: $root\build-win\client\app\$Configuration\eat-app.exe"
    }
}
finally {
    Pop-Location
}

Write-Host "Windows build finished"
