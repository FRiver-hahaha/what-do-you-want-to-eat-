# Build the console client (client_smoke.exe) with MSVC.
# Run inside "x64 Native Tools Command Prompt for VS 2022".
param(
    [string]$Out = ".\client_smoke.exe"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw "cl.exe not found. Open the x64 Native Tools Command Prompt for VS 2022."
}

Push-Location $root
try {
    cl /nologo /std:c++17 /EHsc /O2 /I"$root" `
        "$root\tests\integration\client_smoke.cc" `
        "$root\shared\protocol\FrameCodec.cc" `
        "$root\shared\protocol\Crc32c.cc" `
        "$root\shared\protocol\JsonValue.cc" `
        "$root\shared\protocol\StrictJson.cc" `
        /Fe:"$Out" ws2_32.lib
    if ($LASTEXITCODE -ne 0) { throw "cl failed with $LASTEXITCODE" }
    Write-Host "built $Out"
}
finally {
    Pop-Location
}