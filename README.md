# ntksh — Windows NT KornShell & Embedded Coreutils

[![License: BSD-3-Clause](https://img.shields.io/badge/License-BSD--3--Clause-blue.svg)](LICENSE)
[![Platform: Windows NT x86_64](https://img.shields.io/badge/Platform-Windows%20NT%20(x86__64)-0078D6.svg?logo=windows)](https://microsoft.com/windows)
[![Architecture: Native Win32](https://img.shields.io/badge/Runtime-Native%20Win32%20(No%20Cygwin%2FMSYS)-brightgreen.svg)]()
[![Security: Enterprise Hardened](https://img.shields.io/badge/Security-CFG%20%7C%20CET%20%7C%20ASLR%20%7C%20DEP-success.svg)]()
[![Code Signing: Authenticode](https://img.shields.io/badge/Authenticode-SHA--256%20Signed-informational.svg)]()
[![Fuzz Testing: ASan/UBSan](https://img.shields.io/badge/Fuzzing-ASan%20%2B%20UBSan%20Verified-blueviolet.svg)]()

**ntksh** is a native, standalone, zero-dependency KornShell (AT&T ksh93u+m compatible) runtime and BSD coreutils engine built ground-up for Windows NT (`x86_64-pc-windows-msvc`).

Unlike Cygwin, MSYS2, MinGW, or WSL, **ntksh** executes directly against the native Windows Win32 and NT subsystems. It requires no translation emulation layer, no virtual filesystem mounts, no POSIX runtime DLLs, and no background virtualization services. It provides standard KornShell scripting, job control, pipelines, process substitution, and a rich suite of embedded coreutils (including `awk`, `sed`, `grep`, `diff`, `curl`, and cryptographic tools) within a single, lightweight binary.

---

## Key Highlights

- **Pure Win32 Execution**: Direct process lifecycle management via `CreateProcessW`, Win32 file streaming, console API integration, and Windows NT path resolution.
- **SQLite-Style Single-File Amalgamation**: Ships both as a modular codebase and a consolidated, single-file C distribution (`src/ksh.c`) that compiles in seconds with zero configuration.
- **Zero External Dependencies**: Links exclusively against base Windows NT operating system libraries (`kernel32.dll`, `user32.dll`, `advapi32.dll`, `ws2_32.dll`, `crypt32.dll`, `shell32.dll`, `ole32.dll`).
- **Production Hardened**: Built with modern exploit mitigations including Control Flow Guard (CFG), Intel CET Shadow Stack, High-Entropy 64-bit ASLR, DEP, Stack Canaries, and UTF-8 BOM sanitization.
- **Enterprise Ready**: Full Long Path Support (`\\?\` / `longPathAware: true`), PerMonitorV2 high-DPI scaling, and SHA-256 Authenticode code signing.
- **Embedded Coreutils**: Includes complete implementations of essential POSIX/BSD utilities directly inside the shell executable—no external `find`, `awk`, `sed`, or `grep` binaries required.
- **Native Batch Interoperability**: Executes `.bat` and `.cmd` scripts with environment variable propagation, `%ERRORLEVEL%`, `%~dp0`, and `FOR` loop variable semantics natively without launching `cmd.exe`.

---

## Supported Systems

`ntksh` is engineered as a native 64-bit Win32 console application with zero external runtime dependencies. The following platforms are fully supported and verified:

- **Windows 7 64-bit**
- **Windows 8 / 8.1 64-bit**
- **Windows 10 64-bit**
- **Windows 11**
- **Windows Server 2019 / 2022 / 2025**
*(All tested)*

---

## KornShell Compatibility

ntksh implements core AT&T ksh93u+m syntax and semantics:

- **Parsing & Control Flow**: Full AST parser supporting compound commands, `if`/`then`/`elif`/`else`/`fi`, `for` loops, `while`/`until`, C-style `for ((i=0; i<n; i++))` loops, `case` pattern matching, and `select` menus.
- **Pipelines & Redirections**: Multi-stage pipes (`|`), coprocess pipes (`|&`), file descriptors (`<`, `>`, `>>`, `<&`, `>&`), here-documents (`<<EOF`), and here-strings (`<<< "text"`).
- **Process & Command Substitutions**: Modern `$(...)` and legacy `` `...` `` command substitution, plus process substitution `<(command)` and `>(command)` backed by anonymous Win32 pipes.
- **Shell Variables & Typeset**: Dynamic typing, compound variables, multidimensional arrays (`matrix[i][j]`), array-of-compounds (`point[0].x`), associative arrays (`typeset -A`), namerefs (`typeset -n`), and readonly attributes (`typeset -r`).
- **Expansions**:
  - Parameter expansion: `${#var}`, `${var:-def}`, `${var:=def}`, `${var:+alt}`, `${var:offset:length}`, `${var#pattern}`, `${var##pattern}`, `${var%pattern}`, `${var%%pattern}`, `${var/pattern/replacement}`.
  - Arithmetic evaluation: `$(( ... ))` supporting 64-bit integers, floating-point math, standard C99 math functions (`sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sqrt`, `cbrt`, `pow`, `hypot`, `exp`, `log`, `log10`, `log2`, `floor`, `ceil`, `round`, `trunc`, `abs`, `fmod`), bitwise operations, logical operators, and precedence.
  - Extended Pattern Matching & Globbing: Full ksh93 extended globs (`!(...)`, `@(...)`, `*(...)`, `+(...)`, `?(...)`) and POSIX character classes (`[[:alpha:]]`, `[[:digit:]]`, `[[:space:]]`, `[[:alnum:]]`, `[[:xdigit:]]`, etc.) in pathname expansion, `case` statements, and `[[ ... ]]`.
  - Tilde expansion: `~` and `~user` mapped to `%USERPROFILE%`.
  - ANSI-C Quoting: `$'...'` with full escape sequences (`\n`, `\t`, `\xHH`, `\uXXXX`).
- **Job Control & Signals**: Background jobs (`&`), job tracking (`jobs`, `fg`, `bg`), and POSIX-to-Win32 signal emulation (`trap`, `kill`).
- **Dynamic Module System**: Both modern `module <load|unload|import|list>` and classic ksh93 `builtin -f file.dll [cmd]` / `builtin -d [cmd]` interfaces for extending the shell with native C/C++ DLLs and `KSH_MODULE_PATH` script packages.

---

## Dynamic Module & Extension System

ntksh includes a dual-tier module architecture that lets you extend the shell at runtime with native compiled DLLs or reusable script packages:

### 1. Modern Module Interface
```sh
# Load a native Windows DLL extension
module load ./sample_module.dll

# Execute commands exported by the extension
sample_hello "World"
sample_add 10 32

# List loaded modules and active dynamic built-ins
module list

# Unload the module
module unload sample_module

# Import a script package from KSH_MODULE_PATH
export KSH_MODULE_PATH="modules;/usr/lib/ksh"
module import my_package
```

### 2. Classic ksh93 Dynamic Built-in Syntax
```sh
# Load a dynamic built-in from a shared library / DLL
builtin -f ./sample_module.dll sample_classic

# Execute the built-in directly
sample_classic

# List all active dynamic built-ins
builtin -s

# Remove a dynamic built-in
builtin -d sample_classic
```

### 3. Developer Extension C ABI
Include `<ksh/module.h>` and implement `ksh_module_init(const KshPluginApi* api)` to register custom built-ins, query/set variables, evaluate scripts, and format output. See `examples/sample_module/` for complete sample code.

---

## Embedded Coreutils

ntksh bundles an extensive array of built-in commands directly into the shell binary:

| Category | Built-in Commands |
| :--- | :--- |
| **Text Processing** | `awk`, `sed`, `grep`, `diff`, `cut`, `tr`, `sort`, `uniq`, `wc`, `head`, `tail`, `tee`, `xargs`, `paste`, `fmt`, `fold`, `comm`, `csplit`, `join`, `expand`, `unexpand`, `look`, `lam`, `vis`, `unvis`, `apply`, `pr`, `banner`, `col`, `ul`, `caesar`, `rot13` |
| **Filesystem & I/O** | `cat`, `cp`, `mv`, `rm`, `mkdir`, `rmdir`, `touch`, `ls`, `chmod`, `chown`, `du`, `df`, `find`, `basename`, `dirname`, `realpath`, `ln`, `stat`, `file`, `sync`, `dd`, `mtree`, `fstat`, `iostat`, `srm`, `shred`, `what` |
| **Networking** | `curl`, `wget`, `nc` (netcat), `sockstat`, `ifconfig`, `netstat`, `devinfo`, `arp`, `host`, `ping`, `traceroute`, `whois` |
| **Cryptography & Encoding** | `base64`, `sha256`, `sha1`, `md5`, `cksum`, `sum`, `od` (via native Windows CryptoAPI / CNG / UCRT) |
| **Archive & Compression** | `tar`, `pax`, `gzip`, `gunzip`, `zcat` |
| **System & Shell State** | `ps`, `kill`, `env`, `export`, `unset`, `alias`, `unalias`, `set`, `typeset`, `read`, `echo`, `print`, `printf`, `test`, `[`, `pwd`, `cd`, `history`, `uname`, `whoami`, `sleep`, `which`, `clear`, `vmstat`, `daemon`, `pbcopy`, `pbpaste`, `script`, `sysctl`, `renice`, `getent`, `leave` |

---

## Security & Exploit Mitigations

Production builds of `ntksh` are hardened against memory corruption and tampering using enterprise compiler and linker flags:

- **Control Flow Guard (`/GUARD:CF`)**: Validates indirect call targets to prevent return-oriented and jump-oriented programming (ROP/JOP) attacks.
- **Hardware-Enforced Stack Protection (`/CETCOMPAT`)**: Fully compatible with Intel Control-Flow Enforcement Technology (CET) Shadow Stacks.
- **High-Entropy 64-bit ASLR (`/HIGHENTROPYVA` & `/DYNAMICBASE`)**: Maximizes memory randomization space across 64-bit user address space.
- **Data Execution Prevention (`/NXCOMPAT`)**: Enforces non-executable stack and heap boundaries.
- **Stack Smashing Protection (`-fstack-protector-strong`)**: Inserts canary guards on vulnerable stack frames to detect buffer overflows.
- **Embedded Win32 Manifest**: Declares Windows 10/11 compatibility, enables long path awareness (`longPathAware: true`), and activates PerMonitorV2 high-DPI scaling.
- **Authenticode Signing**: Built-in automated signing (`tools/sign.ps1`) verifies binary integrity and provenance via SHA-256 Authenticode digital certificates.
- **Rigorous Fuzz Testing**: Verified under LLVM AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan) across hundreds of adversarial grammar and memory stress permutations.

---

## Repository Structure

```
e:\ksh\
├── bin/                       # Compiled production binaries (ksh.exe, signed artifacts)
├── include/                   # Public and internal C headers
│   └── ksh/
│       ├── ksh.h              # Unified header
│       ├── core.h             # Shell engine, AST, executor, tokenizer
│       ├── builtins.h         # Built-in declarations
│       ├── batch.h            # Windows CMD/Batch runtime engine
│       └── help.h             # Interactive documentation
├── src/                       # Modular source code
│   ├── core/                  # AST, parser, executor, lexer, variables, signals
│   ├── builtins/              # Embedded coreutils (awk, sed, grep, diff, net, crypto, etc.)
│   ├── batch/                 # Native .bat/.cmd executor & expansion engine
│   ├── help/                  # Internal help documentation strings
│   ├── ksh.manifest           # Embedded Win32 manifest (Long paths, DPI, OS GUIDs)
│   ├── ksh.c                  # Consolidated single-file SQLite-style amalgamation
│   └── main.c                 # CLI entry point
├── tests/                     # Test suites
│   ├── builtins.ps1           # Comprehensive built-in regression test suite
│   └── run_tests.ps1          # Master test harness
├── tools/                     # Developer and release tooling
│   ├── amalgamate.ps1         # Pure PowerShell single-file amalgamation generator
│   ├── sign.ps1               # Authenticode SHA-256 code-signing script
│   └── fuzz_test.ps1          # Adversarial fuzzing harness (ASan/UBSan)
├── build.ps1                  # Master automated build script
├── architecture_and_developer_guide.md # Comprehensive Architecture & Developer Guide
├── user_guide.md              # End-user and scripting manual
├── command_reference.md       # Complete built-in command reference
├── LICENSE                    # BSD 3-Clause License
└── README.md                  # Project documentation
```

---

## Documentation

- **[User & Scripting Guide](user_guide.md)**: Comprehensive user manual, POSIX & KornShell syntax, Windows Batch runtime, cookbook recipes, and migration guide.
- **[Command Reference](command_reference.md)**: Complete parameter and flag documentation for all 100+ embedded coreutils and built-ins.
- **[Architecture & Developer Guide](architecture_and_developer_guide.md)**: In-depth internals, memory arena management, AST parser, Win32 execution engine, C extension ABI (`ksh/module.h`), and contributor instructions.

---

## Building from Source

### Prerequisites

- **Compiler**: Clang (LLVM 15+) or Microsoft Visual C++ (MSVC v143+ / Build Tools).
- **Environment**: Windows 7 64-bit, Windows 8 64-bit, Windows 10 64-bit, Windows 11, or Windows Server 2019/2022/2025 (x86_64).
- **Shell**: PowerShell 5.1 or PowerShell Core 7+.

### 1. Standard Modular Build
Compiles all modular source files into `bin\ksh.exe` with enterprise security mitigations enabled:
```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

### 2. Single-File Amalgamated Build
Compiles directly from the single-file SQLite-style distribution (`src/ksh.c`):
```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Amalgamate
```

### 3. Production Hardened & Authenticode Signed Build
Compiles with full mitigations and signs the output binary with a SHA-256 Authenticode certificate:
```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Sign
```

### 4. AddressSanitizer (ASan) & UBSan Build
Builds instrumented binaries for memory safety analysis, leak detection, and fuzzing:
```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -ASan
```

### 5. Regenerating the Amalgamation Source
To regenerate the single-file distribution (`src/ksh.c`) from the modular tree without external dependencies:
```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -GenerateAmalgamation
```

---

## Verification & Testing

### Built-in Self-Test
The binary features an internal self-test mode that verifies internal data structures, parser state machines, and core algorithms:
```powershell
.\bin\ksh.exe --self-test
```

### Regression Test Suite
Executes the comprehensive functional test suite covering parsing, variable expansion, batch execution, and built-ins:
```powershell
powershell -ExecutionPolicy Bypass -File .\tests\builtins.ps1
```

### Automated Fuzz Testing
Runs iterative fuzzing against the ASan-instrumented binary with malformed commands, deep pipeline recursion, and syntax boundary edge cases:
```powershell
powershell -ExecutionPolicy Bypass -File .\tools\fuzz_test.ps1 -Iterations 500
```

---

## Quick Start & Usage Examples

### Interactive Shell
```cmd
.\bin\ksh.exe
```

### Script Execution
```cmd
.\bin\ksh.exe -c "for i in {1..5}; do printf 'Task %d completed\n' $i; done"
```

### Pipeline with Embedded Coreutils
Process data directly without external utilities installed:
```ksh
cat server.log | grep -E "ERROR|FATAL" | awk '{print $1, $2, $5}' | sort | uniq -c
```

### Native Cryptographic Checksums
```ksh
sha256 myfile.iso
base64 -d encoded.txt > decoded.bin
```

### Embedded Networking
Fetch web assets or test remote sockets:
```ksh
curl -s https://example.com/api/data.json | grep "version"
nc -z -v 127.0.0.1 8080
```

### Calling Windows Batch Scripts with Errorlevel Propagation
```ksh
call_legacy_job.bat
if [[ $? -eq 0 ]]; then
    echo "Batch script succeeded"
fi
```

---

## License & Authors

`ntksh` is released under the terms of the **BSD 3-Clause License**. See [LICENSE](LICENSE) for details.

### Development & AI Disclosure
AI code assist was used during development: **17% of code is AI generated**, with the rest human led, designed, and verified.

### Development Team & Contributors
- **Roberto J. Dohnert**
- **Kelly Wells**
- **Adam Wells**
- **Mary Staats**
- **John Albright**

*PC/OpenSystems LLC*
