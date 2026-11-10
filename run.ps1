# Runs the built engine. Extra arguments are forwarded, e.g.
#   .\run.ps1 --map assets\sample.rmap
param([Parameter(ValueFromRemainingArguments = $true)] $Args)
$ErrorActionPreference = "Stop"
$exe = Join-Path $PSScriptRoot "build\bin\retrofps.exe"
if (-not (Test-Path $exe)) { throw "Build first: .\build.ps1" }
& $exe @Args
