# One-click Windows build: eat-app.exe + eat-agent.exe + web + OpenSSL DLL.
# Usage:
#   powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1
#   powershell -ExecutionPolicy Bypass -File .\scripts\build-windows-oneclick.ps1 `
#     -VcpkgRoot C:\vcpkg -OutDir dist\eat-app-windows-x64

param(
    [string]$VcpkgRoot = "C:\vcpkg",
    [string]$WebView2Root = "",
    [string]$OutDir = "dist\eat-app-windows-x64",
    [switch]$SkipAgent
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

function Require-Command([string]$name, [string]$command) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "$name 未安装，或没有加入 PATH（需要：$command）"
    }
}

function Invoke-Checked([string]$description, [scriptblock]$block) {
    Write-Host "==> $description" -ForegroundColor Cyan
    & $block
    if ($LASTEXITCODE -ne 0) {
        throw "$description 失败（exit=$LASTEXITCODE）"
    }
}

Write-Host "==> 检查工具链" -ForegroundColor Cyan
Require-Command "CMake" "cmake"
Require-Command "Git" "git"

$vsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vsWhere)) {
    throw "未找到 Visual Studio Installer。请安装 VS2022 Build Tools，并勾选“使用 C++ 的桌面开发”"
}
$vsPath = & $vsWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw "未找到 MSVC C++ 工具链。请在 VS Installer 中勾选“使用 C++ 的桌面开发”"
}
Write-Host "    VS: $vsPath"

if (-not $SkipAgent) {
    $python = (Get-Command python -ErrorAction SilentlyContinue)
    if (-not $python) {
        $python = (Get-Command py -ErrorAction SilentlyContinue)
    }
    if (-not $python) {
        throw "未找到 Python。请安装 Python 3.11/3.12，或用 -SkipAgent 跳过 Agent 打包"
    }
}

Write-Host "==> 准备 vcpkg / OpenSSL" -ForegroundColor Cyan
if (-not (Test-Path (Join-Path $VcpkgRoot "vcpkg.exe"))) {
    if (-not (Test-Path $VcpkgRoot)) {
        Invoke-Checked "克隆 vcpkg" { git clone https://github.com/microsoft/vcpkg $VcpkgRoot }
    }
    Invoke-Checked "引导 vcpkg" { & (Join-Path $VcpkgRoot "bootstrap-vcpkg.bat") }
}
if (-not (Test-Path (Join-Path $VcpkgRoot "installed\x64-windows\include\openssl\ssl.h"))) {
    Invoke-Checked "安装 OpenSSL x64" { & (Join-Path $VcpkgRoot "vcpkg.exe") install openssl:x64-windows }
} else {
    Write-Host "    OpenSSL 已安装"
}

Write-Host "==> 准备 WebView2 SDK" -ForegroundColor Cyan
if (-not $WebView2Root) {
    $WebView2Root = Join-Path $repo "third_party\webview2"
}
$webviewHeader = Join-Path $WebView2Root "build\native\include\WebView2.h"
if (-not (Test-Path $webviewHeader)) {
    $parent = Split-Path $WebView2Root
    if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    $zip = Join-Path $env:TEMP "webview2-sdk.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Invoke-Checked "下载 WebView2 SDK" {
        Invoke-WebRequest -Uri "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2" `
            -OutFile $zip -UseBasicParsing
    }
    Expand-Archive -Path $zip -DestinationPath $WebView2Root -Force
}
Write-Host "    WebView2: $WebView2Root"

Write-Host "==> 配置 CMake（Visual Studio 17 2022 / x64）" -ForegroundColor Cyan
Invoke-Checked "CMake 配置" {
    cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 `
        -DCMAKE_TOOLCHAIN_FILE="$VcpkgRoot\scripts\buildsystems\vcpkg.cmake" `
        -DWEBVIEW2_ROOT="$WebView2Root"
}

Write-Host "==> 编译 eat-app.exe" -ForegroundColor Cyan
Invoke-Checked "编译 eat-app" {
    cmake --build build-win --config Release --target eat-app
}

$appExe = Join-Path $repo "build-win\client\app\Release\eat-app.exe"
$webDir = Join-Path $repo "build-win\client\app\Release\web"
if (-not (Test-Path $appExe)) {
    throw "没有找到 eat-app.exe，请检查上面的编译输出"
}

if (-not $SkipAgent) {
    Write-Host "==> 安装 PyInstaller 并打包 eat-agent.exe" -ForegroundColor Cyan
    Invoke-Checked "安装 PyInstaller" { & $python.Source -m pip install --upgrade pyinstaller }
    Invoke-Checked "打包 eat-agent" {
        & $python.Source -m PyInstaller --noconfirm --onefile --name eat-agent `
            --paths . `
            --add-data "agent/config;agent/config" `
            --add-data "agent/skills;agent/skills" `
            agent/ipc_server.py
    }
}

Write-Host "==> 组装运行目录" -ForegroundColor Cyan
if (Test-Path $OutDir) { Remove-Item $OutDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Copy-Item $appExe $OutDir -Force
if (Test-Path $webDir) {
    Copy-Item $webDir (Join-Path $OutDir "web") -Recurse -Force
} else {
    throw "没有找到 web 资源目录：$webDir"
}

$agentExe = Join-Path $repo "dist\eat-agent.exe"
if (-not $SkipAgent -and (Test-Path $agentExe)) {
    Copy-Item $agentExe $OutDir -Force
}

$vcpkgBin = Join-Path $VcpkgRoot "installed\x64-windows\bin"
foreach ($dll in @("libcrypto-3-x64.dll", "libssl-3-x64.dll")) {
    $source = Join-Path $vcpkgBin $dll
    if (Test-Path $source) {
        Copy-Item $source $OutDir -Force
    } else {
        Write-Warning "缺少 $dll，运行 GUI 时可能提示找不到 OpenSSL"
    }
}

$startBat = @"
@echo off
cd /d "%~dp0"
start "" eat-app.exe
"@
Set-Content -Path (Join-Path $OutDir "Start.bat") -Value $startBat -Encoding OEM

$readme = @"
泛饭 Windows 运行目录

1. 确保这台电脑能访问服务端的 9443 端口：
   Test-NetConnection <服务端IP> -Port 9443

2. 双击 Start.bat（或直接双击 eat-app.exe）。

3. 首次启动会显示“连接服务端”：
   - 服务器地址：<服务端IP>
   - 端口：9443
   - 账号 / 密码
   - 首次使用勾选“注册新账号”
   点击“连接”。成功后下次自动进入。

4. 聊天功能需要 DeepSeek Key。可提前设置用户环境变量：
   setx DEEPSEEK_API_KEY "你的Key"
   设置后重新打开程序。

5. 服务端必须运行在 Linux；客户端不会直连 MySQL/Redis。

6. 已知限制：
   - 需要 WebView2 Runtime（Win11 自带，Win10 需安装 Evergreen Runtime）
   - 业务 TCP 为明文，仅限可信局域网
   - 单 exe 尚未实现，本目录需保持完整
"@
Set-Content -Path (Join-Path $OutDir "README.txt") -Value $readme -Encoding UTF8

$zip = Join-Path $repo "dist\eat-app-windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $OutDir "*") -DestinationPath $zip -Force

Write-Host ""
Write-Host "构建完成：" -ForegroundColor Green
Write-Host "  运行目录: $OutDir"
Write-Host "  压缩包:   $zip"
Write-Host ""
Write-Host "下一步：把压缩包发给 Windows 用户，解压后双击 Start.bat。" -ForegroundColor Green
