<#
.SYNOPSIS
    Master test harness for ntksh.
#>
[CmdletBinding()]
param()

$rootDir = Resolve-Path (Join-Path $PSScriptRoot '..')
$binExe = Join-Path $rootDir 'bin\ksh.exe'

Write-Host "[1/2] Running internal self-test suite..." -ForegroundColor Cyan
& $binExe --self-test
if ($LASTEXITCODE -ne 0) {
    Write-Error "Self-test failed with code $LASTEXITCODE"
    exit $LASTEXITCODE
}

Write-Host "`n[2/2] Running comprehensive built-in regression test suite..." -ForegroundColor Cyan
& powershell -ExecutionPolicy Bypass -File (Join-Path $rootDir 'tests\builtins.ps1')
if ($LASTEXITCODE -ne 0) {
    Write-Error "Builtin regression tests failed with code $LASTEXITCODE"
    exit $LASTEXITCODE
}

Write-Host "`n[SUCCESS] All test suites passed!" -ForegroundColor Green
