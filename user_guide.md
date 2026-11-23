# KornShell (ksh) for Windows: Comprehensive User & Scripting Guide

Welcome to the definitive user and scripting manual for **ksh** (`ntksh`), a high-performance native KornShell and Unix coreutils environment engineered specifically for Microsoft Windows NT.

`ksh` delivers a unified automation environment on Windows. It pairs a **POSIX / KornShell (ksh93/mksh)** interpreter with a **native in-process Windows Batch (`.bat` / `.cmd`)** runtime and **over 100 built-in Unix and Windows utilities** in a single standalone binary without requiring POSIX emulation layers, Cygwin, or MSYS DLLs.

---

## Table of Contents

1. [Architecture & Execution Model](#1-architecture--execution-model)
2. [Getting Started & Shell Invocation](#2-getting-started--shell-invocation)
3. [POSIX & KornShell Scripting Guide](#3-posix--kornshell-scripting-guide)
   - [Shebang and Execution Modes](#shebang-and-execution-modes)
   - [Variables, Environment, and Attributes](#variables-environment-and-attributes)
   - [Parameter Expansion & String Manipulation](#parameter-expansion--string-manipulation)
   - [Arrays: Indexed and Associative](#arrays-indexed-and-associative)
   - [Positional Parameters & Argument Parsing](#positional-parameters--argument-parsing)
   - [Arithmetic Expressions](#arithmetic-expressions)
   - [Conditional Statements & Tests (`[` and `[[`)](#conditional-statements--tests--and-)
   - [Loop Constructs (`for`, `while`, `until`)](#loop-constructs-for-while-until)
   - [Functions & Scope](#functions--scope)
   - [Pipelines, Redirection, and Here-Strings](#pipelines-redirection-and-here-strings)
   - [Process Control, Jobs, and Traps](#process-control-jobs-and-traps)
4. [Windows Batch (`.bat` / `.cmd`) Scripting Guide](#4-windows-batch-bat--cmd-scripting-guide)
   - [In-Process Batch Runtime](#in-process-batch-runtime)
   - [Echo, Comments, and Script Structure](#echo-comments-and-script-structure)
   - [Variables & Delayed Expansion (`%VAR%` vs `!VAR!`)](#variables--delayed-expansion-var-vs-var)
   - [Batch Substrings and String Replacement](#batch-substrings-and-string-replacement)
   - [Batch Arithmetic (`set /a`)](#batch-arithmetic-set-a)
   - [Conditional Branching (`if`, `else`, Multi-line Blocks)](#conditional-branching-if-else-multi-line-blocks)
   - [Looping Constructs (`for`, `/L`, `/D`, `/R`, `/F`)](#looping-constructs-for-l-d-r-f)
   - [Labels, Subroutines (`call :label`), and `exit /b`](#labels-subroutines-call-label-and-exit-b)
   - [Modifiers for Script and Loop Arguments (`%~dp0`, etc.)](#modifiers-for-script-and-loop-arguments-dp0-etc)
   - [Redirection, Pipelines, and `ERRORLEVEL` Handling](#redirection-pipelines-and-errorlevel-handling)
5. [Built-in Utilities Reference](#5-built-in-utilities-reference)
   - [POSIX & Coreutils Built-ins](#posix--coreutils-built-ins)
   - [Windows Command-Line Utilities](#windows-command-line-utilities)
6. [Cross-Scripting & Interoperability](#6-cross-scripting--interoperability)
7. [Comprehensive Real-World Cookbooks](#7-comprehensive-real-world-cookbooks)
   - [POSIX / KornShell Recipes](#posix--kornshell-recipes)
   - [Windows Batch Recipes](#windows-batch-recipes)
8. [Syntax Comparison Cheat Sheet](#8-syntax-comparison-cheat-sheet)
9. [Module System & Custom Extensions](#9-module-system--custom-extensions)
   - [Overview & Architecture](#overview--architecture)
   - [Modern `module` Command Suite](#modern-module-command-suite)
   - [Classic ksh93 `builtin -f` / `builtin -d` Syntax](#classic-ksh93-builtin--f--builtin--d-syntax)
   - [Authoring Native C/C++ DLL Extensions (`ksh/module.h`)](#authoring-native-cc-dll-extensions-kshmoduleh)
   - [Authoring Shell Script Packages (`.ksh` / `KSH_MODULE_PATH`)](#authoring-shell-script-packages-ksh--ksh_module_path)
   - [Interactive Dynamic Tab Completion](#interactive-dynamic-tab-completion)

---

## 1. Architecture & Execution Model

### Dual-Personality Engine
`ksh` detects the nature of the script or command you execute:
- When running `.sh` files, `-c "commands"`, or interactive terminal commands, it operates as a standard **KornShell / POSIX** interpreter.
- When running `.bat` or `.cmd` files, it activates its **internal native Batch interpreter**, parsing Windows command syntax, labels, delayed variable expansion, and `set /a` arithmetic *without invoking `cmd.exe`*.

```
                    ┌────────────────────────┐
                    │      ksh.exe           │
                    └───────────┬────────────┘
                                │
               ┌────────────────┴────────────────┐
               │ Script Extension / Invocation   │
               ▼                                 ▼
      [.sh / Interactive / -c]           [.bat / .cmd File]
               │                                 │
     POSIX & KornShell Parser          Native In-Process Batch
     (Variables, $(), [[ ]],          (Labels, %VAR%, !VAR!,
      traps, arrays, coreutils)        call :label, set /a, for /F)
               │                                 │
               └────────────────┬────────────────┘
                                │
            ┌───────────────────┴───────────────────┐
            │   Shared Built-in Engine & Win32 APIs │
            │   (80+ built-ins: sed, awk, grep,     │
            │    findstr, copy, del, CreateProcess) │
            └───────────────────────────────────────┘
```

### Windows-Native Process & Descriptor Architecture
- **No Emulation Layers:** Commands launch native Windows binaries via `CreateProcessW` directly.
- **Unified Path Handling:** Both `/` and `\` path delimiters are accepted across file built-ins.
- **UTF-8 Console Streams:** Standard I/O streams are initialized using wide-character UTF-8 CRT modes (`_O_U8TEXT`), properly handling non-ASCII paths and filenames.
- **NUL Device Mapping:** POSIX `/dev/null` paths are automatically mapped to Windows `NUL`.
- **Command Lookup:** Bare command names are resolved from `PATH` only, never from the current directory (this blocks planted executables). Run a program in the current directory with `./prog`. `command -p` searches only the Windows system directories.
- **Signals:** `kill` supports `HUP`, `INT`, `KILL`, and `TERM` (plus `0` for an existence check). Other POSIX names are rejected with "not supported on Windows". `kill -l` lists the supported set.
- **Expansion Safety:** Values substituted from variables and `$(...)` are treated as data. Characters such as `|`, `;`, `>` or quotes inside a value never become shell syntax, so `x="a | b"; echo $x` prints `a | b`.

---

## 2. Getting Started & Shell Invocation

### Executable Command-Line Syntax

```powershell
# Interactive shell session
ksh.exe

# Execute a single command string
ksh.exe -c "ls -la src/ | head -n 5"

# Execute a POSIX / KornShell script
ksh.exe build.sh arg1 arg2

# Execute a Windows Batch script natively
ksh.exe deploy.cmd production 2026

# Run self-tests
ksh.exe --self-test

# Show built-in reference help
ksh.exe --help
```

### Shell Startup & Configuration Files

| File | Purpose | Location |
| :--- | :--- | :--- |
| `~/ksh.ini` | Startup configuration run automatically on interactive launch | `%USERPROFILE%\ksh.ini` or `<exe_dir>\ksh.ini` |
| `$ENV` | When set to an existing file, that file runs at interactive startup instead of `ksh.ini` | Any path (`%VAR%` and `$VAR` expanded) |
| `~/.ksh_history`| Persistent command history | `%USERPROFILE%\.ksh_history` |

You can customize aliases, functions, environment variables, and prompt configuration in `ksh.ini` (located in `%USERPROFILE%\ksh.ini` or in the `ksh.exe` directory). `ksh.ini` is used instead of `.kshrc` to avoid conflicting with other native builds of ksh installed on the system:

```ini
# Example ksh.ini (INI format)
[ksh]
prompt = $ 

[aliases]
ll = ls -la
cls = clear
gs = git status

[env]
EDITOR = notepad.exe
```

Shell script syntax is also supported:

```sh
# Example ksh.ini (script format)
alias ll='ls -la'
alias cls='clear'
alias gs='git status'
export EDITOR=notepad.exe
export PS1='$ '
```

Related variables:

- `CDPATH` is a `;`-separated list of base directories searched by `cd` for relative targets (for example `CDPATH=C:/Users/you/src`). The separator is `;` because `:` appears in drive letters. An empty entry means the current directory. Paths starting with `.`, `..`, or a drive letter are not searched.
- `PS4` sets the prefix for `set -x` trace lines (default `+ `).

#### Redirecting the shell with `exec`

- `exec >file`, `exec 2>file`, and `exec <file` redirect the shell for the rest of the session. `exec 3>file` opens a numbered descriptor, and `exec 3>&1`, `exec 3>&-`, and `exec 4<input.txt` are also supported. Save the original stdout with `exec 3>&1` before `exec >file`, then restore it with `exec 1>&3`.
- `exec cmd args...` runs `cmd` and then exits the shell with its status. The shell does not really replace itself: the child gets a new process ID and the parent waits for it. The EXIT trap is not run. Traps are not carried over.
- A missing command prints an error and gives status 127 (126 when the file is not executable). A non-interactive shell exits with that status.
- Descriptors 3-9 are not visible to external programs by number. Only fds 0-2 are passed to child processes.
- Redirections in one `exec` line apply in a fixed order: numbered descriptors first, then `>`, `2>`, `2>&1`, and `1>&2`. Write `exec 3>&1 >file`, not `exec 2>&1 >file`, if you need the original stdout.

### Interactive Command Line Editing (Emacs & Vi Modes)

`ntksh` supports both **Emacs** and **Vi** editing modes conforming to authentic KornShell conventions:

- **Default Mode:** Emacs mode is active by default (or if `EDITOR`/`VISUAL` specifies emacs).
- **Toggling Modes:**
  - `set -o emacs` or `set -E` enables Emacs mode.
  - `set -o vi` or `set -V` enables Vi mode.
- **Prompt Privileges:**
  - Standard user accounts display the `$` prompt (`$ `) with the current working directory removed by default.
  - Elevated Administrator accounts automatically display the `#` prompt (`# `).
  - The prompt can be customized through `ksh.ini` via `prompt = ...` or `PS1 = ...` (e.g. `prompt = $PWD $ ` to display the working directory, or `prompt = [$USER] $ `).

#### Emacs Mode Keybindings
| Shortcut | Action |
| :--- | :--- |
| `Ctrl+A` | Move cursor to start of line (Home) |
| `Ctrl+E` | Move cursor to end of line (End) |
| `Ctrl+B` / `Left` | Move backward one character |
| `Ctrl+F` / `Right` | Move forward one character |
| `Ctrl+P` / `Up` | Previous command in history |
| `Ctrl+N` / `Down` | Next command in history |
| `Ctrl+K` | Kill (cut) from cursor to end of line |
| `Ctrl+U` | Kill line from start to cursor |
| `Ctrl+Y` | Yank (paste) last killed text |
| `Ctrl+W` | Kill backward word |
| `Ctrl+L` | Clear screen and redraw current line |
| `Tab` | Context-aware completion (built-ins, commands, files, directories) |

#### Vi Mode Keybindings
- **Insert Mode:** Type characters to insert. Press `Escape` to enter Command Mode.
- **Command Mode:**
  - **Motion:** `h` (left), `l` (right), `0` / `^` (start of line), `$` (end of line), `w` (word forward), `b` (word backward), `e` (word end), `k` (history previous), `j` (history next).
  - **Insertion:** `i` (insert), `I` (insert at start), `a` (append), `A` (append at end), `s` (substitute char), `S` / `cc` (substitute line), `C` (change to end of line).
  - **Deletion:** `x` (delete char), `X` (backspace char), `D` (delete to end of line), `dd` (delete line), `dw` (delete word).
  - **Action:** `r` (replace single char), `~` (toggle character case), `u` (undo last change).
  - `Enter` executes the command directly from either mode.

---

## 3. POSIX & KornShell Scripting Guide

### Shebang and Execution Modes
All standard shell scripts begin with a shebang line or run directly through `ksh`:

```sh
#!/usr/bin/env ksh
echo "Running under ksh on Windows"
```

### Variables, Environment, and Attributes

Variables are untyped by default or typed using `typeset`:

```sh
# Basic assignment (no spaces around '=')
name="KornShell"
version=93
release="Windows Native"

# Referencing variables
echo "Welcome to $name version ${version} (${release})"

# Export to child processes
export DEPLOY_ENV="production"
export PORT=8080

# Read-only variables
readonly API_KEY="secret-token-xyz"
# API_KEY="new" # Error: variable is read-only

# Typed variables via typeset
typeset -u UPPER_STR="convert to uppercase"
echo "$UPPER_STR"  # Outputs: CONVERT TO UPPERCASE

typeset -l LOWER_STR="CONVERT TO LOWERCASE"
echo "$LOWER_STR"  # Outputs: convert to lowercase

typeset -Z5 ZERO_PADDED=42
echo "[$ZERO_PADDED]"  # Outputs: [00042]

typeset -L4 LEFT_TRIM="abcdefgh"
echo "[$LEFT_TRIM]"  # Outputs: [abcd]

typeset -R4 RIGHT_PAD="ab"
echo "[$RIGHT_PAD]"  # Outputs: [  ab]

typeset -i INTEGER_NUM=10
INTEGER_NUM=$((INTEGER_NUM * 3))
echo "$INTEGER_NUM"  # Outputs: 30

# Namerefs (ksh93 Reference Variables)
target_var="Hello from target"
typeset -n ref=target_var   # Or: nameref ref=target_var
echo "$ref"                 # Outputs: Hello from target
ref="Updated via nameref"
echo "$target_var"          # Outputs: Updated via nameref

# Compound Variables (ksh93 Structured Data)
point=( x=10 y=20 label="origin" )
echo "Coordinates: ${point.x}, ${point.y} (${point.label})"

# Nested Compound Variables
tree=( name=root child=( id=42 val=99 ) )
echo "Node: ${tree.name}, Child: ${tree.child.id} (${tree.child.val})"

# Compound Variable Cloning
pt2=point
echo "Cloned: ${pt2.x}, ${pt2.y}"

# Introspection (${!...})
echo "Members of point: ${!point.@}"        # Outputs: label y x
echo "All MY_* vars:    ${!MY_*}"           # Outputs: matching variable names
```

### ksh93 Object Architecture & User-Defined Types (`typeset -T`)

`ntksh` implements the complete ksh93 object model with encapsulated types, methods, and discipline functions:

```ksh
# Define a class/type with fields and member methods
typeset -T Point_t=(
    float x=0.0
    float y=0.0
    function show {
        print "Type: ${.sh.type} -> (${self.x}, ${self.y})"
    }
)

# Instantiate and call methods
Point_t p1=( x=3.5 y=7.2 )
p1.show                                     # Outputs: Type: Point_t -> (3.5, 7.2)

# Discipline functions (reactive hooks)
typeset -A cache
function cache.set {
    .sh.value="cached_${.sh.value}_at_${.sh.subscript}"
}
cache[session]=active
print "${cache[session]}"                   # Outputs: cached_active_at_[session]
```

### Parameter Expansion & String Manipulation

`ksh` supports standard POSIX parameter expansions:

```sh
filepath="archive.tar.gz"

# Length of variable
echo "Length: ${#filepath}"               # Outputs: 14

# Substring extraction: ${var:offset:length} and ${var:offset}
echo "Offset 0..7: ${filepath:0:7}"       # Outputs: archive
echo "Last 2 chars: ${filepath: -2}"      # Outputs: gz

# Default values
echo "${UNSET_VAR:-fallback_value}"       # Outputs: fallback_value
echo "${SET_VAR:=assigned_if_unset}"      # Assigns and outputs

# Alternate values
flag="enabled"
echo "${flag:+Flag is set}"               # Outputs: Flag is set

# Suffix removal: % (shortest) and %% (longest)
echo "${filepath%.gz}"                    # Outputs: archive.tar
echo "${filepath%.*}"                     # Outputs: archive.tar
echo "${filepath%%.*}"                    # Outputs: archive

# Prefix removal: # (shortest) and ## (longest)
fullpath="C:/Users/Administrator/logs/app.log"
echo "${fullpath#*/}"                     # Outputs: Users/Administrator/logs/app.log
echo "${fullpath##*/}"                    # Outputs: app.log (like basename)
echo "${fullpath%/*}"                     # Outputs: C:/Users/Administrator/logs (like dirname)

# Pattern search and replacement
msg="hello world world"
echo "${msg/world/universe}"              # First match: hello universe world
echo "${msg//world/universe}"             # All matches: hello universe universe
```

### Arrays: Indexed and Associative

#### Indexed Arrays
```sh
# Declaration and initialization
fruits=(apple banana cherry date)

# Accessing elements (0-indexed)
echo "First: ${fruits[0]}"                # Outputs: apple
echo "Second: ${fruits[1]}"               # Outputs: banana

# Total element count
echo "Count: ${#fruits[@]}"               # Outputs: 4

# Append or set by index
fruits[4]="elderberry"

# Iterating over elements
for item in "${fruits[@]}"; do
    echo "Fruit: $item"
done

# Iterating with indices
count=${#fruits[@]}
for ((i=0; i<count; i++)); do
    echo "Index $i -> ${fruits[i]}"
done
```

#### Associative Arrays
```sh
# Declare associative array
typeset -A servers

servers[web]="192.168.1.10"
servers[db]="192.168.1.20"
servers[cache]="192.168.1.30"

echo "Web IP: ${servers[web]}"
echo "Total servers: ${#servers[@]}"
```

#### Multidimensional Arrays & Arrays of Compound Types
```sh
# Multidimensional array assignment and dynamic indexing
matrix[0][0]=1
matrix[0][1]=2
matrix[1][0]=3
matrix[1][1]=4

i=1; j=1
echo "Matrix [0][1]: ${matrix[0][1]}"       # Outputs: 2
echo "Matrix dynamic: ${matrix[$i][$j]}"    # Outputs: 4

# Array of compound variables
point[0].x=10
point[0].y=20
point[1].x=30
point[1].y=40

echo "P0: (${point[0].x}, ${point[0].y})"   # Outputs: P0: (10, 20)
echo "P1: (${point[1].x}, ${point[1].y})"   # Outputs: P1: (30, 40)
```

### Positional Parameters & Argument Parsing

```sh
echo "Script name: $0"
echo "Total arguments: $#"
echo "First argument: $1"
echo "Second argument: $2"
echo "All arguments as list: $@"
echo "All arguments as single string: $*"

# Shift arguments
shift
echo "New first argument: $1"

# Manual parameter override
set -- "alpha" "beta gamma" "delta"
echo "Arg 1: $1"      # alpha
echo "Arg 2: $2"      # beta gamma
```

#### Parsing Flags with `getopts`
```sh
# Usage: ./script.sh -v -f input.txt -o out.log arg1
verbose=0
input_file=""
output_file=""

while getopts "vf:o:" opt; do
    case "$opt" in
        v)
            verbose=1
            ;;
        f)
            input_file="$OPTARG"
            ;;
        o)
            output_file="$OPTARG"
            ;;
        *)
            echo "Usage: $0 [-v] [-f input] [-o output] [files...]"
            exit 1
            ;;
    esac
done

shift $((OPTIND - 1))
echo "Verbose: $verbose | Input: $input_file | Output: $output_file | Remaining: $*"
```

### Arithmetic Expressions

Arithmetic is evaluated using `$(( ... ))` or the `let` built-in:

```sh
# Standard operators: +, -, *, /, %, <<, >>, &, |, ^
a=20
b=6

echo "Sum:        $((a + b))"              # 26
echo "Difference: $((a - b))"              # 14
echo "Product:    $((a * b))"              # 120
echo "Quotient:   $((a / b))"              # 3
echo "Remainder:  $((a % b))"              # 2
echo "Bitwise:    $(( (a & b) | (1 << 4) ))" # 20
echo "Precedence: $(( (a + b) * 2 ))"      # 52

# Hexadecimal support
hex_val=$((0x10 + 0x0F))
echo "Hex addition: $hex_val"              # 31

# ksh93 Floating-point arithmetic and math functions
echo "Pi doubled: $(( 3.14159 * 2 ))"      # 6.28318
echo "Division:   $(( 10 / 4.0 ))"          # 2.5
echo "Int divide: $(( 7 / 2 )) $(( 7 % 2 ))"  # 3 1 (integer operands truncate toward zero)
echo "Float div:  $(( 7.0 / 2 ))"           # 3.5 (any float operand gives a float result)
echo "Power:      $(( 2 ** 8 ))"            # 256
echo "Square root: $(( sqrt(16) + 0.5 ))"   # 4.5
echo "Absolute:   $(( abs(-42) ))"          # 42
echo "Rounding:   $(( round(4.6) ))"        # 5
echo "Floor/Ceil: $(( floor(3.9) )) $(( ceil(3.1) ))" # 3 4
echo "Hypotenuse: $(( hypot(3, 4) ))"      # 5
echo "Power func: $(( pow(2, 10) ))"        # 1024
echo "Trig:       $(( sin(0) + cos(0) ))"   # 1
# Supported C99 math functions: sin, cos, tan, asin, acos, atan, atan2, sqrt, cbrt,
# pow, hypot, exp, log, log10, log2, floor, ceil, round, trunc, abs, fmod, int.

# Using let
let result=a*2+5
echo "let result: $result"                 # 45

# Incrementing variables
count=0
count=$((count + 1))

# Arithmetic command: (( expr )) assigns variables and sets the exit status
# (0 when the result is non-zero, 1 when it is zero)
(( count += 5 ))
(( count++ ))
if (( count > 5 && count < 10 )); then echo "in range"; fi
i=0
while (( i < 3 )); do echo "i=$i"; (( i++ )); done
```

### Conditional Statements & Tests (`[` and `[[`)

#### `test` and Single Brackets `[ ... ]`
Standard POSIX testing for files, strings, and numbers:

```sh
# File testing
[ -e "app.log" ] && echo "File exists"
[ -f "app.log" ] && echo "Is a regular file"
[ -d "src" ]     && echo "Is a directory"
[ -s "app.log" ] && echo "File is not empty"
[ -r "app.log" ] && echo "File is readable"
[ -w "app.log" ] && echo "File is writable"

# String testing
str="hello"
[ -z "$str" ] && echo "String is empty"
[ -n "$str" ] && echo "String is non-empty"
[ "$str" = "hello" ] && echo "Strings match"
[ "$str" != "world" ] && echo "Strings differ"

# Numeric testing
x=10
y=20
[ "$x" -eq 10 ] && echo "Equal"
[ "$x" -ne "$y" ] && echo "Not equal"
[ "$x" -lt "$y" ] && echo "Less than"
[ "$x" -le 10 ]   && echo "Less than or equal"
[ "$y" -gt "$x" ] && echo "Greater than"
[ "$y" -ge 20 ]   && echo "Greater than or equal"
```

#### If / Elif / Else Syntax
```sh
status=200

if [ "$status" -eq 200 ]; then
    echo "HTTP OK"
elif [ "$status" -eq 404 ]; then
    echo "Not Found"
elif [ "$status" -ge 500 ]; then
    echo "Server Error"
else
    echo "Status Code: $status"
fi
```

#### Extended Test `[[ ... ]]`
Supports pattern globbing and logical operators `&&` and `||`:

```sh
filename="test_suite.c"

# Glob pattern match
if [[ $filename == test_* ]]; then
    echo "Is a test file"
fi

# POSIX character class matching
if [[ $filename == [[:alpha:]]* ]]; then
    echo "Starts with an alphabetic character"
fi

# ksh93 extended globbing: @(...), !(...), *(...), +(...)
if [[ $filename == @(*.c|*.h) ]]; then
    echo "C source or header"
fi
if [[ $filename == !(build)* ]]; then
    echo "Does not start with build"
fi

if [[ $filename == *.c ]]; then
    echo "Is a C source file"
fi

# String ordering and compounds
if [[ -n "$filename" && $filename != *.bak ]]; then
    echo "Active file"
fi
```

### Loop Constructs (`for`, `while`, `until`)

#### `for` in List
```sh
for target in clean build package deploy; do
    echo "Target: $target"
done
```

#### File Globbing in `for`
```sh
for src in src/*.c; do
    echo "Compiling $src"
done
```

#### C-Style `for` Loop
```sh
for ((i = 1; i <= 5; i++)); do
    echo "Step: $i"
done
```

#### `while` Loop
```sh
count=3
while [ "$count" -gt 0 ]; do
    echo "Countdown: $count"
    count=$((count - 1))
done
```

#### `until` Loop
```sh
retry=0
until [ "$retry" -ge 3 ]; do
    echo "Attempt: $retry"
    retry=$((retry + 1))
done
```

#### Loop Control: `break` and `continue`
```sh
for num in 1 2 3 4 5; do
    if [ "$num" -eq 2 ]; then
        continue # skip 2
    fi
    if [ "$num" -eq 4 ]; then
        break    # stop loop
    fi
    echo "Number: $num"
done
# Outputs: 1, then 3
```

#### `case` Statement
```sh
ext="tar.gz"

case "$ext" in
    c|h|cpp)
        echo "C/C++ Source File"
        ;;
    sh|ksh|bash)
        echo "Shell Script"
        ;;
    tar.gz|tgz)
        echo "Gzip Compressed Archive"
        ;;
    *)
        echo "Unknown extension: $ext"
        ;;
esac
```

### Functions & Scope

#### Function Definition & Arguments
```sh
# POSIX style
log_msg() {
    echo "[$(date +%H:%M:%S)] $*"
}

# KornShell style
function greet {
    echo "Hello, $1!"
}

log_msg "System initializing..."
greet "Developer"
```

#### Return Codes & Local Variables
```sh
# Local variable scoping
compute_square() {
    local val=$1
    echo $((val * val))
}

sq="$(compute_square 9)"
echo "Result: $sq" # Outputs: 81

# Returning status codes (0-255)
file_exists() {
    [ -f "$1" ] && return 0
    return 1
}

if file_exists "src/ksh.c"; then
    echo "Source code verified."
fi
```

### Pipelines, Redirection, and Here-Strings

```sh
# Standard output redirection
echo "log line" > app.log
echo "append line" >> app.log

# Standard error redirection
ls non_existent_file 2> err.log
ls non_existent_file 2>> err.log

# Redirect error to stdout (duplicate descriptor)
run_job > combined.log 2>&1

# Suppress all output (mapped automatically to Windows NUL)
grep "pattern" large.txt > /dev/null 2>&1

# Pipeline chaining
cat users.csv | cut -d, -f1 | sort | uniq -c

# Reading from file into loop
while read -r line; do
    echo "Read: $line"
done < app.log

# Here-String (<<<)
read -r user domain <<< "admin example.com"
echo "User: $user on $domain"

# Here-Document (scripts): variables, $(...) and `...` are expanded when the
# command runs; quote the delimiter ('EOF' or \EOF) to keep the body literal
name=World
cat <<EOF
Hello, $name! Today is $(date +%A). Cost: \$5
EOF
cat <<'EOF'
Literal: $name stays as written
EOF

# Two-Way Co-Processes (|&, print -p, read -p)
# Start a background program connected via two-way pipes:
cmd.exe /c more |&
print -p "hello from coprocess"
read -p response
echo "Received from coprocess: $response"
```

### Process Control, Jobs, and Traps

```sh
# Run command in background
sleep 2 &
bg_pid=$!
echo "Background job PID: $bg_pid"

# Wait for background processes
wait
echo "All background processes finished."

# Signal traps (EXIT trap executes when script terminates)
TEMP_DIR="$TEMP/build_$$"
mkdir -p "$TEMP_DIR"

cleanup() {
    echo "Cleaning temporary files in $TEMP_DIR..."
    rm -rf "$TEMP_DIR"
}

trap cleanup EXIT

echo "Performing build operations..."
```

---

## 4. Windows Batch (`.bat` / `.cmd`) Scripting Guide

### In-Process Batch Runtime
When running `.bat` or `.cmd` files in `ksh`, the scripts execute directly inside the `ksh.exe` process without starting `cmd.exe`. All environment variable alterations, working directory changes, and exit codes stay synchronized with the shell.

### Echo, Comments, and Script Structure

```cmd
@echo off
rem This is a standard remark comment
:: This double-colon syntax is also a valid comment

echo Script execution started.
echo.
echo Above was a blank line.
```

### Variables & Delayed Expansion (`%VAR%` vs `!VAR!`)

#### Standard Expansion (`%VAR%`)
Variables are expanded at *parse time* when a line or block is read:

```cmd
@echo off
set "NAME=World"
echo Hello %NAME%!

set "APP_DIR=C:\Program Files\App"
echo Installed at: "%APP_DIR%"

rem Unset a variable
set UNUSED=
```

#### Delayed Expansion (`!VAR!`)
Inside parenthesized blocks (e.g. `for` loops or `if` statements), `%VAR%` only sees the value from *before* the block started. Enable delayed expansion to read updated values at *runtime*:

```cmd
@echo off
setlocal EnableDelayedExpansion

set COUNT=0
for %%i in (one two three) do (
    set /a COUNT+=1
    echo Iteration !COUNT!: item is %%i
)
echo Final count: !COUNT!

endlocal
```

### Batch Substrings and String Replacement

#### Substrings: `%VAR:~offset,length%`
```cmd
@echo off
set "TEXT=TheQuickBrownFox"

echo Full:       %TEXT%
echo First 3:    %TEXT:~0,3%     :: The
echo From pos 3: %TEXT:~3%       :: QuickBrownFox
echo Length 5:   %TEXT:~8,5%     :: Brown
echo Last 3:     %TEXT:~-3%      :: Fox
echo Up to last: %TEXT:~0,-3%    :: TheQuickBrown
```

#### Search and Replace: `%VAR:find=replace%`
```cmd
@echo off
set "PATH_STR=C:\data\bin\tool.exe"
set "UNIX_STR=%PATH_STR:\=/%"
echo Forward slash path: %UNIX_STR%  :: C:/data/bin/tool.exe

set "GREET=Hello World"
echo Swap: %GREET:World=KornShell%   :: Hello KornShell
```

### Batch Arithmetic (`set /a`)

Batch arithmetic handles 32-bit signed integer calculations:

```cmd
@echo off
rem Basic operations
set /a SUM=10+5
set /a DIFF=20-7
set /a PROD=6*7
set /a QUOT=100/4
set /a MOD=23%%5       :: In .bat files, escape % as %%

echo %SUM% %DIFF% %PROD% %QUOT% %MOD%  :: 15 13 42 25 3

rem Multiple expressions in one line (comma-separated)
set /a X=5, Y=X*3, Z=Y+1
echo Z=%Z%             :: 16

rem Compound assignment operators
set /a TOTAL=10
set /a TOTAL+=5        :: TOTAL = 15
set /a TOTAL*=2        :: TOTAL = 30
echo Total: %TOTAL%

rem Bitwise operations
set /a "MASK=(1<<4) | 1"
echo Mask: %MASK%      :: 17
```

### Conditional Branching (`if`, `else`, Multi-line Blocks)

#### String and Case-Insensitive Comparisons
```cmd
@echo off
set "ENV=PRODUCTION"

if /i "%ENV%"=="production" (
    echo Deploying to live cluster...
) else (
    echo Staging deployment...
)
```

#### Variable Existence & File Tests
```cmd
@echo off
rem Check if a variable is set
if defined PORT (
    echo Port is configured: %PORT%
) else (
    echo Using default port 8080.
)

rem Check file or directory existence
if exist "%SystemRoot%\notepad.exe" (
    echo Notepad is present.
)

if not exist "%TEMP%\build" (
    mkdir "%TEMP%\build"
)
```

#### Numeric Comparisons
| Operator | Meaning |
| :--- | :--- |
| `EQU` | Equal to |
| `NEQ` | Not equal to |
| `LSS` | Less than |
| `LEQ` | Less than or equal to |
| `GTR` | Greater than |
| `GEQ` | Greater than or equal to |

```cmd
@echo off
set /a MEMORY_GB=16

if %MEMORY_GB% GEQ 32 (
    echo High memory profile
) else if %MEMORY_GB% GEQ 16 (
    echo Standard memory profile
) else (
    echo Low memory profile
)
```

### Looping Constructs (`for`, `/L`, `/D`, `/R`, `/F`)

> **Note on % vs %%:** Inside batch script files, loop variables require double percent signs (e.g. `%%i`). On the direct interactive command line, use single percent signs (`%i`).

#### Basic List Iteration
```cmd
@echo off
for %%f in (alpha beta gamma) do (
    echo Processing item: %%f
)
```

#### Numeric Step Counting: `for /L`
```cmd
@echo off
rem Syntax: for /L %%var in (start, step, end) do ...
for /L %%n in (1, 1, 5) do (
    echo Count: %%n
)
```

#### Directory Scanning: `for /D`
```cmd
@echo off
for /D %%d in ("C:\*") do (
    echo Directory: %%~nxd
)
```

#### Recursive File Search: `for /R`
```cmd
@echo off
rem Recursively search current directory tree for C source files
for /R "%~dp0" %%f in (*.c) do (
    echo Found source: %%~nxf (size: %%~zf bytes)
)
```

#### Advanced Parsing: `for /F`
Parses text files, strings, and command output:

```cmd
@echo off
rem Parsing a CSV string
set "ROW=John,Developer,Engineering"
for /F "tokens=1-3 delims=," %%a in ("%ROW%") do (
    echo Name: %%a | Role: %%b | Dept: %%c
)

rem Parsing file content with options
for /F "usebackq skip=1 tokens=1,2* delims=," %%a in ("data.csv") do (
    echo ID: %%a | User: %%b | Extra: %%c
)

rem Parsing output of a command
for /F "tokens=*" %%v in ('ver') do (
    echo Operating System: %%v
)
```

### Labels, Subroutines (`call :label`), and `exit /b`

Subroutines allow structured, modular batch scripting:

```cmd
@echo off
echo Starting main program.

call :calculate_max 15 28
echo Maximum returned: %MAX_VAL%

call :log_message "System operational."

echo Done.
exit /b 0

:: ----------------------------------------------------------------------
:: Subroutines
:: ----------------------------------------------------------------------

:calculate_max
rem Arguments: %1 = number1, %2 = number2
if %1 GTR %2 (
    set MAX_VAL=%1
) else (
    set MAX_VAL=%2
)
exit /b 0

:log_message
echo [%DATE% %TIME%] %~1
exit /b 0
```

### Modifiers for Script and Loop Arguments (`%~dp0`, etc.)

Batch parameters (`%0` to `%9`) and `for` loop variables (`%%i`) can be decomposed using parameter modifiers:

| Modifier | Description | Example for `C:\dir\app.exe` |
| :--- | :--- | :--- |
| `%~1` | Strips enclosing quotes | `"hello world"` -> `hello world` |
| `%~f1` | Fully qualified path | `C:\dir\app.exe` |
| `%~d1` | Drive letter with colon | `C:` |
| `%~p1` | Path directory only | `\dir\` |
| `%~n1` | File name without extension | `app` |
| `%~x1` | File extension with period | `.exe` |
| `%~s1` | Short 8.3 path | `C:\dir\APP~1.EXE` |
| `%~a1` | File attribute flags | `--a------` |
| `%~t1` | File modification timestamp | `11/11/2026 09:30 AM` |
| `%~z1` | File size in bytes | `700928` |
| **`%~dp0`** | Directory of the running script | `C:\Workspace\bin\` |
| **`%~nx0`** | Filename + extension of running script | `deploy.cmd` |

```cmd
@echo off
echo Running from: %~dp0
echo Script name:  %~nx0
echo Full path:    %~f0
```

### Redirection, Pipelines, and `ERRORLEVEL` Handling

```cmd
@echo off
rem Output redirection
echo message > log.txt
echo more >> log.txt

rem Suppress stdout
dir /b *.txt >nul

rem Suppress both stdout and stderr
del /q "%TEMP%\scratch.tmp" >nul 2>nul

rem Conditional execution based on success (&&) or failure (||)
findstr /c:"ERROR" app.log >nul && (
    echo Errors were found in the log!
) || (
    echo Log is clean.
)

rem Check exit code
dir /b missing_file_xyz >nul 2>nul
if errorlevel 1 (
    echo Directory command failed with code %ERRORLEVEL%.
)
```

---

## 5. Built-in Utilities Reference

`ksh` includes built-in implementations of core Unix and Windows tools. These execute natively in-process without spawning separate child shells.

### POSIX & Coreutils Built-ins

| Utility | Description & Key Options | Example |
| :--- | :--- | :--- |
| `awk` | Pattern scanning & text processing | `echo "a b" \| awk '{print $2, $1}'` |
| `sed` | Stream editor (`s/pat/repl/`, `-e`, `-n`) | `sed 's/foo/bar/g' file.txt` |
| `grep` / `egrep` | Regex pattern search (`-i`, `-v`, `-n`, `-c`, `-E`) | `grep -i "error" app.log` |
| `cat` | Concatenate and print files (`-n`, `-b`, `-s`) | `cat -n src/main.c` |
| `head` | Output first lines or bytes (`-n N`, `-c N`) | `head -n 10 file.txt` |
| `tail` | Output last lines or bytes (`-n N`, `-c N`) | `tail -n 20 file.txt` |
| `wc` | Count lines, words, and bytes (`-l`, `-w`, `-c`) | `wc -l < app.log` |
| `sort` | Sort text lines (`-r`, `-n`, `-f`, `-u`, `-k`) | `sort -n -r scores.txt` |
| `uniq` | Report or filter duplicate lines (`-c`, `-d`, `-u`) | `sort list.txt \| uniq -c` |
| `cut` | Extract character or field parts (`-d`, `-f`, `-c`) | `cut -d: -f1 /etc/passwd` |
| `tr` | Translate, squeeze, or delete chars (`-d`, `-s`) | `echo "hello" \| tr a-z A-Z` |
| `paste` | Merge lines horizontally (`-d`, `-s`) | `paste -d, names.txt ages.txt` |
| `fold` | Wrap lines to specified width (`-w N`, `-s`) | `fold -w 80 doc.txt` |
| `nl` | Line numbering filter (`-b style`) | `nl -ba script.sh` |
| `ls` | Directory contents listing (`-a`, `-l`, `-h`, `-r`, `-t`) | `ls -lh src/` |
| `cp` | Copy files and directory trees (`-r`, `-f`, `-p`) | `cp -r src/ backup/` |
| `mv` | Move or rename files and directories (`-f`, `-v`) | `mv old.txt new.txt` |
| `rm` | Remove files and directory trees (`-r`, `-f`, `-v`) | `rm -rf tmp/` |
| `mkdir` | Create directories (`-p`) | `mkdir -p build/logs` |
| `touch` | Update access/mod times or create empty file | `touch .build_timestamp` |
| `find` | Search directory hierarchies | `find . -name "*.c"` |
| `mktemp` | Create unique safe temporary file or directory (`-d`) | `tmp=$(mktemp)` |
| `diff` | Line-by-line file comparison (`-u`, `-q`, `-i`, `-w`) | `diff -u file1.txt file2.txt` |
| `cmp` | Byte-by-byte file comparison (`-l`, `-s`) | `cmp -s f1.bin f2.bin` |
| `basename` | Strip directory prefix and optional suffix | `basename path/to/file.c .c` |
| `dirname` | Strip filename suffix to retrieve directory path | `dirname path/to/file.c` |
| `base64` | Base64 encode or decode data (`-d`) | `echo hello \| base64` |
| `sha256sum` | Compute SHA-256 cryptographic digests | `sha256sum ksh.exe` |
| `md5sum` | Compute MD5 message digests | `md5sum archive.zip` |
| `seq` | Generate sequences of numbers (`seq start step end`) | `for i in $(seq 1 5); do ...` |
| `xargs` | Build and execute command lines from stdin (`-n`) | `cat list.txt \| xargs -n 1 cp -t dest/` |
| `tee` | Duplicate stdin to files and stdout (`-a`) | `make \| tee build.log` |
| `uname` | Print system and kernel name (`-a`, `-s`) | `uname -s` |
| `which` | Locate executable in `PATH` | `which git.exe` |
| `ifconfig` | Display and inspect network interfaces (`-a`, `-l`, `-u`, `-d`, `-m`, `-v`, `-n`, `-f`, `-g`, `-G`, `-L`, `-D`, `-k`, `-C`) | `ifconfig -a` |
| `netstat` | Display network connections, routes, interface and protocol statistics (`-a`, `-n`, `-r`, `-i`, `-s`, `-p`, `-f`, `-4`, `-6`, `-l`, `-v`, `-I`, `-w`, `-d`, `-q`, `-S`, `-W`) | `netstat -an` |
| `devinfo` | Display the device tree and resources (`-r`, `-u`, `-v`, `-p dev`) | `devinfo -p ROOT` |
| `fmt` | Format paragraphs to specified line width (`-c`, `-m`, `-s`, `-w N`) | `fmt -w 72 notes.txt` |
| `csplit` | Context file splitter (`-f pfx`, `-n N`, `-k`, `/regex/`, `{N}`, `{*}`) | `csplit -f chapter_ book.txt '/^# Chapter/' '{*}'` |
| `chmod` | Change file permissions/attributes (octal, `+w`, `-w`, `+x`, `-R`) | `chmod 0755 script.sh` |
| `chown` | Change file owner SID/account (`-R`, `-v`, `-f`) | `chown -R Administrator src/` |
| `jot` | Generate sequential/random data (`-r`, `-c`, `-b word`, `-w fmt`, `-s sep`) | `jot -r 5 1 100` |
| `chflags` | Change file flags (hidden, arch, readonly, system, temp, `-R`) | `chflags hidden,readonly secret.dat` |
| `rs` | Reshape and transpose data matrices (`-t`, `-T`, `-c`, `-C`, `-w`, `-g`) | `rs -t < matrix.txt` |
| `lockf` | Execute command under Win32 kernel file lock (`-t sec`, `-n`, `-s`, `-k`) | `lockf my.lock make build` |
| `unlink` | Remove a directory entry / file | `unlink old.log` |
| `link` | Create hard link with ReFS/FAT symbolic fallback | `link target.txt hardlink.txt` |
| `tsort` | Topological sort for directed acyclic graphs (`-q`) | `tsort graph.txt` |
| `nice` | Run command with modified Windows scheduling priority | `nice -n 10 compile_job` |
| `nohup` | Run command immune to console close/hangups (`nohup.out`) | `nohup long_task &` |
| `patch` | Apply unified diff patch to files (`-p[num]`, `-i`, `-o`, `-R`, `-s`) | `patch -p1 -i fix.patch` |
| `pax` | Standard POSIX ustar archiver (create, extract, copy trees) | `pax -w -f backup.tar src/` |
| `tar` | Standard tape archiver (`-c`, `-x`, `-t`, `-f`, `-v`, `-C`, `-z`) | `tar -czvf backup.tar.gz src/` |
| `curl` | Data transfer via HTTP/HTTPS (`-s`, `-o`, `-O`, `-H`, `-X`, `-d`) | `curl -s https://example.com/api` |
| `wget` | Non-interactive network downloader (`-O`, `-q`) | `wget https://example.com/file.zip` |
| `sockstat` | Display open sockets, owning processes, and states (`-4`, `-6`, `-c`, `-l`, `-p`, `-P`, `-q`, `-U`) | `sockstat -4 -l -p 80,443` |
| `nc` | OpenBSD netcat: TCP/UDP connections and listeners (`-4`, `-6`, `-k`, `-l`, `-n`, `-p`, `-u`, `-v`, `-w`, `-z`) | `nc -z -v -w 2 127.0.0.1 8080` |
| `mtree` | Map or verify directory hierarchy and SHA-256 specs (`-c`, `-f`, `-p`, `-K`, `-q`, `-n`) | `mtree -c -p src/ > spec.txt` |
| `look` | Binary search for lines with prefix in sorted file (`-d`, `-f`, `-t`) | `look -f "get" sorted_symbols.txt` |
| `daemon` | Run command detached from terminal with supervision (`-f`, `-r`, `-p`, `-P`, `-o`, `-m`, `-u`) | `daemon -r -p srv.pid -o srv.log ./server` |
| `lam` | Laminate lines of files side-by-side (`-t`, `-s`, `-p min[.max]`) | `lam -s " | " -p 20 file1.txt file2.txt` |
| `vmstat` | Display virtual memory, paging, and CPU statistics (`-s`, `-w`, `interval [count]`) | `vmstat -s` |
| `pbcopy` | Read stdin and copy data to Windows clipboard (`-pboard`) | `cat file.txt \| pbcopy` |
| `pbpaste` | Output Windows clipboard contents to stdout (`-pboard`, `-Prefer`) | `pbpaste > clip.txt` |
| `fstat` | Display open files, handles, and process locks (`-v`, `-p pid`, `-u user`) | `fstat app.dll` |
| `script` | Make typescript of terminal session and commands (`-a`, `-k`, `-q`) | `script session.log` |
| `vis` | Display non-printable bytes in visual formats (`-b`, `-c`, `-w`, `-n`, `-s`) | `cat script.bat \| vis -c` |
| `unvis` | Decode visual format produced by `vis` back to raw data (`-e`) | `unvis < dump.vis > orig.bin` |
| `iostat` | Display real-time disk I/O throughput and queue stats (`-d`, `-x`, `-c`, `-w`) | `iostat -x 1 5` |
| `apply` | Apply command to sets of arguments (`-a char`, `-1`..`-9`) | `apply -2 diff f1.a f1.b f2.a f2.b` |
| `sysctl` | Query kernel, hardware, CPU, and memory metrics (`-a`, `-n`) | `sysctl hw.ncpu hw.physmem` |
| `renice` | Alter priority class of running processes (`-n pri`, `-p pid`) | `renice -n 5 -p 1240` |
| `arp` | Inspect kernel IPv4 ARP neighbor address cache (`-a`, `-n`) | `arp -a` |
| `host` | DNS lookup utility for resolving host and IP addresses (`-t`) | `host github.com` |
| `getent` | Get administrative database entries (passwd, group, hosts, services) | `getent passwd Administrator` |
| `srm` | Securely overwrite with random passes and delete files (`-z`, `-f`) | `srm -z secret.key` |
| `pr` | Paginate and format text files for printing (`-h`, `-l`, `-t`) | `pr -h "Report" file.txt` |
| `banner` | Render large ASCII art block banner text | `banner "HELLO WORLD"` |
| `ping` | Send ICMP ECHO_REQUEST packets to network hosts (`-c`, `-i`, `-s`, `-t`, `-w`, `-q`) | `ping -c 4 1.1.1.1` |
| `traceroute` | Trace network route hops to a destination (`-m`, `-q`, `-w`) | `traceroute 8.8.8.8` |
| `whois` | Query RFC 3912 domain registration & IP assignments (`-h`, `-p`) | `whois github.com` |
| `col` | Filter reverse line feeds and overstrike backspaces (`-b`, `-x`) | `col -b < manual.txt` |
| `ul` | Translate backspace underlines & overstrikes to ANSI (`-i`) | `ul < formatted.txt` |
| `what` | Scan binaries and source for SCCS `@(#)` identifiers (`-s`) | `what bin/ksh.exe` |
| `od` | Dump files in octal, hex, decimal, or characters (`-c`, `-o`, `-x`, `-d`, `-A`, `-j`, `-N`) | `od -c file.bin` |
| `sum` | Checksum and count file blocks using BSD 13-bit / SysV 32-bit (`-r`, `-s`) | `sum -r archive.tar` |
| `leave` | Set a departure reminder alarm countdown or clock time | `leave +30` |
| `caesar` | Decrypt or encrypt Caesar/ROT-N ciphers with frequency analysis (`rot13`) | `echo "uryyb" \| caesar` |

#### BSD Network and Device Utilities

These three builtins follow the BSD `ifconfig(8)`, `netstat(1)`, and `devinfo(8)` interfaces. Windows does not expose every BSD concept, so some options are unsupported and exit with status 1:

- **`ifconfig`**: Lists adapters by their Windows FriendlyName (`-l` prints them space-separated). Address family filters: `inet`, `inet6`, and `-f inet:cidr` style formats. Interface configuration (create, destroy, up, down, mtu, netmask, alias, description) and jail options (`-j`) are not supported; use `netsh` for changes.
- **`netstat`**: TCP and UDP socket tables use the owner-PID APIs, so the PID column (`-v`) is available. Without `-n`, addresses are reverse-resolved to host names and ports to service names, as on BSD; use `-n` for fast numeric output. Listening sockets are taken from the OS listener table. Windows does not expose per-socket queue sizes to other processes, so Recv-Q and Send-Q are always `0`. Rows whose kernel-reported TCP state is invalid are shown as `UNKNOWN`. Use `-i` for interface counters (`-f link` maps to `-i`).
- **`devinfo`**: Walks the SetupAPI device tree. `-p dev` prints the resources for a matching device.

Examples:

```ksh
ifconfig -a                 # all adapters with addresses and flags
ifconfig -l                 # adapter names only
netstat -an                 # all sockets, numeric addresses
netstat -rn                 # routing table
netstat -s -p tcp           # TCP protocol statistics
netstat -i                  # interface packet counters
devinfo -p ROOT             # resources for the ROOT device
sockstat -4 -l              # IPv4 listening sockets with owning process & user
nc -z -v 127.0.0.1 80 443   # port scan target endpoints
mtree -c -p src/ > spec.txt # generate directory checksum specification
mtree -f spec.txt -p src/   # verify filesystem hierarchy integrity
look -f "kernel" words.txt  # fast binary search for words starting with "kernel"
daemon -r -p app.pid ./app  # spawn supervised background daemon
lam -s " | " -p 15 a.txt b.txt # laminate corresponding lines of two files
vmstat -s                   # system memory and paging summary
echo "hello" | pbcopy       # copy text to Windows clipboard
pbpaste                     # paste text from Windows clipboard
fstat C:\deploy\app.dll     # identify process holding lock on file
script session.log          # record entire terminal session to typescript file
cat script.bat | vis -c     # show invisible CRLF, tabs, and escape sequences
iostat -x 1 5               # real-time disk transfer throughput and queue depth
apply -2 diff f1.a f1.b f2.a f2.b # run command repeatedly on paired arguments
```

### Windows Command-Line Utilities

| Utility | Description & Syntax | Example |
| :--- | :--- | :--- |
| `dir` | List files and directories using Win32 API (`/b`, `/s`, `/a`) | `dir /b /s *.txt` |
| `copy` | Copy files (`/Y`, `/B`, `/V`) | `copy /Y file.txt copy.txt` |
| `del` / `erase` | Delete files (`/Q`, `/F`, `/S`) | `del /Q *.tmp` |
| `move` | Move files or directories (`/Y`) | `move /Y a.txt b.txt` |
| `ren` / `rename` | Rename files | `ren old.txt new.txt` |
| `md` / `mkdir` | Make directory structure | `md sub\folder` |
| `rd` / `rmdir` | Remove directory tree (`/S`, `/Q`) | `rd /S /Q tempdir` |
| `type` | Display file contents | `type config.ini` |
| `attrib` | View and edit file attributes (`+R`, `-R`, `+H`, `/S`) | `attrib +R readonly.txt` |
| `findstr` | String and regex search in files (`/s`, `/i`, `/r`, `/c:`) | `findstr /i /c:"error" *.log` |
| `where` | Search `PATH` for matching executable files | `where cl.exe` |
| `tree` | Graphical directory hierarchy display (`/F`, `/A`) | `tree /F src` |
| `ver` | Print Windows operating system version | `ver` |
| `chcp` | View or change console code page | `chcp 65001` |
| `choice` | Prompt for single-character user choices (`/C`, `/M`) | `choice /C YN /M "Continue?"` |
| `clip` | Copy piped standard input to Windows Clipboard | `cat key.pub \| clip` |
| `pushd` / `popd` | Manage directory stack across drives and shares | `pushd %TEMP% & popd` |

---

## 6. Cross-Scripting & Interoperability

Because `ksh` contains both runtimes, scripts can interoperate seamlessly.

### Calling Batch Scripts from Shell Scripts
When running a `.cmd` or `.bat` script from a shell script, arguments and return codes are preserved:

```sh
#!/usr/bin/env ksh
echo "Calling Windows setup batch file..."

# Invoke the batch file directly
./build_tools.cmd "production" 64
batch_status=$?

if [ "$batch_status" -ne 0 ]; then
    echo "Batch setup failed with exit code $batch_status"
    exit $batch_status
fi

echo "Batch setup succeeded."
```

### Calling Shell Commands from Batch Scripts
From inside a `.bat` or `.cmd` file, you can call `ksh -c` to execute POSIX pipelines, `awk`, or `sed` transformations:

```cmd
@echo off
setlocal EnableDelayedExpansion

echo Running Windows batch pipeline...

rem Transform text using awk inside the batch file
for /F "tokens=*" %%r in ('ksh -c "printf 'apple 5\nbanana 12\n' | awk '{print \$1, \$2 * 2}'"') do (
    echo   Adjusted inventory: %%r
)

echo Done.
```

---

## 7. Comprehensive Real-World Cookbooks

### POSIX / KornShell Recipes

#### Recipe 1: Production Multi-Project Build Runner
```sh
#!/usr/bin/env ksh
# build_runner.sh - Automated build, test, and package script

set -e # Exit immediately on unhandled command failure

BUILD_DIR="$TEMP/ksh_project_build"
LOG_FILE="$BUILD_DIR/build.log"
CONFIG_MODE="${1:-release}"

cleanup() {
    echo "[INFO] Cleaning up build scratch space..."
    rm -rf "$BUILD_DIR"
}
trap cleanup EXIT

mkdir -p "$BUILD_DIR"
echo "[INFO] Starting build in $CONFIG_MODE mode..." | tee "$LOG_FILE"

# Step 1: Verify source files
src_count=$(find src -name "*.c" | wc -l | tr -d ' ')
echo "[INFO] Found $src_count C source files." | tee -a "$LOG_FILE"

if [ "$src_count" -eq 0 ]; then
    echo "[ERROR] No source files detected." >&2
    exit 1
fi

# Step 2: Compile
echo "[INFO] Compiling binaries..." | tee -a "$LOG_FILE"
mkdir -p bin
clang -O2 src/ksh.c -o bin/ksh.exe -luser32 -lshell32 -ladvapi32 -lpsapi32 -liphlpapi -lws2_32 -lsetupapi -lcfgmgr32 >> "$LOG_FILE" 2>&1

# Step 3: Run sanity self-test
echo "[INFO] Executing test suite..." | tee -a "$LOG_FILE"
./bin/ksh.exe --self-test >> "$LOG_FILE" 2>&1

echo "[SUCCESS] Build and verification completed successfully."
```

#### Recipe 2: Web Server Log Analytics Filter
```sh
#!/usr/bin/env ksh
# log_analyzer.sh - Parses HTTP access logs and summarizes status codes

LOG_FILE="${1:-access.log}"

if [ ! -f "$LOG_FILE" ]; then
    echo "Usage: $0 <access.log>" >&2
    exit 1
fi

echo "=========================================================="
echo "           LOG ANALYTICS: $LOG_FILE                       "
echo "=========================================================="

total_lines=$(wc -l < "$LOG_FILE" | tr -d ' ')
echo "Total requests recorded: $total_lines"

echo ""
echo "HTTP Status Code Distribution:"
awk '{print $9}' "$LOG_FILE" | sort | uniq -c | sort -n -r

echo ""
echo "Top 5 Requesting IP Addresses:"
awk '{print $1}' "$LOG_FILE" | sort | uniq -c | sort -n -r | head -n 5

echo ""
echo "Top 5 Requested Endpoints:"
awk '{print $7}' "$LOG_FILE" | sort | uniq -c | sort -n -r | head -n 5
```

#### Recipe 3: Safe Directory Archiver with Rotation
```sh
#!/usr/bin/env ksh
# backup_rotate.sh - Creates dated backups and removes older rotations

SOURCE_DIR="$1"
BACKUP_ROOT="${2:-C:/Backups}"
MAX_KEEP=5

if [ -z "$SOURCE_DIR" ] || [ ! -d "$SOURCE_DIR" ]; then
    echo "Usage: $0 <source_directory> [backup_root]" >&2
    exit 1
fi

mkdir -p "$BACKUP_ROOT"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
BASE_NAME=$(basename "$SOURCE_DIR")
TARGET_ARCHIVE="$BACKUP_ROOT/${BASE_NAME}_${TIMESTAMP}"

echo "[INFO] Copying $SOURCE_DIR to $TARGET_ARCHIVE..."
cp -r "$SOURCE_DIR" "$TARGET_ARCHIVE"

# Rotation check: keep only the newest $MAX_KEEP backups
echo "[INFO] Checking backup rotation..."
backup_count=$(ls -1d "$BACKUP_ROOT/${BASE_NAME}"_* 2>/dev/null | wc -l | tr -d ' ')

if [ "$backup_count" -gt "$MAX_KEEP" ]; then
    prune_count=$((backup_count - MAX_KEEP))
    echo "[INFO] Pruning $prune_count old backup(s)..."
    ls -1td "$BACKUP_ROOT/${BASE_NAME}"_* | tail -n "$prune_count" | while read -r old_dir; do
        echo "  Removing: $old_dir"
        rm -rf "$old_dir"
    done
fi

echo "[SUCCESS] Backup operation complete."
```

---

### Windows Batch Recipes

#### Recipe 1: Environment Health Check & Dependency Validator
```cmd
@echo off
setlocal EnableDelayedExpansion

echo ==========================================================
echo           DEVELOPER ENVIRONMENT HEALTH CHECK             
echo ==========================================================

set ERROR_COUNT=0

rem 1. Check for required compilers and tools
call :check_tool "clang.exe" "LLVM C Compiler"
call :check_tool "git.exe" "Git Version Control"
call :check_tool "powershell.exe" "Windows PowerShell"

rem 2. Verify critical system environment variables
call :check_env "TEMP"
call :check_env "SystemRoot"

rem 3. Check architecture
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
    echo [PASS] CPU Architecture: 64-bit AMD64
) else (
    echo [WARN] Non-standard architecture: %PROCESSOR_ARCHITECTURE%
)

echo.
if %ERROR_COUNT% EQU 0 (
    echo [STATUS] Environment is completely healthy. Ready to build.
    endlocal
    exit /b 0
) else (
    echo [FAIL] Found %ERROR_COUNT% missing requirement(s).
    endlocal
    exit /b 1
)

:: ------------------------------------------------------------
:: Helper Functions
:: ------------------------------------------------------------

:check_tool
where %~1 >nul 2>nul
if errorlevel 1 (
    echo [FAIL] %~2 (%~1) is NOT found in PATH.
    set /a ERROR_COUNT+=1
) else (
    for /F "delims=" %%p in ('where %~1') do (
        echo [PASS] %~2 found: %%p
        goto :tool_found
    )
    :tool_found
)
exit /b 0

:check_env
if defined %~1 (
    echo [PASS] Environment variable %~1 is defined.
) else (
    echo [FAIL] Environment variable %~1 is MISSING.
    set /a ERROR_COUNT+=1
)
exit /b 0
```

#### Recipe 2: Automated CSV Data Ingestion and Report Generator
```cmd
@echo off
setlocal EnableDelayedExpansion

set "INPUT_FILE=%~1"
set "REPORT_FILE=%~dp0summary_report.txt"

if "%INPUT_FILE%"=="" (
    echo Usage: %~nx0 data.csv
    exit /b 1
)

if not exist "%INPUT_FILE%" (
    echo [ERROR] File not found: %INPUT_FILE%
    exit /b 1
)

echo Generating report for %INPUT_FILE%...
echo ===================================================== > "%REPORT_FILE%"
echo           DATA SUMMARY REPORT: %~nx1                 >> "%REPORT_FILE%"
echo           Generated on %DATE% at %TIME%              >> "%REPORT_FILE%"
echo ===================================================== >> "%REPORT_FILE%"
echo. >> "%REPORT_FILE%"

set /a TOTAL_ROWS=0
set /a TOTAL_SALES=0

rem Assuming CSV format: ID,Customer,Amount,Region
for /F "usebackq skip=1 tokens=1-4 delims=," %%a in ("%INPUT_FILE%") do (
    set /a TOTAL_ROWS+=1
    set /a TOTAL_SALES+=%%c
    echo Entry !TOTAL_ROWS!: Customer %%b purchased $%%c in %%d >> "%REPORT_FILE%"
)

echo. >> "%REPORT_FILE%"
echo ----------------------------------------------------- >> "%REPORT_FILE%"
echo Total Transactions: !TOTAL_ROWS!                      >> "%REPORT_FILE%"
echo Gross Sales Total:  $!TOTAL_SALES!                    >> "%REPORT_FILE%"
echo ----------------------------------------------------- >> "%REPORT_FILE%"

echo Report successfully saved to: %REPORT_FILE%
type "%REPORT_FILE%"

endlocal
exit /b 0
```

#### Recipe 3: Robust Service Runner with Automatic Retries
```cmd
@echo off
setlocal EnableDelayedExpansion

set "SERVICE_NAME=DataSyncWorker"
set "LOG_FILE=%TEMP%\%SERVICE_NAME%.log"
set /a MAX_ATTEMPTS=3
set /a ATTEMPT=0

echo [%DATE% %TIME%] %SERVICE_NAME% supervisor started. > "%LOG_FILE%"

:service_loop
set /a ATTEMPT+=1
echo [INFO] Attempt %ATTEMPT% of %MAX_ATTEMPTS%: launching task... | tee -a "%LOG_FILE%"

rem Simulate task workload
ping -n 2 127.0.0.1 >nul

rem Check result of task
if %ATTEMPT% LSS 3 (
    set /a SIMULATED_EXIT=1
) else (
    set /a SIMULATED_EXIT=0
)

if %SIMULATED_EXIT% EQU 0 (
    echo [SUCCESS] Task completed cleanly on attempt %ATTEMPT%. | tee -a "%LOG_FILE%"
    goto :service_complete
) else (
    echo [WARN] Task failed with exit code %SIMULATED_EXIT%. | tee -a "%LOG_FILE%"
    if %ATTEMPT% LSS %MAX_ATTEMPTS% (
        echo [INFO] Waiting 2 seconds before retry...
        ping -n 3 127.0.0.1 >nul
        goto :service_loop
    ) else (
        echo [ERROR] Maximum attempts reached. Terminating. | tee -a "%LOG_FILE%"
        endlocal
        exit /b 1
    )
)

:service_complete
endlocal
exit /b 0
```

---

## 8. Syntax Comparison Cheat Sheet

| Task | POSIX / KornShell (`.sh`) | Windows Batch (`.cmd`) |
| :--- | :--- | :--- |
| **Print text** | `echo "Hello World"` | `echo Hello World` |
| **Print blank line** | `echo ""` or `printf '\n'` | `echo.` |
| **Comment** | `# comment` | `rem comment` or `:: comment` |
| **Assign variable** | `name="John"` *(no spaces)* | `set "name=John"` |
| **Read variable** | `$name` or `${name}` | `%name%` or delayed `!name!` |
| **String length** | `${#name}` | Computed via loop / slicing |
| **Substring** | `${name:0:4}` | `%name:~0,4%` |
| **String replace** | `${name/old/new}` | `%name:old=new%` |
| **Arithmetic** | `x=$((a + b * 2))` | `set /a x=a+b*2` |
| **If equality** | `if [ "$a" = "$b" ]; then` | `if "%a%"=="%b%" (` |
| **If number compare**| `if [ "$a" -gt "$b" ]; then` | `if %a% GTR %b% (` |
| **If file exists** | `if [ -f "app.exe" ]; then` | `if exist "app.exe" (` |
| **If dir exists** | `if [ -d "src" ]; then` | `if exist "src\" (` |
| **Else branch** | `else` ... `fi` | `) else (` |
| **Number loop** | `for ((i=1; i<=10; i++)); do` | `for /L %%i in (1,1,10) do (` |
| **Items loop** | `for x in a b c; do` | `for %%x in (a b c) do (` |
| **Files loop** | `for f in *.txt; do` | `for %%f in (*.txt) do (` |
| **Command loop** | `for l in $(cmd); do` | `for /F "tokens=*" %%l in ('cmd') do (` |
| **Define function**| `my_func() { ...; }` | `:my_func` ... `exit /b 0` |
| **Call function** | `my_func arg1 arg2` | `call :my_func arg1 arg2` |
| **Function arg** | `$1`, `$2`, `$@` | `%1`, `%2`, `%*` |
| **Exit function** | `return 0` | `exit /b 0` |
| **Exit script** | `exit 0` | `exit /b 0` |
| **Check exit code**| `if [ $? -eq 0 ]; then` | `if errorlevel 0` or `%ERRORLEVEL%` |
| **Null redirection**| `> /dev/null 2>&1` | `>nul 2>nul` |
| **Current dir** | `$(pwd)` or `$PWD` | `%CD%` |
| **Script dir** | `$(dirname "$0")` | `%~dp0` |
| **Script filename**| `$(basename "$0")` | `%~nx0` |
| **Temporary dir** | `$TEMP` or `/tmp` | `%TEMP%` |

---

## 9. Advanced KornShell 93 (ksh93) Architecture

`ntksh` implements standard ksh93 behaviors natively on Windows NT without Cygwin or MSYS runtimes:

### 1. Transactional Subshell Isolation `(...)`
Subshells executed inside parentheses inherit functions, aliases, types, and variables from the parent, while isolating all variable assignments, state mutations, and current working directory modifications (`cd`):
```sh
v=10
(
    v=20
    cd sub_dir
    print "Inside: v=$v, dir=$(pwd)"
)
print "Parent: v=$v, dir=$(pwd)"   # v remains 10, directory unchanged
```

### 2. Two-Way Coprocess Subsystem (`|&`, `print -p`, `read -p`)
Start persistent concurrent background coprocesses with bi-directional pipes:
```sh
( while read -r line; do print "REPLY: $line"; done ) |&
print -p "request payload"
read -p response
print "Received: $response"

# Redirection with coprocess descriptors:
exec 3>&p; exec 3>&-     # Duplicate and close coprocess write channel
```

### 3. Raw Network Socket Redirection (`/dev/tcp` & `/dev/udp`)
Native Winsock2 socket descriptors allow pure KornShell network client communication:
```sh
# Open bidirectional TCP socket to remote service
exec 3<>/dev/tcp/example.com/80
print -u3 "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n"
while read -u3 -r line; do
    print "$line"
done
exec 3>&-   # Close socket
```

### 4. NT Kernel Process Freezing & Job Control
Windows NT native thread/process freezing via `NtSuspendProcess` and `NtResumeProcess`:
```sh
long_task &
pid=$!

# Freeze process (suspend all threads)
kill -STOP $pid

# Resume process (thaw all threads)
kill -CONT $pid

# Bring to foreground or background
fg %1
bg %1
```

### 5. Pipeline Stream Concurrency
Compound blocks (`while`, `for`, `if`, `{ ... }`, `( ... )`) can directly terminate pipelines with streaming buffered descriptors:
```sh
print -l red green blue | while read -r color; do
    print "Color received: $color"
done

# Background subshell self-spawning
( print "Async subshell done" ) &
wait $!
```

---

## 9. Module System & Custom Extensions

### Overview & Architecture
`ntksh` features a dual-tier module system allowing developers and sysadmins to extend the shell with either:
1. **Compiled Native Windows DLLs (`.dll`)**: High-performance C, C++, Rust, Zig, or Go dynamic link libraries that run in-process with direct access to the shell engine via the `KshPluginApi` C ABI.
2. **Reusable Script Packages (`.ksh` / `.kshm`)**: Modular shell script libraries discovered automatically across `KSH_MODULE_PATH`, `~/.ksh/modules`, `%APPDATA%\ksh\modules`, and `%PROGRAMDATA%\ksh\modules` with idempotent single-load semantics.

---

### Modern `module` Command Suite
The modern `module` command manages dynamic extensions and script packages:

| Command | Description |
|:---|:---|
| `module load <path/to/module.dll> [cmd]` | Load a native DLL module and register its commands into the shell table |
| `module unload <name>` | Unload a module or dynamic built-in, releasing its DLL handle and resources |
| `module import <pkg_name>` | Import and source a script package from `KSH_MODULE_PATH` |
| `module list` | List all currently loaded native modules, script packages, and dynamic built-ins |
| `module help` | Display usage and subcommand documentation |

```sh
# Load a native extension DLL
module load ./sample_module.dll

# Execute commands registered by the module
sample_hello "DevOps Team"
sample_add 100 250

# Inspect active modules and dynamic built-ins
module list

# Unload the module
module unload sample_module
```

---

### Classic ksh93 `builtin -f` / `builtin -d` Syntax
For strict compatibility with AT&T ksh93 and scripts written for Unix environments:

```sh
# Load a dynamic command from a shared library / DLL
builtin -f ./sample_module.dll sample_hello

# Run the dynamically added built-in
sample_hello

# List dynamic built-ins (or list all built-ins)
builtin -s

# Unload / delete a dynamic built-in
builtin -d sample_hello
```

`builtin -f` searches for standard ksh93 entry points:
- `b_<cmd>` (e.g., `b_sample_hello`)
- `ksh_builtin_<cmd>`
- `Builtin_<cmd>`
- Direct symbol `<cmd>`
- Or multi-command plugins implementing `ksh_module_init`.

---

### Authoring Native C/C++ DLL Extensions (`ksh/module.h`)
Native modules include `<ksh/module.h>` and implement `ksh_module_init`:

```c
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ksh/module.h>
#include <ksh/ast.h>

static const KshPluginApi* g_api = NULL;

static bool Builtin_MyCommand(CommandAST* cmd, int* exit_code) {
    const wchar_t* arg = (cmd->argc > 1) ? cmd->argv[1] : L"default";
    g_api->Print(L"Custom native built-in executed with arg: %ls\n", arg);

    /* Access or set shell variables */
    g_api->SetVariable(L"MY_EXTENSION_STATUS", L"ok");

    *exit_code = 0;
    return true;
}

__declspec(dllexport) int ksh_module_init(const KshPluginApi* api) {
    if (!api || api->api_version != KSH_MODULE_API_VERSION) return KSH_MODULE_ERR_VERSION;
    g_api = api;

    /* Register built-in commands */
    g_api->RegisterBuiltin(L"my_cmd", Builtin_MyCommand, L"my_cmd [arg] - Custom extension");
    return KSH_MODULE_SUCCESS;
}

__declspec(dllexport) void ksh_module_fini(void) {
    /* Optional cleanup when module is unloaded */
    g_api = NULL;
}
```

#### Compilation:
```cmd
clang -shared -O2 -Iinclude my_module.c -o my_module.dll
```

---

### Authoring Shell Script Packages (`.ksh` / `KSH_MODULE_PATH`)
Create a `.ksh` package file (e.g. `math_pkg.ksh`):

```sh
# math_pkg.ksh
function math_square {
    typeset -i n=$1
    echo $(( n * n ))
}

function math_cube {
    typeset -i n=$1
    echo $(( n * n * n ))
}
```

Import from scripts or interactive shells:
```sh
export KSH_MODULE_PATH="/path/to/modules;C:/ProgramData/ksh/modules"
module import math_pkg

math_square 12   # outputs: 144
math_cube 5      # outputs: 125
```
Repeated `module import` calls are idempotent: packages are sourced once, avoiding duplicate function declarations and environment pollution.

---

### Interactive Dynamic Tab Completion
All commands registered through native modules or script packages are immediately available in the interactive REPL. Pressing `[Tab]` in command position automatically completes dynamic module commands alongside standard built-ins, aliases, and functions.


