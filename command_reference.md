# ntksh Built-in Commands Reference Manual

Comprehensive technical reference manual for all built-in commands and coreutils embedded in **ntksh** (Windows NT KornShell). Every command listed executes natively in-process on Windows NT without external DLLs, MSYS, or Cygwin dependencies.

---

## Table of Contents

1. [Special Built-ins & Evaluators (`:`, `.`, `[`, `[[`, `((`, `let`)](#1-special-built-ins--evaluators)
2. [Shell Control, Flow & Scoping](#2-shell-control-flow--scoping)
3. [Environment, Typeset & Variable Management](#3-environment-typeset--variable-management)
4. [I/O, Printing & Reading](#4-io-printing--reading)
5. [Process Control, Jobs & Traps](#5-process-control-jobs--traps)
6. [Interactive Shell, Navigation & History](#6-interactive-shell-navigation--history)
7. [Text Processing & Stream Editing](#7-text-processing--stream-editing)
8. [Filesystem Operations & Metadata](#8-filesystem-operations--metadata)
9. [Archive & Compression](#9-archive--compression)
10. [Cryptography & Message Digests](#10-cryptography--message-digests)
11. [Networking & Diagnostics](#11-networking--diagnostics)
12. [System Information & Process Inspection](#12-system-information--process-inspection)
13. [Windows Batch & CMD Built-in Suite](#13-windows-batch--cmd-built-in-suite)
14. [Dynamic Module & Extension System](#14-dynamic-module--extension-system)

---

## 1. Special Built-ins & Evaluators

### `:` (colon)
- **Syntax:** `: [arguments...]`
- **Description:** Null command; expands arguments and redirections, returns exit status `0`.
- **Example:**
  ```sh
  : ${TIMEOUT:=30}    # Set default if unset
  while :; do echo "loop"; break; done
  ```

### `.` (dot) / `source`
- **Syntax:** `. filename [arguments...]` or `source filename [arguments...]`
- **Description:** Read and execute commands from `filename` in the current shell environment. Positional parameters are temporarily set to `arguments`.
- **Example:**
  ```sh
  . ~/ksh.ini
  source ./config.env
  ```

### `[` (test)
- **Syntax:** `[ expression ]`
- **Description:** POSIX standard condition evaluation utility. Closing bracket `]` is required.
- **Example:**
  ```sh
  [ -f "app.log" ] && echo "Found file"
  [ "$val" -gt 10 ] && echo "Greater"
  ```

### `[[` (extended test)
- **Syntax:** `[[ expression ]]`
- **Description:** KornShell extended compound conditional evaluator. Supports regex matching (`=~`), wildcard glob matching (`==`, `!=`), POSIX character classes (`[[:alpha:]]`, `[[:digit:]]`), and logical operators (`&&`, `||`, `!`). Words are not subjected to word splitting or pathname expansion.
- **Example:**
  ```sh
  [[ $filename == @(*.c|*.h) ]] && echo "C source/header"
  [[ $name == [[:alpha:]]* ]] && echo "Starts with letter"
  [[ $input =~ ^[0-9]+$ ]] && echo "Numeric integer"
  ```

### `((` `))` (arithmetic command)
- **Syntax:** `(( expression ))`
- **Description:** Evaluates arithmetic expression. If the value of the expression is non-zero, exit status is 0; otherwise 1. Supports assignments, pre/post increment, and C99 math functions.
- **Example:**
  ```sh
  (( count++ ))
  (( x = sqrt(16) + 10 ))
  if (( x > 10 )); then echo "Large"; fi
  ```

### `let`
- **Syntax:** `let arg [arg...]`
- **Description:** Evaluates each argument as an arithmetic expression. Returns 0 if the last expression evaluates to non-zero, 1 if zero.
- **Example:**
  ```sh
  let "x = 10 * 5" "y = x + 2"
  let "z = abs(-42)"
  ```

### `expr`
- **Syntax:** `expr arg1 op arg2`
- **Description:** Evaluates an expression and prints the result to standard output (arithmetic, comparison, string length).
- **Example:**
  ```sh
  expr 5 + 10
  expr length "hello world"
  ```

---

## 2. Shell Control, Flow & Scoping

### `eval`
- **Syntax:** `eval [arg...]`
- **Description:** Concatenates arguments with spaces, parses the resulting string as shell code, and executes it in the current environment.
- **Example:**
  ```sh
  cmd="echo 'Running dynamic code'"
  eval "$cmd"
  ```

### `exec`
- **Syntax:** `exec [command [arguments...]]` or `exec <file`, `exec >file`, `exec 3>file`
- **Description:** Replaces shell with `command`, or applies permanent I/O redirections to file descriptors (0–9) without creating a child process.
- **Example:**
  ```sh
  exec 3>&1 >logfile.txt    # Save stdout in fd 3, redirect stdout to file
  exec 1>&3 3>&-           # Restore stdout, close fd 3
  ```

### `command`
- **Syntax:** `command [-v|-V|-p] command [args...]`
- **Description:** Executes `command` while suppressing shell function lookup. `-v` prints command resolution path/type; `-p` restricts PATH to default system directories.
- **Example:**
  ```sh
  command ls -la
  command -v awk
  ```

### `builtin`
- **Syntax:** `builtin [-f lib.dll [cmd]] [-d cmd] [-s] [cmd [args...]]`
- **Description:** Classic ksh93 dynamic built-in loader. `-f` dynamically loads built-in `cmd` from Windows DLL; `-d` removes dynamic built-in; `-s` lists dynamic built-ins; without flags executes built-in directly bypassing functions and aliases.
- **Example:**
  ```sh
  builtin -f ./sample_module.dll sample_classic
  builtin -s
  builtin -d sample_classic
  ```

### `break`
- **Syntax:** `break [n]`
- **Description:** Exits from within the innermost `n` enclosing `for`, `while`, or `until` loops.
- **Example:**
  ```sh
  for i in 1 2 3 4 5; do
      [ "$i" -eq 3 ] && break
  done
  ```

### `continue`
- **Syntax:** `continue [n]`
- **Description:** Resumes the next iteration of the `n`-th enclosing loop.
- **Example:**
  ```sh
  for i in 1 2 3; do
      [ "$i" -eq 2 ] && continue
      echo "$i"
  done
  ```

### `return`
- **Syntax:** `return [n]`
- **Description:** Causes a function or sourced script to stop executing and return status `n` to the caller.
- **Example:**
  ```sh
  my_func() {
      [ -z "$1" ] && return 1
      return 0
  }
  ```

### `exit`
- **Syntax:** `exit [n]`
- **Description:** Terminates the shell session with exit status code `n`.
- **Example:**
  ```sh
  exit 0
  ```

### `true` / `false`
- **Syntax:** `true`, `false`
- **Description:** `true` returns exit status 0 (success); `false` returns exit status 1 (failure).
- **Example:**
  ```sh
  while true; do break; done
  ```

### `functions` / `autoload`
- **Syntax:** `functions [name...]`, `autoload [name...]`
- **Description:** Lists defined shell functions or marks functions for autoloading from `FPATH`.
- **Example:**
  ```sh
  functions
  ```

---

## 3. Environment, Typeset & Variable Management

### `export` / `setenv`
- **Syntax:** `export [-p] [name[=value]...]`, `setenv [name [value]]`
- **Description:** Marks shell variables for export to child processes via Win32 environment inheritance. `-p` outputs reusable export statements.
- **Example:**
  ```sh
  export PATH="/usr/bin:$PATH"
  export PORT=8080
  export -p
  ```

### `unset`
- **Syntax:** `unset [-v|-f] name...`
- **Description:** Unsets environment variables, shell variables, or functions (`-f`).
- **Example:**
  ```sh
  unset MY_VAR
  unset -f my_func
  ```

### `readonly`
- **Syntax:** `readonly [-p] [name[=value]...]`
- **Description:** Marks variables as read-only; subsequent assignments or `unset` produce an error.
- **Example:**
  ```sh
  readonly API_KEY="abc-123"
  ```

### `typeset`
- **Syntax:** `typeset [-p] [-ilurx] [-a|-A|-n] [-Z[n]] [-L[n]|-R[n]] [-T type] [name[=value]...]`
- **Description:** Sets or displays variable types and attributes conforming to ksh93 standards:
  - `-i[base]`: Integer type (optional numeric base).
  - `-l`: Converts assigned values to lowercase.
  - `-u`: Converts assigned values to uppercase.
  - `-r`: Readonly variable attribute.
  - `-x`: Export to environment.
  - `-a`: Indexed array.
  - `-A`: Associative array (key-value hash table).
  - `-n`: Nameref (reference variable / pointer).
  - `-Z[n]`: Right-justified zero-filled numeric padding.
  - `-L[n]`: Left-justified text formatting.
  - `-R[n]`: Right-justified text formatting.
  - `-T`: Define new object type/class with fields and methods.
- **Example:**
  ```sh
  typeset -u UPPER="make upper"
  typeset -i COUNT=42
  typeset -A servers
  typeset -n ref=UPPER
  typeset -Z5 NUM=7             # Output: 00007
  typeset -T Point_t=( float x=0; float y=0 )
  ```

### `local`
- **Syntax:** `local name[=value]...`
- **Description:** Declares variable with function-local scope. Restores previous caller value when function exits.
- **Example:**
  ```sh
  my_func() {
      local temp="isolated"
  }
  ```

### `integer` / `float` / `nameref`
- **Syntax:** `integer var`, `float var`, `nameref ref=target`
- **Description:** Convenience aliases for `typeset -i`, `typeset -E`, and `typeset -n`.
- **Example:**
  ```sh
  integer index=0
  float pi=3.14159
  nameref target_ref=index
  ```

### `setvar`
- **Syntax:** `setvar name value`
- **Description:** Assigns literal value to shell variable without evaluation.
- **Example:**
  ```sh
  setvar raw_text "val with spaces"
  ```

### `set`
- **Syntax:** `set [-e|-u|-x|-n|-v|-C|-o option] [-- args...]`
- **Description:** Controls shell execution flags and positional parameters:
  - `-e`: Exit immediately on unhandled command failure.
  - `-u`: Error when expanding unset variables (`nounset`).
  - `-x`: Trace executed commands to stderr (`xtrace`).
  - `-n`: Read commands without executing (`noexec`).
  - `-v`: Print shell input lines as they are read (`verbose`).
  - `-C`: Prevent overwriting existing regular files on redirection (`noclobber`).
  - `-o pipefail`: Pipeline return status equals last non-zero exit code.
  - `-o emacs` / `-o vi`: Toggle command line editing modes.
  - `-- arg1 arg2`: Set positional parameters `$1`, `$2`, etc.
- **Example:**
  ```sh
  set -e -o pipefail
  set -- alpha beta gamma
  ```

### `shift`
- **Syntax:** `shift [n]`
- **Description:** Shifts positional parameters to the left by `n` positions (default `1`). `$#` is decremented.
- **Example:**
  ```sh
  shift 2
  ```

### `umask`
- **Syntax:** `umask [-S] [-p] [mask]`
- **Description:** Sets or displays file creation mask. Supports octal (`022`) or symbolic mode.
- **Example:**
  ```sh
  umask 022
  ```

---

## 4. I/O, Printing & Reading

### `echo`
- **Syntax:** `echo [-n|-e|-E] [args...]`
- **Description:** Writes arguments separated by spaces and terminated with newline to stdout. `-n` suppresses newline; `-e` enables backslash escape interpretation (`\n`, `\t`); `-E` disables escapes.
- **Example:**
  ```sh
  echo -n "Username: "
  echo -e "Line1\nLine2"
  ```

### `print`
- **Syntax:** `print [-nrR] [-u fd] [-f format] [-p] [args...]`
- **Description:** Authentic KornShell output command:
  - `-n`: Suppress trailing newline.
  - `-r` / `-R`: Raw mode; do not interpret backslash escapes.
  - `-u fd`: Write to file descriptor number `fd` (e.g. `-u 2` for stderr).
  - `-f format`: Format like printf.
  - `-p`: Redirect to active coprocess pipeline.
- **Example:**
  ```sh
  print -u 2 "Error message"
  print -r "C:\path\without\escapes"
  print -p "Message to coprocess"
  ```

### `printf`
- **Syntax:** `printf format [arguments...]`
- **Description:** Formats and writes arguments under control of the format string (supports `%d`, `%s`, `%f`, `%x`, `%b`, width/precision specifiers, escape sequences).
- **Example:**
  ```sh
  printf "%-15s: %04d\n" "Item" 42
  printf "Hex: 0x%X\n" 255
  ```

### `read`
- **Syntax:** `read [-r] [-s] [-p prompt] [-n nchars] [-d delim] [-u fd] [-a|-A arr] [name...]`
- **Description:** Reads one line from standard input (or descriptor `fd`), splits fields according to `IFS`, and assigns to variables:
  - `-r`: Raw mode; do not allow backslashes to escape characters.
  - `-s`: Silent mode; do not echo input to console (passwords).
  - `-p prompt`: Display prompt string before reading.
  - `-n count`: Return after reading `count` characters.
  - `-d delim`: Use delimiter character instead of newline.
  - `-a` / `-A`: Read into indexed array.
- **Example:**
  ```sh
  read -r -p "Enter name: " username
  read -s -p "Password: " secret
  read -A items
  ```

---

## 5. Process Control, Jobs & Traps

### `jobs`
- **Syntax:** `jobs [-l|-p|-r|-s]`
- **Description:** Lists active background processes, job IDs, PIDs, and running/stopped state.
- **Example:**
  ```sh
  jobs -l
  ```

### `bg` / `fg`
- **Syntax:** `bg [%jobid]`, `fg [%jobid]`
- **Description:** Resumes a stopped job in the background (`bg`) or brings a background job to the foreground (`fg`).
- **Example:**
  ```sh
  fg %1
  bg %2
  ```

### `disown`
- **Syntax:** `disown [%jobid | pid]`
- **Description:** Removes specified background job from shell job control tracking table.
- **Example:**
  ```sh
  disown %1
  ```

### `wait`
- **Syntax:** `wait [pid | %jobid]`
- **Description:** Waits for specified background child process or job to complete and returns its exit status. Without arguments, waits for all active child jobs.
- **Example:**
  ```sh
  long_job &
  wait $!
  ```

### `kill` / `pkill`
- **Syntax:** `kill [-s sig | -sig] pid...`, `pkill [-f] pattern`
- **Description:** Sends signal to process IDs or matches process names. Supports `INT` (2), `KILL` (9), `TERM` (15), `HUP` (1), and `0` (liveness check). `kill -l` lists supported signals.
- **Example:**
  ```sh
  kill -TERM 1234
  kill -0 1234         # Verify process is running
  pkill notepad.exe
  ```

### `trap`
- **Syntax:** `trap [action] [signal...]`, `trap -p`
- **Description:** Registers shell command `action` to execute upon receiving specified signals or pseudo-signals (`EXIT`, `ERR`, `INT`, `TERM`).
- **Example:**
  ```sh
  trap 'rm -f "$temp_file"' EXIT
  trap 'echo "Aborted"; exit 1' INT
  ```

### `suspend`
- **Syntax:** `suspend [-f]`
- **Description:** Suspends interactive shell process execution until a key is pressed.
- **Example:**
  ```sh
  suspend
  ```

### `times`
- **Syntax:** `times`
- **Description:** Prints accumulated user and system CPU times for the shell and its children.
- **Example:**
  ```sh
  times
  ```

### `ulimit`
- **Syntax:** `ulimit [-a|-n [limit]]`
- **Description:** Displays or sets process resource limits (open file descriptors).
- **Example:**
  ```sh
  ulimit -a
  ```

---

## 6. Interactive Shell, Navigation & History

### `cd` / `chdir`
- **Syntax:** `cd [-L|-P] [directory]`
- **Description:** Changes current working directory. Resolves relative paths via `CDPATH` (semicolon-separated). `cd -` navigates to `OLDPWD`. Accepts `/` and `\` delimiters.
- **Example:**
  ```sh
  cd src/core
  cd -
  ```

### `pwd`
- **Syntax:** `pwd [-L|-P]`
- **Description:** Prints current working directory. `-L` prints logical path preserving symlinks; `-P` resolves physical filesystem path.
- **Example:**
  ```sh
  pwd -L
  ```

### `pushd` / `popd` / `dirs`
- **Syntax:** `pushd directory`, `popd`, `dirs [-c|-v]`
- **Description:** Manages the in-memory directory stack. `pushd` changes directory and pushes to stack; `popd` pops top directory and navigates to it; `dirs` inspects or clears (`-c`) stack.
- **Example:**
  ```sh
  pushd /c/Windows
  dirs -v
  popd
  ```

### `alias` / `unalias`
- **Syntax:** `alias [-p] [name[=value]...]`, `unalias [-a] name...`
- **Description:** Defines or removes command shortcuts. `-p` outputs reusable alias definitions.
- **Example:**
  ```sh
  alias ll="ls -la"
  alias gs="git status"
  unalias ll
  ```

### `type` / `whence` / `which` / `where`
- **Syntax:** `type cmd`, `whence [-a|-v] cmd`, `which cmd`, `where cmd`
- **Description:** Inspects command resolution precedence (alias, function, builtin, external binary path in `PATH`).
- **Example:**
  ```sh
  type awk
  whence -v grep
  which ksh.exe
  ```

### `history` / `hist` / `fc` / `r`
- **Syntax:** `history [-n] [count]`, `r [old=new] [cmd]`
- **Description:** Displays command history list or re-executes previous commands with substitution.
- **Example:**
  ```sh
  history 20
  r                      # Re-execute last command
  r foo=bar 105          # Re-execute #105 replacing foo with bar
  ```

### `clear` / `cls`
- **Syntax:** `clear`, `cls`
- **Description:** Clears the Windows console display buffer and repositions the cursor at `(0,0)`.
- **Example:**
  ```sh
  clear
  ```

### `help` / `man`
- **Syntax:** `help [category | command]`, `man [command]`
- **Description:** Displays categorized built-in help (`scripting`, `coreutils`, `batch`, `all`) or command syntax.
- **Example:**
  ```sh
  help coreutils
  help grep
  ```

---

## 7. Text Processing & Stream Editing

### `awk`
- **Syntax:** `awk [-F fs] [-v var=val] 'program' [file...]`
- **Description:** Embedded pattern scanning, field parsing, and data processing engine. Supports math functions, regex, associative arrays, built-in variables (`NR`, `NF`, `FS`, `OFS`), and custom functions.
- **Example:**
  ```sh
  awk -F: '{print $1, $3}' /etc/passwd
  cat server.log | awk '/ERROR/ {count++} END {print count}'
  ```

### `sed`
- **Syntax:** `sed [-n] [-e script] [-i[ext]] [file...]`
- **Description:** Embedded non-interactive stream editor. Supports substitute commands (`s/pat/rep/flags`), address ranges, deletion (`d`), insertion (`i`), appending (`a`), and hold buffer operations.
- **Example:**
  ```sh
  sed -e 's/localhost/127.0.0.1/g' config.json
  sed -n '10,25p' log.txt
  ```

### `grep` / `egrep` / `fgrep`
- **Syntax:** `grep [-i|-v|-c|-l|-n|-E|-F|-r|-o|-q] pattern [file...]`
- **Description:** Fast text pattern searching using basic, extended (`-E`), or fixed string (`-F`) regex matching across streams or directory trees (`-r`).
- **Example:**
  ```sh
  grep -E "ERROR|FATAL" app.log
  grep -rn "TODO" src/
  ```

### `diff`
- **Syntax:** `diff [-u|-q|-i|-w|-B|-c] file1 file2`
- **Description:** Compares two files line by line. Outputs unified diffs (`-u`), context diffs (`-c`), or brief status (`-q`).
- **Example:**
  ```sh
  diff -u old.c new.c > changes.patch
  ```

### `patch`
- **Syntax:** `patch [-p num] [-R] [-i patchfile]`
- **Description:** Applies unified diff patches to target source files. `-R` reverses patch.
- **Example:**
  ```sh
  patch -p1 -i changes.patch
  ```

### `cut`
- **Syntax:** `cut [-b list] [-c list] [-f list] [-d delim] [file...]`
- **Description:** Extracts sections from each line of files by byte (`-b`), character (`-c`), or delimited field (`-f`).
- **Example:**
  ```sh
  cut -d: -f1,3 /etc/passwd
  cut -c1-10 table.txt
  ```

### `tr`
- **Syntax:** `tr [-c|-d|-s] string1 [string2]`
- **Description:** Translates, squeezes (`-s`), or deletes (`-d`) characters from standard input.
- **Example:**
  ```sh
  cat text.txt | tr '[:lower:]' '[:upper:]'
  cat input.txt | tr -d '\r' > unix.txt
  ```

### `sort`
- **Syntax:** `sort [-r|-n|-f|-u|-k field] [file...]`
- **Description:** Sorts lines of text files. Supports reverse order (`-r`), numeric sorting (`-n`), case-insensitive (`-f`), and unique keys (`-u`).
- **Example:**
  ```sh
  sort -n -k2 data.tsv
  sort -u words.txt
  ```

### `uniq`
- **Syntax:** `uniq [-c|-d|-u|-i] [input [output]]`
- **Description:** Filters or reports adjacent duplicate lines. `-c` prefixes line counts; `-d` prints only duplicates; `-u` prints only unique lines.
- **Example:**
  ```sh
  sort log.txt | uniq -c | sort -nr
  ```

### `wc`
- **Syntax:** `wc [-l|-w|-c|-m|-L] [file...]`
- **Description:** Counts lines (`-l`), words (`-w`), bytes (`-c`), characters (`-m`), or maximum line length (`-L`).
- **Example:**
  ```sh
  wc -l src/*.c
  ```

### `head` / `tail`
- **Syntax:** `head [-n lines | -c bytes] [file...]`, `tail [-n lines | -c bytes | -f] [file...]`
- **Description:** Outputs the first (`head`) or last (`tail`) part of files. `tail -f` continuously follows appending files.
- **Example:**
  ```sh
  head -n 20 access.log
  tail -f /var/log/syslog
  ```

### `tee`
- **Syntax:** `tee [-a] [file...]`
- **Description:** Duplicates standard input to standard output and simultaneously writes copies to specified files. `-a` appends.
- **Example:**
  ```sh
  build.sh 2>&1 | tee build.log
  ```

### `xargs`
- **Syntax:** `xargs [-n num] [-I replace] [-P maxprocs] [command [initial-args]]`
- **Description:** Constructs and executes command lines from standard input items.
- **Example:**
  ```sh
  find . -name "*.tmp" | xargs rm -f
  cat files.txt | xargs -n 1 -I {} cp {} backup/
  ```

### `paste`
- **Syntax:** `paste [-s] [-d delimiters] [file...]`
- **Description:** Merges lines of files sequentially or side-by-side separated by delimiters.
- **Example:**
  ```sh
  paste -d, names.txt emails.txt
  ```

### `fmt` / `fold`
- **Syntax:** `fmt [-w width] [file...]`, `fold [-w width] [-s] [file...]`
- **Description:** Re-formats text paragraphs or breaks long lines to fit specified column width.
- **Example:**
  ```sh
  fmt -w 72 message.txt
  fold -w 80 -s longline.txt
  ```

### `csplit` / `split`
- **Syntax:** `csplit [-f prefix] [-n digits] file pattern...`, `split [-b bytes | -l lines] [file [prefix]]`
- **Description:** Splits files into pieces based on byte size, line count, or context regex patterns.
- **Example:**
  ```sh
  split -l 1000 bigdata.csv chunk_
  csplit server.log '/ERROR/' '{*}'
  ```

### `comm` / `join`
- **Syntax:** `comm [-123] file1 file2`, `join [-t delim] [-1 field] [-2 field] file1 file2`
- **Description:** Compares two sorted files line by line (`comm`) or performs relational database-style merges on common fields (`join`).
- **Example:**
  ```sh
  comm -12 sorted1.txt sorted2.txt
  join -t, -1 1 -2 1 users.csv orders.csv
  ```

### `expand` / `unexpand`
- **Syntax:** `expand [-t tabstops] [file...]`, `unexpand [-a] [file...]`
- **Description:** Converts tabs to spaces (`expand`) or leading spaces back to tabs (`unexpand`).
- **Example:**
  ```sh
  expand -t 4 source.c > expanded.c
  ```

### `nl`
- **Syntax:** `nl [-b style] [-s separator] [file...]`
- **Description:** Numbers lines of files using configurable formatting styles.
- **Example:**
  ```sh
  nl -ba script.sh
  ```

### `rev` / `tac`
- **Syntax:** `rev [file...]`, `tac [file...]`
- **Description:** Reverses lines character-by-character (`rev`) or reverses line order from bottom to top (`tac`).
- **Example:**
  ```sh
  echo "abcdef" | rev         # fedcba
  tac history.log
  ```

### `colrm` / `column`
- **Syntax:** `colrm [start [stop]]`, `column [-t] [-s delim] [file...]`
- **Description:** Strips columns of characters from lines (`colrm`) or aligns delimited fields into clean terminal tables (`column`).
- **Example:**
  ```sh
  column -t -s: /etc/passwd
  ```

### `rs`
- **Syntax:** `rs [-t|-T] [-c c] [-C c] [-w width] [rows [cols]]`
- **Description:** BSD array reshape and matrix transposition filter.
- **Example:**
  ```sh
  echo "1 2 3 4" | rs -T 2 2
  ```

### `strings` / `hexdump` / `hd` / `od`
- **Syntax:** `strings [file...]`, `hexdump [-C] [file...]`, `hd [file...]`, `od [-b|-c|-d|-x] [file...]`
- **Description:** Dumps file bytes in hexadecimal, octal, or decimal formats, or extracts printable ASCII strings from binaries.
- **Example:**
  ```sh
  strings ksh.exe | grep "NT"
  hexdump -C binary.dat | head -n 10
  ```

### `dos2unix` / `unix2dos`
- **Syntax:** `dos2unix [file...]`, `unix2dos [file...]`
- **Description:** Converts CRLF (Windows) and LF (Unix) line endings directly in-place.
- **Example:**
  ```sh
  dos2unix build.sh
  ```

### `lam` / `look`
- **Syntax:** `lam [-t term] [-s del] file...`, `look [-df] prefix [file]`
- **Description:** Laminates files side-by-side (`lam`) or searches dictionary/sorted file for words starting with `prefix` (`look`).
- **Example:**
  ```sh
  look "apple" words.txt
  lam file1.txt file2.txt
  ```

### `vis` / `unvis`
- **Syntax:** `vis [file...]`, `unvis [file...]`
- **Description:** Encodes non-printable characters into visual representation, or restores visual representations back to binary data.
- **Example:**
  ```sh
  vis raw_stream.bin
  ```

### `apply`
- **Syntax:** `apply [-a char] [-n] command arg...`
- **Description:** Applies command to successive sets of arguments.
- **Example:**
  ```sh
  apply "echo File:" *.txt
  ```

### `pr` / `banner` / `col` / `ul`
- **Syntax:** `pr [file...]`, `banner text...`, `col [file...]`, `ul [file...]`
- **Description:** Text presentation tools: paginates text for printing (`pr`), generates large block letters (`banner`), removes reverse line feeds (`col`), and translates terminal underlines (`ul`).
- **Example:**
  ```sh
  banner "READY"
  ```

---

## 8. Filesystem Operations & Metadata

### `ls`
- **Syntax:** `ls [-a|-l|-h|-r|-t|-S|-1|-d|-F] [path...]`
- **Description:** Lists directory contents with permissions, size, modification times, and attributes. Supports POSIX long format (`-l`) and human-readable sizes (`-h`).
- **Example:**
  ```sh
  ls -lah src/
  ```

### `cp`
- **Syntax:** `cp [-r|-R|-f|-p|-v] source... destination`
- **Description:** Copies files or directory trees (`-r`). Preserves timestamps and attributes (`-p`).
- **Example:**
  ```sh
  cp -rp src/ backup/src_backup/
  ```

### `mv`
- **Syntax:** `mv [-f|-n|-v] source... destination`
- **Description:** Moves or renames files and directories.
- **Example:**
  ```sh
  mv old_name.txt new_name.txt
  ```

### `rm` / `unlink`
- **Syntax:** `rm [-r|-R|-f|-v|-d] file...`, `unlink file`
- **Description:** Removes files or directories (`-r`). Force flag (`-f`) ignores missing files.
- **Example:**
  ```sh
  rm -rf temp_dir/
  ```

### `mkdir` / `rmdir`
- **Syntax:** `mkdir [-p] [-m mode] dir...`, `rmdir dir...`
- **Description:** Creates directories (including intermediate parents `-p`) or deletes empty directories.
- **Example:**
  ```sh
  mkdir -p build/obj/x64
  ```

### `touch`
- **Syntax:** `touch [-a|-m|-c|-r ref] file...`
- **Description:** Updates file access and modification timestamps to the current time, or creates empty file if missing.
- **Example:**
  ```sh
  touch build.timestamp
  ```

### `ln` / `link`
- **Syntax:** `ln [-s|-f|-v] target [linkname]`, `link old new`
- **Description:** Creates hard links or Windows symbolic links (`-s`) and directory junctions via native Win32 APIs.
- **Example:**
  ```sh
  ln -s target_file.txt symlink.txt
  ```

### `chmod` / `chown` / `chflags`
- **Syntax:** `chmod [-R] mode file...`, `chown [-R] user[:group] file...`, `chflags [-R] flags file...`
- **Description:** Modifies Win32 file attributes and security descriptors. Maps read-only, hidden, archive, and system flags.
- **Example:**
  ```sh
  chmod 755 build.sh
  chflags hidden secret.key
  ```

### `find`
- **Syntax:** `find [path...] [expression]`
- **Description:** Searches directory trees matching criteria: `-name`, `-type [f|d|l]`, `-size`, `-mtime`, `-print`, `-exec cmd {} +`.
- **Example:**
  ```sh
  find src/ -name "*.c" -exec wc -l {} +
  find . -type f -size +10M
  ```

### `stat` / `file`
- **Syntax:** `stat [-f fmt] [file...]`, `file [file...]`
- **Description:** Queries detailed NTFS file metadata (size, allocation, attributes, creation/access timestamps) or inspects magic bytes to detect file MIME type.
- **Example:**
  ```sh
  stat ksh.exe
  file archive.tar.gz
  ```

### `basename` / `dirname`
- **Syntax:** `basename path [suffix]`, `dirname path`
- **Description:** Strips leading directory path from filename (`basename`) or returns parent directory component (`dirname`).
- **Example:**
  ```sh
  basename /c/Users/Administrator/notes.txt .txt  # notes
  dirname /c/Users/Administrator/notes.txt       # /c/Users/Administrator
  ```

### `readlink` / `realpath`
- **Syntax:** `readlink [-f] file`, `realpath [path...]`
- **Description:** Prints symlink target or resolves absolute canonical Windows path.
- **Example:**
  ```sh
  realpath ./bin/../src/
  ```

### `mktemp`
- **Syntax:** `mktemp [-d|-u] [template]`
- **Description:** Safely creates unique temporary files or directories (`-d`) in `%TEMP%`.
- **Example:**
  ```sh
  temp=$(mktemp /tmp/build.XXXXXX)
  ```

### `install`
- **Syntax:** `install [-d] [-m mode] source... destination`
- **Description:** Copies files and sets attributes, or creates destination directory trees (`-d`).
- **Example:**
  ```sh
  install -d /bin
  install -m 755 ksh.exe /bin/ksh.exe
  ```

### `truncate`
- **Syntax:** `truncate -s size file...`
- **Description:** Shrinks or extends files to specified size (supports `K`, `M`, `G` suffixes).
- **Example:**
  ```sh
  truncate -s 10M sparse.img
  ```

### `dd`
- **Syntax:** `dd [if=in] [of=out] [bs=size] [count=n] [status=none]`
- **Description:** Converts and copies data blocks between streams and files.
- **Example:**
  ```sh
  dd if=/dev/zero of=test.img bs=1M count=10
  ```

### `sync`
- **Syntax:** `sync`
- **Description:** Flushes all buffered filesystem writes to disk across all drives.
- **Example:**
  ```sh
  sync
  ```

### `du` / `df`
- **Syntax:** `du [-a|-h|-s] [path...]`, `df [-h] [drive...]`
- **Description:** Reports disk space used by directories (`du`) or free disk volume capacity (`df`).
- **Example:**
  ```sh
  du -sh src/
  df -h C:
  ```

### `srm` / `shred`
- **Syntax:** `srm [-f|-z] file...`, `shred [-f|-z|-n passes] file...`
- **Description:** Securely destroys sensitive files by overwriting data blocks with random passes before unlinking.
- **Example:**
  ```sh
  shred -z -n 3 credentials.key
  ```

### `what`
- **Syntax:** `what [-s] file...`
- **Description:** Scans files for SCCS identification strings matching `@(#)`.
- **Example:**
  ```sh
  what ksh.exe
  ```

---

## 9. Archive & Compression

### `tar`
- **Syntax:** `tar [-c|-x|-t] [-v] [-f archive] [-z] [-C directory] [files...]`
- **Description:** Built-in Tape Archiver utility: creates (`-c`), extracts (`-x`), or lists (`-t`) standard POSIX `.tar` and `.tar.gz` (`-z`) archives without external dependencies.
- **Example:**
  ```sh
  tar -cvzf backup.tar.gz src/ include/
  tar -xvzf backup.tar.gz -C output/
  tar -tvf backup.tar.gz
  ```

### `pax`
- **Syntax:** `pax [-r|-w|-v] [-f archive] [files...]`
- **Description:** Portable Archive Interchange utility; reads, writes, and lists tar/cpio archive streams.
- **Example:**
  ```sh
  pax -w -f backup.pax src/
  ```

### `gzip` / `gunzip` / `zcat`
- **Syntax:** `gzip [-d|-c|-f] [file...]`, `gunzip [file...]`, `zcat [file...]`
- **Description:** Compresses or decompresses streams and files using Deflate/Gzip compression.
- **Example:**
  ```sh
  gzip -c server.log > server.log.gz
  zcat server.log.gz | grep "FATAL"
  ```

---

## 10. Cryptography & Message Digests

### `sha256` / `sha256sum`
- **Syntax:** `sha256 [file...]`, `sha256sum [-c] [file...]`
- **Description:** Computes and verifies SHA-256 cryptographic digests using native Windows CryptoAPI / CNG hardware-accelerated algorithms.
- **Example:**
  ```sh
  sha256sum ksh.exe
  sha256sum -c checksums.txt
  ```

### `md5` / `md5sum`
- **Syntax:** `md5 [file...]`, `md5sum [-c] [file...]`
- **Description:** Computes and verifies MD5 checksums.
- **Example:**
  ```sh
  md5sum data.zip
  ```

### `sha1` / `sha1sum` / `sha384` / `sha512`
- **Syntax:** `sha1sum file...`, `sha384sum file...`, `sha512sum file...`
- **Description:** Computes SHA-1, SHA-384, or SHA-512 cryptographic digests.
- **Example:**
  ```sh
  sha512sum release.iso
  ```

### `cksum` / `sum`
- **Syntax:** `cksum [file...]`, `sum [-r|-s] [file...]`
- **Description:** Computes POSIX standard 32-bit CRC checksum (`cksum`) or historic BSD/SysV block checksums (`sum`).
- **Example:**
  ```sh
  cksum document.pdf
  ```

### `base64`
- **Syntax:** `base64 [-d] [input [output]]`
- **Description:** Base64 encodes or decodes (`-d`) streams or files.
- **Example:**
  ```sh
  base64 image.png > image.b64
  base64 -d image.b64 > restored.png
  ```

### `caesar` / `rot13`
- **Syntax:** `caesar [rot]`, `rot13`
- **Description:** Encrypts or decrypts Caesar rotation ciphers (ROT13) or performs statistical frequency cryptanalysis.
- **Example:**
  ```sh
  echo "Uryyb Jbeyq" | rot13
  ```

---

## 11. Networking & Diagnostics

### `curl`
- **Syntax:** `curl [-s] [-o file] [-O] [-H header] [-X method] [-d data] [-L] URL`
- **Description:** Transfers data over HTTP/HTTPS/FTP via native Windows WinHTTP/SChannel APIs with SSL/TLS validation. Supports custom headers, POST data, and redirect following (`-L`).
- **Example:**
  ```sh
  curl -s https://api.github.com/users/octocat
  curl -o download.zip -L https://example.com/file.zip
  ```

### `wget`
- **Syntax:** `wget [-q] [-O output] URL`
- **Description:** Downloads files over HTTP/HTTPS.
- **Example:**
  ```sh
  wget -O package.tgz https://example.com/package.tgz
  ```

### `nc` / `netcat`
- **Syntax:** `nc [-46klnuvz] [-w timeout] [-p port] [host] [port]`
- **Description:** Built-in OpenBSD Netcat implementation for Windows. Creates arbitrary TCP/UDP outbound connections and listener sockets.
- **Example:**
  ```sh
  nc -zv 127.0.0.1 80 443 8080      # Port scan
  nc -l -p 9000 > received.dat       # File receiver listener
  ```

### `ping`
- **Syntax:** `ping [-c count] [-w timeout_ms] host`
- **Description:** Sends ICMP ECHO_REQUEST packets to network hosts using native Win32 `Iphlpapi` calls.
- **Example:**
  ```sh
  ping -c 4 8.8.8.8
  ```

### `traceroute` / `tracert`
- **Syntax:** `traceroute [-m max_hops] host`
- **Description:** Traces network hop route to a remote destination with round-trip latency measurements.
- **Example:**
  ```sh
  traceroute google.com
  ```

### `host`
- **Syntax:** `host [-t type] name`
- **Description:** Resolves DNS records (`A`, `AAAA`, `MX`, `TXT`, `CNAME`) for domain names and IP addresses.
- **Example:**
  ```sh
  host google.com
  host -t MX github.com
  ```

### `whois`
- **Syntax:** `whois [-h server] query`
- **Description:** Queries RFC 3912 WHOIS directory databases for domain registrations and ASN blocks.
- **Example:**
  ```sh
  whois example.com
  ```

### `ifconfig`
- **Syntax:** `ifconfig [-adlmuv] [interface]`
- **Description:** BSD-style network interface inspection; displays IP addresses, MAC addresses, MTU, gateway, and adapter flags.
- **Example:**
  ```sh
  ifconfig
  ```

### `netstat`
- **Syntax:** `netstat [-46alnv] [-p proto]`
- **Description:** Displays active TCP/UDP network connections, routing tables, and interface statistics.
- **Example:**
  ```sh
  netstat -an
  ```

### `sockstat`
- **Syntax:** `sockstat [-46clqU] [-p ports]`
- **Description:** Displays open internet sockets and the owning process names/PIDs.
- **Example:**
  ```sh
  sockstat -4 -l
  ```

### `arp`
- **Syntax:** `arp [-a] [-n] [ip]`
- **Description:** Inspects and queries kernel IPv4 ARP neighbor cache tables.
- **Example:**
  ```sh
  arp -a
  ```

---

## 12. System Information & Process Inspection

### `ps` / `pgrep`
- **Syntax:** `ps [-a|-e|-f|-u user]`, `pgrep [-f] pattern`
- **Description:** Takes snapshot of current running Windows processes, PIDs, session IDs, and command lines.
- **Example:**
  ```sh
  ps -ef | grep ksh
  pgrep chrome
  ```

### `uname` / `hostname`
- **Syntax:** `uname [-a|-s|-n|-r|-m]`, `hostname`
- **Description:** Prints operating system name, Windows NT build version, machine architecture (`x86_64`), and computer hostname.
- **Example:**
  ```sh
  uname -a
  ```

### `whoami` / `id` / `logname`
- **Syntax:** `whoami`, `id`, `logname`
- **Description:** Prints current Windows user account, SID, primary group, and elevated token privileges.
- **Example:**
  ```sh
  id
  whoami
  ```

### `uptime` / `free` / `nproc`
- **Syntax:** `uptime`, `free [-h]`, `nproc`
- **Description:** Reports Windows system uptime, total and available physical RAM/pagefile capacity, and online processor count.
- **Example:**
  ```sh
  uptime
  free -h
  nproc
  ```

### `vmstat` / `iostat`
- **Syntax:** `vmstat [-s] [interval [count]]`, `iostat [-dx] [interval]`
- **Description:** Real-time system performance monitors: virtual memory/paging statistics (`vmstat`) and drive I/O throughput (`iostat`).
- **Example:**
  ```sh
  vmstat 2 5
  ```

### `sysctl`
- **Syntax:** `sysctl [-a] [name...]`
- **Description:** Queries kernel and hardware parameters (CPU model, clock frequency, RAM, page size).
- **Example:**
  ```sh
  sysctl -a
  ```

### `nice` / `renice`
- **Syntax:** `nice [-n priority] command`, `renice [-n priority] -p pid`
- **Description:** Executes commands or modifies running processes with altered Win32 priority classes (`IDLE`, `BELOW_NORMAL`, `NORMAL`, `ABOVE_NORMAL`, `HIGH`).
- **Example:**
  ```sh
  nice -n 10 make -j8
  renice -n 5 -p 1234
  ```

### `nohup` / `daemon`
- **Syntax:** `nohup command [args...]`, `daemon [-fr] [-p pidfile] command...`
- **Description:** Runs commands immune to terminal hangups (`nohup`) or detaches background daemon processes with optional process supervision (`daemon`).
- **Example:**
  ```sh
  nohup ./server > server.log 2>&1 &
  ```

### `lockf`
- **Syntax:** `lockf [-kns] [-t seconds] file command [args...]`
- **Description:** Executes command while acquiring and holding an exclusive advisory lock on `file`.
- **Example:**
  ```sh
  lockf /tmp/deploy.lock ./deploy.sh
  ```

### `timeout`
- **Syntax:** `timeout seconds command [args...]`
- **Description:** Runs command and terminates it if it is still executing after `seconds`.
- **Example:**
  ```sh
  timeout 10 ping 192.168.1.1
  ```

### `watch`
- **Syntax:** `watch [-n seconds] command`
- **Description:** Periodically executes command fullscreen to monitor changes over time.
- **Example:**
  ```sh
  watch -n 2 "ps | grep ksh"
  ```

### `pbcopy` / `pbpaste` / `clip`
- **Syntax:** `pbcopy`, `pbpaste`, `clip`
- **Description:** Direct integration with the Windows system clipboard. `pbcopy`/`clip` writes stdin to clipboard; `pbpaste` prints clipboard content to stdout.
- **Example:**
  ```sh
  cat file.txt | pbcopy
  pbpaste > copy.txt
  ```

### `uuidgen`
- **Syntax:** `uuidgen`
- **Description:** Generates unique RFC 4122 Version 4 UUIDs using system cryptographic entropy.
- **Example:**
  ```sh
  uuidgen
  ```

### `date` / `cal` / `time`
- **Syntax:** `date [+format]`, `cal [[month] year]`, `time command`
- **Description:** Displays or formats calendar date/time, renders monthly calendars, or benchmarks elapsed process execution time.
- **Example:**
  ```sh
  date +"%Y-%m-%d %H:%M:%S"
  cal 2026
  time make
  ```

### `factor` / `seq` / `jot` / `shuf`
- **Syntax:** `factor [num...]`, `seq [first [step]] last`, `jot count [start]`, `shuf [file]`
- **Description:** Math generators: prime factorization (`factor`), numeric sequences (`seq`), random/continuous sequence generator (`jot`), or random permutation line shuffle (`shuf`).
- **Example:**
  ```sh
  seq 1 2 10              # 1 3 5 7 9
  factor 123456
  shuf names.txt
  ```

### `sleep` / `pause` / `leave`
- **Syntax:** `sleep seconds`, `pause`, `leave [[+]hhmm]`
- **Description:** Suspends execution for specified seconds, waits for a key press (`pause`), or sets departure alert timer (`leave`).
- **Example:**
  ```sh
  sleep 2.5
  pause
  ```

---

## 13. Windows Batch & CMD Built-in Suite

ntksh embeds a complete native in-process Windows Batch interpreter:

| Command | Syntax | Description |
| :--- | :--- | :--- |
| `dir` | `dir [/w] [/s] [/b] [/o:sort] [path]` | Lists files using Windows FindFile APIs and formatting. |
| `copy` | `copy [/A\|/B] [/V] [/Y\|/-Y] src dst` | Copies files with verification and binary/ASCII modes. |
| `move` | `move [/Y\|/-Y] src dst` | Moves files or renames directories across drives. |
| `del` / `erase` | `del [/P] [/F] [/S] [/Q] [/A:attrs] files` | Deletes files with optional attribute filtering. |
| `md` / `mkdir` | `md [path]` | Creates directories and intermediate folders. |
| `rd` / `rmdir` | `rd [/S] [/Q] [path]` | Removes directory trees recursively. |
| `ren` / `rename` | `ren oldname newname` | Renames files or directories. |
| `replace` | `replace [/A] [/P] [/R] [/S] [/U] src dir` | Replaces or updates files in target directory. |
| `attrib` | `attrib [+/-R] [+/-A] [+/-S] [+/-H] [/S /D]` | Displays or modifies NTFS file attributes. |
| `tree` | `tree [/F] [/A] [path]` | Renders graphical ASCII tree of directories and files. |
| `assoc` | `assoc [.ext[=fileType]]` | Queries or sets file extension associations. |
| `ftype` | `ftype [fileType[=openCommandString]]` | Queries or modifies open verbs for file types. |
| `findstr` | `findstr [/S] [/I] [/R] [/C:str] pattern files` | Searches strings and regular expressions in files. |
| `fc` | `fc [/B] [/L] [/C] file1 file2` | Compares files in binary (`/B`) or text (`/L`) mode. |
| `mklink` | `mklink [/D\|/H\|/J] link target` | Creates directory links, hard links, or NTFS junctions. |
| `subst` | `subst [drive: path] [/D]` | Associates a path with virtual drive letter. |
| `chcp` | `chcp [nnn]` | Displays or sets console code page (e.g. `65001` for UTF-8). |
| `choice` | `choice [/C choices] [/N] [/M prompt]` | Prompts user for a single-key interactive choice. |
| `color` | `color [attr]` | Sets console foreground and background colors. |
| `doskey` | `doskey [/history] [macro=text]` | Manages command line macros and command recall. |
| `mode` | `mode [cols=c lines=n]` | Configures console screen buffer dimensions. |
| `more` | `more [/C] [/P] [/S] [file...]` | Paginates output one screen at a time. |
| `path` | `path [[drive:]path[;...]]` | Displays or modifies command search path. |
| `prompt` | `prompt [text]` | Configures Windows command prompt formatting. |
| `title` | `title string` | Sets the Windows console window title bar text. |
| `ver` | `ver` | Displays Windows NT operating system version string. |
| `verify` | `verify [ON\|OFF]` | Toggles disk write verification flag. |
| `vol` | `vol [drive:]` | Displays disk volume label and serial number. |
| `break` | `break [ON\|OFF]` | Toggles extended Ctrl+C checking in batch scripts. |
| `tasklist` | `tasklist [/FO fmt] [/NH]` | Enumerates running Windows processes and services. |
| `taskkill` | `taskkill [/F] [/PID pid \| /IM imagename]` | Terminates running Windows tasks by name or PID. |
| `systeminfo` | `systeminfo` | Displays detailed OS configuration and hardware metrics. |
| `shutdown` | `shutdown [/s\|/r\|/a] [/t sec]` | Initiates shutdown, restart, or aborts pending shutdown. |

---

## 14. Dynamic Module & Extension System

### `module`
- **Syntax:** `module <load|unload|import|list> [args...]`
- **Description:** Modern runtime extension and plugin manager:
  - `module load <path/to/plugin.dll>`: Loads compiled native C/C++ extension and registers custom built-ins into the active shell.
  - `module unload <name>`: Unregisters built-ins and unloads DLL.
  - `module list`: Displays all loaded modules and their exported dynamic built-ins.
  - `module import <package>`: Searches `KSH_MODULE_PATH` for `<package>.ksh` and sources it into the shell environment.
- **Example:**
  ```sh
  module load ./sample_module.dll
  sample_hello "World"
  module list
  module unload sample_module

  export KSH_MODULE_PATH="modules;/usr/lib/ksh"
  module import math_pkg
  ```

---

*ntksh Reference Manual — Generated for ntksh on Windows NT (x86_64).*
