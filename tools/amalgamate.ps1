$rootDir = Resolve-Path (Join-Path $PSScriptRoot '..')
$outputKsh = Join-Path $rootDir 'src\ksh.c'

$headers = @(
    "include\ksh\common.h",
    "include\ksh\ast.h",
    "include\ksh\ksh.h",
    "include\ksh\builtins.h",
    "include\ksh\help.h",
    "include\ksh\module.h"
)

$sources = @(
    "src\core\common.c",
    "src\core\env.c",
    "src\core\parser.c",
    "src\core\executor.c",
    "src\core\jobs.c",
    "src\core\repl.c",
    "src\core\module.c",
    "src\batch\batch_expand.c",
    "src\batch\batch_runtime.c",
    "src\batch\batch_pipeline.c",
    "src\core\shell.c",
    "src\builtins\builtins_core.c",
    "src\builtins\builtins_test.c",
    "src\builtins\builtins_cmd.c",
    "src\builtins\builtins_text.c",
    "src\builtins\builtins_regex.c",
    "src\builtins\builtins_grep.c",
    "src\builtins\builtins_sed.c",
    "src\builtins\builtins_awk.c",
    "src\builtins\builtins_diff.c",
    "src\builtins\builtins_file.c",
    "src\builtins\builtins_crypto.c",
    "src\builtins\builtins_system.c",
    "src\builtins\builtins_net.c",
    "src\builtins\builtins_bsd.c",
    "src\builtins\builtins_bsd2.c",
    "src\builtins\builtins_bsd3.c",
    "src\builtins\builtins_bsd4.c",
    "src\builtins\builtins_bsd5.c",
    "src\builtins\builtins_bsd6.c",
    "src\builtins\dispatch.c",
    "src\help\help.c",
    "src\main.c"
)

Write-Host "[AMALGAMATE] Generating $outputKsh..." -ForegroundColor Cyan

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine(@"
/**
 * ntksh
 * Copyright (C) 2023-2026 PC/OpenSystems. All rights reserved.
 * Licensed under the terms of the BSD-3 Clause License
 *
 * Maintainer: Roberto J. Dohnert, Kelly Wells, Adam Wells, Mary Staats, John Albright
 * Inspired by the AT&T KornShell
 * A minimal Korn Shell implementation for Windows NT systems
 *
 * Single-file amalgamation distribution (SQLite-style target).
 * Generated automatically by tools/amalgamate.ps1
 */
"@)

foreach ($h in $headers) {
    $path = Join-Path $rootDir $h
    [void]$sb.AppendLine("`n/* ========================================================================= */")
    [void]$sb.AppendLine("/* Header: $h */")
    [void]$sb.AppendLine("/* ========================================================================= */`n")
    $lines = [System.IO.File]::ReadAllLines($path, [System.Text.Encoding]::UTF8)
    foreach ($line in $lines) {
        $trimmed = $line.Trim()
        if ($trimmed.StartsWith('#include <ksh/') -or $trimmed.StartsWith('#include "ksh/')) {
            continue
        }
        [void]$sb.AppendLine($line)
    }
}

foreach ($s in $sources) {
    $path = Join-Path $rootDir $s
    [void]$sb.AppendLine("`n/* ========================================================================= */")
    [void]$sb.AppendLine("/* Source Module: $s */")
    [void]$sb.AppendLine("/* ========================================================================= */`n")
    $lines = [System.IO.File]::ReadAllLines($path, [System.Text.Encoding]::UTF8)
    foreach ($line in $lines) {
        $trimmed = $line.Trim()
        if ($trimmed.StartsWith('#include <ksh/') -or $trimmed.StartsWith('#include "ksh/')) {
            continue
        }
        [void]$sb.AppendLine($line)
    }
}

[System.IO.File]::WriteAllText($outputKsh, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
$len = (Get-Item $outputKsh).Length
Write-Host "[AMALGAMATE] Successfully generated $outputKsh ($len bytes)" -ForegroundColor Green
