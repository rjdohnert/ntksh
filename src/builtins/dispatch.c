#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

bool IsBuiltinCommand(const CommandAST* cmd) {
    if (!cmd || cmd->argc == 0) return false;
    const wchar_t* name = cmd->argv[0];
    if (Module_IsDynamicBuiltin(name)) return true;
    return _wcsicmp(name, L"apply") == 0 ||
        _wcsicmp(name, L"arp") == 0 ||
        _wcsicmp(name, L"assoc") == 0 ||
        _wcsicmp(name, L"attrib") == 0 ||
        wcscmp(name, L"autoload") == 0 ||
        _wcsicmp(name, L"awk") == 0 ||
        _wcsicmp(name, L"banner") == 0 ||
        _wcsicmp(name, L"base64") == 0 ||
        _wcsicmp(name, L"basename") == 0 ||
        wcscmp(name, L"bg") == 0 ||
        _wcsicmp(name, L"break") == 0 ||
        _wcsicmp(name, L"cal") == 0 ||
        _wcsicmp(name, L"caesar") == 0 ||
        _wcsicmp(name, L"cat") == 0 ||
        _wcsicmp(name, L"chdir") == 0 ||
        _wcsicmp(name, L"chcp") == 0 ||
        _wcsicmp(name, L"chflags") == 0 ||
        _wcsicmp(name, L"chmod") == 0 ||
        _wcsicmp(name, L"chown") == 0 ||
        _wcsicmp(name, L"choice") == 0 ||
        _wcsicmp(name, L"cksum") == 0 ||
        _wcsicmp(name, L"clip") == 0 ||
        _wcsicmp(name, L"cmp") == 0 ||
        _wcsicmp(name, L"color") == 0 ||
        _wcsicmp(name, L"col") == 0 ||
        _wcsicmp(name, L"colrm") == 0 ||
        _wcsicmp(name, L"column") == 0 ||
        _wcsicmp(name, L"comm") == 0 ||
        _wcsicmp(name, L"csplit") == 0 ||
        wcscmp(name, L"continue") == 0 ||
        _wcsicmp(name, L"copy") == 0 ||
        _wcsicmp(name, L"cp") == 0 ||
        _wcsicmp(name, L"curl") == 0 ||
        _wcsicmp(name, L"cut") == 0 ||
        _wcsicmp(name, L"date") == 0 ||
        _wcsicmp(name, L"dd") == 0 ||
        _wcsicmp(name, L"del") == 0 ||
        _wcsicmp(name, L"df") == 0 ||
        _wcsicmp(name, L"diff") == 0 ||
        _wcsicmp(name, L"dirname") == 0 ||
        wcscmp(name, L"dirs") == 0 ||
        wcscmp(name, L"disown") == 0 ||
        _wcsicmp(name, L"dos2unix") == 0 ||
        _wcsicmp(name, L"doskey") == 0 ||
        _wcsicmp(name, L"du") == 0 ||
        _wcsicmp(name, L"egrep") == 0 ||
        _wcsicmp(name, L"env") == 0 ||
        _wcsicmp(name, L"erase") == 0 ||
        _wcsicmp(name, L"expand") == 0 ||
        _wcsicmp(name, L"expr") == 0 ||
        _wcsicmp(name, L"factor") == 0 ||
        _wcsicmp(name, L"fc") == 0 ||
        wcscmp(name, L"fg") == 0 ||
        _wcsicmp(name, L"fgrep") == 0 ||
        _wcsicmp(name, L"file") == 0 ||
        _wcsicmp(name, L"find") == 0 ||
        _wcsicmp(name, L"findstr") == 0 ||
        wcscmp(name, L"float") == 0 ||
        _wcsicmp(name, L"fmt") == 0 ||
        _wcsicmp(name, L"fold") == 0 ||
        _wcsicmp(name, L"free") == 0 ||
        _wcsicmp(name, L"ftype") == 0 ||
        wcscmp(name, L"functions") == 0 ||
        _wcsicmp(name, L"getent") == 0 ||
        _wcsicmp(name, L"grep") == 0 ||
        _wcsicmp(name, L"gunzip") == 0 ||
        _wcsicmp(name, L"gzip") == 0 ||
        wcscmp(name, L"hash") == 0 ||
        _wcsicmp(name, L"hd") == 0 ||
        _wcsicmp(name, L"head") == 0 ||
        _wcsicmp(name, L"help") == 0 ||
        _wcsicmp(name, L"hexdump") == 0 ||
        wcscmp(name, L"hist") == 0 ||
        wcscmp(name, L"history") == 0 ||
        _wcsicmp(name, L"host") == 0 ||
        _wcsicmp(name, L"hostname") == 0 ||
        _wcsicmp(name, L"id") == 0 ||
        _wcsicmp(name, L"install") == 0 ||
        wcscmp(name, L"integer") == 0 ||
        wcscmp(name, L"jobs") == 0 ||
        _wcsicmp(name, L"join") == 0 ||
        _wcsicmp(name, L"jot") == 0 ||
        wcscmp(name, L"kill") == 0 ||
        _wcsicmp(name, L"ln") == 0 ||
        _wcsicmp(name, L"link") == 0 ||
        _wcsicmp(name, L"leave") == 0 ||
        _wcsicmp(name, L"lockf") == 0 ||
        _wcsicmp(name, L"logname") == 0 ||
        _wcsicmp(name, L"ls") == 0 ||
        _wcsicmp(name, L"md") == 0 ||
        _wcsicmp(name, L"md5") == 0 ||
        _wcsicmp(name, L"md5sum") == 0 ||
        _wcsicmp(name, L"mkdir") == 0 ||
        _wcsicmp(name, L"mklink") == 0 ||
        _wcsicmp(name, L"mktemp") == 0 ||
        _wcsicmp(name, L"mode") == 0 ||
        _wcsicmp(name, L"more") == 0 ||
        _wcsicmp(name, L"move") == 0 ||
        _wcsicmp(name, L"mv") == 0 ||
        wcscmp(name, L"nameref") == 0 ||
        _wcsicmp(name, L"nice") == 0 ||
        _wcsicmp(name, L"nl") == 0 ||
        _wcsicmp(name, L"nohup") == 0 ||
        _wcsicmp(name, L"nproc") == 0 ||
        _wcsicmp(name, L"od") == 0 ||
        _wcsicmp(name, L"paste") == 0 ||
        _wcsicmp(name, L"ping") == 0 ||
        _wcsicmp(name, L"patch") == 0 ||
        _wcsicmp(name, L"pax") == 0 ||
        _wcsicmp(name, L"path") == 0 ||
        _wcsicmp(name, L"pgrep") == 0 ||
        _wcsicmp(name, L"pkill") == 0 ||
        _wcsicmp(name, L"pr") == 0 ||
        _wcsicmp(name, L"printenv") == 0 ||
        _wcsicmp(name, L"prompt") == 0 ||
        _wcsicmp(name, L"ps") == 0 ||
        wcscmp(name, L"r") == 0 ||
        _wcsicmp(name, L"rs") == 0 ||
        _wcsicmp(name, L"rot13") == 0 ||
        _wcsicmp(name, L"rd") == 0 ||
        _wcsicmp(name, L"readlink") == 0 ||
        _wcsicmp(name, L"realpath") == 0 ||
        _wcsicmp(name, L"ren") == 0 ||
        _wcsicmp(name, L"rename") == 0 ||
        _wcsicmp(name, L"renice") == 0 ||
        _wcsicmp(name, L"replace") == 0 ||
        _wcsicmp(name, L"rev") == 0 ||
        _wcsicmp(name, L"rm") == 0 ||
        _wcsicmp(name, L"rmdir") == 0 ||
        _wcsicmp(name, L"sed") == 0 ||
        _wcsicmp(name, L"seq") == 0 ||
        _wcsicmp(name, L"sha1") == 0 ||
        _wcsicmp(name, L"sha1sum") == 0 ||
        _wcsicmp(name, L"sha256") == 0 ||
        _wcsicmp(name, L"sha256sum") == 0 ||
        _wcsicmp(name, L"sha384") == 0 ||
        _wcsicmp(name, L"sha384sum") == 0 ||
        _wcsicmp(name, L"sha512") == 0 ||
        _wcsicmp(name, L"sha512sum") == 0 ||
        _wcsicmp(name, L"shred") == 0 ||
        _wcsicmp(name, L"shuf") == 0 ||
        _wcsicmp(name, L"shutdown") == 0 ||
        wcscmp(name, L"sleep") == 0 ||
        _wcsicmp(name, L"sort") == 0 ||
        _wcsicmp(name, L"split") == 0 ||
        _wcsicmp(name, L"srm") == 0 ||
        _wcsicmp(name, L"start") == 0 ||
        _wcsicmp(name, L"stat") == 0 ||
        _wcsicmp(name, L"strings") == 0 ||
        _wcsicmp(name, L"stty") == 0 ||
        _wcsicmp(name, L"subst") == 0 ||
        _wcsicmp(name, L"sum") == 0 ||
        _wcsicmp(name, L"sync") == 0 ||
        _wcsicmp(name, L"sysctl") == 0 ||
        _wcsicmp(name, L"systeminfo") == 0 ||
        _wcsicmp(name, L"tac") == 0 ||
        _wcsicmp(name, L"tail") == 0 ||
        _wcsicmp(name, L"tar") == 0 ||
        _wcsicmp(name, L"taskkill") == 0 ||
        _wcsicmp(name, L"tasklist") == 0 ||
        _wcsicmp(name, L"tee") == 0 ||
        _wcsicmp(name, L"time") == 0 ||
        _wcsicmp(name, L"timeout") == 0 ||
        _wcsicmp(name, L"title") == 0 ||
        _wcsicmp(name, L"touch") == 0 ||
        _wcsicmp(name, L"tr") == 0 ||
        wcscmp(name, L"trap") == 0 ||
        _wcsicmp(name, L"tree") == 0 ||
        _wcsicmp(name, L"traceroute") == 0 ||
        _wcsicmp(name, L"tracert") == 0 ||
        _wcsicmp(name, L"truncate") == 0 ||
        _wcsicmp(name, L"tsort") == 0 ||
        _wcsicmp(name, L"tty") == 0 ||
        _wcsicmp(name, L"uname") == 0 ||
        _wcsicmp(name, L"devinfo") == 0 ||
        _wcsicmp(name, L"ifconfig") == 0 ||
        _wcsicmp(name, L"netstat") == 0 ||
        _wcsicmp(name, L"unexpand") == 0 ||
        _wcsicmp(name, L"ul") == 0 ||
        _wcsicmp(name, L"uniq") == 0 ||
        _wcsicmp(name, L"unlink") == 0 ||
        _wcsicmp(name, L"unix2dos") == 0 ||
        _wcsicmp(name, L"uptime") == 0 ||
        _wcsicmp(name, L"uuidgen") == 0 ||
        _wcsicmp(name, L"ver") == 0 ||
        _wcsicmp(name, L"verify") == 0 ||
        _wcsicmp(name, L"vol") == 0 ||
        wcscmp(name, L"wait") == 0 ||
        _wcsicmp(name, L"watch") == 0 ||
        _wcsicmp(name, L"wc") == 0 ||
        _wcsicmp(name, L"what") == 0 ||
        _wcsicmp(name, L"whois") == 0 ||
        _wcsicmp(name, L"wget") == 0 ||
        _wcsicmp(name, L"where") == 0 ||
        _wcsicmp(name, L"which") == 0 ||
        _wcsicmp(name, L"whoami") == 0 ||
        _wcsicmp(name, L"xargs") == 0 ||
        _wcsicmp(name, L"yes") == 0 ||
        _wcsicmp(name, L"zcat") == 0 ||
        _wcsicmp(name, L"version") == 0 ||
        wcscmp(name, L"--version") == 0 ||
        _wcsicmp(name, L"cd") == 0 ||
        _wcsicmp(name, L"pushd") == 0 ||
        _wcsicmp(name, L"popd") == 0 ||
        _wcsicmp(name, L"pwd") == 0 ||
        _wcsicmp(name, L"echo") == 0 ||
        wcscmp(name, L"printf") == 0 ||
        wcscmp(name, L"print") == 0 ||
        wcscmp(name, L"let") == 0 ||
        wcscmp(name, L"setvar") == 0 ||
        wcscmp(name, L"eval") == 0 ||
        wcscmp(name, L"command") == 0 ||
        wcscmp(name, L"builtin") == 0 ||
        wcscmp(name, L"alias") == 0 ||
        wcscmp(name, L"unalias") == 0 ||
        wcscmp(name, L"type") == 0 ||
        wcscmp(name, L"whence") == 0 ||
        _wcsicmp(name, L"export") == 0 ||
        wcscmp(name, L"set") == 0 ||
        wcscmp(name, L"shift") == 0 ||
        wcscmp(name, L"readonly") == 0 ||
        wcscmp(name, L"typeset") == 0 ||
        wcscmp(name, L"local") == 0 ||
        wcscmp(name, L"exec") == 0 ||
        wcscmp(name, L"read") == 0 ||
        wcscmp(name, L"return") == 0 ||
        wcscmp(name, L"getopts") == 0 ||
        wcscmp(name, L"umask") == 0 ||
        wcscmp(name, L"ulimit") == 0 ||
        wcscmp(name, L"suspend") == 0 ||
        wcscmp(name, L"times") == 0 ||
        wcscmp(name, L"source") == 0 ||
        wcscmp(name, L".") == 0 ||
        _wcsicmp(name, L"setenv") == 0 ||
        _wcsicmp(name, L"unset") == 0 ||
        _wcsicmp(name, L"cls") == 0 ||
        _wcsicmp(name, L"clear") == 0 ||
        _wcsicmp(name, L"dir") == 0 ||
        wcscmp(name, L"test") == 0 ||
        wcscmp(name, L"[") == 0 ||
        wcscmp(name, L"[[") == 0 ||
        wcscmp(name, L":") == 0 ||
        wcscmp(name, L"true") == 0 ||
        wcscmp(name, L"false") == 0 ||
        _wcsicmp(name, L"sockstat") == 0 ||
        _wcsicmp(name, L"nc") == 0 ||
        _wcsicmp(name, L"netcat") == 0 ||
        _wcsicmp(name, L"mtree") == 0 ||
        _wcsicmp(name, L"look") == 0 ||
        _wcsicmp(name, L"daemon") == 0 ||
        _wcsicmp(name, L"lam") == 0 ||
        _wcsicmp(name, L"vmstat") == 0 ||
        _wcsicmp(name, L"pbcopy") == 0 ||
        _wcsicmp(name, L"pbpaste") == 0 ||
        _wcsicmp(name, L"fstat") == 0 ||
        _wcsicmp(name, L"script") == 0 ||
        _wcsicmp(name, L"vis") == 0 ||
        _wcsicmp(name, L"unvis") == 0 ||
        _wcsicmp(name, L"iostat") == 0 ||
        _wcsicmp(name, L"apply") == 0 ||
        _wcsicmp(name, L"pause") == 0 ||
        _wcsicmp(name, L"exit") == 0 ||
        _wcsicmp(name, L"module") == 0 ||
        _wcsicmp(name, L"help") == 0 ||
        _wcsicmp(name, L"man") == 0;
}

