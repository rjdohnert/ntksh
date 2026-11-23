[CmdletBinding()]
param(
    [string]$KshPath,
    [int]$Iterations = 1000
)

$ErrorActionPreference = 'Stop'
if (-not $KshPath) {
    $KshPath = Join-Path $PSScriptRoot '..\bin\ksh.exe'
}
$resolvedKsh = Resolve-Path $KshPath
Write-Host "[FUZZ] Starting fuzzing session against $resolvedKsh with $Iterations iterations..." -ForegroundColor Cyan

$chars = 'abcdefghijklmnopqrstuvwxyz0123456789 `~!@#$%^&*()-_=+[{]}\;:",<.>/?'
$rng = New-Object System.Random(42)

$crashCount = 0
$hangCount = 0
$completedCount = 0

function Run-KshFuzzCase([string]$cmdText) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $resolvedKsh
    $psi.Arguments = "-c ""$($cmdText -replace '(\\*)""', '$1$1\""')"""
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    
    $p = [System.Diagnostics.Process]::Start($psi)
    if (-not $p.WaitForExit(3000)) {
        $p.Kill()
        $p.WaitForExit()
        return 'HANG'
    }
    
    # 0xC0000005 is ACCESS_VIOLATION, 0xC00000FD is STACK_OVERFLOW
    if ($p.ExitCode -eq -1073741819 -or $p.ExitCode -eq -1073741571 -or $p.ExitCode -eq 3221225477 -or $p.ExitCode -eq 3221225725) {
        return 'CRASH'
    }
    return 'OK'
}

# 1. Structural Fuzzing Suites
Write-Host "[FUZZ] 1/4: Testing nested expansions and arithmetic boundaries..." -ForegroundColor Yellow
for ($i = 0; $i -lt 150; $i++) {
    $depth = $rng.Next(1, 40)
    $nested = ('$(' * $depth) + 'echo ok' + (')' * $depth)
    $res = Run-KshFuzzCase $nested
    if ($res -eq 'CRASH') { $crashCount++; Write-Host "CRASH on: $nested" -ForegroundColor Red }
    elseif ($res -eq 'HANG') { $hangCount++; Write-Host "HANG on: $nested" -ForegroundColor Red }
    else { $completedCount++ }

    $num1 = $rng.Next(-1000000, 1000000)
    $num2 = $rng.Next(-10, 10)
    $op = @('+', '-', '*', '/', '%', '<<', '>>', '&', '|', '^')[$rng.Next(0, 10)]
    $expr = "echo `$(( $num1 $op $num2 ))"
    $res = Run-KshFuzzCase $expr
    if ($res -eq 'CRASH') { $crashCount++; Write-Host "CRASH on: $expr" -ForegroundColor Red }
    elseif ($res -eq 'HANG') { $hangCount++; Write-Host "HANG on: $expr" -ForegroundColor Red }
    else { $completedCount++ }
}

Write-Host "[FUZZ] 2/4: Testing parameter expansion edge cases & pattern substitutions..." -ForegroundColor Yellow
for ($i = 0; $i -lt 150; $i++) {
    $patLen = $rng.Next(1, 20)
    $repLen = $rng.Next(0, 20)
    $pat = -join ((1..$patLen) | % { $chars[$rng.Next(0, $chars.Length)] })
    $rep = -join ((0..$repLen) | % { $chars[$rng.Next(0, $chars.Length)] })
    $testCmd = "v='test string value 12345'; echo `${v//$pat/$rep}; echo `${v:$($rng.Next(-50, 50)):$($rng.Next(-10, 50))}"
    $res = Run-KshFuzzCase $testCmd
    if ($res -eq 'CRASH') { $crashCount++; Write-Host "CRASH on param expansion" -ForegroundColor Red }
    elseif ($res -eq 'HANG') { $hangCount++; Write-Host "HANG on param expansion" -ForegroundColor Red }
    else { $completedCount++ }
}

Write-Host "[FUZZ] 3/4: Testing malformed tokens, quotes, and redirections..." -ForegroundColor Yellow
for ($i = 0; $i -lt 150; $i++) {
    $len = $rng.Next(5, 120)
    $garbage = -join ((1..$len) | % { $chars[$rng.Next(0, $chars.Length)] })
    $res = Run-KshFuzzCase "echo $garbage >& $($rng.Next(-5, 30))"
    if ($res -eq 'CRASH') { $crashCount++; Write-Host "CRASH on garbage" -ForegroundColor Red }
    elseif ($res -eq 'HANG') { $hangCount++; Write-Host "HANG on garbage" -ForegroundColor Red }
    else { $completedCount++ }
}

Write-Host "[FUZZ] 4/4: Testing oversized inputs and boundary buffers (> 4096 bytes)..." -ForegroundColor Yellow
for ($i = 0; $i -lt 50; $i++) {
    $bigLen = $rng.Next(4090, 8200)
    $bigStr = -join ((1..$bigLen) | % { 'A' })
    $res = Run-KshFuzzCase "v='$bigStr'; echo `${#v}"
    if ($res -eq 'CRASH') { $crashCount++; Write-Host "CRASH on oversized input" -ForegroundColor Red }
    elseif ($res -eq 'HANG') { $hangCount++; Write-Host "HANG on oversized input" -ForegroundColor Red }
    else { $completedCount++ }
}

$color = if ($crashCount -eq 0 -and $hangCount -eq 0) { 'Green' } else { 'Red' }
Write-Host "[FUZZ] Finished: $completedCount cases passed | Crashes: $crashCount | Hangs: $hangCount" -ForegroundColor $color
if ($crashCount -gt 0 -or $hangCount -gt 0) {
    throw "Fuzzing detected failures."
}
