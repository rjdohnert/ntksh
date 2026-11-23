#ifndef KSH_BUILTINS_H
#define KSH_BUILTINS_H

#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>

bool IsBuiltinCommand(const CommandAST* cmd);
bool TryExecBuiltin(IShell* shell, CommandAST* cmd, int* out_code);
bool FindCommandPath(const wchar_t* name, wchar_t* path, size_t capacity);
bool TestParseInteger(const wchar_t* text, long long* result);
extern const char b64_table[];

/* Core builtins */
bool Builtin_CD(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_PWD(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Echo(CommandAST* cmd, int* code);
bool Builtin_Printf(CommandAST* cmd, int* code);
bool Builtin_Print(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Let(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Setvar(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Eval(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Exec(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_TypeLike(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Command(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Export(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Readonly(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Alias(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Unalias(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Typeset(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Set(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Shift(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Read(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Source(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Return(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Getopts(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Umask(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Ulimit(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Times(CommandAST* cmd, int* code);
bool Builtin_Setenv(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Unset(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Exit(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Colon(CommandAST* cmd, int* code);
bool Builtin_True(CommandAST* cmd, int* code);
bool Builtin_False(CommandAST* cmd, int* code);
bool Builtin_Local(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Pushd(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Popd(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Dirs(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Integer(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Float(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Functions(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Autoload(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Nameref(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Version(CommandAST* cmd, int* code);

/* Test builtins */
bool Builtin_Test(CommandAST* cmd, int* code);

/* Text builtins */
bool Builtin_Cat(CommandAST* cmd, int* code);
bool Builtin_Head(CommandAST* cmd, int* code);
bool Builtin_Tail(CommandAST* cmd, int* code);
bool Builtin_Wc(CommandAST* cmd, int* code);
bool Builtin_Tee(CommandAST* cmd, int* code);
bool Builtin_Seq(CommandAST* cmd, int* code);
bool Builtin_Expr(CommandAST* cmd, int* code);
bool Builtin_Cmp(CommandAST* cmd, int* code);
bool Builtin_Cut(CommandAST* cmd, int* code);
bool Builtin_Tr(CommandAST* cmd, int* code);
bool Builtin_Sort(CommandAST* cmd, int* code);
bool Builtin_Uniq(CommandAST* cmd, int* code);
bool Builtin_Paste(CommandAST* cmd, int* code);
bool Builtin_Fold(CommandAST* cmd, int* code);
bool Builtin_Expand(CommandAST* cmd, int* code);
bool Builtin_Unexpand(CommandAST* cmd, int* code);
bool Builtin_Nl(CommandAST* cmd, int* code);
bool Builtin_Comm(CommandAST* cmd, int* code);
bool Builtin_Join(CommandAST* cmd, int* code);
bool Builtin_Split(CommandAST* cmd, int* code);
bool Builtin_Rev(CommandAST* cmd, int* code);
bool Builtin_Tac(CommandAST* cmd, int* code);
bool Builtin_Shuf(CommandAST* cmd, int* code);
bool Builtin_Column(CommandAST* cmd, int* code);
bool Builtin_Colrm(CommandAST* cmd, int* code);
bool Builtin_Strings(CommandAST* cmd, int* code);
bool Builtin_Hexdump(CommandAST* cmd, int* code);
bool Builtin_Dos2Unix(bool to_unix, CommandAST* cmd, int* code);

/* Regex engine (POSIX BRE/ERE with GNU/BSD extensions) */
#define REGEX_GROUPS 10
#define REGEX_UNSET_POS ((size_t)-1)
#define REGEX_LONGEST 1u   /* leftmost-longest overall match (sed, awk, grep -o) */
#define REGEX_WORD 2u      /* match must not be adjacent to word characters (grep -w) */
#define REGEX_LINE 4u      /* match must span the whole line (grep -x) */
typedef struct RegexProg RegexProg;
typedef struct RegexMatch {
    size_t s[REGEX_GROUPS];
    size_t e[REGEX_GROUPS];
} RegexMatch;
bool RegexCompile(const wchar_t* pattern, bool ere, bool icase, RegexProg** out, const wchar_t** err);
void RegexFree(RegexProg* prog);
bool RegexSearch(RegexProg* prog, const wchar_t* text, size_t len, size_t from, unsigned flags, RegexMatch* m);

/* Search and stream-editing builtins */
bool GrepRegexMatch(const wchar_t* re, const wchar_t* text, bool ignore_case, bool word, bool exact, int* out_offset, int* out_len);
bool Builtin_Grep(CommandAST* cmd, int* code);
bool Builtin_Sed(CommandAST* cmd, int* code);
bool Builtin_Awk(CommandAST* cmd, int* code);
bool Builtin_Diff(CommandAST* cmd, int* code);

/* File builtins */
bool Builtin_Ls(CommandAST* cmd, int* code);
bool Builtin_Cp(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Mv(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Rm(CommandAST* cmd, int* code);
bool Builtin_Ln(CommandAST* cmd, int* code);
bool Builtin_Touch(CommandAST* cmd, int* code);
bool Builtin_Stat(CommandAST* cmd, int* code);
bool Builtin_Du(CommandAST* cmd, int* code);
bool Builtin_Df(CommandAST* cmd, int* code);
bool Builtin_File(CommandAST* cmd, int* code);
bool Builtin_Mktemp(CommandAST* cmd, int* code);
bool Builtin_Truncate(CommandAST* cmd, int* code);
bool Builtin_Dd(CommandAST* cmd, int* code);
bool Builtin_Install(CommandAST* cmd, int* code);
bool Builtin_Sync(CommandAST* cmd, int* code);
bool Builtin_Basename(CommandAST* cmd, int* code);
bool Builtin_Dirname(CommandAST* cmd, int* code);
bool Builtin_Realpath(CommandAST* cmd, int* code);
bool Builtin_Readlink(CommandAST* cmd, int* code);

/* Crypto builtins */
bool Builtin_Base64(CommandAST* cmd, int* code);
bool Builtin_Checksum(ALG_ID alg, const wchar_t* alg_name, CommandAST* cmd, int* code);
bool Builtin_Cksum(CommandAST* cmd, int* code);
bool Builtin_Uuidgen(CommandAST* cmd, int* code);
bool Builtin_Gzip(CommandAST* cmd, int* code);

/* System builtins */
bool Builtin_Ps(CommandAST* cmd, int* code);
bool Builtin_Pgrep(CommandAST* cmd, int* code);
bool Builtin_Pkill(CommandAST* cmd, int* code);
bool Builtin_Tasklist(CommandAST* cmd, int* code);
bool Builtin_Taskkill(CommandAST* cmd, int* code);
bool Builtin_Uptime(CommandAST* cmd, int* code);
bool Builtin_Free(CommandAST* cmd, int* code);
bool Builtin_Nproc(CommandAST* cmd, int* code);
bool Builtin_Cal(CommandAST* cmd, int* code);
bool Builtin_Factor(CommandAST* cmd, int* code);
bool Builtin_Hostname(CommandAST* cmd, int* code);
bool Builtin_Whoami(CommandAST* cmd, int* code);
bool Builtin_Id(CommandAST* cmd, int* code);
bool Builtin_Logname(CommandAST* cmd, int* code);
bool Builtin_Uname(CommandAST* cmd, int* code);
bool Builtin_Devinfo(CommandAST* cmd, int* code);
bool Builtin_Ifconfig(CommandAST* cmd, int* code);
bool Builtin_Netstat(CommandAST* cmd, int* code);
bool Builtin_Env(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Printenv(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Where(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Which(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Stty(CommandAST* cmd, int* code);
bool Builtin_Watch(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Timeout(CommandAST* cmd, int* code);
bool Builtin_Sleep(CommandAST* cmd, int* code);
bool Builtin_Cls(CommandAST* cmd, int* code);
bool Builtin_Shutdown(CommandAST* cmd, int* code);
bool Builtin_Systeminfo(CommandAST* cmd, int* code);
bool Builtin_Pause(CommandAST* cmd, int* code);
bool Builtin_Suspend(CommandAST* cmd, int* code);
bool Builtin_Jobs(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Bg(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Fg(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Wait(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Kill(CommandAST* cmd, int* code);
bool Builtin_Hash(CommandAST* cmd, int* code);
bool Builtin_Trap(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Break(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Continue(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Disown(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_History(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Hist(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_R(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Xargs(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Clip(CommandAST* cmd, int* code);
bool Builtin_Choice(CommandAST* cmd, int* code);
bool Builtin_Tty(CommandAST* cmd, int* code);
bool Builtin_Yes(CommandAST* cmd, int* code);

/* BSD builtins */
bool Builtin_Fmt(CommandAST* cmd, int* code);
bool Builtin_Csplit(CommandAST* cmd, int* code);
bool Builtin_Chmod(CommandAST* cmd, int* code);
bool Builtin_Chown(CommandAST* cmd, int* code);
bool Builtin_Jot(CommandAST* cmd, int* code);
bool Builtin_Chflags(CommandAST* cmd, int* code);
bool Builtin_Rs(CommandAST* cmd, int* code);
bool Builtin_Lockf(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Unlink(CommandAST* cmd, int* code);
bool Builtin_Link(CommandAST* cmd, int* code);
bool Builtin_Tsort(CommandAST* cmd, int* code);
bool Builtin_Nice(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Nohup(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Patch(CommandAST* cmd, int* code);
bool Builtin_Pax(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Tar(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Sockstat(CommandAST* cmd, int* code);
bool Builtin_Nc(CommandAST* cmd, int* code);
bool Builtin_Curl(CommandAST* cmd, int* code);
bool Builtin_Wget(CommandAST* cmd, int* code);
bool Builtin_Mtree(CommandAST* cmd, int* code);
bool Builtin_Look(CommandAST* cmd, int* code);
bool Builtin_Daemon(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Lam(CommandAST* cmd, int* code);
bool Builtin_Vmstat(CommandAST* cmd, int* code);
bool Builtin_Pbcopy(CommandAST* cmd, int* code);
bool Builtin_Pbpaste(CommandAST* cmd, int* code);
bool Builtin_Fstat(CommandAST* cmd, int* code);
bool Builtin_Script(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Vis(CommandAST* cmd, int* code);
bool Builtin_Unvis(CommandAST* cmd, int* code);
bool Builtin_Iostat(CommandAST* cmd, int* code);
bool Builtin_Apply(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Sysctl(CommandAST* cmd, int* code);
bool Builtin_Renice(CommandAST* cmd, int* code);
bool Builtin_Arp(CommandAST* cmd, int* code);
bool Builtin_Host(CommandAST* cmd, int* code);
bool Builtin_Getent(CommandAST* cmd, int* code);
bool Builtin_Srm(CommandAST* cmd, int* code);
bool Builtin_Pr(CommandAST* cmd, int* code);
bool Builtin_Banner(CommandAST* cmd, int* code);
bool Builtin_Ping(CommandAST* cmd, int* code);
bool Builtin_Traceroute(CommandAST* cmd, int* code);
bool Builtin_Whois(CommandAST* cmd, int* code);
bool Builtin_Col(CommandAST* cmd, int* code);
bool Builtin_Ul(CommandAST* cmd, int* code);
bool Builtin_What(CommandAST* cmd, int* code);
bool Builtin_Od(CommandAST* cmd, int* code);
bool Builtin_Sum(CommandAST* cmd, int* code);
bool Builtin_Leave(CommandAST* cmd, int* code);
bool Builtin_Caesar(CommandAST* cmd, int* code);
bool Builtin_Builtin(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Module(IShell* shell, CommandAST* cmd, int* code);

/* Dynamic Module API */
bool Module_Init(IShell* shell);
void Module_Cleanup(void);
bool Module_IsDynamicBuiltin(const wchar_t* name);
bool Module_ExecDynamicBuiltin(CommandAST* cmd, int* code);
size_t Module_GetDynamicBuiltinNames(const wchar_t* names[], size_t max_count);

/* CMD builtins */
bool Builtin_Assoc(CommandAST* cmd, int* code);
bool Builtin_Ftype(CommandAST* cmd, int* code);
bool Builtin_Mklink(CommandAST* cmd, int* code);
bool Builtin_Start(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Color(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Title(CommandAST* cmd, int* code);
bool Builtin_Vol(CommandAST* cmd, int* code);
bool Builtin_Chcp(CommandAST* cmd, int* code);
bool Builtin_Mode(CommandAST* cmd, int* code);
bool Builtin_More(CommandAST* cmd, int* code);
bool Builtin_Subst(CommandAST* cmd, int* code);
bool Builtin_Attrib(CommandAST* cmd, int* code);
bool Builtin_Doskey(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Tree(CommandAST* cmd, int* code);
bool Builtin_Replace(CommandAST* cmd, int* code);
bool Builtin_Ver(CommandAST* cmd, int* code);
bool Builtin_Verify(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Date(CommandAST* cmd, int* code);
bool Builtin_Date_POSIX(CommandAST* cmd, int* code);
bool Builtin_Time(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Prompt(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Find_BSD(CommandAST* cmd, int* code);
bool Builtin_Find(CommandAST* cmd, int* code);
bool Builtin_Findstr(CommandAST* cmd, int* code);
bool Builtin_FC(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Copy(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Move(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Del(CommandAST* cmd, int* code);
bool Builtin_Ren(CommandAST* cmd, int* code);
bool Builtin_Md(CommandAST* cmd, int* code);
bool Builtin_Rd(CommandAST* cmd, int* code);
bool Builtin_Path(IShell* shell, CommandAST* cmd, int* code);
bool Builtin_Dir(CommandAST* cmd, int* code);

#endif /* KSH_BUILTINS_H */