bool TryExecBuiltin(IShell* shell, CommandAST* cmd, int* out_code) {
    const wchar_t* name = cmd->argv[0];

    if (_wcsicmp(name, L"arp") == 0) return Builtin_Arp(cmd, out_code);
    if (_wcsicmp(name, L"assoc") == 0) return Builtin_Assoc(cmd, out_code);
    if (_wcsicmp(name, L"attrib") == 0) return Builtin_Attrib(cmd, out_code);
    if (wcscmp(name, L"autoload") == 0) return Builtin_Autoload(shell, cmd, out_code);
    if (_wcsicmp(name, L"awk") == 0) return Builtin_Awk(cmd, out_code);
    if (_wcsicmp(name, L"banner") == 0) return Builtin_Banner(cmd, out_code);
    if (_wcsicmp(name, L"base64") == 0) return Builtin_Base64(cmd, out_code);
    if (_wcsicmp(name, L"basename") == 0) return Builtin_Basename(cmd, out_code);
    if (wcscmp(name, L"bg") == 0) return Builtin_Bg(shell, cmd, out_code);
    if (_wcsicmp(name, L"break") == 0) return Builtin_Break(shell, cmd, out_code);
    if (_wcsicmp(name, L"cal") == 0) return Builtin_Cal(cmd, out_code);
    if (_wcsicmp(name, L"caesar") == 0 || _wcsicmp(name, L"rot13") == 0) return Builtin_Caesar(cmd, out_code);
    if (_wcsicmp(name, L"cat") == 0) return Builtin_Cat(cmd, out_code);
    if (_wcsicmp(name, L"chdir") == 0) return Builtin_CD(shell, cmd, out_code);
    if (_wcsicmp(name, L"chcp") == 0) return Builtin_Chcp(cmd, out_code);
    if (_wcsicmp(name, L"chflags") == 0) return Builtin_Chflags(cmd, out_code);
    if (_wcsicmp(name, L"chmod") == 0) return Builtin_Chmod(cmd, out_code);
    if (_wcsicmp(name, L"chown") == 0) return Builtin_Chown(cmd, out_code);
    if (_wcsicmp(name, L"choice") == 0) return Builtin_Choice(cmd, out_code);
    if (_wcsicmp(name, L"cksum") == 0) return Builtin_Cksum(cmd, out_code);
    if (_wcsicmp(name, L"clip") == 0) return Builtin_Clip(cmd, out_code);
    if (_wcsicmp(name, L"cmp") == 0) return Builtin_Cmp(cmd, out_code);
    if (_wcsicmp(name, L"color") == 0) return Builtin_Color(shell, cmd, out_code);
    if (_wcsicmp(name, L"col") == 0) return Builtin_Col(cmd, out_code);
    if (_wcsicmp(name, L"colrm") == 0) return Builtin_Colrm(cmd, out_code);
    if (_wcsicmp(name, L"column") == 0) return Builtin_Column(cmd, out_code);
    if (_wcsicmp(name, L"comm") == 0) return Builtin_Comm(cmd, out_code);
    if (_wcsicmp(name, L"csplit") == 0) return Builtin_Csplit(cmd, out_code);
    if (wcscmp(name, L"continue") == 0) return Builtin_Continue(shell, cmd, out_code);
    if (_wcsicmp(name, L"copy") == 0) return Builtin_Copy(shell, cmd, out_code);
    if (_wcsicmp(name, L"cp") == 0) return Builtin_Cp(shell, cmd, out_code);
    if (_wcsicmp(name, L"curl") == 0) return Builtin_Curl(cmd, out_code);
    if (_wcsicmp(name, L"cut") == 0) return Builtin_Cut(cmd, out_code);
    if (_wcsicmp(name, L"date") == 0) return Builtin_Date(cmd, out_code);
    if (_wcsicmp(name, L"dd") == 0) return Builtin_Dd(cmd, out_code);
    if (_wcsicmp(name, L"del") == 0 || _wcsicmp(name, L"erase") == 0) return Builtin_Del(cmd, out_code);
    if (_wcsicmp(name, L"df") == 0) return Builtin_Df(cmd, out_code);
    if (_wcsicmp(name, L"diff") == 0) return Builtin_Diff(cmd, out_code);
    if (_wcsicmp(name, L"dirname") == 0) return Builtin_Dirname(cmd, out_code);
    if (wcscmp(name, L"dirs") == 0) return Builtin_Dirs(shell, cmd, out_code);
    if (wcscmp(name, L"disown") == 0) return Builtin_Disown(shell, cmd, out_code);
    if (_wcsicmp(name, L"dos2unix") == 0) return Builtin_Dos2Unix(true, cmd, out_code);
    if (_wcsicmp(name, L"doskey") == 0) return Builtin_Doskey(shell, cmd, out_code);
    if (_wcsicmp(name, L"du") == 0) return Builtin_Du(cmd, out_code);
    if (_wcsicmp(name, L"egrep") == 0) return Builtin_Grep(cmd, out_code);
    if (_wcsicmp(name, L"env") == 0) return Builtin_Env(shell, cmd, out_code);
    if (_wcsicmp(name, L"expand") == 0) return Builtin_Expand(cmd, out_code);
    if (_wcsicmp(name, L"expr") == 0) return Builtin_Expr(cmd, out_code);
    if (_wcsicmp(name, L"factor") == 0) return Builtin_Factor(cmd, out_code);
    if (_wcsicmp(name, L"fc") == 0) return Builtin_FC(shell, cmd, out_code);
    if (wcscmp(name, L"fg") == 0) return Builtin_Fg(shell, cmd, out_code);
    if (_wcsicmp(name, L"fgrep") == 0) return Builtin_Grep(cmd, out_code);
    if (_wcsicmp(name, L"file") == 0) return Builtin_File(cmd, out_code);
    if (_wcsicmp(name, L"find") == 0) return Builtin_Find(cmd, out_code);
    if (_wcsicmp(name, L"findstr") == 0) return Builtin_Findstr(cmd, out_code);
    if (wcscmp(name, L"float") == 0) return Builtin_Float(shell, cmd, out_code);
    if (_wcsicmp(name, L"fmt") == 0) return Builtin_Fmt(cmd, out_code);
    if (_wcsicmp(name, L"fold") == 0) return Builtin_Fold(cmd, out_code);
    if (_wcsicmp(name, L"free") == 0) return Builtin_Free(cmd, out_code);
    if (_wcsicmp(name, L"ftype") == 0) return Builtin_Ftype(cmd, out_code);
    if (wcscmp(name, L"functions") == 0) return Builtin_Functions(shell, cmd, out_code);
    if (_wcsicmp(name, L"getent") == 0) return Builtin_Getent(cmd, out_code);
    if (_wcsicmp(name, L"grep") == 0) return Builtin_Grep(cmd, out_code);
    if (_wcsicmp(name, L"gunzip") == 0 || _wcsicmp(name, L"gzip") == 0 || _wcsicmp(name, L"zcat") == 0) return Builtin_Gzip(cmd, out_code);
    if (wcscmp(name, L"hash") == 0) return Builtin_Hash(cmd, out_code);
    if (_wcsicmp(name, L"hd") == 0 || _wcsicmp(name, L"hexdump") == 0) return Builtin_Hexdump(cmd, out_code);
    if (_wcsicmp(name, L"head") == 0) return Builtin_Head(cmd, out_code);
    if (wcscmp(name, L"hist") == 0) return Builtin_Hist(shell, cmd, out_code);
    if (wcscmp(name, L"history") == 0) return Builtin_History(shell, cmd, out_code);
    if (_wcsicmp(name, L"host") == 0) return Builtin_Host(cmd, out_code);
    if (_wcsicmp(name, L"hostname") == 0) return Builtin_Hostname(cmd, out_code);
    if (_wcsicmp(name, L"id") == 0) return Builtin_Id(cmd, out_code);
    if (_wcsicmp(name, L"install") == 0) return Builtin_Install(cmd, out_code);
    if (wcscmp(name, L"integer") == 0) return Builtin_Integer(shell, cmd, out_code);
    if (wcscmp(name, L"jobs") == 0) return Builtin_Jobs(shell, cmd, out_code);
    if (_wcsicmp(name, L"join") == 0) return Builtin_Join(cmd, out_code);
    if (_wcsicmp(name, L"jot") == 0) return Builtin_Jot(cmd, out_code);
    if (wcscmp(name, L"kill") == 0) return Builtin_Kill(cmd, out_code);
    if (_wcsicmp(name, L"ln") == 0) return Builtin_Ln(cmd, out_code);
    if (_wcsicmp(name, L"link") == 0) return Builtin_Link(cmd, out_code);
    if (_wcsicmp(name, L"leave") == 0) return Builtin_Leave(cmd, out_code);
    if (_wcsicmp(name, L"lockf") == 0) return Builtin_Lockf(shell, cmd, out_code);
    if (_wcsicmp(name, L"logname") == 0) return Builtin_Logname(cmd, out_code);
    if (_wcsicmp(name, L"ls") == 0) return Builtin_Ls(cmd, out_code);
    if (_wcsicmp(name, L"md") == 0 || _wcsicmp(name, L"mkdir") == 0) return Builtin_Md(cmd, out_code);
    if (_wcsicmp(name, L"md5") == 0 || _wcsicmp(name, L"md5sum") == 0) return Builtin_Checksum(CALG_MD5, L"md5", cmd, out_code);
    if (_wcsicmp(name, L"mklink") == 0) return Builtin_Mklink(cmd, out_code);
    if (_wcsicmp(name, L"mktemp") == 0) return Builtin_Mktemp(cmd, out_code);
    if (_wcsicmp(name, L"mode") == 0) return Builtin_Mode(cmd, out_code);
    if (_wcsicmp(name, L"more") == 0) return Builtin_More(cmd, out_code);
    if (_wcsicmp(name, L"move") == 0) return Builtin_Move(shell, cmd, out_code);
    if (_wcsicmp(name, L"mv") == 0) return Builtin_Mv(shell, cmd, out_code);
    if (wcscmp(name, L"nameref") == 0) return Builtin_Nameref(shell, cmd, out_code);
    if (_wcsicmp(name, L"nice") == 0) return Builtin_Nice(shell, cmd, out_code);
    if (_wcsicmp(name, L"nl") == 0) return Builtin_Nl(cmd, out_code);
    if (_wcsicmp(name, L"nohup") == 0) return Builtin_Nohup(shell, cmd, out_code);
    if (_wcsicmp(name, L"nproc") == 0) return Builtin_Nproc(cmd, out_code);
    if (_wcsicmp(name, L"od") == 0) return Builtin_Od(cmd, out_code);
    if (_wcsicmp(name, L"paste") == 0) return Builtin_Paste(cmd, out_code);
    if (_wcsicmp(name, L"ping") == 0) return Builtin_Ping(cmd, out_code);
    if (_wcsicmp(name, L"patch") == 0) return Builtin_Patch(cmd, out_code);
    if (_wcsicmp(name, L"pax") == 0) return Builtin_Pax(shell, cmd, out_code);
    if (_wcsicmp(name, L"path") == 0) return Builtin_Path(shell, cmd, out_code);
    if (_wcsicmp(name, L"pgrep") == 0) return Builtin_Pgrep(cmd, out_code);
    if (_wcsicmp(name, L"pkill") == 0) return Builtin_Pkill(cmd, out_code);
    if (_wcsicmp(name, L"pr") == 0) return Builtin_Pr(cmd, out_code);
    if (_wcsicmp(name, L"printenv") == 0) return Builtin_Printenv(shell, cmd, out_code);
    if (_wcsicmp(name, L"prompt") == 0) return Builtin_Prompt(shell, cmd, out_code);
    if (_wcsicmp(name, L"ps") == 0) return Builtin_Ps(cmd, out_code);
    if (wcscmp(name, L"r") == 0) return Builtin_R(shell, cmd, out_code);
    if (_wcsicmp(name, L"rs") == 0) return Builtin_Rs(cmd, out_code);
    if (_wcsicmp(name, L"rd") == 0 || _wcsicmp(name, L"rmdir") == 0) return Builtin_Rd(cmd, out_code);
    if (_wcsicmp(name, L"readlink") == 0) return Builtin_Readlink(cmd, out_code);
    if (_wcsicmp(name, L"realpath") == 0) return Builtin_Realpath(cmd, out_code);
    if (_wcsicmp(name, L"ren") == 0 || _wcsicmp(name, L"rename") == 0) return Builtin_Ren(cmd, out_code);
    if (_wcsicmp(name, L"renice") == 0) return Builtin_Renice(cmd, out_code);
    if (_wcsicmp(name, L"replace") == 0) return Builtin_Replace(cmd, out_code);
    if (_wcsicmp(name, L"rev") == 0) return Builtin_Rev(cmd, out_code);
    if (_wcsicmp(name, L"rm") == 0) return Builtin_Rm(cmd, out_code);
    if (_wcsicmp(name, L"sed") == 0) return Builtin_Sed(cmd, out_code);
    if (_wcsicmp(name, L"seq") == 0) return Builtin_Seq(cmd, out_code);
    if (_wcsicmp(name, L"sha1") == 0 || _wcsicmp(name, L"sha1sum") == 0) return Builtin_Checksum(CALG_SHA1, L"sha1", cmd, out_code);
    if (_wcsicmp(name, L"sha256") == 0 || _wcsicmp(name, L"sha256sum") == 0) return Builtin_Checksum(CALG_SHA_256, L"sha256", cmd, out_code);
    if (_wcsicmp(name, L"sha384") == 0 || _wcsicmp(name, L"sha384sum") == 0) return Builtin_Checksum(CALG_SHA_384, L"sha384", cmd, out_code);
    if (_wcsicmp(name, L"sha512") == 0 || _wcsicmp(name, L"sha512sum") == 0) return Builtin_Checksum(CALG_SHA_512, L"sha512", cmd, out_code);
    if (_wcsicmp(name, L"shuf") == 0) return Builtin_Shuf(cmd, out_code);
    if (_wcsicmp(name, L"shutdown") == 0) return Builtin_Shutdown(cmd, out_code);
    if (wcscmp(name, L"sleep") == 0) return Builtin_Sleep(cmd, out_code);
    if (_wcsicmp(name, L"sort") == 0) return Builtin_Sort(cmd, out_code);
    if (_wcsicmp(name, L"split") == 0) return Builtin_Split(cmd, out_code);
    if (_wcsicmp(name, L"srm") == 0 || _wcsicmp(name, L"shred") == 0) return Builtin_Srm(cmd, out_code);
    if (_wcsicmp(name, L"start") == 0) return Builtin_Start(shell, cmd, out_code);
    if (_wcsicmp(name, L"stat") == 0) return Builtin_Stat(cmd, out_code);
    if (_wcsicmp(name, L"strings") == 0) return Builtin_Strings(cmd, out_code);
    if (_wcsicmp(name, L"stty") == 0) return Builtin_Stty(cmd, out_code);
    if (_wcsicmp(name, L"subst") == 0) return Builtin_Subst(cmd, out_code);
    if (_wcsicmp(name, L"sum") == 0) return Builtin_Sum(cmd, out_code);
    if (_wcsicmp(name, L"sync") == 0) return Builtin_Sync(cmd, out_code);
    if (_wcsicmp(name, L"sysctl") == 0) return Builtin_Sysctl(cmd, out_code);
    if (_wcsicmp(name, L"systeminfo") == 0) return Builtin_Systeminfo(cmd, out_code);
    if (_wcsicmp(name, L"tac") == 0) return Builtin_Tac(cmd, out_code);
    if (_wcsicmp(name, L"tail") == 0) return Builtin_Tail(cmd, out_code);
    if (_wcsicmp(name, L"tar") == 0) return Builtin_Tar(shell, cmd, out_code);
    if (_wcsicmp(name, L"taskkill") == 0) return Builtin_Taskkill(cmd, out_code);
    if (_wcsicmp(name, L"tasklist") == 0) return Builtin_Tasklist(cmd, out_code);
    if (_wcsicmp(name, L"tee") == 0) return Builtin_Tee(cmd, out_code);
    if (_wcsicmp(name, L"time") == 0) return Builtin_Time(shell, cmd, out_code);
    if (_wcsicmp(name, L"timeout") == 0) return Builtin_Timeout(cmd, out_code);
    if (_wcsicmp(name, L"title") == 0) return Builtin_Title(cmd, out_code);
    if (_wcsicmp(name, L"touch") == 0) return Builtin_Touch(cmd, out_code);
    if (_wcsicmp(name, L"tr") == 0) return Builtin_Tr(cmd, out_code);
    if (wcscmp(name, L"trap") == 0) return Builtin_Trap(shell, cmd, out_code);
    if (_wcsicmp(name, L"tree") == 0) return Builtin_Tree(cmd, out_code);
    if (_wcsicmp(name, L"traceroute") == 0 || _wcsicmp(name, L"tracert") == 0) return Builtin_Traceroute(cmd, out_code);
    if (_wcsicmp(name, L"truncate") == 0) return Builtin_Truncate(cmd, out_code);
    if (_wcsicmp(name, L"tsort") == 0) return Builtin_Tsort(cmd, out_code);
    if (_wcsicmp(name, L"tty") == 0) return Builtin_Tty(cmd, out_code);
    if (_wcsicmp(name, L"devinfo") == 0) return Builtin_Devinfo(cmd, out_code);
    if (_wcsicmp(name, L"ifconfig") == 0) return Builtin_Ifconfig(cmd, out_code);
    if (_wcsicmp(name, L"netstat") == 0) return Builtin_Netstat(cmd, out_code);
    if (_wcsicmp(name, L"uname") == 0) return Builtin_Uname(cmd, out_code);
    if (_wcsicmp(name, L"unexpand") == 0) return Builtin_Unexpand(cmd, out_code);
    if (_wcsicmp(name, L"ul") == 0) return Builtin_Ul(cmd, out_code);
    if (_wcsicmp(name, L"uniq") == 0) return Builtin_Uniq(cmd, out_code);
    if (_wcsicmp(name, L"unlink") == 0) return Builtin_Unlink(cmd, out_code);
    if (_wcsicmp(name, L"unix2dos") == 0) return Builtin_Dos2Unix(false, cmd, out_code);
    if (_wcsicmp(name, L"uptime") == 0) return Builtin_Uptime(cmd, out_code);
    if (_wcsicmp(name, L"uuidgen") == 0) return Builtin_Uuidgen(cmd, out_code);
    if (_wcsicmp(name, L"ver") == 0) return Builtin_Ver(cmd, out_code);
    if (_wcsicmp(name, L"verify") == 0) return Builtin_Verify(shell, cmd, out_code);
    if (_wcsicmp(name, L"vol") == 0) return Builtin_Vol(cmd, out_code);
    if (wcscmp(name, L"wait") == 0) return Builtin_Wait(shell, cmd, out_code);
    if (_wcsicmp(name, L"watch") == 0) return Builtin_Watch(shell, cmd, out_code);
    if (_wcsicmp(name, L"wc") == 0) return Builtin_Wc(cmd, out_code);
    if (_wcsicmp(name, L"wget") == 0) return Builtin_Wget(cmd, out_code);
    if (_wcsicmp(name, L"where") == 0) return Builtin_Where(shell, cmd, out_code);
    if (_wcsicmp(name, L"what") == 0) return Builtin_What(cmd, out_code);
    if (_wcsicmp(name, L"whois") == 0) return Builtin_Whois(cmd, out_code);
    if (_wcsicmp(name, L"which") == 0) return Builtin_Which(shell, cmd, out_code);
    if (_wcsicmp(name, L"whoami") == 0) return Builtin_Whoami(cmd, out_code);
    if (_wcsicmp(name, L"xargs") == 0) return Builtin_Xargs(shell, cmd, out_code);
    if (_wcsicmp(name, L"yes") == 0) return Builtin_Yes(cmd, out_code);
    if (_wcsicmp(name, L"sockstat") == 0) return Builtin_Sockstat(cmd, out_code);
    if (_wcsicmp(name, L"nc") == 0 || _wcsicmp(name, L"netcat") == 0) return Builtin_Nc(cmd, out_code);
    if (_wcsicmp(name, L"mtree") == 0) return Builtin_Mtree(cmd, out_code);
    if (_wcsicmp(name, L"look") == 0) return Builtin_Look(cmd, out_code);
    if (_wcsicmp(name, L"daemon") == 0) return Builtin_Daemon(shell, cmd, out_code);
    if (_wcsicmp(name, L"lam") == 0) return Builtin_Lam(cmd, out_code);
    if (_wcsicmp(name, L"vmstat") == 0) return Builtin_Vmstat(cmd, out_code);
    if (_wcsicmp(name, L"pbcopy") == 0) return Builtin_Pbcopy(cmd, out_code);
    if (_wcsicmp(name, L"pbpaste") == 0) return Builtin_Pbpaste(cmd, out_code);
    if (_wcsicmp(name, L"fstat") == 0) return Builtin_Fstat(cmd, out_code);
    if (_wcsicmp(name, L"script") == 0) return Builtin_Script(shell, cmd, out_code);
    if (_wcsicmp(name, L"vis") == 0) return Builtin_Vis(cmd, out_code);
    if (_wcsicmp(name, L"unvis") == 0) return Builtin_Unvis(cmd, out_code);
    if (_wcsicmp(name, L"iostat") == 0) return Builtin_Iostat(cmd, out_code);
    if (_wcsicmp(name, L"apply") == 0) return Builtin_Apply(shell, cmd, out_code);

    if (_wcsicmp(name, L"cd") == 0) return Builtin_CD(shell, cmd, out_code);
    if (_wcsicmp(name, L"pushd") == 0)
        return Builtin_Pushd(shell, cmd, out_code);
    if (_wcsicmp(name, L"popd") == 0)
        return Builtin_Popd(shell, cmd, out_code);
    if (_wcsicmp(name, L"pwd") == 0) return Builtin_PWD(shell, cmd, out_code);
    if (_wcsicmp(name, L"echo") == 0) return Builtin_Echo(cmd, out_code);
    if (wcscmp(name, L"printf") == 0) return Builtin_Printf(cmd, out_code);
    if (wcscmp(name, L"print") == 0)
        return Builtin_Print(shell, cmd, out_code);
    if (wcscmp(name, L"let") == 0)
        return Builtin_Let(shell, cmd, out_code);
    if (wcscmp(name, L"setvar") == 0)
        return Builtin_Setvar(shell, cmd, out_code);
    if (wcscmp(name, L"eval") == 0) return Builtin_Eval(shell, cmd, out_code);
    if (wcscmp(name, L"command") == 0) return Builtin_Command(shell, cmd, out_code);
    if (wcscmp(name, L"builtin") == 0) return Builtin_Builtin(shell, cmd, out_code);
    if (_wcsicmp(name, L"module") == 0) return Builtin_Module(shell, cmd, out_code);
    if (wcscmp(name, L"alias") == 0)
        return Builtin_Alias(shell, cmd, out_code);
    if (wcscmp(name, L"unalias") == 0)
        return Builtin_Unalias(shell, cmd, out_code);
    if (wcscmp(name, L"type") == 0 || wcscmp(name, L"whence") == 0)
        return Builtin_TypeLike(shell, cmd, out_code);
    if (_wcsicmp(name, L"export") == 0) return Builtin_Export(shell, cmd, out_code);
    if (wcscmp(name, L"set") == 0) return Builtin_Set(shell, cmd, out_code);
    if (wcscmp(name, L"shift") == 0) return Builtin_Shift(shell, cmd, out_code);
    if (wcscmp(name, L"readonly") == 0) return Builtin_Readonly(shell, cmd, out_code);
    if (wcscmp(name, L"typeset") == 0) return Builtin_Typeset(shell, cmd, out_code);
    if (wcscmp(name, L"local") == 0) return Builtin_Local(shell, cmd, out_code);
    if (wcscmp(name, L"exec") == 0)
        return Builtin_Exec(shell, cmd, out_code);
    if (wcscmp(name, L"read") == 0) return Builtin_Read(shell, cmd, out_code);
    if (wcscmp(name, L"return") == 0) return Builtin_Return(shell, cmd, out_code);
    if (wcscmp(name, L"getopts") == 0) return Builtin_Getopts(shell, cmd, out_code);
    if (wcscmp(name, L"umask") == 0) return Builtin_Umask(shell, cmd, out_code);
    if (wcscmp(name, L"ulimit") == 0) return Builtin_Ulimit(shell, cmd, out_code);
    if (wcscmp(name, L"suspend") == 0) return Builtin_Suspend(cmd, out_code);
    if (wcscmp(name, L"times") == 0) return Builtin_Times(cmd, out_code);
    if (wcscmp(name, L"source") == 0 || wcscmp(name, L".") == 0)
        return Builtin_Source(shell, cmd, out_code);
    if (_wcsicmp(name, L"setenv") == 0) return Builtin_Setenv(shell, cmd, out_code);
    if (_wcsicmp(name, L"unset") == 0) return Builtin_Unset(shell, cmd, out_code);
    if (_wcsicmp(name, L"cls") == 0 || _wcsicmp(name, L"clear") == 0) return Builtin_Cls(cmd, out_code);
    if (_wcsicmp(name, L"dir") == 0) return Builtin_Dir(cmd, out_code);
    if (wcscmp(name, L"test") == 0 || wcscmp(name, L"[") == 0 || wcscmp(name, L"[[") == 0) return Builtin_Test(cmd, out_code);
    if (wcscmp(name, L":") == 0) return Builtin_Colon(cmd, out_code);
    if (wcscmp(name, L"true") == 0) return Builtin_True(cmd, out_code);
    if (wcscmp(name, L"false") == 0) return Builtin_False(cmd, out_code);
    if (_wcsicmp(name, L"pause") == 0) return Builtin_Pause(cmd, out_code);
    if (_wcsicmp(name, L"exit") == 0) return Builtin_Exit(shell, cmd, out_code);
    if (_wcsicmp(name, L"version") == 0 || wcscmp(name, L"--version") == 0) return Builtin_Version(cmd, out_code);
    if (_wcsicmp(name, L"help") == 0 || _wcsicmp(name, L"man") == 0) {
        DisplayComprehensiveHelp(cmd->argc > 1 ? cmd->argv[1] : NULL);
        *out_code = 0;
        return true;
    }

    if (Module_ExecDynamicBuiltin(cmd, out_code)) return true;

    return false;
}

