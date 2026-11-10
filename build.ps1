# Configures and builds the engine with the MSYS2 UCRT64 toolchain.
# Usage:  .\build.ps1 [-Debug] [-Clean]
param(
    [switch]$Debug,
    [switch]$Clean
)

$ErrorActionPreference = "Continue"
$root = $PSScriptRoot
$msys = "C:\Users\ADMIN\tools\msys64"
$ucrt = Join-Path $msys "ucrt64"

if (-not (Test-Path (Join-Path $ucrt "bin\g++.exe"))) {
    throw "MinGW toolchain not found at $ucrt. Install MSYS2 UCRT64 GCC first."
}

$env:PATH = "$ucrt\bin;$msys\usr\bin;$env:PATH"

$buildDir = Join-Path $root "build"
if ($Clean -and (Test-Path $buildDir)) {
    Remove-Item -Recurse -Force $buildDir
}

$config = if ($Debug) { "Debug" } else { "Release" }
# PowerShell 5.1 passes "-DNAME=$var" tokens through without expanding $var,
# so build the argument strings explicitly first.
$buildTypeArg = "-DCMAKE_BUILD_TYPE=$config"
$prefixArg = "-DCMAKE_PREFIX_PATH=$ucrt"
$ccArg = "-DCMAKE_C_COMPILER=$ucrt\bin\gcc.exe"
$cxxArg = "-DCMAKE_CXX_COMPILER=$ucrt\bin\g++.exe"

& cmake -S $root -B $buildDir -G Ninja `
    $buildTypeArg $prefixArg $ccArg $cxxArg
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

& cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }

Write-Host ""
Write-Host "Build complete: $buildDir\bin\retrofps.exe"
