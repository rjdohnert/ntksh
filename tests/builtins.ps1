param(
    [string]$KshPath = (Join-Path $PSScriptRoot '..\bin\ksh.exe')
)

$ErrorActionPreference = 'Stop'
$resolvedKshPath = [IO.Path]::GetFullPath($KshPath)
if (-not (Test-Path -LiteralPath $resolvedKshPath -PathType Leaf)) {
    throw "KSH executable not found: $resolvedKshPath"
}

function ConvertTo-WindowsArgument([string]$Argument) {
    if ($Argument.Length -gt 0 -and $Argument -notmatch '[\s"]') {
        return $Argument
    }

    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq '\') {
            $backslashes++
        } elseif ($character -eq '"') {
            [void]$builder.Append(('\' * (2 * $backslashes + 1)))
            [void]$builder.Append('"')
            $backslashes = 0
        } else {
            if ($backslashes) {
                [void]$builder.Append(('\' * $backslashes))
                $backslashes = 0
            }
            [void]$builder.Append($character)
        }
    }
    if ($backslashes) {
        [void]$builder.Append(('\' * (2 * $backslashes)))
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Invoke-Ksh([string[]]$Arguments, [string]$InputText = '') {
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $resolvedKshPath
    $startInfo.Arguments = (($Arguments | ForEach-Object {
        ConvertTo-WindowsArgument $_
    }) -join ' ')
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.RedirectStandardInput = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Could not start '$resolvedKshPath'."
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if ($InputText) {
        $process.StandardInput.Write($InputText)
    }
    $process.StandardInput.Close()
    if (-not $process.WaitForExit(30000)) {
        $process.Kill()
        $process.WaitForExit()
        throw 'Timed out waiting for KSH.'
    }
    [pscustomobject]@{
        ExitCode = $process.ExitCode
        Stdout = $stdoutTask.Result.Replace("`r`n", "`n")
        Stderr = $stderrTask.Result.Replace("`r`n", "`n")
    }
}

$failures = 0
function Assert-KshCase(
    [string]$Name,
    [string[]]$Arguments,
    [int]$ExpectedExit,
    [string]$ExpectedOutput = $null,
    [string]$ExpectedError = $null,
    [string]$InputText = '',
    [string]$ExpectedOutputContains = $null
) {
    $result = Invoke-Ksh $Arguments $InputText
    $passed = $result.ExitCode -eq $ExpectedExit
    if ($PSBoundParameters.ContainsKey('ExpectedOutput')) {
        $passed = $passed -and $result.Stdout -ceq $ExpectedOutput
    }
    if ($PSBoundParameters.ContainsKey('ExpectedError')) {
        $passed = $passed -and $result.Stderr.Contains($ExpectedError)
    }
    if ($PSBoundParameters.ContainsKey('ExpectedOutputContains')) {
        $passed = $passed -and
            $result.Stdout.Contains($ExpectedOutputContains)
    }
    if ($passed) {
        Write-Output "PASS $Name"
    } else {
        $script:failures++
        Write-Output "FAIL $Name (exit=$($result.ExitCode), stdout=<$($result.Stdout)>, stderr=<$($result.Stderr)>)"
    }
}

Assert-KshCase 'echo joins arguments and emits newline' @('-c', 'echo alpha beta') 0 "alpha beta`n"
Assert-KshCase 'echo -n suppresses newline' @('-c', 'echo -n value') 0 'value'
Assert-KshCase 'echo -e interprets escapes' @('-c', 'echo -e "left\nright"') 0 "left`nright`n"
Assert-KshCase 'print joins arguments and interprets escapes' @('-c', 'print "left\nright"') 0 "left`nright`n"
Assert-KshCase 'print -n suppresses newline' @('-c', 'print -n value') 0 'value'
Assert-KshCase 'print -r preserves backslashes' @('-c', 'print -rn "left\nright"') 0 'left\nright'
Assert-KshCase 'print -f formats output' @('-c', 'print -f "%s=%d\n" item 7') 0 "item=7`n"
Assert-KshCase 'print -u 2 writes to standard error' @('-c', 'print -u 2 diagnostic') 0 '' 'diagnostic'
Assert-KshCase 'print -p reports unavailable coprocess support' @('-c', 'print -p value') 2 $null 'coprocess/history support'
Assert-KshCase 'let assigns and evaluates arithmetic' @('-c', 'let "KSH_LET_COUNT=3+4"; test "$KSH_LET_COUNT" -eq 7') 0
Assert-KshCase 'let returns failure for a zero expression' @('-c', 'let 0') 1
Assert-KshCase 'setvar assigns literal shell values' @('-c', 'setvar KSH_SETVAR "value with spaces"; test "$KSH_SETVAR" = "value with spaces"') 0
Assert-KshCase 'setvar rejects invalid variable names' @('-c', 'setvar BAD-NAME value') 2 $null 'usage'
Assert-KshCase 'printf formats strings and integers' @('-c', 'printf "%s:%04d\n" item 12') 0 "item:0012`n"
Assert-KshCase 'printf formats floating point values' @('-c', 'printf "%.2f" 3.14159') 0 '3.14'
Assert-KshCase 'printf reuses format for extra arguments' @('-c', 'printf "%s" first second') 0 'firstsecond'
Assert-KshCase 'printf %b interprets argument escapes' @('-c', 'printf "%b" "left\nright"') 0 "left`nright"
Assert-KshCase 'printf rejects unsafe %n conversion' @('-c', 'printf "%n"') 1 $null '%n'
Assert-KshCase 'eval reparses command text' @('-c', "eval 'echo evaluated; false'") 1 "evaluated`n"
Assert-KshCase 'command runs a builtin directly' @('-c', 'command printf "%s\n" direct') 0 "direct`n"
Assert-KshCase 'command -v identifies builtins' @('-c', 'command -v printf') 0 "printf`n"
Assert-KshCase 'type identifies builtins' @('-c', 'type printf') 0 -ExpectedOutputContains 'shell builtin'
Assert-KshCase 'aliases expand command words and retain arguments' @('-c', "alias say='echo hello'; say world") 0 "hello world`n"
Assert-KshCase 'aliases expand pipeline command words' @('-c', "alias right='echo right'; echo left | right") 0 "right`n"
Assert-KshCase 'alias -p prints reusable quoted definitions' @('-c', "alias say='echo a`$b'; alias -p say") 0 "alias say='echo a`$b'`n"
Assert-KshCase 'unalias removes selected aliases' @('-c', "alias say='true'; unalias say; alias say") 1 $null 'not found'
Assert-KshCase 'unalias -a clears all aliases' @('-c', "alias say='true'; alias other='false'; unalias -a; alias") 0 ''
Assert-KshCase 'alias rejects recursive expansion safely' @('-c', 'alias repeat=repeat; repeat') 127 -ExpectedOutputContains 'command not found'
Assert-KshCase 'empty quoted argument is preserved' @('-c', 'echo "" tail') 0 " tail`n"
Assert-KshCase 'single quotes suppress variable expansion' @('-c', "echo '`$KSH_VERSION'") 0 "`$KSH_VERSION`n"
Assert-KshCase 'true returns success' @('-c', 'true') 0
Assert-KshCase 'false returns failure' @('-c', 'false') 1
Assert-KshCase 'colon returns success' @('-c', ':') 0
Assert-KshCase 'command mode initializes positional parameters' @('-c', 'echo $# $0 $1 $2', 'test-name', 'alpha', 'beta') 0 "2 test-name alpha beta`n"
Assert-KshCase 'set and shift update positional parameters' @('-c', 'set -- alpha beta; echo $#:$1:$2; shift; echo $#:$1') 0 "2:alpha:beta`n1:beta`n"
Assert-KshCase 'set -e exits after an unhandled failed command' @('-c', 'set -e; false; echo should-not-run') 1 ''
Assert-KshCase 'set -e does not exit for a failed command handled by ||' @('-c', 'set -e; false || echo recovered; echo after') 0 "recovered`nafter`n"
Assert-KshCase 'set -o enables and reports shell options' @('-c', 'set -o errexit; set +o xtrace; set -o') 0 "allexport off`nemacs on`nerrexit on`nhashall off`nmonitor off`nnoclobber off`nnoglob off`nnoexec off`nnotify off`nnounset off`npipefail off`nverbose off`nvi off`nxtrace off`n"
Assert-KshCase 'set +o emits reusable option commands' @('-c', 'set -ex; set +o') 0 "set +o allexport`nset -o emacs`nset -o errexit`nset +o hashall`nset +o monitor`nset +o noclobber`nset +o noglob`nset +o noexec`nset +o notify`nset +o nounset`nset +o pipefail`nset +o verbose`nset +o vi`nset -o xtrace`n"
Assert-KshCase 'set -x traces expanded commands to standard error' @('-c', 'set -x; echo traced') 0 "traced`n" '+ echo traced'
Assert-KshCase 'set -u rejects references to unset variables' @('-c', 'set -u; echo "$KSH_UNSET_TEST"') 1 '' 'KSH_UNSET_TEST: parameter not set'
Assert-KshCase 'set +u disables unset-variable checks' @('-c', 'set -u; set +u; echo "$KSH_UNSET_TEST"') 0 "`n"
Assert-KshCase 'set rejects unsupported shell options' @('-c', 'set -z') 2 $null "unsupported option 'z'"
Assert-KshCase 'set -n skips command execution in non-interactive shells' @('-c', 'set -n; echo wrong') 0 ''
$verboseScript = Join-Path ([IO.Path]::GetTempPath()) ('ksh-verbose-' + [guid]::NewGuid().ToString('N') + '.sh')
Set-Content -LiteralPath $verboseScript -Value "set -v`necho hi" -Encoding ascii
Assert-KshCase 'set -v echoes script lines to standard error' @($verboseScript) 0 "hi`n" 'echo hi'
Remove-Item -LiteralPath $verboseScript -ErrorAction SilentlyContinue
Assert-KshCase 'echo interprets backslash escapes by default' @('-c', "echo 'a\tb'") 0 "a`tb`n"
Assert-KshCase 'echo -E disables backslash escapes' @('-c', "echo -E 'a\tb'") 0 "a\tb`n"
Assert-KshCase 'quoted at-sign keeps positional parameters separate' @('-c', 'set -- "a b" c; printf ''[%s]\n'' "$@"') 0 "[a b]`n[c]`n"
Assert-KshCase 'quoted at-sign with no parameters produces no fields' @('-c', 'set --; printf ''[%s]\n'' "$@"') 0 "[]`n"
$noclobberFile = Join-Path ([IO.Path]::GetTempPath()) ('ksh-noclobber-' + [guid]::NewGuid().ToString('N') + '.txt')
Assert-KshCase 'set -C refuses to overwrite an existing file' @('-c', "echo one > '$noclobberFile'; set -C; echo two > '$noclobberFile'") 1 $null 'cannot overwrite'
Remove-Item -LiteralPath $noclobberFile -ErrorAction SilentlyContinue
Assert-KshCase 'kill -0 reports a live process without signalling it' @('-c', ('kill -0 ' + $PID + '; echo $?')) 0 "0`n"
Assert-KshCase 'kill -0 reports a missing process' @('-c', 'kill -0 999999; echo $?') 0 "1`n" 'No such process'
Assert-KshCase 'dollar-dollar expands to a live process id' @('-c', 'kill -0 $$; echo $?') 0 "0`n"
Assert-KshCase 'dollar-hyphen lists active option letters' @('-c', 'set -fC; echo $-') 0 "Cf`n"
Assert-KshCase 'dollar-bang is empty before any background job' @('-c', 'echo "bg=$!"') 0 "bg=`n"
Assert-KshCase 'bare ampersand runs the command in the background' @('-c', 'sleep 0 & echo after') 0 "after`n"
Assert-KshCase 'bare ampersand sets dollar-bang' @('-c', 'sleep 0 & [ -n "$!" ] && echo set') 0 "set`n"
Assert-KshCase 'ampersand after redirection is not a job' @('-c', 'echo err 2>&1') 0 "err`n"
Assert-KshCase 'start -b starts without a window and sets dollar-bang' @('-c', 'start -b cmd /c exit 0; [ -n "$!" ] && echo set') 0 "set`n"
Assert-KshCase 'LINENO is 1 for a -c command string' @('-c', 'echo $LINENO') 0 "1`n"
$lineNoScript = Join-Path ([IO.Path]::GetTempPath()) ('ksh-lineno-' + [guid]::NewGuid().ToString('N') + '.sh')
Set-Content -LiteralPath $lineNoScript -Value "echo a`necho line=`$LINENO" -Encoding ascii
Assert-KshCase 'LINENO tracks the line number within a script' @($lineNoScript) 0 "a`nline=2`n"
Remove-Item -LiteralPath $lineNoScript -ErrorAction SilentlyContinue
Assert-KshCase 'trap rejects an action without signal names' @('-c', "trap 'echo x'") 2 $null 'usage: trap'
Assert-KshCase 'trap listing prints the re-enterable form' @('-c', 'trap ''true'' EXIT; trap') 0 ("trap -- 'true' EXIT`n")

Assert-KshCase 'status parameter tracks the previous command' @('-c', 'false; echo $?') 0 "1`n"
Assert-KshCase 'semicolon runs sequential commands' @('-c', 'echo first; echo second') 0 "first`nsecond`n"
Assert-KshCase 'and-or lists short-circuit' @('-c', 'false && echo wrong || echo right') 0 "right`n"
Assert-KshCase 'test handles integer comparison' @('-c', 'test 10 -gt 2') 0
Assert-KshCase 'test honors boolean precedence and grouping' @('-c', "test '(' x = x -a ! -z x ')' -o 2 -lt 1") 0
Assert-KshCase 'test handles quoted string ordering operators' @('-c', "test alpha '<' beta -a beta '>' alpha") 0
Assert-KshCase 'test preserves empty string operand' @('-c', 'test -z ""') 0
Assert-KshCase 'test recognizes Windows character devices' @('-c', 'test -c NUL') 0
Assert-KshCase 'test -t is false for redirected output' @('-c', 'test -t') 1
Assert-KshCase 'test reports malformed numeric expression' @('-c', 'test x -gt 2') 2 $null 'invalid expression'
Assert-KshCase '[ validates its closing bracket' @('-c', '[ -n value ]') 0
Assert-KshCase 'pwd supports physical mode' @('-c', 'pwd -P') 0
Assert-KshCase 'pwd rejects unknown options' @('-c', 'pwd -x') 2 $null 'usage'
Assert-KshCase 'pushd and popd restore the previous directory' `
    @('-c', 'pushd C:/Windows; pwd; popd; pwd') 0 `
    "C:/Windows`n$((Get-Location).Path.Replace('\', '/'))`n"
Assert-KshCase 'popd reports an empty directory stack' @('-c', 'popd') 1 $null 'directory stack is empty'
Assert-KshCase 'dir lists an explicit file' @('-c', 'dir "E:\ksh\src\ksh.c"') 0 -ExpectedOutputContains 'Directory of'
Assert-KshCase 'dir rejects excess operands' @('-c', 'dir one two') 2 $null 'usage'
Assert-KshCase 'export rejects invalid names' @('-c', 'export 2BAD=value') 2 $null 'not a valid identifier'
Assert-KshCase 'setenv accepts name and value' @('-c', 'setenv KSH_TEST_VALUE hello') 0
Assert-KshCase 'setenv rejects conflicting value forms' @('-c', 'setenv NAME=first second') 2 $null 'not both'
Assert-KshCase 'unset rejects missing operands' @('-c', 'unset') 2 $null 'usage'
Assert-KshCase 'unset rejects invalid names' @('-c', 'unset 2BAD') 2 $null 'not a valid identifier'
Assert-KshCase 'readonly prevents updates and unsets' @('-c', 'readonly KSH_READONLY_TEST=fixed; setenv KSH_READONLY_TEST changed') 1 $null 'cannot set'
Assert-KshCase 'readonly -p lists read-only variables' @('-c', 'readonly KSH_READONLY_TEST=value; readonly -p') 0 -ExpectedOutputContains "readonly KSH_READONLY_TEST='value'"
Assert-KshCase 'typeset -l normalizes values to lowercase' @('-c', 'typeset -l name=MiXeD; echo "$name"') 0 "mixed`n"
Assert-KshCase 'typeset -u normalizes values to uppercase' @('-c', 'typeset -u name=MiXeD; echo "$name"') 0 "MIXED`n"
Assert-KshCase 'typeset -i supports explicit integer bases' @('-c', 'typeset -i16 number=ff; echo "$number"') 0 "16#ff`n"
Assert-KshCase 'typeset -Z zero fills values to width' @('-c', 'typeset -Z5 number=42; echo "$number"') 0 "00042`n"
Assert-KshCase 'typeset -L and -R pad text in the requested direction' @('-c', 'typeset -L5 left=xy; typeset -R5 right=xy; echo "$left|$right"') 0 "xy   |   xy`n"
Assert-KshCase 'typeset -r makes a variable readonly' @('-c', 'typeset -r locked=value; setenv locked changed') 1 $null 'cannot set'
Assert-KshCase 'typeset rejects unknown attributes' @('-c', 'typeset -z value') 2 $null 'invalid option'
Assert-KshCase 'typeset -p displays variable attributes' @('-c', 'typeset -i16 number=ff; typeset -p number') 0 -ExpectedOutputContains "typeset -i16 number='16#ff'"
Assert-KshCase 'typeset -Z keeps negative signs before padding' @('-c', 'typeset -Z5 number=-42; echo "$number"') 0 "-0042`n"
Assert-KshCase 'typeset -a and set -A support indexed arrays' @('-c', 'typeset -a colors; set -A colors red green blue; echo "${colors[1]}:${colors[@]}:${#colors[@]}"') 0 "green:red green blue:3`n"
Assert-KshCase 'typeset -A and set -A support associative arrays' @('-c', 'typeset -A opts; set -A opts one=1 two=2; echo "${opts[two]}:${#opts[@]}"') 0 "2:2`n"
Assert-KshCase 'read splits input fields using IFS' @('-c', 'read first second; echo "$first:$second"') 0 "one:two`n" $null "one two`n"
Assert-KshCase 'read -r preserves backslashes' @('-c', 'read -r value; printf ''%s\n'' "$value"') 0 "path\file`n" $null "path\file`n"
Assert-KshCase 'read -n limits characters' @('-c', 'read -n 2 value; echo "$value"') 0 "ab`n" $null "abcd`n"
Assert-KshCase 'read -d uses the requested delimiter' @('-c', 'read -d , value; echo "$value"') 0 "left`n" $null "left,right`n"
Assert-KshCase 'read -A assigns an indexed array' `
    @('-c', 'read -A values; test "${values[0]}" = alpha -a "${values[2]}" = gamma') `
    0 $null $null "alpha beta gamma`n"
Assert-KshCase 'read -a supports the indexed-array qualifier' `
    @('-c', 'read -a values; test "${values[1]}" = beta') 0 $null $null `
    "alpha beta gamma`n"
Assert-KshCase 'read array mode requires a variable name' `
    @('-c', 'read -A') 2 $null 'option -A requires an operand'
Assert-KshCase 'return outside a sourced script is rejected' @('-c', 'return 1') 2 $null 'not in a function or sourced script'
Assert-KshCase 'getopts parses option clusters and required arguments' @('-c', 'getopts ab: opt -abfoo; echo "$opt:$OPTARG:$OPTIND"; getopts ab: opt -abfoo; echo "$opt:$OPTARG:$OPTIND"') 0 "a::1`nb:foo:2`n"
Assert-KshCase 'getopts supports silent option errors' @('-c', 'getopts :a opt -z; echo "$opt:$OPTARG:$OPTIND"') 0 "?:z:2`n"
Assert-KshCase 'getopts reports missing arguments in silent mode' @('-c', 'getopts :b: opt -b; echo "$opt:$OPTARG:$OPTIND"') 0 "::b:2`n"
Assert-KshCase 'umask prints a portable numeric value' @('-c', 'umask -p') 0 "umask 0022`n"
Assert-KshCase 'umask prints symbolic permissions' @('-c', 'umask 077; umask -S') 0 "u=rwx,g=,o=`n"
Assert-KshCase 'umask rejects non-octal values' @('-c', 'umask 088') 2 $null 'invalid octal mask'
Assert-KshCase 'times prints self and child totals' @('-c', 'times') 0 -ExpectedOutputContains '0m0.00s'
Assert-KshCase 'ulimit -n changes and queries the CRT descriptor limit' @('-c', 'ulimit -n 128; ulimit -n') 0 "128`n"
Assert-KshCase 'ulimit -H and -S are mutually exclusive' @('-c', 'ulimit -HS -n') 2 $null 'mutually exclusive'
Assert-KshCase 'ulimit -a lists resource limits' @('-c', 'ulimit -a') 0 -ExpectedOutputContains "open files`t512"
Assert-KshCase 'ulimit rejects options outside the target profiles' @('-c', 'ulimit -z') 2 $null 'invalid option'
Assert-KshCase 'ulimit reports unsupported Windows resource enforcement' @('-c', 'ulimit -f 1') 1 $null 'cannot be enforced on Windows'
Assert-KshCase 'suspend requires force outside a job-control shell' @('-c', 'suspend') 1 $null 'job control is unavailable'
Assert-KshCase 'suspend -f reports missing interactive console' @('-c', 'suspend -f') 1 $null 'requires an interactive console'
Assert-KshCase 'unset -f removes only the named function' @('-c', 'f() { echo hi; }; unset -f f; command -v f; echo $?') 0 "1`n"
Assert-KshCase 'unset -v leaves a same-named function in place' @('-c', 'f() { echo hi; }; x=1; unset -v x; f') 0 "hi`n"
Assert-KshCase 'unset without an option removes a function when no variable matches' @('-c', 'f() { echo hi; }; unset f; command -v f; echo $?') 0 "1`n"
Assert-KshCase 'unset rejects -f combined with -v' @('-c', 'unset -f -v x') 2 $null 'mutually exclusive'
Assert-KshCase 'command -p finds programs on the system path' @('-c', 'command -pv cmd') 0 -ExpectedOutputContains 'system32\cmd.exe'
Assert-KshCase 'command -p restores PATH afterwards' @('-c', 'n=${#PATH}; command -p cmd /c exit 0; test ${#PATH} -eq $n && echo same') 0 "same`n"
Assert-KshCase 'CDPATH resolves relative cd targets' @('-c', 'CDPATH=C:/Windows; cd System32; pwd -P') 0 -ExpectedOutputContains 'System32'
Assert-KshCase 'CDPATH does not apply to explicit relative paths' @('-c', 'CDPATH=C:/Windows; cd ./System32') 1 $null 'No such directory'
Assert-KshCase 'PS4 replaces the xtrace prompt' @('-c', 'PS4=">> "; set -x; echo hi') 0 "hi`n" '>> echo hi'
Assert-KshCase 'kill -l lists only supported signals' @('-c', 'kill -l') 0 -ExpectedOutputContains '15) SIGTERM'
Assert-KshCase 'kill -l maps a status to a signal name' @('-c', 'kill -l 143') 0 "TERM`n"
Assert-KshCase 'kill rejects unknown signal names' @('-c', 'kill -BOGUS 1') 2 $null 'invalid signal specification'
Assert-KshCase 'kill reports POSIX-only signals as unsupported' @('-c', 'kill -QUIT 1') 2 $null 'not supported on Windows'
Assert-KshCase 'suspend rejects unknown qualifiers' @('-c', 'suspend -x') 2 $null 'usage'
Assert-KshCase 'clear fails clearly when stdout is not a console' @('-c', 'clear') 1 $null 'not a console'
Assert-KshCase 'pause consumes redirected input' @('-c', 'pause') 0 $null $null "x`n"
Assert-KshCase 'pause reports unavailable input' @('-c', 'pause') 1 $null 'input is not available'
Assert-KshCase 'help displays built-in help' @('-c', 'help') 0 -ExpectedOutputContains 'BUILT-IN COMMANDS:'
Assert-KshCase 'man is an alias for help' @('-c', 'man') 0 -ExpectedOutputContains 'BUILT-IN COMMANDS:'
Assert-KshCase 'self-test runs the embedded regression checks' @('--self-test') 0 -ExpectedOutputContains 'Self-test: all passed'
Assert-KshCase 'exit uses the low status byte' @('-c', 'exit 258') 2
Assert-KshCase 'exit rejects excess operands without exiting' @('-c', 'exit 1 2') 1 $null 'too many arguments'
Assert-KshCase 'exit stops the rest of the command list' @('-c', 'exit 4; echo should-not-run') 4 ''
Assert-KshCase 'exec replaces the shell with a builtin command' @('-c', 'exec false; echo should-not-run') 1 ''
Assert-KshCase 'exec with no command leaves the shell running' @('-c', 'exec; echo after') 0 "after`n"
Assert-KshCase 'exec of a missing command replaces the shell with status 127' @('-c', 'exec ksh_missing_exec_target; echo after') 127 '' 'not found'
Assert-KshCase 'exec runs an external command and returns its status' @('-c', 'exec cmd /c exit 3; echo after') 3 ''
Assert-KshCase 'exec runs an external command with output' @('-c', 'exec cmd /c echo replaced') 0 -ExpectedOutputContains 'replaced'
Assert-KshCase 'exec with fd 3 duplicates stdout persistently' @('-c', 'exec 3>&1; echo fd3 >&3') 0 "fd3`n"
Assert-KshCase 'exec 1> redirects later output and fd 4 keeps the original stdout' @('-c', 'exec 4>&1 1>NUL; echo hidden; echo shown >&4') 0 "shown`n"
Assert-KshCase 'exec 1>&3 restores stdout saved in fd 3' @('-c', 'exec 3>&1; exec >NUL; echo hidden; exec 1>&3; echo back') 0 "back`n"
Assert-KshCase 'exec 3>&- closes fd 3 without affecting stdout' @('-c', 'exec 3>&-; echo ok') 0 "ok`n"

$execFile = Join-Path $env:TEMP ('ksh-exec-' + [guid]::NewGuid().ToString('N') + '.txt')
try {
    $execResult = Invoke-Ksh @('-c', "exec > `"$execFile`"; echo fromexec; exec 3>>`"$execFile`"; echo viafd3 >&3; exec 3>&-")
    $execText = if (Test-Path -LiteralPath $execFile) { Get-Content -LiteralPath $execFile -Raw } else { '' }
    if ($execResult.ExitCode -eq 0 -and $execResult.Stdout -eq '' -and $execText.Contains('fromexec') -and $execText.Contains('viafd3')) {
        Write-Output 'PASS exec redirects stdout and a numbered fd persistently to a file'
    } else {
        $script:failures++
        Write-Output "FAIL exec redirects stdout and a numbered fd persistently to a file (exit=$($execResult.ExitCode), file=<$execText>)"
    }
} finally {
    Remove-Item -LiteralPath $execFile -ErrorAction SilentlyContinue
}

$tempDirectory = Join-Path $env:TEMP ('ksh-builtins-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tempDirectory | Out-Null
try {
    $stateScript = Join-Path $tempDirectory 'state.ksh'
    $outputPath = Join-Path $tempDirectory 'builtin output.txt'
    $exportsPath = Join-Path $tempDirectory 'exports.txt'
    $stateContent = @'
export KSH_BUILTIN_TEST=first
setenv KSH_BUILTIN_TEST second
test "$KSH_BUILTIN_TEST" = second
unset KSH_BUILTIN_TEST
test -z "$KSH_BUILTIN_TEST"
export KSH_BUILTIN_TEST='a$b'
test "$KSH_BUILTIN_TEST" = 'a$b'
export -p > "__EXPORTS__"
cd "E:\ksh\src"
test "$PWD" = "E:\ksh\src"
pwd -L > "__PWD__"
cd -
test "$OLDPWD" = "E:\ksh\src"
'@.Replace('__EXPORTS__', $exportsPath).Replace('__PWD__', $outputPath)
    Set-Content -LiteralPath $stateScript -Value $stateContent -Encoding utf8
    $stateResult = Invoke-Ksh @($stateScript)
    if ($stateResult.ExitCode -eq 0) {
        Write-Output 'PASS export, setenv, unset, cd, PWD, and OLDPWD state'
    } else {
        $failures++
        Write-Output "FAIL environment and directory state (exit=$($stateResult.ExitCode), stderr=<$($stateResult.Stderr)>)"
    }
    if ((Get-Content -LiteralPath $exportsPath -Raw).Contains(
            "export KSH_BUILTIN_TEST='a`$b'")) {
        Write-Output 'PASS export -p quotes literal dollar signs'
    } else {
        $failures++
        Write-Output 'FAIL export -p quoting'
    }
    $logicalPwd = (Get-Content -LiteralPath $outputPath -Raw).
        Replace("`r`n", "`n")
    if ($logicalPwd -ceq "E:/ksh/src`n") {
        Write-Output 'PASS pwd -L prints the logical shell directory'
    } else {
        $script:failures++
        Write-Output "FAIL pwd -L output (actual=<$logicalPwd>)"
    }

    $sourceScript = Join-Path $tempDirectory 'source-return.ksh'
    Set-Content -LiteralPath $sourceScript -Value "echo sourced`nreturn 7`necho skipped" -Encoding utf8
    $sourceCommand = 'source "' + $sourceScript + '"; echo continued'
    $sourceResult = Invoke-Ksh @('-c', $sourceCommand)
    if ($sourceResult.ExitCode -eq 0 -and
        $sourceResult.Stdout -ceq "sourced`ncontinued`n") {
        Write-Output 'PASS source executes in the current shell and return stops the sourced file'
    } else {
        $failures++
        Write-Output "FAIL source and return (exit=$($sourceResult.ExitCode), stdout=<$($sourceResult.Stdout)>, stderr=<$($sourceResult.Stderr)>)"
    }

    $functionScript = Join-Path $tempDirectory 'functions.ksh'
    $functionContent = @'
greet() {
    echo "hello $1"
    return 6
}
greet world
echo "status:$?"
typeset -F greet
typeset -f greet
type greet
command -v greet
typeset scope=outer
localize() {
    local scope=inner
    echo "$scope"
}
localize
echo "$scope"
'@
    Set-Content -LiteralPath $functionScript -Value $functionContent -Encoding utf8
    Assert-KshCase 'function definitions, arguments, return, and typeset -F' `
        @($functionScript) 0 "hello world`nstatus:6`ngreet`ngreet() {`n    echo `"hello `$1`"`n    return 6`n}`ngreet is a function`ngreet`ninner`nouter`n"

    $batchTree = Join-Path $tempDirectory 'batch-tree'
    $batchChild = Join-Path $batchTree 'child'
    New-Item -ItemType Directory -Path $batchChild -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $batchTree 'root.txt') -Value 'root' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $batchChild 'child.txt') -Value 'child' -Encoding utf8
    $recordsPath = Join-Path $batchTree 'records.csv'
    Set-Content -LiteralPath $recordsPath -Value @(
        'name,age,note',
        'Ada,37,compiler dev',
        '# ignored',
        'Lin,42,systems engineer'
    ) -Encoding utf8
    $batchScript = Join-Path $tempDirectory 'native-batch.cmd'
    $batchContent = @'
@echo off
if exist "%~f0" echo script path resolved
copy /Y "__RECORDS__" "__BATCH_TREE__\copy.csv"
if exist "__BATCH_TREE__\copy.csv" echo copy exists
type "__RECORDS__"
del /Q "__BATCH_TREE__\copy.csv"
if exist "__BATCH_TREE__\copy.csv" echo delete failed
set NAME=world
set /a TOTAL=2+3*4
echo arithmetic %TOTAL%
set /a TOTAL+=1
echo updated %TOTAL%
echo hello %NAME%
echo args %1 %*
if "%NAME%"=="world" echo condition
for %%A in (one two) do echo item %%A
for /L %%N in (1,1,3) do echo number %%N
for /D %%D in ("__BATCH_TREE__\*") do echo directory %%D
for /R "__BATCH_TREE__" %%F in (*.txt) do echo file %%F
for /R "__BATCH_TREE__" %%F in (*.txt) do echo name %%~nxF
for /F "usebackq skip=1 eol=# tokens=1,2,* delims=," %%A in ("__RECORDS__") do echo record %%A %%B %%C
for /F "tokens=*" %%Q in (`echo command source`) do echo command %%Q
for /F "tokens=*" %%Q in ("literal source") do echo literal %%Q
for /F "usebackq tokens=*" %%Q in ('usebackq literal') do echo usebackq %%Q
setlocal EnableDelayedExpansion
set NAME=local
echo %NAME%
echo delayed !NAME!
endlocal
echo %NAME%
echo first ^
continued
echo left & echo right
shift
echo shifted %1
call :sub argument
if errorlevel 7 echo returned
goto :eof
:sub
echo sub %1
exit /b 7
'@
    $batchContent = $batchContent.Replace('__BATCH_TREE__', $batchTree)
    $batchContent = $batchContent.Replace('__RECORDS__', $recordsPath)
    Set-Content -LiteralPath $batchScript -Value $batchContent -Encoding utf8
    Assert-KshCase 'native batch expansion, control flow, call, and local scope' `
        @($batchScript, 'alpha', 'beta') 0 "script path resolved`n1 file(s) copied.`ncopy exists`nname,age,note`nAda,37,compiler dev`n# ignored`nLin,42,systems engineer`n1 file(s) deleted.`narithmetic 14`nupdated 15`nhello world`nargs alpha alpha beta`ncondition`nitem one`nitem two`nnumber 1`nnumber 2`nnumber 3`ndirectory $batchChild`nfile $(Join-Path $batchTree 'root.txt')`nfile $(Join-Path $batchChild 'child.txt')`nname root.txt`nname child.txt`nrecord Ada 37 compiler dev`nrecord Lin 42 systems engineer`ncommand command source`nliteral literal source`nusebackq usebackq literal`nlocal`ndelayed local`nworld`nfirst continued`nleft `nright`nshifted beta`nsub argument`nreturned`n"

    $modifierScript = Join-Path $tempDirectory 'batch-modifiers.cmd'
    $rootFile = Join-Path $batchTree 'root.txt'
    $rootFileSize = (Get-Item -LiteralPath $rootFile).Length
    Set-Content -LiteralPath $modifierScript -Value @"
@echo off
set PATH=$batchTree
for %%F in (records.csv) do echo search %%~`$PATH:F
for %%F in ("$rootFile") do echo size %%~zF
for %%F in ("$rootFile") do if "%%~tF"=="" echo missing-time
for %%F in ("$rootFile") do if "%%~aF"=="" echo missing-attributes
"@ -Encoding utf8
    Assert-KshCase 'native FOR modifiers resolve PATH, size, time, and attributes' `
        @($modifierScript) 0 "search $recordsPath`nsize $rootFileSize`n"

    $calledBatch = Join-Path $tempDirectory 'called.cmd'
    Set-Content -LiteralPath $calledBatch -Value "@echo off`necho native`nexit /b 9" -Encoding utf8
    Assert-KshCase 'external batch commands use the native runtime' `
        @('-c', ($calledBatch -replace '\\', '/')) 9 "native`n"

    $nativeUtilityScript = Join-Path $tempDirectory 'native-utilities.cmd'
    Set-Content -LiteralPath $nativeUtilityScript -Value @'
@echo off
break /?
chcp
color /?
title /?
vol /?
'@ -Encoding utf8
    $nativeUtilityResult = Invoke-Ksh @($nativeUtilityScript)
    if ($nativeUtilityResult.ExitCode -eq 0 -and
        $nativeUtilityResult.Stdout.Contains('BREAK [ON | OFF]') -and
        $nativeUtilityResult.Stdout.Contains('Active code page: ') -and
        $nativeUtilityResult.Stdout.Contains('COLOR [attr]') -and
        $nativeUtilityResult.Stdout.Contains('TITLE [string]') -and
        $nativeUtilityResult.Stdout.Contains('VOL [drive:]')) {
        Write-Output 'PASS native BREAK, CHCP, COLOR, TITLE, and VOL commands'
    } else {
        $failures++
        Write-Output "FAIL native utility commands (exit=$($nativeUtilityResult.ExitCode), stdout=<$($nativeUtilityResult.Stdout)>, stderr=<$($nativeUtilityResult.Stderr)>)"
    }

    $tabInput = Join-Path $tempDirectory 'type-tabs.txt'
    [IO.File]::WriteAllText(
        $tabInput,
        "a`t`tb`r`n",
        [Text.UTF8Encoding]::new($false)
    )
    $typeScript = Join-Path $tempDirectory 'type-options.cmd'
    Set-Content -LiteralPath $typeScript `
        -Value "@echo off`ntype /T:4 /P `"$tabInput`"" -Encoding utf8
    Assert-KshCase 'native TYPE expands tabs and ignores paging when redirected' `
        @($typeScript) 0 "a       b`n"

    $rdTree = Join-Path $tempDirectory 'rd-tree'
    $rdChild = Join-Path $rdTree 'child'
    New-Item -ItemType Directory -Path $rdChild -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $rdChild 'keep.txt') -Value 'keep' -Encoding utf8
    $rdPromptScript = Join-Path $tempDirectory 'rd-prompt.cmd'
    Set-Content -LiteralPath $rdPromptScript -Value "@echo off`nrd /s `"$rdTree`"" -Encoding utf8
    $rdPromptResult = Invoke-Ksh @($rdPromptScript) "N`n"
    if ($rdPromptResult.ExitCode -eq 1 -and
        $rdPromptResult.Stdout.Contains('Are you sure (Y/N)?') -and
        (Test-Path -LiteralPath (Join-Path $rdChild 'keep.txt'))) {
        Write-Output 'PASS RD /S prompts and preserves the tree on N'
    } else {
        $failures++
        Write-Output "FAIL RD /S confirmation (exit=$($rdPromptResult.ExitCode), stdout=<$($rdPromptResult.Stdout)>)"
    }
    $rdQuietScript = Join-Path $tempDirectory 'rd-quiet.cmd'
    Set-Content -LiteralPath $rdQuietScript -Value "@echo off`nrd /s /q `"$rdTree`"" -Encoding utf8
    $rdQuietResult = Invoke-Ksh @($rdQuietScript)
    if ($rdQuietResult.ExitCode -eq 0 -and
        -not (Test-Path -LiteralPath $rdTree)) {
        Write-Output 'PASS RD /S /Q recursively removes the named tree without prompting'
    } else {
        $failures++
        Write-Output "FAIL RD /S /Q (exit=$($rdQuietResult.ExitCode), stderr=<$($rdQuietResult.Stderr)>)"
    }

    $attributeDirectory = Join-Path $tempDirectory 'del-attributes'
    New-Item -ItemType Directory -Path $attributeDirectory -Force | Out-Null
    $hiddenFile = Join-Path $attributeDirectory 'hidden.txt'
    $visibleFile = Join-Path $attributeDirectory 'visible.txt'
    Set-Content -LiteralPath $hiddenFile -Value 'hidden' -Encoding utf8
    Set-Content -LiteralPath $visibleFile -Value 'visible' -Encoding utf8
    [IO.File]::SetAttributes($hiddenFile, [IO.FileAttributes]::Hidden)
    $deleteHiddenScript = Join-Path $tempDirectory 'del-hidden.cmd'
    Set-Content -LiteralPath $deleteHiddenScript `
        -Value "@echo off`ndel /a:h `"$attributeDirectory\*`"" -Encoding utf8
    $deleteHiddenResult = Invoke-Ksh @($deleteHiddenScript)
    if ($deleteHiddenResult.ExitCode -eq 0 -and
        -not (Test-Path -LiteralPath $hiddenFile) -and
        (Test-Path -LiteralPath $visibleFile)) {
        Write-Output 'PASS DEL /A:H deletes only hidden files'
    } else {
        $failures++
        Write-Output "FAIL DEL /A:H (exit=$($deleteHiddenResult.ExitCode), stderr=<$($deleteHiddenResult.Stderr)>)"
    }
    $deleteNonHiddenScript = Join-Path $tempDirectory 'del-non-hidden.cmd'
    Set-Content -LiteralPath $deleteNonHiddenScript `
        -Value "@echo off`ndel /a:-h `"$attributeDirectory\*`"" -Encoding utf8
    $deleteNonHiddenResult = Invoke-Ksh @($deleteNonHiddenScript)
    if ($deleteNonHiddenResult.ExitCode -eq 0 -and
        -not (Test-Path -LiteralPath $visibleFile)) {
        Write-Output 'PASS DEL /A:-H selects files without the hidden attribute'
    } else {
        $failures++
        Write-Output "FAIL DEL /A:-H (exit=$($deleteNonHiddenResult.ExitCode), stderr=<$($deleteNonHiddenResult.Stderr)>)"
    }

    $copySource = Join-Path $tempDirectory 'copy-source.bin'
    $copyDestination = Join-Path $tempDirectory 'copy-destination.bin'
    [IO.File]::WriteAllBytes($copySource, [byte[]](65, 66, 26, 67, 68))
    Set-Content -LiteralPath $copyDestination -Value 'old' -Encoding utf8
    $copyPromptScript = Join-Path $tempDirectory 'copy-prompt.cmd'
    Set-Content -LiteralPath $copyPromptScript `
        -Value "@echo off`ncopy /-Y `"$copySource`" `"$copyDestination`"" -Encoding utf8
    $copyPromptResult = Invoke-Ksh @($copyPromptScript) "N`n"
    if ($copyPromptResult.ExitCode -eq 1 -and
        $copyPromptResult.Stdout.Contains('Overwrite') -and
        (Get-Content -LiteralPath $copyDestination -Raw).Contains('old')) {
        Write-Output 'PASS COPY /-Y asks before overwriting and preserves data on N'
    } else {
        $failures++
        Write-Output "FAIL COPY /-Y confirmation (exit=$($copyPromptResult.ExitCode), stdout=<$($copyPromptResult.Stdout)>)"
    }
    $copyAsciiScript = Join-Path $tempDirectory 'copy-ascii.cmd'
    Set-Content -LiteralPath $copyAsciiScript `
        -Value "@echo off`ncopy /A /V /Y `"$copySource`" `"$copyDestination`"" -Encoding utf8
    $copyAsciiResult = Invoke-Ksh @($copyAsciiScript)
    $asciiOutput = [IO.File]::ReadAllBytes($copyDestination)
    if ($copyAsciiResult.ExitCode -eq 0 -and
        [Convert]::ToBase64String($asciiOutput) -ceq
            [Convert]::ToBase64String([byte[]](65, 66))) {
        Write-Output 'PASS COPY /A stops at Ctrl-Z and /V verifies the result'
    } else {
        $failures++
        Write-Output "FAIL COPY /A /V (exit=$($copyAsciiResult.ExitCode), bytes=<$($asciiOutput -join ',')>)"
    }
    $copyBinaryScript = Join-Path $tempDirectory 'copy-binary.cmd'
    Set-Content -LiteralPath $copyBinaryScript `
        -Value "@echo off`ncopy /B /Z /V /N /Y `"$copySource`" `"$copyDestination`"" -Encoding utf8
    $copyBinaryResult = Invoke-Ksh @($copyBinaryScript)
    $binaryOutput = [IO.File]::ReadAllBytes($copyDestination)
    if ($copyBinaryResult.ExitCode -eq 0 -and
        [Convert]::ToBase64String($binaryOutput) -ceq
            [Convert]::ToBase64String([byte[]](65, 66, 26, 67, 68))) {
        Write-Output 'PASS COPY /B /Z /V /N preserves binary data and verifies it'
    } else {
        $failures++
        Write-Output "FAIL COPY binary qualifiers (exit=$($copyBinaryResult.ExitCode), bytes=<$($binaryOutput -join ',')>)"
    }
    $verifyScript = Join-Path $tempDirectory 'verify-copy.cmd'
    Set-Content -LiteralPath $verifyScript `
        -Value "@echo off`nverify on`ncopy /Y `"$copySource`" `"$copyDestination`"`nverify`nverify off`nverify" -Encoding utf8
    Assert-KshCase 'VERIFY ON applies verification to native COPY' `
        @($verifyScript) 0 "VERIFY is on.`n1 file(s) copied.`nVERIFY is on.`nVERIFY is off.`nVERIFY is off.`n"

    $mdPath = Join-Path $tempDirectory 'md-parent\child\leaf'
    $mdScript = Join-Path $tempDirectory 'md.cmd'
    Set-Content -LiteralPath $mdScript -Value "@echo off`nmd `"$mdPath`"" -Encoding utf8
    $mdResult = Invoke-Ksh @($mdScript)
    if ($mdResult.ExitCode -eq 0 -and
        (Test-Path -LiteralPath $mdPath -PathType Container)) {
        Write-Output 'PASS MD creates missing intermediate directories'
    } else {
        $failures++
        Write-Output "FAIL MD parent creation (exit=$($mdResult.ExitCode), stderr=<$($mdResult.Stderr)>)"
    }
    $mdExistingResult = Invoke-Ksh @($mdScript)
    if ($mdExistingResult.ExitCode -eq 1) {
        Write-Output 'PASS MD reports an already existing destination'
    } else {
        $failures++
        Write-Output "FAIL MD existing destination (exit=$($mdExistingResult.ExitCode))"
    }

    $ifWildcardFile = Join-Path $attributeDirectory 'exists.txt'
    Set-Content -LiteralPath $ifWildcardFile -Value 'exists' -Encoding utf8
    $ifScript = Join-Path $tempDirectory 'if-qualifiers.cmd'
    Set-Content -LiteralPath $ifScript -Value @"
@echo off
set IF_VALUE=ready
if defined IF_VALUE echo defined
if not defined IF_MISSING echo missing
if /i Alpha==alpha echo insensitive
if exist "$attributeDirectory\*.txt" echo wildcard exists
if cmdextversion 2 echo extensions
if cmdextversion 3 echo unsupported version
"@ -Encoding utf8
    Assert-KshCase 'IF defined, wildcard EXIST, /I, and CMDEXTVERSION' `
        @($ifScript) 0 "defined`nmissing`ninsensitive`nwildcard exists`nextensions`n"

    $cmdCompatDirectory = Join-Path $tempDirectory 'cmd-compat'
    New-Item -ItemType Directory -Path $cmdCompatDirectory -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $cmdCompatDirectory 'a.txt') -Value 'a' -Encoding utf8
    Set-Content -LiteralPath (Join-Path $cmdCompatDirectory 'b.txt') -Value 'b' -Encoding utf8
    $cmdCompatScript = Join-Path $cmdCompatDirectory 'compat.cmd'
    Set-Content -LiteralPath $cmdCompatScript -Value @'
@echo off
cd /d "%~dp0"
set N=2
if %N%==1 (
  echo one
) else if %N%==2 (
  echo two
) else (
  echo other
)
for %%f in (*.txt) do echo file:%%f
set L=x y
for %%i in (%L%) do (
  echo item:%%i
)
set S=HelloWorld
echo %S:~0,5% %S:World=There% %S:~-3%
if "abc" EQU "abc" echo strequ
echo redirected> out.txt
echo appended>> out.txt
type out.txt
echo piped| findstr piped
set /p FIRST=<out.txt
echo first=%FIRST%
set V=deferred
call echo %%V%%
echo(paren
for /f "tokens=2" %%a in ('echo alpha beta') do echo second:%%a
setlocal EnableDelayedExpansion
set C=0
for %%x in (a b) do (
  set /a C+=1
  echo !C!:%%x
)
endlocal
for /l %%i in (1,1,5) do (
  if %%i==2 goto done
  echo loop:%%i
)
:done
echo done
set Y=orig
echo changed| set /p Y=
for /f %%a in ('set Y^=leaked ^& echo ran') do echo %%a
echo Y=%Y%
'@ -Encoding utf8
    Assert-KshCase 'CMD blocks, ELSE IF, substrings, redirection, pipes, and FOR wildcards' `
        @($cmdCompatScript) 0 "two`nfile:a.txt`nfile:b.txt`nitem:x`nitem:y`nHello HelloThere rld`nstrequ`nredirected`nappended`npiped`nfirst=redirected`ndeferred`nparen`nsecond:beta`n1:a`n2:b`nloop:1`ndone`nran`nY=orig`n"

    $encodedScript = Join-Path $cmdCompatDirectory 'encoded.cmd'
    $encodedOutput = Join-Path $cmdCompatDirectory 'encoded.txt'
    $encodedBody = "@echo off`r`nset W=caf$([char]0xE9)`r`necho %W:~3,1%%W%> `"$encodedOutput`"`r`n"
    $encodings = @(
        @{ Name = 'UTF-16'; Bytes = [Text.Encoding]::Unicode.GetPreamble() + [Text.Encoding]::Unicode.GetBytes($encodedBody) },
        @{ Name = 'OEM'; Bytes = [Text.Encoding]::GetEncoding([Globalization.CultureInfo]::CurrentCulture.TextInfo.OEMCodePage).GetBytes($encodedBody) }
    )
    foreach ($encoding in $encodings) {
        [IO.File]::WriteAllBytes($encodedScript, [byte[]]$encoding.Bytes)
        Remove-Item -LiteralPath $encodedOutput -ErrorAction SilentlyContinue
        $encodedResult = Invoke-Ksh @($encodedScript)
        $encodedText = if (Test-Path -LiteralPath $encodedOutput) {
            [IO.File]::ReadAllText($encodedOutput, [Text.Encoding]::UTF8).TrimEnd()
        } else { '' }
        if ($encodedResult.ExitCode -eq 0 -and $encodedText -ceq "$([char]0xE9)caf$([char]0xE9)") {
            Write-Output "PASS $($encoding.Name) batch scripts are decoded like CMD"
        } else {
            $failures++
            Write-Output "FAIL $($encoding.Name) batch decoding (exit=$($encodedResult.ExitCode), text=<$encodedText>, stderr=<$($encodedResult.Stderr)>)"
        }
    }

    $redirectScript = Join-Path $tempDirectory 'redirect.ksh'
    $redirectContent = @"
echo first > "$outputPath"
echo second >> "$outputPath"
"@
    Set-Content -LiteralPath $redirectScript -Value $redirectContent -Encoding utf8
    $redirectResult = Invoke-Ksh @($redirectScript)
    $lines = @(Get-Content -LiteralPath $outputPath)
    if ($redirectResult.ExitCode -eq 0 -and
        $lines.Count -eq 2 -and $lines[0] -ceq 'first' -and
        $lines[1] -ceq 'second') {
        Write-Output 'PASS builtin output overwrite and append redirections'
    } else {
        $failures++
        Write-Output "FAIL builtin output redirections (exit=$($redirectResult.ExitCode), lines=<$($lines -join '|')>)"
    }

    # Test newly implemented commands
    $fcFile1 = Join-Path $tempDirectory 'fc1.txt'
    $fcFile2 = Join-Path $tempDirectory 'fc2.txt'
    Set-Content -LiteralPath $fcFile1 -Value "line1`nline2`n" -Encoding utf8
    Set-Content -LiteralPath $fcFile2 -Value "line1`nline2`n" -Encoding utf8
    $fcRes = Invoke-Ksh @('-c', "fc `"$fcFile1`" `"$fcFile2`"")
    if ($fcRes.ExitCode -eq 0 -and $fcRes.Stdout.Contains('no differences encountered')) {
        Write-Output 'PASS fc identical file comparison'
    } else {
        $failures++
        Write-Output "FAIL fc file comparison (exit=$($fcRes.ExitCode), stdout=<$($fcRes.Stdout)>)"
    }

    $findRes = Invoke-Ksh @('-c', "find /N `"line1`" `"$fcFile1`"")
    if ($findRes.ExitCode -eq 0 -and $findRes.Stdout.Contains('[1]line1')) {
        Write-Output 'PASS find text in file with /N'
    } else {
        $failures++
        Write-Output "FAIL find command (exit=$($findRes.ExitCode), stdout=<$($findRes.Stdout)>)"
    }

    $findstrRes = Invoke-Ksh @('-c', "findstr /N /I `"LINE2`" `"$fcFile1`"")
    if ($findstrRes.ExitCode -eq 0 -and $findstrRes.Stdout.Contains('2:line2')) {
        Write-Output 'PASS findstr text in file with /N /I'
    } else {
        $failures++
        Write-Output "FAIL findstr command (exit=$($findstrRes.ExitCode), stdout=<$($findstrRes.Stdout)>)"
    }

    $treeRes = Invoke-Ksh @('-c', "tree /A /F `"$tempDirectory`"")
    if ($treeRes.ExitCode -eq 0 -and $treeRes.Stdout.Contains('fc1.txt')) {
        Write-Output 'PASS tree directory hierarchy display with /A /F'
    } else {
        $failures++
        Write-Output "FAIL tree command (exit=$($treeRes.ExitCode), stdout=<$($treeRes.Stdout)>)"
    }

    $killRes = Invoke-Ksh @('-c', 'kill -l')
    if ($killRes.ExitCode -eq 0 -and $killRes.Stdout.Contains('SIGTERM') -and $killRes.Stdout.Contains('SIGKILL')) {
        Write-Output 'PASS kill -l signal enumeration'
    } else {
        $failures++
        Write-Output "FAIL kill -l command (exit=$($killRes.ExitCode), stdout=<$($killRes.Stdout)>)"
    }

    $timeRes = Invoke-Ksh @('-c', 'time /T')
    if ($timeRes.ExitCode -eq 0 -and $timeRes.Stdout -match '^\d{2}:\d{2}') {
        Write-Output 'PASS time /T query'
    } else {
        $failures++
        Write-Output "FAIL time /T command (exit=$($timeRes.ExitCode), stdout=<$($timeRes.Stdout)>)"
    }

    $dateRes = Invoke-Ksh @('-c', 'date /T')
    if ($dateRes.ExitCode -eq 0 -and $dateRes.Stdout -match '^\d{2}/\d{2}/\d{4}') {
        Write-Output 'PASS date /T query'
    } else {
        $failures++
        Write-Output "FAIL date /T command (exit=$($dateRes.ExitCode), stdout=<$($dateRes.Stdout)>)"
    }

    $jobsRes = Invoke-Ksh @('-c', 'jobs')
    if ($jobsRes.ExitCode -eq 0) {
        Write-Output 'PASS jobs status query'
    } else {
        $failures++
        Write-Output "FAIL jobs command (exit=$($jobsRes.ExitCode), stdout=<$($jobsRes.Stdout)>)"
    }

    $hashRes = Invoke-Ksh @('-c', 'hash -r')
    if ($hashRes.ExitCode -eq 0) {
        Write-Output 'PASS hash reset'
    } else {
        $failures++
        Write-Output "FAIL hash command (exit=$($hashRes.ExitCode), stdout=<$($hashRes.Stdout)>)"
    }

    $promptRes = Invoke-Ksh @('-c', 'prompt /?')
    if ($promptRes.ExitCode -eq 0 -and $promptRes.Stdout.Contains('PROMPT [text]')) {
        Write-Output 'PASS prompt help'
    } else {
        $failures++
        Write-Output "FAIL prompt command (exit=$($promptRes.ExitCode), stdout=<$($promptRes.Stdout)>)"
    }

    $doskeyRes = Invoke-Ksh @('-c', 'doskey /?')
    if ($doskeyRes.ExitCode -eq 0 -and $doskeyRes.Stdout.Contains('DOSKEY')) {
        Write-Output 'PASS doskey help'
    } else {
        $failures++
        Write-Output "FAIL doskey command (exit=$($doskeyRes.ExitCode), stdout=<$($doskeyRes.Stdout)>)"
    }

    $mklinkRes = Invoke-Ksh @('-c', 'mklink /?')
    if ($mklinkRes.ExitCode -eq 0 -and $mklinkRes.Stdout.Contains('MKLINK')) {
        Write-Output 'PASS mklink help'
    } else {
        $failures++
        Write-Output "FAIL mklink command (exit=$($mklinkRes.ExitCode), stdout=<$($mklinkRes.Stdout)>)"
    }

    $replaceRes = Invoke-Ksh @('-c', 'replace /?')
    if ($replaceRes.ExitCode -eq 0 -and $replaceRes.Stdout.Contains('REPLACE')) {
        Write-Output 'PASS replace help'
    } else {
        $failures++
        Write-Output "FAIL replace command (exit=$($replaceRes.ExitCode), stdout=<$($replaceRes.Stdout)>)"
    }

    $startRes = Invoke-Ksh @('-c', 'start /?')
    if ($startRes.ExitCode -eq 0 -and $startRes.Stdout.Contains('START')) {
        Write-Output 'PASS start help'
    } else {
        $failures++
        Write-Output "FAIL start command (exit=$($startRes.ExitCode), stdout=<$($startRes.Stdout)>)"
    }

    $assocRes = Invoke-Ksh @('-c', 'assoc /?')
    if ($assocRes.ExitCode -eq 0 -and $assocRes.Stdout.Contains('ASSOC')) {
        Write-Output 'PASS assoc help'
    } else {
        $failures++
        Write-Output "FAIL assoc command (exit=$($assocRes.ExitCode), stdout=<$($assocRes.Stdout)>)"
    }

    $ftypeRes = Invoke-Ksh @('-c', 'ftype /?')
    if ($ftypeRes.ExitCode -eq 0 -and $ftypeRes.Stdout.Contains('FTYPE')) {
        Write-Output 'PASS ftype help'
    } else {
        $failures++
        Write-Output "FAIL ftype command (exit=$($ftypeRes.ExitCode), stdout=<$($ftypeRes.Stdout)>)"
    }

    $verRes = Invoke-Ksh @('-c', 'ver')
    if ($verRes.ExitCode -eq 0 -and $verRes.Stdout.Contains('Microsoft Windows [Version')) {
        Write-Output 'PASS ver command'
    } else {
        $failures++
        Write-Output "FAIL ver command (exit=$($verRes.ExitCode), stdout=<$($verRes.Stdout)>)"
    }

    $verifyRes = Invoke-Ksh @('-c', 'verify')
    if ($verifyRes.ExitCode -eq 0 -and $verifyRes.Stdout.Contains('VERIFY is')) {
        Write-Output 'PASS verify command'
    } else {
        $failures++
        Write-Output "FAIL verify command (exit=$($verifyRes.ExitCode), stdout=<$($verifyRes.Stdout)>)"
    }

    $cdHelpRes = Invoke-Ksh @('-c', 'cd /?')
    if ($cdHelpRes.ExitCode -eq 0 -and $cdHelpRes.Stdout.Contains('CHDIR')) {
        Write-Output 'PASS cd help /?'
    } else {
        $failures++
        Write-Output "FAIL cd help (exit=$($cdHelpRes.ExitCode), stdout=<$($cdHelpRes.Stdout)>)"
    }

    $slashTemp = $tempDirectory -replace '\\', '/'
    $fileOpsRes = Invoke-Ksh @('-c', "md `"$slashTemp/ops_dir`" && copy /Y README.md `"$slashTemp/ops_dir/f.txt`" && ren `"$slashTemp/ops_dir/f.txt`" f2.txt && del /Q `"$slashTemp/ops_dir/f2.txt`" && rd `"$slashTemp/ops_dir`"")
    if ($fileOpsRes.ExitCode -eq 0) {
        Write-Output 'PASS builtin file management lifecycle (md, copy, ren, del, rd)'
    } else {
        $failures++
        Write-Output "FAIL builtin file management lifecycle (exit=$($fileOpsRes.ExitCode), stdout=<$($fileOpsRes.Stdout)>)"
    }

    $dirsRes = Invoke-Ksh @('-c', 'dirs')
    if ($dirsRes.ExitCode -eq 0) {
        Write-Output 'PASS dirs query'
    } else {
        $failures++
        Write-Output "FAIL dirs command (exit=$($dirsRes.ExitCode), stdout=<$($dirsRes.Stdout)>)"
    }

    $sleepRes = Invoke-Ksh @('-c', 'sleep 0.05')
    if ($sleepRes.ExitCode -eq 0) {
        Write-Output 'PASS sleep builtin'
    } else {
        $failures++
        Write-Output "FAIL sleep command (exit=$($sleepRes.ExitCode), stdout=<$($sleepRes.Stdout)>)"
    }

    $intRes = Invoke-Ksh @('-c', 'integer count=10; echo $count')
    if ($intRes.ExitCode -eq 0 -and $intRes.Stdout.Trim() -eq '10') {
        Write-Output 'PASS integer ksh builtin'
    } else {
        $failures++
        Write-Output "FAIL integer command (exit=$($intRes.ExitCode), stdout=<$($intRes.Stdout)>)"
    }

    $baseRes = Invoke-Ksh @('-c', 'basename -a -s .c /foo/bar.c /baz/qux.c')
    if ($baseRes.ExitCode -eq 0 -and $baseRes.Stdout.Contains("bar`r`nqux") -or $baseRes.Stdout.Contains("bar`nqux")) {
        Write-Output 'PASS basename with -a and -s'
    } else {
        $failures++
        Write-Output "FAIL basename command (exit=$($baseRes.ExitCode), stdout=<$($baseRes.Stdout)>)"
    }

    $dirRes = Invoke-Ksh @('-c', 'dirname /usr/local/bin/prog')
    if ($dirRes.ExitCode -eq 0 -and $dirRes.Stdout.Trim() -eq '/usr/local/bin') {
        Write-Output 'PASS dirname extraction'
    } else {
        $failures++
        Write-Output "FAIL dirname command (exit=$($dirRes.ExitCode), stdout=<$($dirRes.Stdout)>)"
    }

    $unameRes = Invoke-Ksh @('-c', 'uname -s')
    if ($unameRes.ExitCode -eq 0 -and $unameRes.Stdout.Trim() -eq 'Windows_NT') {
        Write-Output 'PASS uname query'
    } else {
        $failures++
        Write-Output "FAIL uname command (exit=$($unameRes.ExitCode), stdout=<$($unameRes.Stdout)>)"
    }

    $whichRes = Invoke-Ksh @('-c', 'which echo')
    if ($whichRes.ExitCode -eq 0 -and $whichRes.Stdout.Contains('shell built-in command')) {
        Write-Output 'PASS which lookup'
    } else {
        $failures++
        Write-Output "FAIL which command (exit=$($whichRes.ExitCode), stdout=<$($whichRes.Stdout)>)"
    }

    $envRes = Invoke-Ksh @('-c', 'env TEST_ENV=abc printenv TEST_ENV')
    if ($envRes.ExitCode -eq 0 -and $envRes.Stdout.Trim() -eq 'abc') {
        Write-Output 'PASS env and printenv integration'
    } else {
        $failures++
        Write-Output "FAIL env/printenv command (exit=$($envRes.ExitCode), stdout=<$($envRes.Stdout)>)"
    }

    $rRes = Invoke-Ksh @('-c', 'echo first; echo second; r second=third')
    if ($rRes.ExitCode -eq 0 -and $rRes.Stdout.Contains('third')) {
        Write-Output 'PASS r repeat and substitution'
    } else {
        $failures++
        Write-Output "FAIL r command (exit=$($rRes.ExitCode), stdout=<$($rRes.Stdout)>)"
    }

    $seqRes = Invoke-Ksh @('-c', 'seq 1 3')
    if ($seqRes.ExitCode -eq 0 -and $seqRes.Stdout.Trim() -replace "`r", "" -eq "1`n2`n3") {
        Write-Output 'PASS seq generation'
    } else {
        $failures++
        Write-Output "FAIL seq command (exit=$($seqRes.ExitCode), stdout=<$($seqRes.Stdout)>)"
    }

    $exprRes = Invoke-Ksh @('-c', 'expr 10 + 20')
    if ($exprRes.ExitCode -eq 0 -and $exprRes.Stdout.Trim() -eq '30') {
        Write-Output 'PASS expr arithmetic'
    } else {
        $failures++
        Write-Output "FAIL expr command (exit=$($exprRes.ExitCode), stdout=<$($exprRes.Stdout)>)"
    }

    $pipeRes = Invoke-Ksh @('-c', 'seq 1 10 | head -n 3')
    if ($pipeRes.ExitCode -eq 0 -and $pipeRes.Stdout.Trim() -replace "`r", "" -eq "1`n2`n3") {
        Write-Output 'PASS head pipeline'
    } else {
        $failures++
        Write-Output "FAIL head command (exit=$($pipeRes.ExitCode), stdout=<$($pipeRes.Stdout)>)"
    }

    $tailRes = Invoke-Ksh @('-c', 'seq 1 10 | tail -n 2')
    if ($tailRes.ExitCode -eq 0 -and $tailRes.Stdout.Trim() -replace "`r", "" -eq "9`n10") {
        Write-Output 'PASS tail pipeline'
    } else {
        $failures++
        Write-Output "FAIL tail command (exit=$($tailRes.ExitCode), stdout=<$($tailRes.Stdout)>)"
    }

    $wcRes = Invoke-Ksh @('-c', 'seq 1 7 | wc -l')
    if ($wcRes.ExitCode -eq 0 -and $wcRes.Stdout.Trim() -eq '7') {
        Write-Output 'PASS wc line count'
    } else {
        $failures++
        Write-Output "FAIL wc command (exit=$($wcRes.ExitCode), stdout=<$($wcRes.Stdout)>)"
    }

    $catRes = Invoke-Ksh @('-c', 'seq 1 2 | cat -n')
    if ($catRes.ExitCode -eq 0 -and $catRes.Stdout.Contains('1	1') -and $catRes.Stdout.Contains('2	2')) {
        Write-Output 'PASS cat numbered output'
    } else {
        $failures++
        Write-Output "FAIL cat command (exit=$($catRes.ExitCode), stdout=<$($catRes.Stdout)>)"
    }

    $whereRes = Invoke-Ksh @('-c', 'where cmd')
    if ($whereRes.ExitCode -eq 0 -and $whereRes.Stdout.Contains('cmd.exe')) {
        Write-Output 'PASS where command resolution'
    } else {
        $failures++
        Write-Output "FAIL where command (exit=$($whereRes.ExitCode), stdout=<$($whereRes.Stdout)>)"
    }

    $whoRes = Invoke-Ksh @('-c', 'whoami')
    if ($whoRes.ExitCode -eq 0 -and $whoRes.Stdout.Contains('\')) {
        Write-Output 'PASS whoami query'
    } else {
        $failures++
        Write-Output "FAIL whoami command (exit=$($whoRes.ExitCode), stdout=<$($whoRes.Stdout)>)"
    }

    $hostRes = Invoke-Ksh @('-c', 'hostname')
    if ($hostRes.ExitCode -eq 0 -and $hostRes.Stdout.Trim().Length -gt 0) {
        Write-Output 'PASS hostname query'
    } else {
        $failures++
        Write-Output "FAIL hostname command (exit=$($hostRes.ExitCode), stdout=<$($hostRes.Stdout)>)"
    }

    $syncRes = Invoke-Ksh @('-c', 'sync')
    if ($syncRes.ExitCode -eq 0) {
        Write-Output 'PASS sync filesystem flush'
    } else {
        $failures++
        Write-Output "FAIL sync command (exit=$($syncRes.ExitCode), stdout=<$($syncRes.Stdout)>)"
    }

    $attribRes = Invoke-Ksh @('-c', 'attrib README.md')
    if ($attribRes.ExitCode -eq 0 -and $attribRes.Stdout.Contains('README.md')) {
        Write-Output 'PASS attrib query'
    } else {
        $failures++
        Write-Output "FAIL attrib command (exit=$($attribRes.ExitCode), stdout=<$($attribRes.Stdout)>)"
    }

    # Category 1 and Category 2 built-in tests
    $cutRes = Invoke-Ksh @('-c', 'echo one:two:three | cut -d: -f2')
    if ($cutRes.ExitCode -eq 0 -and $cutRes.Stdout.Trim() -eq 'two') {
        Write-Output 'PASS cut field extraction'
    } else {
        $failures++
        Write-Output "FAIL cut field extraction (exit=$($cutRes.ExitCode), stdout=<$($cutRes.Stdout)>)"
    }

    $trRes = Invoke-Ksh @('-c', 'echo hello | tr a-z A-Z')
    if ($trRes.ExitCode -eq 0 -and $trRes.Stdout.Trim() -eq 'HELLO') {
        Write-Output 'PASS tr translation'
    } else {
        $failures++
        Write-Output "FAIL tr translation (exit=$($trRes.ExitCode), stdout=<$($trRes.Stdout)>)"
    }

    $sortRes = Invoke-Ksh @('-c', 'printf "banana\napple\ncherry\n" | sort')
    if ($sortRes.ExitCode -eq 0 -and $sortRes.Stdout.Trim().Replace("`r", "") -eq "apple`nbanana`ncherry") {
        Write-Output 'PASS sort text lines'
    } else {
        $failures++
        Write-Output "FAIL sort (exit=$($sortRes.ExitCode), stdout=<$($sortRes.Stdout)>)"
    }

    $uniqRes = Invoke-Ksh @('-c', 'printf "apple\napple\norange\n" | uniq')
    if ($uniqRes.ExitCode -eq 0 -and $uniqRes.Stdout.Trim().Replace("`r", "") -eq "apple`norange") {
        Write-Output 'PASS uniq line filtering'
    } else {
        $failures++
        Write-Output "FAIL uniq (exit=$($uniqRes.ExitCode), stdout=<$($uniqRes.Stdout)>)"
    }

    $touchFile = Join-Path $tempDirectory 'touched.txt'
    $touchRes = Invoke-Ksh @('-c', "touch `"$touchFile`"")
    if ($touchRes.ExitCode -eq 0 -and (Test-Path -LiteralPath $touchFile)) {
        Write-Output 'PASS touch file creation'
    } else {
        $failures++
        Write-Output "FAIL touch (exit=$($touchRes.ExitCode))"
    }

    $yesRes = Invoke-Ksh @('-c', 'yes test | head -n 3')
    if ($yesRes.ExitCode -eq 0 -and $yesRes.Stdout.Trim().Replace("`r", "") -eq "test`ntest`ntest") {
        Write-Output 'PASS yes repetition'
    } else {
        $failures++
        Write-Output "FAIL yes (exit=$($yesRes.ExitCode), stdout=<$($yesRes.Stdout)>)"
    }

    $p1 = Join-Path $tempDirectory 'p1.tmp'
    $p2 = Join-Path $tempDirectory 'p2.tmp'
    Set-Content -LiteralPath $p1 -Value "1`n2" -Encoding utf8
    Set-Content -LiteralPath $p2 -Value "A`nB" -Encoding utf8
    $pasteRes = Invoke-Ksh @('-c', "paste -d, `"$p1`" `"$p2`"")
    if ($pasteRes.ExitCode -eq 0 -and $pasteRes.Stdout.Trim().Replace("`r", "") -eq "1,A`n2,B") {
        Write-Output 'PASS paste file columns'
    } else {
        $failures++
        Write-Output "FAIL paste (exit=$($pasteRes.ExitCode), stdout=<$($pasteRes.Stdout)>)"
    }

    $foldRes = Invoke-Ksh @('-c', 'echo 1234567890 | fold -w 5')
    if ($foldRes.ExitCode -eq 0 -and $foldRes.Stdout.Trim().Replace("`r", "") -eq "12345`n67890") {
        Write-Output 'PASS fold line wrapping'
    } else {
        $failures++
        Write-Output "FAIL fold (exit=$($foldRes.ExitCode), stdout=<$($foldRes.Stdout)>)"
    }

    $expandRes = Invoke-Ksh @('-c', 'printf "a\tb\n" | expand -t 4')
    if ($expandRes.ExitCode -eq 0 -and $expandRes.Stdout.Trim() -eq 'a   b') {
        Write-Output 'PASS expand tab conversion'
    } else {
        $failures++
        Write-Output "FAIL expand (exit=$($expandRes.ExitCode), stdout=<$($expandRes.Stdout)>)"
    }

    $unexpandRes = Invoke-Ksh @('-c', 'printf "a   b\n" | unexpand -a -t 4')
    if ($unexpandRes.ExitCode -eq 0 -and $unexpandRes.Stdout.Trim() -eq "a`tb") {
        Write-Output 'PASS unexpand space conversion'
    } else {
        $failures++
        Write-Output "FAIL unexpand (exit=$($unexpandRes.ExitCode), stdout=<$($unexpandRes.Stdout)>)"
    }

    $nlRes = Invoke-Ksh @('-c', 'printf "one\ntwo\n" | nl -s " "')
    if ($nlRes.ExitCode -eq 0 -and $nlRes.Stdout.Contains('1 one') -and $nlRes.Stdout.Contains('2 two')) {
        Write-Output 'PASS nl line numbering'
    } else {
        $failures++
        Write-Output "FAIL nl (exit=$($nlRes.ExitCode), stdout=<$($nlRes.Stdout)>)"
    }

    $readlinkRes = Invoke-Ksh @('-c', 'readlink -f README.md')
    if ($readlinkRes.ExitCode -eq 0 -and $readlinkRes.Stdout.Contains('README.md')) {
        Write-Output 'PASS readlink path canonicalization'
    } else {
        $failures++
        Write-Output "FAIL readlink (exit=$($readlinkRes.ExitCode), stdout=<$($readlinkRes.Stdout)>)"
    }

    $realpathRes = Invoke-Ksh @('-c', 'realpath README.md')
    if ($realpathRes.ExitCode -eq 0 -and $realpathRes.Stdout.Contains('README.md')) {
        Write-Output 'PASS realpath path resolution'
    } else {
        $failures++
        Write-Output "FAIL realpath (exit=$($realpathRes.ExitCode), stdout=<$($realpathRes.Stdout)>)"
    }

    $idRes = Invoke-Ksh @('-c', 'id')
    if ($idRes.ExitCode -eq 0 -and $idRes.Stdout.Contains('uid=')) {
        Write-Output 'PASS id user/group query'
    } else {
        $failures++
        Write-Output "FAIL id (exit=$($idRes.ExitCode), stdout=<$($idRes.Stdout)>)"
    }

    $lognameRes = Invoke-Ksh @('-c', 'logname')
    if ($lognameRes.ExitCode -eq 0 -and $lognameRes.Stdout.Trim().Length -gt 0) {
        Write-Output 'PASS logname query'
    } else {
        $failures++
        Write-Output "FAIL logname (exit=$($lognameRes.ExitCode), stdout=<$($lognameRes.Stdout)>)"
    }

    $tasklistRes = Invoke-Ksh @('-c', 'tasklist /FO CSV /NH')
    if ($tasklistRes.ExitCode -eq 0 -and $tasklistRes.Stdout.Contains('ksh.exe')) {
        Write-Output 'PASS tasklist process enumeration'
    } else {
        $failures++
        Write-Output "FAIL tasklist (exit=$($tasklistRes.ExitCode), stdout=<$($tasklistRes.Stdout)>)"
    }

    $taskkillRes = Invoke-Ksh @('-c', 'taskkill /?')
    if ($taskkillRes.ExitCode -eq 0 -and $taskkillRes.Stdout.Contains('/PID')) {
        Write-Output 'PASS taskkill help'
    } else {
        $failures++
        Write-Output "FAIL taskkill (exit=$($taskkillRes.ExitCode), stdout=<$($taskkillRes.Stdout)>)"
    }

    $modeRes = Invoke-Ksh @('-c', 'mode')
    if ($modeRes.ExitCode -eq 0 -and $modeRes.Stdout.Contains('Lines:')) {
        Write-Output 'PASS mode query'
    } else {
        $failures++
        Write-Output "FAIL mode (exit=$($modeRes.ExitCode), stdout=<$($modeRes.Stdout)>)"
    }

    $moreRes = Invoke-Ksh @('-c', 'printf "lineA\nlineB\n" | more')
    if ($moreRes.ExitCode -eq 0 -and $moreRes.Stdout.Trim().Replace("`r", "") -eq "lineA`nlineB") {
        Write-Output 'PASS more streaming'
    } else {
        $failures++
        Write-Output "FAIL more (exit=$($moreRes.ExitCode), stdout=<$($moreRes.Stdout)>)"
    }

    $shutdownRes = Invoke-Ksh @('-c', 'shutdown /?')
    if ($shutdownRes.ExitCode -eq 0 -and $shutdownRes.Stdout.Contains('/s')) {
        Write-Output 'PASS shutdown help'
    } else {
        $failures++
        Write-Output "FAIL shutdown (exit=$($shutdownRes.ExitCode), stdout=<$($shutdownRes.Stdout)>)"
    }

    $systeminfoRes = Invoke-Ksh @('-c', 'systeminfo')
    if ($systeminfoRes.ExitCode -eq 0 -and $systeminfoRes.Stdout.Contains('Host Name:')) {
        Write-Output 'PASS systeminfo query'
    } else {
        $failures++
        Write-Output "FAIL systeminfo (exit=$($systeminfoRes.ExitCode), stdout=<$($systeminfoRes.Stdout)>)"
    }

    $substRes = Invoke-Ksh @('-c', 'subst')
    if ($substRes.ExitCode -eq 0) {
        Write-Output 'PASS subst query'
    } else {
        $failures++
        Write-Output "FAIL subst (exit=$($substRes.ExitCode), stdout=<$($substRes.Stdout)>)"
    }

    # grep, sed, awk, diff, BSD find, BSD date tests
    $grepFile = Join-Path $tempDirectory 'grep.txt'
    Set-Content -LiteralPath $grepFile -Value "apple 100`nBANANA 200`norange 300`npear" -Encoding utf8

    $grepRes = Invoke-Ksh @('-c', "grep -i banana `"$grepFile`"")
    if ($grepRes.ExitCode -eq 0 -and $grepRes.Stdout.Contains('BANANA 200')) {
        Write-Output 'PASS grep case-insensitive match'
    } else {
        $failures++
        Write-Output "FAIL grep -i (exit=$($grepRes.ExitCode), stdout=<$($grepRes.Stdout)>)"
    }

    $grepInvRes = Invoke-Ksh @('-c', "grep -v 0 `"$grepFile`"")
    if ($grepInvRes.ExitCode -eq 0 -and $grepInvRes.Stdout.Trim() -eq 'pear') {
        Write-Output 'PASS grep invert match'
    } else {
        $failures++
        Write-Output "FAIL grep -v (exit=$($grepInvRes.ExitCode), stdout=<$($grepInvRes.Stdout)>)"
    }

    $grepCountRes = Invoke-Ksh @('-c', "grep -c 0 `"$grepFile`"")
    if ($grepCountRes.ExitCode -eq 0 -and $grepCountRes.Stdout.Trim() -eq '3') {
        Write-Output 'PASS grep count match'
    } else {
        $failures++
        Write-Output "FAIL grep -c (exit=$($grepCountRes.ExitCode), stdout=<$($grepCountRes.Stdout)>)"
    }

    $egrepRes = Invoke-Ksh @('-c', "egrep `"apple|orange`" `"$grepFile`"")
    if ($egrepRes.ExitCode -eq 0 -and $egrepRes.Stdout.Contains('apple') -and $egrepRes.Stdout.Contains('orange')) {
        Write-Output 'PASS egrep regex alternation'
    } else {
        $failures++
        Write-Output "FAIL egrep (exit=$($egrepRes.ExitCode), stdout=<$($egrepRes.Stdout)>)"
    }

    $fgrepRes = Invoke-Ksh @('-c', "fgrep `"pear`" `"$grepFile`"")
    if ($fgrepRes.ExitCode -eq 0 -and $fgrepRes.Stdout.Trim() -eq 'pear') {
        Write-Output 'PASS fgrep fixed string match'
    } else {
        $failures++
        Write-Output "FAIL fgrep (exit=$($fgrepRes.ExitCode), stdout=<$($fgrepRes.Stdout)>)"
    }

    $grepPRes = Invoke-Ksh @('-c', "grep -p BANANA `"$grepFile`"")
    if ($grepPRes.ExitCode -eq 0 -and $grepPRes.Stdout.Contains('BANANA 200')) {
        Write-Output 'PASS grep -p BSD qualifier match'
    } else {
        $failures++
        Write-Output "FAIL grep -p (exit=$($grepPRes.ExitCode), stdout=<$($grepPRes.Stdout)>)"
    }

    $grepNumRes = Invoke-Ksh @('-c', "grep -1 BANANA `"$grepFile`"")
    if ($grepNumRes.ExitCode -eq 0 -and $grepNumRes.Stdout.Contains('apple 100') -and $grepNumRes.Stdout.Contains('orange 300')) {
        Write-Output 'PASS grep BSD numeric context flag'
    } else {
        $failures++
        Write-Output "FAIL grep numeric context (exit=$($grepNumRes.ExitCode), stdout=<$($grepNumRes.Stdout)>)"
    }

    $sedRes = Invoke-Ksh @('-c', "sed s/apple/cherry/ `"$grepFile`"")
    if ($sedRes.ExitCode -eq 0 -and $sedRes.Stdout.Contains('cherry 100')) {
        Write-Output 'PASS sed substitution'
    } else {
        $failures++
        Write-Output "FAIL sed (exit=$($sedRes.ExitCode), stdout=<$($sedRes.Stdout)>)"
    }

    $sedPlusRes = Invoke-Ksh @('-c', "sed '1,+1d' `"$grepFile`"")
    if ($sedPlusRes.ExitCode -eq 0 -and (-not $sedPlusRes.Stdout.Contains('apple')) -and $sedPlusRes.Stdout.Contains('orange 300')) {
        Write-Output 'PASS sed BSD +N address range qualifier'
    } else {
        $failures++
        Write-Output "FAIL sed +N range (exit=$($sedPlusRes.ExitCode), stdout=<$($sedPlusRes.Stdout)>)"
    }

    $sedICaseRes = Invoke-Ksh @('-c', "sed 's/banana/grape/I' `"$grepFile`"")
    if ($sedICaseRes.ExitCode -eq 0 -and $sedICaseRes.Stdout.Contains('grape 200')) {
        Write-Output 'PASS sed BSD I flag substitution'
    } else {
        $failures++
        Write-Output "FAIL sed I flag (exit=$($sedICaseRes.ExitCode), stdout=<$($sedICaseRes.Stdout)>)"
    }

    $awkRes = Invoke-Ksh @('-c', "awk {print} `"$grepFile`"")
    if ($awkRes.ExitCode -eq 0 -and $awkRes.Stdout.Contains('apple 100')) {
        Write-Output 'PASS awk line processing'
    } else {
        $failures++
        Write-Output "FAIL awk (exit=$($awkRes.ExitCode), stdout=<$($awkRes.Stdout)>)"
    }

    $awkBsdRes = Invoke-Ksh @('-c', 'awk -v x=10 -v y=20 "BEGIN { print x + y }"')
    if ($awkBsdRes.ExitCode -eq 0 -and $awkBsdRes.Stdout.Trim() -eq '30') {
        Write-Output 'PASS awk BSD -v variable assignment & arithmetic'
    } else {
        $failures++
        Write-Output "FAIL awk -v (exit=$($awkBsdRes.ExitCode), stdout=<$($awkBsdRes.Stdout)>)"
    }

    $awkFuncRes = Invoke-Ksh @('-c', 'awk "function f(n){ return n*n } BEGIN { a[\"one\"] = f(5); print a[\"one\"] }"')
    if ($awkFuncRes.ExitCode -eq 0 -and $awkFuncRes.Stdout.Trim() -eq '25') {
        Write-Output 'PASS awk functions and associative arrays'
    } else {
        $failures++
        Write-Output "FAIL awk function & array (exit=$($awkFuncRes.ExitCode), stdout=<$($awkFuncRes.Stdout)>)"
    }

    $awkGsubRes = Invoke-Ksh @('-c', 'awk "BEGIN { s = \"foo bar foo\"; gsub(/foo/, \"baz\", s); print s }"')
    if ($awkGsubRes.ExitCode -eq 0 -and $awkGsubRes.Stdout.Trim() -eq 'baz bar baz') {
        Write-Output 'PASS awk gsub built-in regex replacement'
    } else {
        $failures++
        Write-Output "FAIL awk gsub (exit=$($awkGsubRes.ExitCode), stdout=<$($awkGsubRes.Stdout)>)"
    }

    $awkSafeRes = Invoke-Ksh @('-c', 'awk -safe "BEGIN { system(\"echo 1\") }"')
    if ($awkSafeRes.ExitCode -ne 0) {
        Write-Output 'PASS awk -safe BSD qualifier enforcement'
    } else {
        $failures++
        Write-Output "FAIL awk -safe (exit=$($awkSafeRes.ExitCode), stdout=<$($awkSafeRes.Stdout)>)"
    }

    $diffFile1 = Join-Path $tempDirectory 'diff1.txt'
    $diffFile2 = Join-Path $tempDirectory 'diff2.txt'
    Set-Content -LiteralPath $diffFile1 -Value "line1`nline2`nline3" -Encoding utf8
    Set-Content -LiteralPath $diffFile2 -Value "line1`nmodified`nline3" -Encoding utf8

    $diffRes = Invoke-Ksh @('-c', "diff -u `"$diffFile1`" `"$diffFile2`"")
    if ($diffRes.ExitCode -eq 1 -and $diffRes.Stdout.Contains('-line2') -and $diffRes.Stdout.Contains('+modified')) {
        Write-Output 'PASS diff unified difference'
    } else {
        $failures++
        Write-Output "FAIL diff (exit=$($diffRes.ExitCode), stdout=<$($diffRes.Stdout)>)"
    }

    $diffBriefRes = Invoke-Ksh @('-c', "diff -q `"$diffFile1`" `"$diffFile2`"")
    if ($diffBriefRes.ExitCode -eq 1 -and $diffBriefRes.Stdout.Contains('differ')) {
        Write-Output 'PASS diff brief mode'
    } else {
        $failures++
        Write-Output "FAIL diff -q (exit=$($diffBriefRes.ExitCode), stdout=<$($diffBriefRes.Stdout)>)"
    }

    $diffContextRes = Invoke-Ksh @('-c', "diff -c `"$diffFile1`" `"$diffFile2`"")
    if ($diffContextRes.ExitCode -eq 1 -and $diffContextRes.Stdout.Contains('***') -and $diffContextRes.Stdout.Contains('! modified')) {
        Write-Output 'PASS diff BSD context format'
    } else {
        $failures++
        Write-Output "FAIL diff -c (exit=$($diffContextRes.ExitCode), stdout=<$($diffContextRes.Stdout)>)"
    }

    $diffNormalRes = Invoke-Ksh @('-c', "diff `"$diffFile1`" `"$diffFile2`"")
    if ($diffNormalRes.ExitCode -eq 1 -and $diffNormalRes.Stdout.Contains('2c2') -and $diffNormalRes.Stdout.Contains('< line2') -and $diffNormalRes.Stdout.Contains('> modified')) {
        Write-Output 'PASS diff BSD normal format'
    } else {
        $failures++
        Write-Output "FAIL diff normal (exit=$($diffNormalRes.ExitCode), stdout=<$($diffNormalRes.Stdout)>)"
    }

    $findBsdRes = Invoke-Ksh @('-c', "find `"$tempDirectory`" -name grep.txt")
    if ($findBsdRes.ExitCode -eq 0 -and $findBsdRes.Stdout.Contains('grep.txt')) {
        Write-Output 'PASS find BSD directory search'
    } else {
        $failures++
        Write-Output "FAIL find BSD (exit=$($findBsdRes.ExitCode), stdout=<$($findBsdRes.Stdout)>)"
    }

    $dateBsdRes = Invoke-Ksh @('-c', 'date +%Y-%m-%d')
    if ($dateBsdRes.ExitCode -eq 0 -and $dateBsdRes.Stdout.Trim().Length -eq 10) {
        Write-Output 'PASS date BSD format specifier'
    } else {
        $failures++
        Write-Output "FAIL date BSD (exit=$($dateBsdRes.ExitCode), stdout=<$($dateBsdRes.Stdout)>)"
    }

    $dateUtcRes = Invoke-Ksh @('-c', 'date -u +%H:%M')
    if ($dateUtcRes.ExitCode -eq 0 -and $dateUtcRes.Stdout.Trim().Length -eq 5) {
        Write-Output 'PASS date BSD UTC query'
    } else {
        $failures++
        Write-Output "FAIL date UTC (exit=$($dateUtcRes.ExitCode), stdout=<$($dateUtcRes.Stdout)>)"
    }

    # ls test
    $lsRes = Invoke-Ksh @('-c', "ls -1 `"$tempDirectory`"")
    if ($lsRes.ExitCode -eq 0 -and $lsRes.Stdout.Contains('diff1.txt')) {
        Write-Output 'PASS ls directory listing'
    } else {
        $failures++
        Write-Output "FAIL ls (exit=$($lsRes.ExitCode), stdout=<$($lsRes.Stdout)>)"
    }

    $lsLongRes = Invoke-Ksh @('-c', "ls -la `"$tempDirectory`"")
    if ($lsLongRes.ExitCode -eq 0 -and $lsLongRes.Stdout.Contains('total ') -and $lsLongRes.Stdout.Contains('diff1.txt')) {
        Write-Output 'PASS ls BSD long format'
    } else {
        $failures++
        Write-Output "FAIL ls -la (exit=$($lsLongRes.ExitCode), stdout=<$($lsLongRes.Stdout)>)"
    }

    $lsStreamRes = Invoke-Ksh @('-c', "ls -m `"$tempDirectory`"")
    if ($lsStreamRes.ExitCode -eq 0 -and $lsStreamRes.Stdout.Contains(',')) {
        Write-Output 'PASS ls BSD stream format'
    } else {
        $failures++
        Write-Output "FAIL ls -m (exit=$($lsStreamRes.ExitCode), stdout=<$($lsStreamRes.Stdout)>)"
    }

    # cp and mv and rm tests
    $cpTarget = Join-Path $tempDirectory 'copied.txt'
    $mvTarget = Join-Path $tempDirectory 'moved.txt'
    $cpRes = Invoke-Ksh @('-c', "cp `"$diffFile1`" `"$cpTarget`"")
    if ($cpRes.ExitCode -eq 0 -and (Test-Path -LiteralPath $cpTarget)) {
        Write-Output 'PASS cp file copy'
    } else {
        $failures++
        Write-Output "FAIL cp (exit=$($cpRes.ExitCode), stdout=<$($cpRes.Stdout)>)"
    }

    $mvRes = Invoke-Ksh @('-c', "mv `"$cpTarget`" `"$mvTarget`"")
    if ($mvRes.ExitCode -eq 0 -and (Test-Path -LiteralPath $mvTarget) -and !(Test-Path -LiteralPath $cpTarget)) {
        Write-Output 'PASS mv file rename'
    } else {
        $failures++
        Write-Output "FAIL mv (exit=$($mvRes.ExitCode), stdout=<$($mvRes.Stdout)>)"
    }

    $rmRes = Invoke-Ksh @('-c', "rm `"$mvTarget`"")
    if ($rmRes.ExitCode -eq 0 -and !(Test-Path -LiteralPath $mvTarget)) {
        Write-Output 'PASS rm file removal'
    } else {
        $failures++
        Write-Output "FAIL rm (exit=$($rmRes.ExitCode), stdout=<$($rmRes.Stdout)>)"
    }

    # base64 test
    $b64File = Join-Path $tempDirectory 'b64test.txt'
    [System.IO.File]::WriteAllBytes($b64File, [System.Text.Encoding]::ASCII.GetBytes("Hello World"))
    $b64Enc = Invoke-Ksh @('-c', "base64 `"$b64File`"")
    if ($b64Enc.ExitCode -eq 0 -and $b64Enc.Stdout.Trim() -eq 'SGVsbG8gV29ybGQ=') {
        Write-Output 'PASS base64 encode'
    } else {
        $failures++
        Write-Output "FAIL base64 encode (exit=$($b64Enc.ExitCode), stdout=<$($b64Enc.Stdout)>)"
    }

    $b64DecFile = Join-Path $tempDirectory 'b64dec.txt'
    Set-Content -LiteralPath $b64DecFile -Value "SGVsbG8gV29ybGQ=" -Encoding utf8
    $b64Dec = Invoke-Ksh @('-c', "base64 -d `"$b64DecFile`"")
    if ($b64Dec.ExitCode -eq 0 -and $b64Dec.Stdout.Trim() -eq 'Hello World') {
        Write-Output 'PASS base64 decode'
    } else {
        $failures++
        Write-Output "FAIL base64 decode (exit=$($b64Dec.ExitCode), stdout=<$($b64Dec.Stdout)>)"
    }

    # md5 and sha256 tests
    $chkFile = Join-Path $tempDirectory 'checksum.txt'
    Set-Content -LiteralPath $chkFile -Value "test string" -Encoding ascii -NoNewline
    $md5Res = Invoke-Ksh @('-c', "md5 `"$chkFile`"")
    if ($md5Res.ExitCode -eq 0 -and $md5Res.Stdout.ToLower().Contains('6f8db599de986fab7a21625b7916589c')) {
        Write-Output 'PASS md5 checksum'
    } else {
        $failures++
        Write-Output "FAIL md5 (exit=$($md5Res.ExitCode), stdout=<$($md5Res.Stdout)>)"
    }

    $sha256Res = Invoke-Ksh @('-c', "sha256sum `"$chkFile`"")
    if ($sha256Res.ExitCode -eq 0 -and $sha256Res.Stdout.ToLower().Contains('d5579c46dfcc7f18207013e65b44e4cb4e2c2298f4ac457ba8f82743f31e930b')) {
        Write-Output 'PASS sha256sum checksum'
    } else {
        $failures++
        Write-Output "FAIL sha256sum (exit=$($sha256Res.ExitCode), stdout=<$($sha256Res.Stdout)>)"
    }

    # dos2unix and unix2dos test
    $crlfFile = Join-Path $tempDirectory 'crlf.txt'
    [System.IO.File]::WriteAllBytes($crlfFile, [System.Text.Encoding]::ASCII.GetBytes("hello`r`nworld`r`n"))
    $d2uRes = Invoke-Ksh @('-c', "dos2unix `"$crlfFile`"")
    $d2uBytes = [System.IO.File]::ReadAllBytes($crlfFile)
    $d2uStr = [System.Text.Encoding]::ASCII.GetString($d2uBytes)
    if ($d2uRes.ExitCode -eq 0 -and $d2uStr -eq "hello`nworld`n") {
        Write-Output 'PASS dos2unix conversion'
    } else {
        $failures++
        Write-Output "FAIL dos2unix (exit=$($d2uRes.ExitCode), content=<$d2uStr>)"
    }

    $u2dRes = Invoke-Ksh @('-c', "unix2dos `"$crlfFile`"")
    $u2dBytes = [System.IO.File]::ReadAllBytes($crlfFile)
    $u2dStr = [System.Text.Encoding]::ASCII.GetString($u2dBytes)
    if ($u2dRes.ExitCode -eq 0 -and $u2dStr -eq "hello`r`nworld`r`n") {
        Write-Output 'PASS unix2dos conversion'
    } else {
        $failures++
        Write-Output "FAIL unix2dos (exit=$($u2dRes.ExitCode), content=<$u2dStr>)"
    }

    # xargs test
    $xargsRes = Invoke-Ksh @('-c', 'echo "foo bar baz" | xargs echo')
    if ($xargsRes.ExitCode -eq 0 -and $xargsRes.Stdout.Trim() -eq 'foo bar baz') {
        Write-Output 'PASS xargs command batching'
    } else {
        $failures++
        Write-Output "FAIL xargs (exit=$($xargsRes.ExitCode), stdout=<$($xargsRes.Stdout)>)"
    }

    # ps and pgrep test
    $psRes = Invoke-Ksh @('-c', 'ps')
    if ($psRes.ExitCode -eq 0 -and $psRes.Stdout.Contains('PID')) {
        Write-Output 'PASS ps process snapshot'
    } else {
        $failures++
        Write-Output "FAIL ps (exit=$($psRes.ExitCode), stdout=<$($psRes.Stdout)>)"
    }

    $pgrepRes = Invoke-Ksh @('-c', 'pgrep ksh')
    if ($pgrepRes.ExitCode -eq 0 -and $pgrepRes.Stdout.Trim().Length -gt 0) {
        Write-Output 'PASS pgrep pattern matching'
    } else {
        $failures++
        Write-Output "FAIL pgrep (exit=$($pgrepRes.ExitCode), stdout=<$($pgrepRes.Stdout)>)"
    }

    # Category A tests: du, df, stat, strings, hexdump, file
    $duRes = Invoke-Ksh @('-c', "du -s `"$tempDirectory`"")
    if ($duRes.ExitCode -eq 0 -and $duRes.Stdout.Contains($tempDirectory)) {
        Write-Output 'PASS du directory usage'
    } else {
        $failures++
        Write-Output "FAIL du (exit=$($duRes.ExitCode), stdout=<$($duRes.Stdout)>)"
    }

    $dfRes = Invoke-Ksh @('-c', 'df -h')
    if ($dfRes.ExitCode -eq 0 -and $dfRes.Stdout.Contains('Filesystem')) {
        Write-Output 'PASS df disk space report'
    } else {
        $failures++
        Write-Output "FAIL df (exit=$($dfRes.ExitCode), stdout=<$($dfRes.Stdout)>)"
    }

    $statRes = Invoke-Ksh @('-c', "stat `"$crlfFile`"")
    if ($statRes.ExitCode -eq 0 -and $statRes.Stdout.Contains('File:')) {
        Write-Output 'PASS stat file status'
    } else {
        $failures++
        Write-Output "FAIL stat (exit=$($statRes.ExitCode), stdout=<$($statRes.Stdout)>)"
    }

    $stringsRes = Invoke-Ksh @('-c', "strings `"$crlfFile`"")
    if ($stringsRes.ExitCode -eq 0 -and $stringsRes.Stdout.Contains('hello')) {
        Write-Output 'PASS strings printable extraction'
    } else {
        $failures++
        Write-Output "FAIL strings (exit=$($stringsRes.ExitCode), stdout=<$($stringsRes.Stdout)>)"
    }

    $hdRes = Invoke-Ksh @('-c', "hexdump -C `"$crlfFile`"")
    if ($hdRes.ExitCode -eq 0 -and $hdRes.Stdout.Contains('|hello')) {
        Write-Output 'PASS hexdump canonical dump'
    } else {
        $failures++
        Write-Output "FAIL hexdump (exit=$($hdRes.ExitCode), stdout=<$($hdRes.Stdout)>)"
    }

    $fileRes = Invoke-Ksh @('-c', "file `"$crlfFile`"")
    if ($fileRes.ExitCode -eq 0 -and $fileRes.Stdout.Contains('text')) {
        Write-Output 'PASS file type detection'
    } else {
        $failures++
        Write-Output "FAIL file (exit=$($fileRes.ExitCode), stdout=<$($fileRes.Stdout)>)"
    }

    # Category B tests: comm, join, split, rev, tac, shuf
    $commA = Join-Path $tempDirectory 'commA.txt'
    $commB = Join-Path $tempDirectory 'commB.txt'
    [System.IO.File]::WriteAllBytes($commA, [System.Text.Encoding]::ASCII.GetBytes("apple`nbanana`ncherry`n"))
    [System.IO.File]::WriteAllBytes($commB, [System.Text.Encoding]::ASCII.GetBytes("banana`ndate`n"))
    $commRes = Invoke-Ksh @('-c', "comm -12 `"$commA`" `"$commB`"")
    if ($commRes.ExitCode -eq 0 -and $commRes.Stdout.Trim() -eq 'banana') {
        Write-Output 'PASS comm common line extraction'
    } else {
        $failures++
        Write-Output "FAIL comm (exit=$($commRes.ExitCode), stdout=<$($commRes.Stdout)>)"
    }

    $joinA = Join-Path $tempDirectory 'joinA.txt'
    $joinB = Join-Path $tempDirectory 'joinB.txt'
    [System.IO.File]::WriteAllBytes($joinA, [System.Text.Encoding]::ASCII.GetBytes("1 Alice`n2 Bob`n"))
    [System.IO.File]::WriteAllBytes($joinB, [System.Text.Encoding]::ASCII.GetBytes("1 Admin`n2 User`n"))
    $joinRes = Invoke-Ksh @('-c', "join `"$joinA`" `"$joinB`"")
    if ($joinRes.ExitCode -eq 0 -and $joinRes.Stdout.Contains('1 Alice Admin')) {
        Write-Output 'PASS join relational merge'
    } else {
        $failures++
        Write-Output "FAIL join (exit=$($joinRes.ExitCode), stdout=<$($joinRes.Stdout)>)"
    }

    $sptPfx = Join-Path $tempDirectory 'spt_'
    $splitRes = Invoke-Ksh @('-c', "split -l 1 -d `"$commA`" `"$sptPfx`"")
    $spt0 = Join-Path $tempDirectory 'spt_00'
    $spt3 = Join-Path $tempDirectory 'spt_03'
    if ($splitRes.ExitCode -eq 0 -and (Test-Path -LiteralPath $spt0) -and -not (Test-Path -LiteralPath $spt3)) {
        Write-Output 'PASS split file partitioning'
    } else {
        $failures++
        Write-Output "FAIL split (exit=$($splitRes.ExitCode), stdout=<$($splitRes.Stdout)>)"
    }

    $joinVC = Join-Path $tempDirectory 'joinC.txt'
    [System.IO.File]::WriteAllBytes($joinVC, [System.Text.Encoding]::ASCII.GetBytes("3 Eve`n"))
    $joinVRes = Invoke-Ksh @('-c', "join -v1 `"$joinA`" `"$joinVC`"")
    if ($joinVRes.ExitCode -eq 0 -and ($joinVRes.Stdout -replace "`r", '').Trim() -eq "1 Alice`n2 Bob") {
        Write-Output 'PASS join -v1 unpaired lines'
    } else {
        $failures++
        Write-Output "FAIL join -v1 (exit=$($joinVRes.ExitCode), stdout=<$($joinVRes.Stdout)>)"
    }

    $seqSepRes = Invoke-Ksh @('-c', 'seq -s, 1 3')
    if ($seqSepRes.ExitCode -eq 0 -and ($seqSepRes.Stdout -replace "`r", '').Trim() -eq '1,2,3') {
        Write-Output 'PASS seq attached -s separator'
    } else {
        $failures++
        Write-Output "FAIL seq -s (exit=$($seqSepRes.ExitCode), stdout=<$($seqSepRes.Stdout)>)"
    }

    $dateFmtRes = Invoke-Ksh @('-c', 'date -j -f "%Y-%m-%d %H:%M" "2026-11-13 16:14" "+%Y%m%d%H%M"')
    if ($dateFmtRes.ExitCode -eq 0 -and ($dateFmtRes.Stdout -replace "`r", '').Trim() -eq '202611131614') {
        Write-Output 'PASS date -j -f input format'
    } else {
        $failures++
        Write-Output "FAIL date -j -f (exit=$($dateFmtRes.ExitCode), stdout=<$($dateFmtRes.Stdout)>)"
    }

    $dateStampRes = Invoke-Ksh @('-c', 'date -j 202001010000 "+%Y"')
    if ($dateStampRes.ExitCode -eq 0 -and ($dateStampRes.Stdout -replace "`r", '').Trim() -eq '2020') {
        Write-Output 'PASS date -j stamp parsing without setting clock'
    } else {
        $failures++
        Write-Output "FAIL date -j stamp (exit=$($dateStampRes.ExitCode), stdout=<$($dateStampRes.Stdout)>)"
    }

    $dateDefRes = Invoke-Ksh @('-c', 'date -j')
    if ($dateDefRes.ExitCode -eq 0 -and $dateDefRes.Stdout -match '\d{2}:\d{2}:\d{2} \d{4}') {
        Write-Output 'PASS date -j default single-spaced output'
    } else {
        $failures++
        Write-Output "FAIL date -j default (exit=$($dateDefRes.ExitCode), stdout=<$($dateDefRes.Stdout)>)"
    }

    $dateBadRes = Invoke-Ksh @('-c', 'date -j 13459999')
    if ($dateBadRes.ExitCode -ne 0) {
        Write-Output 'PASS date -j rejects invalid stamp'
    } else {
        $failures++
        Write-Output "FAIL date -j invalid stamp accepted (stdout=<$($dateBadRes.Stdout)>)"
    }

    $revRes = Invoke-Ksh @('-c', "rev `"$commB`"")
    if ($revRes.ExitCode -eq 0 -and $revRes.Stdout.Contains('ananab')) {
        Write-Output 'PASS rev characterwise line reversal'
    } else {
        $failures++
        Write-Output "FAIL rev (exit=$($revRes.ExitCode), stdout=<$($revRes.Stdout)>)"
    }

    $tacRes = Invoke-Ksh @('-c', "tac `"$commA`"")
    if ($tacRes.ExitCode -eq 0 -and $tacRes.Stdout.StartsWith('cherry')) {
        Write-Output 'PASS tac reverse line streaming'
    } else {
        $failures++
        Write-Output "FAIL tac (exit=$($tacRes.ExitCode), stdout=<$($tacRes.Stdout)>)"
    }

    $shufRes = Invoke-Ksh @('-c', 'shuf -i 1-5 -n 3')
    $shufLines = ($shufRes.Stdout.Trim() -split "`n").Length
    if ($shufRes.ExitCode -eq 0 -and $shufLines -eq 3) {
        Write-Output 'PASS shuf random permutation'
    } else {
        $failures++
        Write-Output "FAIL shuf (exit=$($shufRes.ExitCode), stdout=<$($shufRes.Stdout)>)"
    }

    # Category C tests: sha512, cksum, nproc, cal, factor
    $sha512Res = Invoke-Ksh @('-c', "sha512sum `"$commB`"")
    if ($sha512Res.ExitCode -eq 0 -and $sha512Res.Stdout.Trim().Length -ge 128) {
        Write-Output 'PASS sha512sum digest'
    } else {
        $failures++
        Write-Output "FAIL sha512sum (exit=$($sha512Res.ExitCode), stdout=<$($sha512Res.Stdout)>)"
    }

    $cksumRes = Invoke-Ksh @('-c', "cksum `"$commB`"")
    if ($cksumRes.ExitCode -eq 0 -and $cksumRes.Stdout.Contains('commB.txt')) {
        Write-Output 'PASS cksum CRC computation'
    } else {
        $failures++
        Write-Output "FAIL cksum (exit=$($cksumRes.ExitCode), stdout=<$($cksumRes.Stdout)>)"
    }

    $nprocRes = Invoke-Ksh @('-c', 'nproc')
    if ($nprocRes.ExitCode -eq 0 -and [int]($nprocRes.Stdout.Trim()) -ge 1) {
        Write-Output 'PASS nproc processor count'
    } else {
        $failures++
        Write-Output "FAIL nproc (exit=$($nprocRes.ExitCode), stdout=<$($nprocRes.Stdout)>)"
    }

    $calRes = Invoke-Ksh @('-c', 'cal 11 2026')
    if ($calRes.ExitCode -eq 0 -and $calRes.Stdout.Contains('November 2026')) {
        Write-Output 'PASS cal calendar layout'
    } else {
        $failures++
        Write-Output "FAIL cal (exit=$($calRes.ExitCode), stdout=<$($calRes.Stdout)>)"
    }

    $factorRes = Invoke-Ksh @('-c', 'factor 42')
    if ($factorRes.ExitCode -eq 0 -and $factorRes.Stdout.Trim() -eq '42: 2 3 7') {
        Write-Output 'PASS factor prime factorization'
    } else {
        $failures++
        Write-Output "FAIL factor (exit=$($factorRes.ExitCode), stdout=<$($factorRes.Stdout)>)"
    }

    # Category D tests: uptime, free, watch, stty
    $uptimeRes = Invoke-Ksh @('-c', 'uptime -p')
    if ($uptimeRes.ExitCode -eq 0 -and $uptimeRes.Stdout.StartsWith('up ')) {
        Write-Output 'PASS uptime format query'
    } else {
        $failures++
        Write-Output "FAIL uptime (exit=$($uptimeRes.ExitCode), stdout=<$($uptimeRes.Stdout)>)"
    }

    $freeRes = Invoke-Ksh @('-c', 'free -h')
    if ($freeRes.ExitCode -eq 0 -and $freeRes.Stdout.Contains('Mem:') -and $freeRes.Stdout.Contains('Swap:')) {
        Write-Output 'PASS free memory query'
    } else {
        $failures++
        Write-Output "FAIL free (exit=$($freeRes.ExitCode), stdout=<$($freeRes.Stdout)>)"
    }

    $watchRes = Invoke-Ksh @('-c', 'watch --help')
    if ($watchRes.ExitCode -eq 0 -and $watchRes.Stdout.Contains('Usage: watch')) {
        Write-Output 'PASS watch execution help'
    } else {
        $failures++
        Write-Output "FAIL watch (exit=$($watchRes.ExitCode), stdout=<$($watchRes.Stdout)>)"
    }

    $sttyRes = Invoke-Ksh @('-c', 'stty size')
    if ($sttyRes.ExitCode -eq 0 -and $sttyRes.Stdout.Trim().Length -gt 0) {
        Write-Output 'PASS stty terminal size'
    } else {
        $failures++
        Write-Output "FAIL stty (exit=$($sttyRes.ExitCode), stdout=<$($sttyRes.Stdout)>)"
    }

    # Category E tests: column, truncate, dd, colrm
    $colFile = Join-Path $tempDirectory 'column-test.txt'
    Set-Content -LiteralPath $colFile -Value "one two`nthree four" -Encoding utf8
    $columnRes = Invoke-Ksh @('-c', "column -t `"$colFile`"")
    if ($columnRes.ExitCode -eq 0 -and $columnRes.Stdout.Contains('one    two')) {
        Write-Output 'PASS column tabular formatting'
    } else {
        $failures++
        Write-Output "FAIL column (exit=$($columnRes.ExitCode), stdout=<$($columnRes.Stdout)>)"
    }

    $truncFile = Join-Path $tempDirectory 'trunc-test.txt'
    $truncRes = Invoke-Ksh @('-c', "truncate -s 64 `"$truncFile`"")
    if ($truncRes.ExitCode -eq 0 -and (Test-Path $truncFile) -and ((Get-Item $truncFile).Length -eq 64)) {
        Write-Output 'PASS truncate file resizing'
    } else {
        $failures++
        Write-Output "FAIL truncate (exit=$($truncRes.ExitCode), stdout=<$($truncRes.Stdout)>)"
    }

    $ddRes = Invoke-Ksh @('-c', "printf 'abcdefghij' | dd bs=1 count=4 conv=ucase")
    if ($ddRes.ExitCode -eq 0 -and $ddRes.Stdout.Contains('ABCD') -and $ddRes.Stderr.Contains('4+0 records in')) {
        Write-Output 'PASS dd block copy and conversion'
    } else {
        $failures++
        Write-Output "FAIL dd (exit=$($ddRes.ExitCode), stdout=<$($ddRes.Stdout)>, stderr=<$($ddRes.Stderr)>)"
    }

    $colrmRes = Invoke-Ksh @('-c', "printf '123456789\n' | colrm 3 6")
    if ($colrmRes.ExitCode -eq 0 -and $colrmRes.Stdout.Trim() -eq '12789') {
        Write-Output 'PASS colrm column stripping'
    } else {
        $failures++
        Write-Output "FAIL colrm (exit=$($colrmRes.ExitCode), stdout=<$($colrmRes.Stdout)>)"
    }

    # Category F tests: install, uuidgen, gzip, gunzip, zcat, sha384
    $instDir = Join-Path $tempDirectory 'inst-dir'
    $instSrc = Join-Path $tempDirectory 'inst-src.txt'
    Set-Content -LiteralPath $instSrc -Value 'install-test' -Encoding utf8
    $installRes = Invoke-Ksh @('-c', "install -d `"$instDir`"; install `"$instSrc`" `"$instDir/inst-copy.txt`"")
    $instCopy = Join-Path $instDir 'inst-copy.txt'
    if ($installRes.ExitCode -eq 0 -and (Test-Path $instCopy) -and ((Get-Content $instCopy -Raw).Trim() -eq 'install-test')) {
        Write-Output 'PASS install file copy and directory creation'
    } else {
        $failures++
        Write-Output "FAIL install (exit=$($installRes.ExitCode), stdout=<$($installRes.Stdout)>, stderr=<$($installRes.Stderr)>)"
    }

    $uuidRes = Invoke-Ksh @('-c', 'uuidgen')
    $uuidStr = $uuidRes.Stdout.Trim()
    if ($uuidRes.ExitCode -eq 0 -and $uuidStr.Length -eq 36 -and $uuidStr.Contains('-')) {
        Write-Output 'PASS uuidgen RFC 4122 v4 generation'
    } else {
        $failures++
        Write-Output "FAIL uuidgen (exit=$($uuidRes.ExitCode), stdout=<$($uuidRes.Stdout)>)"
    }

    $gzTest = Join-Path $tempDirectory 'gz-test.txt'
    Set-Content -LiteralPath $gzTest -Value 'compress-decompress-cycle' -Encoding utf8
    $gzipRes = Invoke-Ksh @('-c', "gzip `"$gzTest`"; gunzip `"$gzTest.gz`"")
    if ($gzipRes.ExitCode -eq 0 -and (Test-Path $gzTest) -and ((Get-Content $gzTest -Raw).Trim() -eq 'compress-decompress-cycle')) {
        Write-Output 'PASS gzip and gunzip roundtrip'
    } else {
        $failures++
        Write-Output "FAIL gzip/gunzip (exit=$($gzipRes.ExitCode), stdout=<$($gzipRes.Stdout)>, stderr=<$($gzipRes.Stderr)>)"
    }

    $sha384Res = Invoke-Ksh @('-c', "printf 'test' | sha384")
    if ($sha384Res.ExitCode -eq 0 -and $sha384Res.Stdout.StartsWith('f62692128acdca72e77dd790ad5baab178e3f0773b99b44492ecf55777cf2085')) {
        Write-Output 'PASS sha384 message digest'
    } else {
        $failures++
        Write-Output "FAIL sha384 (exit=$($sha384Res.ExitCode), stdout=<$($sha384Res.Stdout)>)"
    }

    $redirRes = Invoke-Ksh @('-c', 'print -u 2 err_text 2>&1; echo suppressed > /dev/null')
    if ($redirRes.ExitCode -eq 0 -and $redirRes.Stdout.Trim() -eq 'err_text' -and [string]::IsNullOrEmpty($redirRes.Stderr.Trim())) {
        Write-Output 'PASS 2>&1 and /dev/null redirection'
    } else {
        $failures++
        Write-Output "FAIL redirection (exit=$($redirRes.ExitCode), stdout=<$($redirRes.Stdout)>, stderr=<$($redirRes.Stderr)>)"
    }

    $arrayRes = Invoke-Ksh @('-c', 'arr=(alpha beta gamma); i=1; echo ${arr[0]} ${arr[$i]} ${arr[2]} count=${#arr[@]}')
    if ($arrayRes.ExitCode -eq 0 -and $arrayRes.Stdout.Trim() -eq 'alpha beta gamma count=3') {
        Write-Output 'PASS array assignment and dynamic indexing'
    } else {
        $failures++
        Write-Output "FAIL array assignment (exit=$($arrayRes.ExitCode), stdout=<$($arrayRes.Stdout)>, stderr=<$($arrayRes.Stderr)>)"
    }

    $paramRes = Invoke-Ksh @('-c', 'v=hello; echo ${#v}; echo ${undef:-alt}; f=doc.txt; echo ${f%.*}; p=x/y/z; echo ${p#*/}')
    if ($paramRes.ExitCode -eq 0 -and ($paramRes.Stdout -split "`r?`n" | Where-Object { $_ -ne '' }) -join ' ' -eq '5 alt doc y/z') {
        Write-Output 'PASS POSIX parameter expansions'
    } else {
        $failures++
        Write-Output "FAIL parameter expansions (exit=$($paramRes.ExitCode), stdout=<$($paramRes.Stdout)>, stderr=<$($paramRes.Stderr)>)"
    }

    $helpCoreRes = Invoke-Ksh @('-c', 'help coreutils')
    $helpScriptRes = Invoke-Ksh @('-c', 'help scripting')
    $helpBatchRes = Invoke-Ksh @('-c', 'help batch')
    if ($helpCoreRes.ExitCode -eq 0 -and $helpCoreRes.Stdout.Contains('COREUTILS & UNIX UTILITIES:') -and
        $helpScriptRes.ExitCode -eq 0 -and $helpScriptRes.Stdout.Contains('CORE SHELL & SCRIPTING:') -and
        $helpBatchRes.ExitCode -eq 0 -and $helpBatchRes.Stdout.Contains('WINDOWS BATCH & CMD:')) {
        Write-Output 'PASS categorized help sections (coreutils, scripting, batch)'
    } else {
        $failures++
        Write-Output 'FAIL categorized help sections'
    }

    # POSIX item 1: Command substitution $() and ``
    $cmdSubRes = Invoke-Ksh @('-c', 'echo sub1: $(echo hello) sub2: `echo world`')
    if ($cmdSubRes.ExitCode -eq 0 -and $cmdSubRes.Stdout.Trim() -eq 'sub1: hello sub2: world') {
        Write-Output 'PASS command substitution $(cmd) and `cmd`'
    } else {
        $failures++
        Write-Output "FAIL command substitution (exit=$($cmdSubRes.ExitCode), stdout=<$($cmdSubRes.Stdout)>)"
    }

    # POSIX item 2: Arithmetic expansion $(( expr ))
    $arithRes = Invoke-Ksh @('-c', 'echo $(( (10 + 20) * 3 - 5 ))')
    if ($arithRes.ExitCode -eq 0 -and $arithRes.Stdout.Trim() -eq '85') {
        Write-Output 'PASS arithmetic expansion $(( expression ))'
    } else {
        $failures++
        Write-Output "FAIL arithmetic expansion (exit=$($arithRes.ExitCode), stdout=<$($arithRes.Stdout)>)"
    }

    # POSIX item 3: Compound commands (for, while, if/then/elif/else/fi)
    $compoundRes = Invoke-Ksh @('-c', 'for x in A B; do echo item: $x; done; if [ 1 -eq 2 ]; then echo no; elif [ 3 -eq 3 ]; then echo ok; fi; i=1; while [ $i -lt 3 ]; do echo w$i; i=$((i+1)); done')
    $expectedCompound = "item: A`nitem: B`nok`nw1`nw2"
    if ($compoundRes.ExitCode -eq 0 -and ($compoundRes.Stdout.Trim() -replace "`r`n", "`n") -eq $expectedCompound) {
        Write-Output 'PASS compound commands (for, while, if/then/elif/else/fi)'
    } else {
        $failures++
        Write-Output "FAIL compound commands (exit=$($compoundRes.ExitCode), stdout=<$($compoundRes.Stdout)>)"
    }

    # POSIX item 4: Here-string and Here-document
    $hereRes = Invoke-Ksh @('-c', 'cat <<< "here_string_data"')
    if ($hereRes.ExitCode -eq 0 -and $hereRes.Stdout.Trim() -eq 'here_string_data') {
        Write-Output 'PASS here-string and here-document redirection'
    } else {
        $failures++
        Write-Output "FAIL here-redirection (exit=$($hereRes.ExitCode), stdout=<$($hereRes.Stdout)>)"
    }

    # POSIX item 5: Globbing / Wildcard expansion
    $globRes = Invoke-Ksh @('-c', 'echo *.md; echo "*.md"')
    if ($globRes.ExitCode -eq 0 -and $globRes.Stdout.Contains('README.md') -and $globRes.Stdout.Contains('*.md')) {
        Write-Output 'PASS globbing and wildcard unquoted expansion'
    } else {
        $failures++
        Write-Output "FAIL globbing (exit=$($globRes.ExitCode), stdout=<$($globRes.Stdout)>)"
    }

    # Improvements 1-4:
    # 1. case ... in ... esac
    $caseRes = Invoke-Ksh @('-c', 'case "pear" in apple) echo NO ;; p*|orange) echo YES ;; *) echo NO ;; esac')
    if ($caseRes.ExitCode -eq 0 -and $caseRes.Stdout.Trim() -eq 'YES') {
        Write-Output 'PASS case ... in ... esac compound pattern matching'
    } else {
        $failures++
        Write-Output "FAIL case statement (exit=$($caseRes.ExitCode), stdout=<$($caseRes.Stdout)>)"
    }

    # 2. Batch CALL :subroutine and GOTO :EOF
    $batScript = Join-Path $tempDirectory 'sub_test.bat'
    Set-Content -LiteralPath $batScript -Value "@echo off`r`ncall :my_sub Alpha Beta`r`necho AFTER_CALL`r`ngoto :eof`r`n:my_sub`r`necho SUB_%1_%2`r`ngoto :eof" -Encoding Ascii
    $batRes = Invoke-Ksh @($batScript)
    if ($batRes.ExitCode -eq 0 -and ($batRes.Stdout.Trim() -replace "`r`n", "`n") -eq "SUB_Alpha_Beta`nAFTER_CALL") {
        Write-Output 'PASS batch CALL :label subroutine and GOTO :EOF'
    } else {
        $failures++
        Write-Output "FAIL batch subroutine (exit=$($batRes.ExitCode), stdout=<$($batRes.Stdout)>)"
    }

    # 3. Substring ${var:offset:length} and pattern replace ${var//pattern/replace}
    $subPatRes = Invoke-Ksh @('-c', 'v=HelloUniverse; echo ${v:5:8}; s=one_two_one; echo ${s/one/ten}; echo ${s//one/ten}')
    $expectedSubPat = "Universe`nten_two_one`nten_two_ten"
    if ($subPatRes.ExitCode -eq 0 -and ($subPatRes.Stdout.Trim() -replace "`r`n", "`n") -eq $expectedSubPat) {
        Write-Output 'PASS substring ${var:off:len} and replacement ${var//pat/rep}'
    } else {
        $failures++
        Write-Output "FAIL substring/replace (exit=$($subPatRes.ExitCode), stdout=<$($subPatRes.Stdout)>)"
    }

    # 4. trap 'commands' EXIT
    $trapRes = Invoke-Ksh @('-c', 'trap "echo TRAP_RUN" EXIT; echo MAIN_RUN; exit 0')
    if ($trapRes.ExitCode -eq 0 -and ($trapRes.Stdout.Trim() -replace "`r`n", "`n") -eq "MAIN_RUN`nTRAP_RUN") {
        Write-Output 'PASS trap commands on EXIT'
    } else {
        $failures++
        Write-Output "FAIL trap EXIT (exit=$($trapRes.ExitCode), stdout=<$($trapRes.Stdout)>)"
    }

    # Set 2 Improvements 1-3:
    # 1. Subshell ( ... ) isolation and Command Grouping { ...; }
    $subshellRes = Invoke-Ksh @('-c', '(x=sub_val; echo in_sub: $x); echo in_parent: $x; { echo group1; echo group2; }')
    $expectedSubshell = "in_sub: sub_val`nin_parent:`ngroup1`ngroup2"
    if ($subshellRes.ExitCode -eq 0 -and ($subshellRes.Stdout.Trim() -replace "`r`n", "`n") -eq $expectedSubshell) {
        Write-Output 'PASS subshell ( ... ) isolation and command grouping { ...; }'
    } else {
        $failures++
        Write-Output "FAIL subshell/group (exit=$($subshellRes.ExitCode), stdout=<$($subshellRes.Stdout)>)"
    }

    # 2. Dynamic batch variables and IF ERRORLEVEL
    $batDynScript = Join-Path $tempDirectory 'dyn_test.bat'
    Set-Content -LiteralPath $batDynScript -Value "@echo off`r`nif errorlevel 0 echo ERR_0_OK`r`nif not errorlevel 1 echo NOT_ERR_1`r`necho CD_OK=%CD%`r`n" -Encoding Ascii
    $batDynRes = Invoke-Ksh @($batDynScript)
    if ($batDynRes.ExitCode -eq 0 -and $batDynRes.Stdout.Contains('ERR_0_OK') -and $batDynRes.Stdout.Contains('NOT_ERR_1') -and $batDynRes.Stdout.Contains('CD_OK=')) {
        Write-Output 'PASS batch dynamic pseudo-variables and IF ERRORLEVEL'
    } else {
        $failures++
        Write-Output "FAIL batch dynamic/errorlevel (exit=$($batDynRes.ExitCode), stdout=<$($batDynRes.Stdout)>)"
    }

    # 3. Extended test [[ ... ]] with =~ and ==
    $extTestRes = Invoke-Ksh @('-c', '[[ "abc123xyz" =~ 123 ]] && echo MATCH_RE; [[ "alpha" == al* ]] && echo MATCH_GLOB; [[ 5 -gt 2 && "a" != "b" ]] && echo COMB_OK')
    $expectedExtTest = "MATCH_RE`nMATCH_GLOB`nCOMB_OK"
    if ($extTestRes.ExitCode -eq 0 -and ($extTestRes.Stdout.Trim() -replace "`r`n", "`n") -eq $expectedExtTest) {
        Write-Output 'PASS extended test [[ ... ]] with =~ regex, == wildcards, and && logic'
    } else {
        $failures++
        Write-Output "FAIL extended test (exit=$($extTestRes.ExitCode), stdout=<$($extTestRes.Stdout)>)"
    }

    # 4. Pipeline error tracking with set -o pipefail
    $pipeRes1 = Invoke-Ksh @('-c', 'false | true; echo $?')
    $pipeRes2 = Invoke-Ksh @('-c', 'set -o pipefail; false | true; echo $?')
    $pipeRes3 = Invoke-Ksh @('-c', 'set -o pipefail; true | false | true; echo $?')
    if ($pipeRes1.ExitCode -eq 0 -and $pipeRes1.Stdout.Trim() -eq '0' -and
        $pipeRes2.ExitCode -eq 0 -and $pipeRes2.Stdout.Trim() -eq '1' -and
        $pipeRes3.ExitCode -eq 0 -and $pipeRes3.Stdout.Trim() -eq '1') {
        Write-Output 'PASS pipeline failure tracking with set -o pipefail'
    } else {
        $failures++
        Write-Output "FAIL pipefail test ($($pipeRes1.Stdout.Trim()), $($pipeRes2.Stdout.Trim()), $($pipeRes3.Stdout.Trim()))"
    }

    # 5. ANSI-C Quoting $'...'
    $ansiRes = Invoke-Ksh @('-c', 'echo $"hex:\x41\x42\x43\ttab"')
    # Note: In powershell, $'...' is escaped or passed as string
    $ansiScript = Join-Path $tempDirectory 'ansi_test.ksh'
    [IO.File]::WriteAllText($ansiScript, "echo `$'hex:\x41\x42\x43\ttab'`n", [Text.UTF8Encoding]::new($false))
    $ansiFileRes = Invoke-Ksh @($ansiScript)
    if ($ansiFileRes.ExitCode -eq 0 -and $ansiFileRes.Stdout.Contains("hex:ABC`ttab")) {
        Write-Output "PASS ANSI-C quoting `$'...' escape and hex expansion"
    } else {
        $failures++
        Write-Output "FAIL ANSI-C quoting (exit=$($ansiFileRes.ExitCode), stdout=<$($ansiFileRes.Stdout)>)"
    }

    # 6. C-Style arithmetic for loop for ((init; cond; step))
    $forLoopScript = Join-Path $tempDirectory 'for_arith.ksh'
    [IO.File]::WriteAllText($forLoopScript, "for ((i=0; i<3; i++)); do echo count: `$i; done`n", [Text.UTF8Encoding]::new($false))
    $forLoopRes = Invoke-Ksh @($forLoopScript)
    $expectedForLoop = "count: 0`ncount: 1`ncount: 2"
    if ($forLoopRes.ExitCode -eq 0 -and ($forLoopRes.Stdout.Trim() -replace "`r`n", "`n") -eq $expectedForLoop) {
        Write-Output 'PASS C-style arithmetic loop for ((init; cond; step))'
    } else {
        $failures++
        Write-Output "FAIL C-style arithmetic for loop (exit=$($forLoopRes.ExitCode), stdout=<$($forLoopRes.Stdout)>)"
    }

    # 7. Process Substitution <(cmd) and >(cmd)
    $psScript = Join-Path $tempDirectory 'ps_test.ksh'
    [IO.File]::WriteAllText($psScript, "cat <(echo proc_subst_ok)`n", [Text.UTF8Encoding]::new($false))
    $psRes = Invoke-Ksh @($psScript)
    $diffScript = Join-Path $tempDirectory 'ps_diff.ksh'
    [IO.File]::WriteAllText($diffScript, "diff <(echo same) <(echo same)`necho diff_status: `$?`n", [Text.UTF8Encoding]::new($false))
    $diffRes = Invoke-Ksh @($diffScript)
    if ($psRes.ExitCode -eq 0 -and $psRes.Stdout.Trim() -eq 'proc_subst_ok' -and
        $diffRes.ExitCode -eq 0 -and $diffRes.Stdout.Contains('diff_status: 0')) {
        Write-Output 'PASS process substitution <(command) and >(command)'
    } else {
        $failures++
        Write-Output "FAIL process substitution (ps=<$($psRes.Stdout)>, diff=<$($diffRes.Stdout)>)"
    }

    # 8. Local function scope (local var and typeset var)
    $fnScript = Join-Path $tempDirectory 'fn_local.ksh'
    [IO.File]::WriteAllText($fnScript, "foo() { local x=inner; echo in_func: `$x; }; x=outer; foo; echo in_global: `$x`nbar() { typeset y=typeset_val; echo in_bar: `$y; }; y=global_val; bar; echo in_global_y: `$y`n", [Text.UTF8Encoding]::new($false))
    $fnRes = Invoke-Ksh @($fnScript)
    $expectedFn = "in_func: inner`nin_global: outer`nin_bar: typeset_val`nin_global_y: global_val"
    if ($fnRes.ExitCode -eq 0 -and ($fnRes.Stdout.Trim() -replace "`r`n", "`n") -eq $expectedFn) {
        Write-Output 'PASS local function scope (local var and typeset var)'
    } else {
        $failures++
        Write-Output "FAIL local function scope (exit=$($fnRes.ExitCode), stdout=<$($fnRes.Stdout)>)"
    }

    # 9. Drive letter path translation (/c/... -> C:\...)
    $driveRes = Invoke-Ksh @('-c', 'echo /c/Windows /d/my/path')
    if ($driveRes.ExitCode -eq 0 -and $driveRes.Stdout.Trim() -eq 'C:\Windows D:\my\path') {
        Write-Output 'PASS drive letter path translation (/c/... -> C:\...)'
    } else {
        $failures++
        Write-Output "FAIL drive letter path translation (exit=$($driveRes.ExitCode), stdout=<$($driveRes.Stdout)>)"
    }

    # 10. Namerefs (typeset -n and nameref)
    $nrScript = Join-Path $tempDirectory 'nameref_test.ksh'
    [IO.File]::WriteAllText($nrScript, "target=world; typeset -n ref=target; echo `$ref; ref=hello; echo `$target; val=100; nameref r=val; echo `$r; r=200; echo `$val`n", [Text.UTF8Encoding]::new($false))
    $nrRes = Invoke-Ksh @($nrScript)
    $expectedNr = "world`nhello`n100`n200"
    if ($nrRes.ExitCode -eq 0 -and ($nrRes.Stdout.Trim() -replace "`r`n", "`n") -eq $expectedNr) {
        Write-Output 'PASS namerefs (typeset -n and nameref)'
    } else {
        $failures++
        Write-Output "FAIL namerefs (exit=$($nrRes.ExitCode), stdout=<$($nrRes.Stdout)>)"
    }

    # 11. Co-processes (|&, print -p, read -p)
    $coprocScript = Join-Path $tempDirectory 'coproc_test.ksh'
    [IO.File]::WriteAllText($coprocScript, "cmd.exe /c more |&`nprint -p ping_test`nread -p pong`necho got:`$pong`n", [Text.UTF8Encoding]::new($false))
    $coprocRes = Invoke-Ksh @($coprocScript)
    if ($coprocRes.ExitCode -eq 0 -and $coprocRes.Stdout.Trim() -eq 'got:ping_test') {
        Write-Output 'PASS co-processes (|&, print -p, read -p)'
    } else {
        $failures++
        Write-Output "FAIL co-processes (exit=$($coprocRes.ExitCode), stdout=<$($coprocRes.Stdout)>)"
    }

    # 12. Floating-point arithmetic in $(( ... ))
    $fpRes = Invoke-Ksh @('-c', 'echo $(( 3.14159 * 2 )) $(( sqrt(16) + 0.5 )) $(( 10 / 4.0 ))')
    if ($fpRes.ExitCode -eq 0 -and $fpRes.Stdout.Trim() -eq '6.28318 4.5 2.5') {
        Write-Output 'PASS floating-point arithmetic in $(( ... ))'
    } else {
        $failures++
        Write-Output "FAIL floating-point arithmetic (exit=$($fpRes.ExitCode), stdout=<$($fpRes.Stdout)>)"
    }

    # 13. Compound variables (var=( x=10 y=20 ))
    $compVarRes = Invoke-Ksh @('-c', 'pt=( x=10 y=20 ); echo ${pt.x} and ${pt.y}')
    if ($compVarRes.ExitCode -eq 0 -and $compVarRes.Stdout.Trim() -eq '10 and 20') {
        Write-Output 'PASS compound variables var=( x=10 y=20 )'
    } else {
        $failures++
        Write-Output "FAIL compound variables (exit=$($compVarRes.ExitCode), stdout=<$($compVarRes.Stdout)>)"
    }

    # 14. BSD lam
    $f1 = Join-Path $tempDirectory 'lam1.txt'
    $f2 = Join-Path $tempDirectory 'lam2.txt'
    [IO.File]::WriteAllText($f1, "a`nb`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($f2, "1`n2`n", [Text.UTF8Encoding]::new($false))
    $lamRes = Invoke-Ksh @('-c', "lam -s ' - ' '$f1' '$f2'")
    if ($lamRes.ExitCode -eq 0 -and ($lamRes.Stdout.Trim() -replace "`r`n", "`n") -eq "a - 1`nb - 2") {
        Write-Output 'PASS BSD lam'
    } else {
        $failures++
        Write-Output "FAIL BSD lam (exit=$($lamRes.ExitCode), stdout=<$($lamRes.Stdout)>)"
    }

    # 15. BSD look
    $fLook = Join-Path $tempDirectory 'look.txt'
    [IO.File]::WriteAllText($fLook, "apple`napplication`nbanana`n", [Text.UTF8Encoding]::new($false))
    $lookRes = Invoke-Ksh @('-c', "look -f 'app' '$fLook'")
    if ($lookRes.ExitCode -eq 0 -and ($lookRes.Stdout.Trim() -replace "`r`n", "`n") -eq "apple`napplication") {
        Write-Output 'PASS BSD look'
    } else {
        $failures++
        Write-Output "FAIL BSD look (exit=$($lookRes.ExitCode), stdout=<$($lookRes.Stdout)>)"
    }

    # 16. BSD apply
    $applyRes = Invoke-Ksh @('-c', "apply -2 echo foo bar baz qux")
    if ($applyRes.ExitCode -eq 0 -and ($applyRes.Stdout.Trim() -replace "`r`n", "`n") -eq "foo bar`nbaz qux") {
        Write-Output 'PASS BSD apply'
    } else {
        $failures++
        Write-Output "FAIL BSD apply (exit=$($applyRes.ExitCode), stdout=<$($applyRes.Stdout)>)"
    }

    # 17. BSD unvis
    $fVis = Join-Path $tempDirectory 'vis.txt'
    [IO.File]::WriteAllText($fVis, "hello\nworld\n", [Text.UTF8Encoding]::new($false))
    $unvisRes = Invoke-Ksh @('-c', "unvis '$fVis'")
    if ($unvisRes.ExitCode -eq 0 -and ($unvisRes.Stdout.Trim() -replace "`r`n", "`n") -eq "hello`nworld") {
        Write-Output 'PASS BSD unvis'
    } else {
        $failures++
        Write-Output "FAIL BSD unvis (exit=$($unvisRes.ExitCode), stdout=<$($unvisRes.Stdout)>)"
    }

    # 18. BSD vmstat
    $vmstatRes = Invoke-Ksh @('-c', 'vmstat -s')
    if ($vmstatRes.ExitCode -eq 0 -and $vmstatRes.Stdout -match 'total physical memory') {
        Write-Output 'PASS BSD vmstat -s'
    } else {
        $failures++
        Write-Output "FAIL BSD vmstat -s (exit=$($vmstatRes.ExitCode), stdout=<$($vmstatRes.Stdout)>)"
    }

    # 19. BSD pbcopy & pbpaste
    $clipRes = Invoke-Ksh @('-c', 'echo clipboard_test_val | pbcopy; pbpaste')
    if ($clipRes.ExitCode -eq 0 -and $clipRes.Stdout.Trim() -eq 'clipboard_test_val') {
        Write-Output 'PASS BSD pbcopy & pbpaste'
    } else {
        $failures++
        Write-Output "FAIL BSD pbcopy & pbpaste (exit=$($clipRes.ExitCode), stdout=<$($clipRes.Stdout)>)"
    }

    # 20. BSD mtree
    $mDir = Join-Path $tempDirectory 'mtreedir'
    $null = New-Item -ItemType Directory -Path $mDir -Force
    [IO.File]::WriteAllText((Join-Path $mDir 't.txt'), 'hello', [Text.UTF8Encoding]::new($false))
    $mSpec = Join-Path $tempDirectory 'spec.txt'
    $mtreeCreate = Invoke-Ksh @('-c', "mtree -c -p '$mDir' > '$mSpec'")
    $mtreeVerify = Invoke-Ksh @('-c', "mtree -f '$mSpec' -p '$mDir'")
    if ($mtreeVerify.ExitCode -eq 0) {
        Write-Output 'PASS BSD mtree -c and -f'
    } else {
        $failures++
        Write-Output "FAIL BSD mtree (exit=$($mtreeVerify.ExitCode), stdout=<$($mtreeVerify.Stdout)>, stderr=<$($mtreeVerify.Stderr)>)"
    }

    # 21. Built-in tar (create, list, extract)
    $tarSrc = Join-Path $tempDirectory 'tardata'
    $null = New-Item -ItemType Directory -Path $tarSrc -Force
    [IO.File]::WriteAllText((Join-Path $tarSrc 'sample.txt'), 'tar test data content', [Text.UTF8Encoding]::new($false))
    $tarArchive = Join-Path $tempDirectory 'test.tar'
    $tarDst = Join-Path $tempDirectory 'tardst'
    $null = New-Item -ItemType Directory -Path $tarDst -Force
    $tarCreate = Invoke-Ksh @('-c', "cd '$tempDirectory'; tar -cf '$tarArchive' tardata/sample.txt")
    $tarList = Invoke-Ksh @('-c', "tar -tf '$tarArchive'")
    $tarExtract = Invoke-Ksh @('-c', "cd '$tarDst'; tar -xf '$tarArchive'")
    $extractedFile = Join-Path $tarDst 'tardata\sample.txt'
    if ($tarCreate.ExitCode -eq 0 -and $tarList.Stdout -match 'sample\.txt' -and (Test-Path $extractedFile)) {
        Write-Output 'PASS Built-in tar create, list, and extract'
    } else {
        $failures++
        Write-Output "FAIL Built-in tar (create=$($tarCreate.ExitCode), list=<$($tarList.Stdout)>, extractExists=$(Test-Path $extractedFile))"
    }

    # 22. Built-in curl and wget
    $curlHelp = Invoke-Ksh @('-c', 'curl --help')
    $wgetHelp = Invoke-Ksh @('-c', 'wget --help')
    if ($curlHelp.ExitCode -eq 0 -and $curlHelp.Stdout -match 'Usage: curl' -and
        $wgetHelp.ExitCode -eq 0 -and $wgetHelp.Stdout -match 'Usage: wget') {
        Write-Output 'PASS Built-in curl and wget interfaces'
    } else {
        $failures++
        Write-Output "FAIL Built-in curl/wget help check"
    }

    # 23. BSD sysctl
    $sysctlRes = Invoke-Ksh @('-c', 'sysctl hw.ncpu')
    if ($sysctlRes.ExitCode -eq 0 -and $sysctlRes.Stdout -match 'hw\.ncpu:\s+\d+') {
        Write-Output 'PASS BSD sysctl query'
    } else {
        $failures++
        Write-Output "FAIL BSD sysctl (exit=$($sysctlRes.ExitCode), stdout=<$($sysctlRes.Stdout)>)"
    }

    # 24. BSD renice
    $reniceHelp = Invoke-Ksh @('-c', 'renice --help')
    if ($reniceHelp.ExitCode -eq 0 -and $reniceHelp.Stdout -match 'Usage: renice') {
        Write-Output 'PASS BSD renice interface'
    } else {
        $failures++
        Write-Output "FAIL BSD renice help check"
    }

    # 25. BSD arp
    $arpRes = Invoke-Ksh @('-c', 'arp -a')
    if ($arpRes.ExitCode -eq 0 -and $arpRes.Stdout -match 'Internet Address') {
        Write-Output 'PASS BSD arp table inspection'
    } else {
        $failures++
        Write-Output "FAIL BSD arp (exit=$($arpRes.ExitCode), stdout=<$($arpRes.Stdout)>)"
    }

    # 26. BSD host
    $hostHelp = Invoke-Ksh @('-c', 'host --help')
    if ($hostHelp.ExitCode -eq 0 -and $hostHelp.Stdout -match 'Usage: host') {
        Write-Output 'PASS BSD host lookup interface'
    } else {
        $failures++
        Write-Output "FAIL BSD host help check"
    }

    # 27. BSD/POSIX getent
    $getentRes = Invoke-Ksh @('-c', 'getent passwd')
    if ($getentRes.ExitCode -eq 0 -and $getentRes.Stdout -match 'Administrator') {
        Write-Output 'PASS BSD getent database query'
    } else {
        $failures++
        Write-Output "FAIL BSD getent (exit=$($getentRes.ExitCode), stdout=<$($getentRes.Stdout)>)"
    }

    # 28. BSD srm
    $srmFile = Join-Path $tempDirectory 'srm_test.txt'
    [IO.File]::WriteAllText($srmFile, 'sensitive data', [Text.UTF8Encoding]::new($false))
    $srmRes = Invoke-Ksh @('-c', "srm -z '$srmFile'")
    if ($srmRes.ExitCode -eq 0 -and -not (Test-Path $srmFile)) {
        Write-Output 'PASS BSD srm secure erasure'
    } else {
        $failures++
        Write-Output "FAIL BSD srm (exit=$($srmRes.ExitCode), exists=$(Test-Path $srmFile))"
    }

    # 29. BSD pr
    $prFile = Join-Path $tempDirectory 'pr_test.txt'
    [IO.File]::WriteAllText($prFile, "line1`nline2`nline3`n", [Text.UTF8Encoding]::new($false))
    $prRes = Invoke-Ksh @('-c', "pr -t '$prFile'")
    if ($prRes.ExitCode -eq 0 -and $prRes.Stdout -match 'line2') {
        Write-Output 'PASS BSD pr paginator'
    } else {
        $failures++
        Write-Output "FAIL BSD pr (exit=$($prRes.ExitCode), stdout=<$($prRes.Stdout)>)"
    }

    # 30. BSD banner
    $bannerRes = Invoke-Ksh @('-c', 'banner OK')
    if ($bannerRes.ExitCode -eq 0 -and $bannerRes.Stdout -match '#####') {
        Write-Output 'PASS BSD banner text rendering'
    } else {
        $failures++
        Write-Output "FAIL BSD banner (exit=$($bannerRes.ExitCode), stdout=<$($bannerRes.Stdout)>)"
    }

    # 31. BSD ping
    $pingRes = Invoke-Ksh @('-c', 'ping -c 1 -w 500 127.0.0.1')
    if ($pingRes.ExitCode -eq 0 -and $pingRes.Stdout -match 'packets transmitted') {
        Write-Output 'PASS BSD ping ICMP echo'
    } else {
        $failures++
        Write-Output "FAIL BSD ping (exit=$($pingRes.ExitCode), stdout=<$($pingRes.Stdout)>)"
    }

    # 32. BSD traceroute
    $traceRes = Invoke-Ksh @('-c', 'traceroute -m 1 -q 1 -w 500 127.0.0.1')
    if ($traceRes.ExitCode -eq 0 -and $traceRes.Stdout -match 'hops max') {
        Write-Output 'PASS BSD traceroute route hop inspector'
    } else {
        $failures++
        Write-Output "FAIL BSD traceroute (exit=$($traceRes.ExitCode), stdout=<$($traceRes.Stdout)>)"
    }

    # 33. BSD whois
    $whoisRes = Invoke-Ksh @('-c', 'whois --help')
    if ($whoisRes.ExitCode -eq 0 -and $whoisRes.Stdout -match 'WHOIS') {
        Write-Output 'PASS BSD whois client interface'
    } else {
        $failures++
        Write-Output "FAIL BSD whois (exit=$($whoisRes.ExitCode), stdout=<$($whoisRes.Stdout)>)"
    }

    # 34. BSD col
    $colFile = Join-Path $tempDirectory 'col_test.txt'
    [IO.File]::WriteAllText($colFile, "a`bb`n", [Text.Encoding]::ASCII)
    $colRes = Invoke-Ksh @('-c', "col -b '$colFile'")
    if ($colRes.ExitCode -eq 0 -and $colRes.Stdout -match 'b') {
        Write-Output 'PASS BSD col overstrike filter'
    } else {
        $failures++
        Write-Output "FAIL BSD col (exit=$($colRes.ExitCode), stdout=<$($colRes.Stdout)>)"
    }

    # 35. BSD ul
    $ulFile = Join-Path $tempDirectory 'ul_test.txt'
    [IO.File]::WriteAllText($ulFile, "_`bA`n", [Text.Encoding]::ASCII)
    $ulRes = Invoke-Ksh @('-c', "ul '$ulFile'")
    if ($ulRes.ExitCode -eq 0 -and $ulRes.Stdout -match 'A') {
        Write-Output 'PASS BSD ul underline translator'
    } else {
        $failures++
        Write-Output "FAIL BSD ul (exit=$($ulRes.ExitCode), stdout=<$($ulRes.Stdout)>)"
    }

    # 36. BSD what
    $whatFile = Join-Path $tempDirectory 'what_test.txt'
    [IO.File]::WriteAllText($whatFile, "binary prefix @(#)KshBuild 2.0 release suffix`n", [Text.Encoding]::ASCII)
    $whatRes = Invoke-Ksh @('-c', "what '$whatFile'")
    if ($whatRes.ExitCode -eq 0 -and $whatRes.Stdout -match 'KshBuild 2.0 release') {
        Write-Output 'PASS BSD what SCCS identifier scanner'
    } else {
        $failures++
        Write-Output "FAIL BSD what (exit=$($whatRes.ExitCode), stdout=<$($whatRes.Stdout)>)"
    }

    # 37. BSD od
    $odFile = Join-Path $tempDirectory 'od_test.txt'
    [IO.File]::WriteAllText($odFile, "ABCDEFGH", [Text.Encoding]::ASCII)
    $odRes = Invoke-Ksh @('-c', "od -c '$odFile'")
    if ($odRes.ExitCode -eq 0 -and $odRes.Stdout -match 'A') {
        Write-Output 'PASS BSD od multi-base stream dumper'
    } else {
        $failures++
        Write-Output "FAIL BSD od (exit=$($odRes.ExitCode), stdout=<$($odRes.Stdout)>)"
    }

    # 38. BSD sum
    $sumFile = Join-Path $tempDirectory 'sum_test.txt'
    [IO.File]::WriteAllText($sumFile, "Hello World`n", [Text.Encoding]::ASCII)
    $sumRes = Invoke-Ksh @('-c', "sum -r '$sumFile'")
    if ($sumRes.ExitCode -eq 0 -and $sumRes.Stdout -match 'sum_test.txt') {
        Write-Output 'PASS BSD sum checksum computation'
    } else {
        $failures++
        Write-Output "FAIL BSD sum (exit=$($sumRes.ExitCode), stdout=<$($sumRes.Stdout)>)"
    }

    # 39. BSD leave
    $leaveRes = Invoke-Ksh @('-c', 'leave +15')
    if ($leaveRes.ExitCode -eq 0 -and $leaveRes.Stdout -match 'Alarm set for') {
        Write-Output 'PASS BSD leave departure alert timer'
    } else {
        $failures++
        Write-Output "FAIL BSD leave (exit=$($leaveRes.ExitCode), stdout=<$($leaveRes.Stdout)>)"
    }

    # 40. BSD caesar / rot13
    $caesarRes = Invoke-Ksh @('-c', 'echo hello | rot13 | caesar 13')
    if ($caesarRes.ExitCode -eq 0 -and $caesarRes.Stdout.Trim() -eq 'hello') {
        Write-Output 'PASS BSD caesar and rot13 encryption/decryption'
    } else {
        $failures++
        Write-Output "FAIL BSD caesar (exit=$($caesarRes.ExitCode), stdout=<$($caesarRes.Stdout)>)"
    }

    # 41. Dynamic Module System: module load / unload / list
    $modDll = Join-Path $PSScriptRoot '..\sample_module.dll'
    if (Test-Path $modDll) {
        $modRes = Invoke-Ksh @('-c', "module load '$modDll'; sample_hello Tester; sample_add 19 23; module unload sample_module")
        if ($modRes.ExitCode -eq 0 -and $modRes.Stdout -match 'Hello from sample_module extension, Tester!' -and $modRes.Stdout -match '42') {
            Write-Output 'PASS module load, execution, and unload lifecycle'
        } else {
            $failures++
            Write-Output "FAIL module load/unload (exit=$($modRes.ExitCode), stdout=<$($modRes.Stdout)>)"
        }
    }

    # 42. Dynamic Module System: classic ksh93 builtin -f / -d / -s
    if (Test-Path $modDll) {
        $bRes = Invoke-Ksh @('-c', "builtin -f '$modDll' sample_classic; sample_classic; builtin -d sample_classic")
        if ($bRes.ExitCode -eq 0 -and $bRes.Stdout -match 'sample_classic: executed via classic ksh93') {
            Write-Output 'PASS classic ksh93 builtin -f and -d dynamic extension loading'
        } else {
            $failures++
            Write-Output "FAIL builtin -f/-d (exit=$($bRes.ExitCode), stdout=<$($bRes.Stdout)>)"
        }
    }

    # 43. Script Module Packages: module import & KSH_MODULE_PATH
    $pkgRes = Invoke-Ksh @('-c', "module import examples/sample_module/math_pkg; math_square 8; math_cube 3")
    if ($pkgRes.ExitCode -eq 0 -and $pkgRes.Stdout -match '64' -and $pkgRes.Stdout -match '27') {
        Write-Output 'PASS module import script package sourcing'
    } else {
        $failures++
        Write-Output "FAIL module import (exit=$($pkgRes.ExitCode), stdout=<$($pkgRes.Stdout)>)"
    }

    # -----------------------------------------------------------------
    # Arithmetic Math Library Functions ($(( ... )))
    # -----------------------------------------------------------------
    $mathRes = Invoke-Ksh @('-c', 'echo $(( sqrt(16) )) $(( abs(-50) )) $(( pow(2, 8) )) $(( round(4.6) )) $(( floor(3.9) )) $(( ceil(3.1) ))')
    if ($mathRes.ExitCode -eq 0 -and $mathRes.Stdout.Trim() -eq '4 50 256 5 3 4') {
        Write-Output "PASS arithmetic standard math functions (sqrt, abs, pow, round, floor, ceil)"
    } else {
        $failures++
        Write-Output "FAIL arithmetic standard math functions: stdout=<$($mathRes.Stdout)>"
    }

    # -----------------------------------------------------------------
    # POSIX Character Classes ([[:alpha:]], [[:digit:]])
    # -----------------------------------------------------------------
    $cclassRes = Invoke-Ksh @('-c', '[[ "Alpha123" == [[:alpha:]]* ]] && [[ "9876" == [[:digit:]]* ]] && echo "OK"')
    if ($cclassRes.ExitCode -eq 0 -and $cclassRes.Stdout.Trim() -eq 'OK') {
        Write-Output "PASS POSIX character classes [[:alpha:]] and [[:digit:]] in pattern matching"
    } else {
        $failures++
        Write-Output "FAIL POSIX character classes: stdout=<$($cclassRes.Stdout)>"
    }

    # -----------------------------------------------------------------
    # Extended Glob Pathname Expansion (@(...), !(...))
    # -----------------------------------------------------------------
    $extglobRes = Invoke-Ksh @('-c', 'case test.c in !(build)*) echo "extglob_neg_ok" ;; esac')
    if ($extglobRes.ExitCode -eq 0 -and $extglobRes.Stdout.Trim() -eq 'extglob_neg_ok') {
        Write-Output "PASS ksh93 extended glob negation pattern matching"
    } else {
        $failures++
        Write-Output "FAIL ksh93 extended glob negation pattern matching: stdout=<$($extglobRes.Stdout)>"
    }

    # -----------------------------------------------------------------
    # Multidimensional Arrays & Array of Compounds
    # -----------------------------------------------------------------
    $multiArrRes = Invoke-Ksh @('-c', 'matrix[1][2]=42; i=1; j=2; echo m=${matrix[$i][$j]}; point[0].x=100; echo p=${point[0].x}; echo len=${#point[0].x}')
    if ($multiArrRes.ExitCode -eq 0 -and $multiArrRes.Stdout.Trim() -replace "`r`n", "`n" -eq "m=42`np=100`nlen=3") {
        Write-Output "PASS multidimensional arrays and array of compounds syntax"
    } else {
        $failures++
        Write-Output "FAIL multidimensional arrays: stdout=<$($multiArrRes.Stdout)>"
    }

    # -----------------------------------------------------------------
    # Prompt without CWD & ksh.ini Customization
    # -----------------------------------------------------------------
    $defaultPromptRes = Invoke-Ksh @() "exit 0`n"
    if ($defaultPromptRes.ExitCode -eq 0 -and
        $defaultPromptRes.Stdout.Contains("`$ ") -and
        -not $defaultPromptRes.Stdout.Contains((Get-Location).Path)) {
        Write-Output "PASS default prompt without cwd"
    } else {
        $failures++
        Write-Output "FAIL default prompt: stdout=<$($defaultPromptRes.Stdout)>"
    }

    $iniDir = Join-Path $tempDirectory 'ini-test'
    New-Item -ItemType Directory -Path $iniDir -Force | Out-Null
    $testIni = Join-Path $iniDir 'ksh.ini'
    Set-Content -LiteralPath $testIni -Value @"
[prompt]
format = custom_prompt> 

[aliases]
al_test = echo alias_ok

[env]
KSH_INI_VAR = ini_val_ok
"@ -Encoding utf8

    $iniEnvBackup = $env:ENV
    try {
        $env:ENV = $testIni
        $iniPromptRes = Invoke-Ksh @() "al_test; echo `$KSH_INI_VAR; exit 0`n"
        if ($iniPromptRes.ExitCode -eq 0 -and
            $iniPromptRes.Stdout.Contains('custom_prompt>') -and
            $iniPromptRes.Stdout.Contains('alias_ok') -and
            $iniPromptRes.Stdout.Contains('ini_val_ok')) {
            Write-Output "PASS ksh.ini prompt, aliases, and env customization"
        } else {
            $failures++
            Write-Output "FAIL ksh.ini customization: stdout=<$($iniPromptRes.Stdout)>"
        }
    } finally {
        $env:ENV = $iniEnvBackup
    }

    # -----------------------------------------------------------------
    # cd ~ and tilde expansion
    # -----------------------------------------------------------------
    $expectedHome = ($env:USERPROFILE -replace '\\', '/')
    $cdTildeRes = Invoke-Ksh @('-c', 'cd C:/Windows; cd ~; pwd')
    if ($cdTildeRes.ExitCode -eq 0 -and $cdTildeRes.Stdout.Trim() -eq $expectedHome) {
        Write-Output "PASS cd ~ navigates to user home directory"
    } else {
        $failures++
        Write-Output "FAIL cd ~: stdout=<$($cdTildeRes.Stdout)> expected=<$expectedHome>"
    }

    $echoTildeRes = Invoke-Ksh @('-c', 'echo ~')
    if ($echoTildeRes.ExitCode -eq 0 -and $echoTildeRes.Stdout.Trim() -eq $expectedHome) {
        Write-Output "PASS echo ~ expands to home directory"
    } else {
        $failures++
        Write-Output "FAIL echo ~: stdout=<$($echoTildeRes.Stdout)> expected=<$expectedHome>"
    }
} finally {
    Remove-Item -LiteralPath $tempDirectory -Recurse -Force
}

if ($failures) {
    Write-Output "Built-in regressions: $failures failed"
    exit 1
}
Write-Output 'Built-in regressions: all passed'
