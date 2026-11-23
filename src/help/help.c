#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

/* ========================================================================= */
/*                          Comprehensive Help                               */
/* ========================================================================= */

void DisplayComprehensiveHelp(const wchar_t* category) {
    bool showScripting = true;
    bool showCoreutils = true;
    bool showBatch = true;

    if (category) {
        if (_wcsicmp(category, L"coreutils") == 0 || _wcsicmp(category, L"core") == 0 ||
            _wcsicmp(category, L"unix") == 0) {
            showScripting = false;
            showBatch = false;
        } else if (_wcsicmp(category, L"scripting") == 0 || _wcsicmp(category, L"posix") == 0 ||
                   _wcsicmp(category, L"ksh") == 0 || _wcsicmp(category, L"sh") == 0) {
            showCoreutils = false;
            showBatch = false;
        } else if (_wcsicmp(category, L"batch") == 0 || _wcsicmp(category, L"bat") == 0 ||
                   _wcsicmp(category, L"cmd") == 0) {
            showScripting = false;
            showCoreutils = false;
        }
    }

    wprintf(L"\n===============================================================================\n");
    wprintf(L"                             ksh General Help Index                            \n");
    wprintf(L"===============================================================================\n\n");

    if (!category) {
        wprintf(L"OVERVIEW:\n");
        wprintf(L"  ntksh is a native Korn Shell built for Windows NT.\n");
        wprintf(L"  It provides Windows-native process and filesystem integration alongside\n");
        wprintf(L"  FreeBSD sh, mksh, coreutils, and Windows batch compatibility features.\n\n");
        wprintf(L"HELP CATEGORIES:\n");
        wprintf(L"  help scripting       List POSIX and KornShell scripting built-ins\n");
        wprintf(L"  help coreutils       List Unix & BSD coreutils built-ins\n");
        wprintf(L"  help batch           List Windows CMD / batch built-ins\n");
        wprintf(L"  help all             List all available built-ins\n\n");
        wprintf(L"SYNTAX COMPATIBILITY:\n");
        wprintf(L"  - POSIX/Ksh Style:   Variable expansion ($VAR, ${VAR}), array assignment (arr=(...)),\n");
        wprintf(L"                       array indexing (${arr[idx]}, ${arr[@]}, ${#arr[@]}), parameter\n");
        wprintf(L"                       expansions (${var:-def}, ${var:=def}, ${var:+alt}, ${var#pat}, ${var%pat}),\n");
        wprintf(L"                       pipes (|), redirection (<, >, >>, 2>, 2>>, 2>&1, 1>&2, &>, >&),\n");
        wprintf(L"                       and standard /dev/null mapping to Windows NUL.\n");
        wprintf(L"  - Batch / CMD Style: .bat/.cmd scripts run in the native in-process interpreter;\n");
        wprintf(L"                       they are not delegated to cmd.exe. Supported syntax includes\n");
        wprintf(L"                       labels/goto/call, if, for (/L /D /R /F), set /A and /P,\n");
        wprintf(L"                       percent/delayed expansion, setlocal/endlocal, and core file commands.\n\n");
    }

    if (showScripting) {
        wprintf(L"BUILT-IN COMMANDS: CORE SHELL & SCRIPTING:\n");
        wprintf(L"  cd [dir]             Change current directory (accepts '/' or '\\' separators).\n");
        wprintf(L"  pushd directory, popd Change directories with directory stack.\n");
        wprintf(L"  pwd [-L | -P]        Print logical or physical working directory.\n");
        wprintf(L"  dirs [-c|-l|-p|-v]   Display or clear directory stack.\n");
        wprintf(L"  echo [-neE] [args...] Print arguments; -n suppresses newline, -e/-E toggle escapes.\n");
        wprintf(L"  printf format [args...] Format values with shell printf conversions.\n");
        wprintf(L"  print [-nrR] [-u fd] [args...], print -f format [args...]\n");
        wprintf(L"                       Print arguments or formatted output; -u 2 for stderr.\n");
        wprintf(L"  read [-r] [-s] [-p prompt] [-n count] [-d delim] [-u fd] [name...]\n");
        wprintf(L"                       Read input line, split fields using IFS, assign variables.\n");
        wprintf(L"  let expression...    Evaluate arithmetic expressions; zero returns status 1.\n");
        wprintf(L"  (( expression ))     Arithmetic command (assignments, ++, <<=, ...); zero returns status 1.\n");
        wprintf(L"  expr arg1 op arg2    Evaluate arithmetic/string expressions.\n");
        wprintf(L"  setvar name value    Assign a shell variable without evaluating value.\n");
        wprintf(L"  eval [args...]       Reparse and execute the joined command text.\n");
        wprintf(L"  command [args...]    Run a command bypassing shell functions.\n");
        wprintf(L"  builtin [-f file.dll [cmd]] [-d cmd] [-s] [cmd [args...]]\n");
        wprintf(L"                       Load/unload dynamic C/C++ built-ins or run builtins directly.\n");
        wprintf(L"  module <load|unload|import|list> [args...]\n");
        wprintf(L"                       Manage native DLL extensions and KSH_MODULE_PATH script packages.\n");
        wprintf(L"  alias [-p] [name[=val]...], unalias [-a] name...\n");
        wprintf(L"                       Define, list, expand, or remove shell aliases.\n");
        wprintf(L"  type, whence         Describe shell builtins and executable paths.\n");
        wprintf(L"  export [-p] [name[=val]...], setenv name [value], unset <name...>\n");
        wprintf(L"                       Manage environment variables.\n");
        wprintf(L"  readonly [-p] [name[=val]...] Set or display read-only shell variables.\n");
        wprintf(L"  typeset [-p] [-ilurx] [-a|-A|-f|-F] [-E[n]|-F[n]] [-L[n]|-R[n]|-Z[n]] [name[=val]...]\n");
        wprintf(L"                       Set scalar/array attributes or inspect functions.\n");
        wprintf(L"  local name[=val]...  Declare function-local variables.\n");
        wprintf(L"  integer, float, nameref Declare typed variable attributes.\n");
        wprintf(L"  set [-e|-u|-x] [-o option] [-- args...] Configure options or positional parameters.\n");
        wprintf(L"  shift [n]            Remove parameters from the start of positional list.\n");
        wprintf(L"  test, [ expr ], [[ expr ]] Evaluate expressions; [[ ]] supports extglobs and || / &&.\n");
        wprintf(L"  select var in items; do ...; done Interactive menu selection loop.\n");
        wprintf(L"  true, false, :       Return status 0 (true/colon) or 1 (false).\n");
        wprintf(L"  exec [cmd [args...]] Replace the shell with a command.\n");
        wprintf(L"  . file, source file  Execute a script in the current shell environment.\n");
        wprintf(L"  return [status]      Return from a function or sourced script.\n");
        wprintf(L"  exit [code]          Exit the shell with return code.\n");
        wprintf(L"  functions, autoload  List or define autoloading functions.\n");
        wprintf(L"  jobs, bg, fg, disown, wait Manage background processes and jobs.\n");
        wprintf(L"  kill [-s sig] pid... Terminate processes or list signals (-l).\n");
        wprintf(L"  trap [action] [sig...] Trap signals and execute handlers.\n");
        wprintf(L"  umask [-S] [-p]      Query or set shell creation mask.\n");
        wprintf(L"  times, ulimit [-a|-n] Display process CPU totals or descriptor limits.\n");
        wprintf(L"  suspend [-f]         Suspend console shell until key press.\n");
        wprintf(L"  history, hist, r     Display or re-execute command history.\n");
        wprintf(L"  cls, clear           Clear console display buffer.\n");
        wprintf(L"  pause, sleep sec     Pause execution or wait for a key.\n");
        wprintf(L"  help, man [cat]      Display help for shell, coreutils, or batch commands.\n\n");
    }

    if (showCoreutils) {
        wprintf(L"BUILT-IN COMMANDS: COREUTILS & UNIX UTILITIES:\n");
        wprintf(L"  cat [-n|-b|-s|-E|-T] Concatenate and print files to stdout.\n");
        wprintf(L"  head [-n N|-c N], tail [-n N|-c N] Output first or last part of files.\n");
        wprintf(L"  wc [-l|-w|-c|-m|-L]  Print newline, word, and byte counts.\n");
        wprintf(L"  grep, egrep, fgrep   Search text matching regex or fixed patterns.\n");
        wprintf(L"  sed [-n] [-e script] Stream editor for filtering and transforming text.\n");
        wprintf(L"  awk [-F fs] 'prog'   Pattern scanning and text processing language.\n");
        wprintf(L"  cut [-b|-c|-f LIST]  Print selected character/field parts of lines.\n");
        wprintf(L"  tr [-c|-d|-s] S1 [S2] Translate, squeeze, or delete characters.\n");
        wprintf(L"  sort [-r|-n|-f|-u]   Sort lines of text files.\n");
        wprintf(L"  uniq [-c|-d|-u|-i]   Report or filter repeated lines.\n");
        wprintf(L"  paste [-s] [-d del]  Merge lines of files horizontally.\n");
        wprintf(L"  fmt [-c|-m|-s] [-w width] Format paragraphs to specified line width.\n");
        wprintf(L"  csplit [-k|-s] [-f prefix] [-n digits] file arg... Context-based file splitting.\n");
        wprintf(L"  fold [-w width]      Wrap input lines to specified width.\n");
        wprintf(L"  expand, unexpand     Convert tabs to spaces and spaces to tabs.\n");
        wprintf(L"  nl [-b style]        Number lines of files.\n");
        wprintf(L"  rev, tac             Reverse lines characterwise or line-by-line.\n");
        wprintf(L"  colrm [start [stop]] Remove selected character columns from lines.\n");
        wprintf(L"  column [-t] [-s del] Columnate lists into tabular layout.\n");
        wprintf(L"  rs [-t|-T] [-c c] [-C c] [-w width] [-g gutter] [rows [cols]] Reshape/transpose arrays.\n");
        wprintf(L"  comm, join, split, shuf Common lines, relational merge, partitioning, shuffle.\n");
        wprintf(L"  strings, hexdump, hd, od Find printable strings or dump hexadecimal/octal.\n");
        wprintf(L"  ls [-a|-l|-h|-r|-t|-S|-1|-d] List directory contents.\n");
        wprintf(L"  cp [-r|-f|-p|-v]     Copy files and directories.\n");
        wprintf(L"  mv [-f|-n|-v]        Move or rename files and directories.\n");
        wprintf(L"  rm [-r|-f|-v|-d]     Remove files or directories.\n");
        wprintf(L"  ln [-s|-f|-v], link  Make symbolic or hard links between files.\n");
        wprintf(L"  unlink file          Call system unlink to remove specified file.\n");
        wprintf(L"  chmod [-R|-v|-f] mode file... Change file mode or read-only attribute.\n");
        wprintf(L"  chown [-R|-v|-f|-h] [owner][:group] file... Change file owner/group on NT.\n");
        wprintf(L"  chflags [-R|-v|-f|-h] flags file... Change file flags (hidden, readonly, archive).\n");
        wprintf(L"  touch [-a|-m|-c|-r|-t] Update file access and modification times.\n");
        wprintf(L"  mktemp [-d|-u]       Create temporary file or directory safely.\n");
        wprintf(L"  install [-d] [-m mode] Copy files and create directories with modes.\n");
        wprintf(L"  truncate -s size     Shrink or extend file to specified size.\n");
        wprintf(L"  dd [if=..] [of=..]   Convert and copy file with block size.\n");
        wprintf(L"  sync                 Synchronize cached writes to disk.\n");
        wprintf(L"  basename, dirname    Strip directory path or filename suffix.\n");
        wprintf(L"  readlink, realpath   Print resolved symlink target or canonical path.\n");
        wprintf(L"  find [path...]       Search directory trees (BSD mode).\n");
        wprintf(L"  diff [-u|-q|-i|-w|-B] Compare files line by line.\n");
        wprintf(L"  patch [-R|-s|-b] [-p n] [-i patch] Apply unified diff patches.\n");
        wprintf(L"  pax [-r|-w|-v|-k|-u] [-f arch] Portable Archive Interchange (tar/cpio/copy).\n");
        wprintf(L"  tar [-c|-x|-t] [-v] [-f arch] [-z] [-C dir] Standard Tape Archiver (create, extract, list).\n");
        wprintf(L"  cmp [-l|-s]          Compare two files byte by byte.\n");
        wprintf(L"  du [-a|-h|-s], df [-h] Report disk usage and free filesystem space.\n");
        wprintf(L"  file, stat           Determine file type MIME / query metadata.\n");
        wprintf(L"  base64 [-d]          Base64 encode or decode data.\n");
        wprintf(L"  md5, md5sum, sha1sum Compute and verify MD5/SHA-1 message digests.\n");
        wprintf(L"  sha256sum, sha384, sha512 Compute SHA-256, SHA-384, SHA-512 digests.\n");
        wprintf(L"  cksum                Print POSIX 32-bit CRC checksum and byte count.\n");
        wprintf(L"  uuidgen              Generate a new RFC 4122 v4 UUID.\n");
        wprintf(L"  gzip, gunzip, zcat   Compress or decompress files with DEFLATE/GZIP.\n");
        wprintf(L"  uname [-a], hostname Print OS, kernel, machine, and hostname info.\n");
        wprintf(L"  ifconfig [-adlmuv] [-f fmt] [inet|inet6] [if] BSD-style interface configuration display.\n");
        wprintf(L"  netstat [-46alnv] [-f fam] [-p proto] [-rsi] [-w n] BSD-style network status and statistics.\n");
        wprintf(L"  devinfo [-ruv] [-p dev] BSD-style device tree, resources, and device paths.\n");
        wprintf(L"  id, logname, whoami  Print user and group identities.\n");
        wprintf(L"  ps, pgrep, pkill     Snapshot, pattern search, or terminate processes.\n");
        wprintf(L"  nproc, uptime, free  Processor count, uptime, and memory usage.\n");
        wprintf(L"  cal, factor, seq, jot Calendar, prime factors, seq/random generator.\n");
        wprintf(L"  tsort [-q] [file]    Topological sort on pairs of nodes.\n");
        wprintf(L"  nice [-n inc] cmd    Run command with altered process priority.\n");
        wprintf(L"  nohup cmd [args...]  Run command immune to terminal hangups.\n");
        wprintf(L"  lockf [-kns] [-t sec] file cmd [args...] Execute command while holding file lock.\n");
        wprintf(L"  date, time [-p]      Print/format system date and measure execution.\n");
        wprintf(L"  timeout, yes         Timeout command runner or repeat strings.\n");
        wprintf(L"  which, where         Locate binaries and builtins in PATH.\n");
        wprintf(L"  tee [-a], xargs      Tee pipeline streams or batch argument execution.\n");
        wprintf(L"  watch, stty, tty     Periodic fullscreen runner and terminal info.\n");
        wprintf(L"  dos2unix, unix2dos   Convert CRLF and LF text file line breaks.\n");
        wprintf(L"  sockstat [-46clqU] [-p ports] [-P proto] Display open sockets and owning processes.\n");
        wprintf(L"  nc [-46klnuvz] [-w sec] [-p port] OpenBSD Netcat: arbitrary TCP/UDP connections & listeners.\n");
        wprintf(L"  curl [-s] [-o file] [-O] [-H hdr] [-X method] [-d data] [-L] URL Transfer URL data.\n");
        wprintf(L"  wget [-q] [-O file] URL Retrieve files over HTTP/HTTPS.\n");
        wprintf(L"  mtree [-c|-f spec] [-p path] [-K kw] Map or verify directory hierarchy & SHA-256 specs.\n");
        wprintf(L"  look [-df] [-t term] str [file] Search for lines with prefix in sorted file.\n");
        wprintf(L"  daemon [-fr] [-p pid] [-o out] cmd Run command detached with optional supervision.\n");
        wprintf(L"  lam [-t term] [-s del] [-p pad] file... Laminate lines of files side-by-side.\n");
        wprintf(L"  vmstat [-sw] [interval [count]] BSD virtual memory, paging, and CPU statistics.\n");
        wprintf(L"  pbcopy [-pboard name] Copy standard input to the Windows clipboard.\n");
        wprintf(L"  pbpaste [-pboard name] [-Prefer type] Print Windows clipboard contents to stdout.\n");
        wprintf(L"  fstat [-v] [-p pid] [-u user] [file...] Display open files and processes holding locks.\n");
        wprintf(L"  script [-akq] [file [command...]] Make typescript of terminal session.\n");
        wprintf(L"  vis [-bcwns] [file...] Display non-printable characters in a visual format.\n");
        wprintf(L"  unvis [-e] [file...] Decode visual representations back to original data.\n");
        wprintf(L"  iostat [-dx] [-c count] [-w wait] [drives...] Real-time disk I/O throughput monitor.\n");
        wprintf(L"  apply [-a char] [-n] cmd arg... Apply command to sets of arguments.\n");
        wprintf(L"  sysctl [-a] [-n] [name...] Display system state, CPU, memory, and kernel metrics.\n");
        wprintf(L"  renice [-n pri] -p pid Alter priority class of running processes.\n");
        wprintf(L"  arp [-a] [-n] [ip]   Display and inspect kernel IPv4 ARP neighbor cache.\n");
        wprintf(L"  host [-t type] name  DNS lookup utility for resolving host and IP addresses.\n");
        wprintf(L"  getent db [key...]   Get entries from administrative databases (passwd, group, hosts, services).\n");
        wprintf(L"  srm, shred [-f] [-z] Securely overwrite and remove files with random passes.\n");
        wprintf(L"  pr [-h hdr] [-l len] Paginate or format files for printing.\n");
        wprintf(L"  banner string...     Print large ASCII art block banners.\n");
        wprintf(L"  ping [-c cnt] [-w ms] Send ICMP ECHO_REQUEST packets to network hosts.\n");
        wprintf(L"  traceroute [-m hops] Trace the network route hops to a destination.\n");
        wprintf(L"  whois [-h host] query Query RFC 3912 domain registration and IP assignments.\n");
        wprintf(L"  col [-b] [-x] [file] Filter reverse line feeds and backspaces from input.\n");
        wprintf(L"  ul [-i] [file]       Translate backspace underlines and overstrikes to ANSI.\n");
        wprintf(L"  what [-s] file...    Display SCCS identification strings matching @(#).\n");
        wprintf(L"  od [-b|-c|-d|-o|-x]  Dump files in octal, hex, decimal, or character formats.\n");
        wprintf(L"  sum [-r|-s] [file]   Checksum and count the blocks in a file (BSD/SysV).\n");
        wprintf(L"  leave [[+]hhmm]      Set a departure reminder alarm.\n");
        wprintf(L"  caesar [rot], rot13  Decrypt/encrypt Caesar ciphers with frequency analysis.\n\n");
    }

    if (showBatch) {
        wprintf(L"BUILT-IN COMMANDS: WINDOWS BATCH & CMD:\n");
        wprintf(L"  dir [path]           List directory contents using Windows FindFile APIs.\n");
        wprintf(L"  copy [/A|/B] [/V] [/Y|/-Y] [/Z] Copy one or more files.\n");
        wprintf(L"  del, erase [/P|/F|/S|/Q|/A] Delete specified files.\n");
        wprintf(L"  md, mkdir            Create a new directory and intermediate folders.\n");
        wprintf(L"  move [/Y|/-Y]        Move files or rename directories.\n");
        wprintf(L"  rd, rmdir [/S] [/Q]  Remove directories (optionally tree).\n");
        wprintf(L"  ren, rename          Rename file or files.\n");
        wprintf(L"  replace [/A|/P|/R|/S|/U|/W] Replace or add files in directory.\n");
        wprintf(L"  attrib [+/-R|A|S|H] [/S /D] Display or change file attributes.\n");
        wprintf(L"  tree [/F] [/A]       Graphically display directory folder structure.\n");
        wprintf(L"  assoc, ftype         Query or set file extension associations and verbs.\n");
        wprintf(L"  break [ON|OFF]       Configure break checking in batch scripts.\n");
        wprintf(L"  chcp [nnn]           Display or set console code page.\n");
        wprintf(L"  choice [/C] [/N] [/M] Prompt for interactive single-key choice.\n");
        wprintf(L"  clip                 Redirect standard input to Windows clipboard.\n");
        wprintf(L"  color [attr]         Set console text and background colors.\n");
        wprintf(L"  doskey [/history]    Manage command line macros and history.\n");
        wprintf(L"  fc [/b|/l|/c|/w]     Compare two files.\n");
        wprintf(L"  findstr [/s|/i|/r]   Search for strings and regex patterns in files.\n");
        wprintf(L"  mklink [/D|/H|/J]    Create symbolic link, hard link, or junction.\n");
        wprintf(L"  mode [cols=c lines=n] Query or configure console display mode.\n");
        wprintf(L"  more [/C|/P|/S]      Display output one screen at a time.\n");
        wprintf(L"  path [[drive:]path]  Display, set, or clear search PATH.\n");
        wprintf(L"  prompt [text]        Set command prompt format.\n");
        wprintf(L"  shutdown [/s|/r|/a]  Shutdown, restart, or abort restart.\n");
        wprintf(L"  start [\"title\"]      Start a program in a separate window or background.\n");
        wprintf(L"  subst [drive: path]  Associate a path with virtual drive letter.\n");
        wprintf(L"  systeminfo           Display detailed OS and hardware configuration.\n");
        wprintf(L"  tasklist, taskkill   Display or terminate running tasks by PID or name.\n");
        wprintf(L"  title [string]       Set console window title.\n");
        wprintf(L"  ver, verify [ON|OFF] Display Windows version or toggle write verification.\n");
        wprintf(L"  vol [drive:]         Display disk volume label and serial number.\n\n");
    }

    if (!category) {
        wprintf(L"PIPELINES & REDIRECTIONS:\n");
        wprintf(L"  cmd1 | cmd2          Connects stdout of cmd1 to stdin of cmd2 via Win32 pipes.\n");
        wprintf(L"  cmd > file, >> file  Redirect stdout to file (overwrite or append).\n");
        wprintf(L"  cmd 2> file, 2>> file Redirect stderr to file (overwrite or append).\n");
        wprintf(L"  cmd 2>&1, 1>&2       Duplicate stderr to stdout, or stdout to stderr.\n");
        wprintf(L"  cmd &> file, >& file Redirect both stdout and stderr to file.\n");
        wprintf(L"  cmd < file           Redirect stdin from file.\n");
        wprintf(L"  cmd > /dev/null      Discard output using Windows NUL device mapping.\n\n");
        wprintf(L"CONFIGURATION & RECALL:\n");
        wprintf(L"  ~/ksh.ini            Automatic startup configuration in user's home directory.\n");
        wprintf(L"  ~/.ksh_history       Timestamped persistent command history in home directory.\n");
        wprintf(L"  Up/Down Arrow        Interactive command line history recall in console.\n");
        wprintf(L"  Backspace            Character deletion with console erase in interactive mode.\n\n");
        wprintf(L"SECURITY & RUNTIME:\n");
        wprintf(L"  - No system() or shims are used. Binaries are executed via CreateProcessW.\n");
        wprintf(L"  - Safe in-process CRT descriptor redirection with full wide UTF-8 support.\n");
    }
    wprintf(L"===============================================================================\n\n");
}

