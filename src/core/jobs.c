#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

wchar_t* g_history[MAX_HISTORY_ENTRIES];
int g_history_count = 0;
ShellJob g_jobs[64];
int g_job_count = 0;
void History_SaveToFile(const wchar_t* line) {
    if (!line || !*line) return;
    wchar_t home[MAX_PATH] = {0};
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
    if (!len) len = GetEnvironmentVariableW(L"HOME", home, MAX_PATH);
    if (!len || len >= MAX_PATH) return;

    wchar_t histPath[MAX_PATH];
    swprintf_s(histPath, MAX_PATH, L"%ls\\.ksh_history", home);

    FILE* fp = NULL;
    if (_wfopen_s(&fp, histPath, L"a, ccs=UTF-8") == 0 && fp) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fwprintf(fp, L"# %04d-%02d-%02d %02d:%02d:%02d\n%ls\n",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                 line);
        fclose(fp);
    }
}

void History_LoadFromFile(void) {
    wchar_t home[MAX_PATH] = {0};
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
    if (!len) len = GetEnvironmentVariableW(L"HOME", home, MAX_PATH);
    if (!len || len >= MAX_PATH) return;

    wchar_t histPath[MAX_PATH];
    swprintf_s(histPath, MAX_PATH, L"%ls\\.ksh_history", home);

    FILE* fp = NULL;
    if ((_wfopen_s(&fp, histPath, L"r, ccs=UTF-8") == 0 || _wfopen_s(&fp, histPath, L"r") == 0) && fp) {
        wchar_t line[MAX_LINE_LEN];
        while (fgetws(line, MAX_LINE_LEN, fp)) {
            size_t l = wcslen(line);
            while (l > 0 && (line[l - 1] == L'\n' || line[l - 1] == L'\r')) line[--l] = L'\0';
            if (l == 0 || line[0] == L'#') continue;
            if (g_history_count < MAX_HISTORY_ENTRIES) {
                wchar_t* dup = _wcsdup(line);
                if (dup) g_history[g_history_count++] = dup;
            }
        }
        fclose(fp);
    }
}

void History_Add(const wchar_t* line) {
    if (!line || !line[0]) return;
    if (wcscmp(line, L"r") == 0 || wcsncmp(line, L"r ", 2) == 0) return;
    if (g_history_count > 0 && wcscmp(g_history[g_history_count - 1], line) == 0) return;
    wchar_t* dup = _wcsdup(line);
    if (!dup) return;
    if (g_history_count < MAX_HISTORY_ENTRIES) {
        g_history[g_history_count++] = dup;
    } else {
        free(g_history[0]);
        memmove(&g_history[0], &g_history[1], (MAX_HISTORY_ENTRIES - 1) * sizeof(wchar_t*));
        g_history[MAX_HISTORY_ENTRIES - 1] = dup;
    }
    History_SaveToFile(line);
}

void History_Cleanup(void) {
    for (int i = 0; i < g_history_count; i++) {
        if (g_history[i]) {
            free(g_history[i]);
            g_history[i] = NULL;
        }
    }
    g_history_count = 0;
}

void Shell_UpdateJobsNotify(IShell* shell) {
    for (int i = 0; i < g_job_count; i++) {
        if (g_jobs[i].running && g_jobs[i].hProcess) {
            DWORD waitRes = WaitForSingleObject(g_jobs[i].hProcess, 0);
            if (waitRes == WAIT_OBJECT_0) {
                DWORD ec = 0;
                GetExitCodeProcess(g_jobs[i].hProcess, &ec);
                g_jobs[i].exit_code = (int)ec;
                g_jobs[i].running = false;
                CloseHandle(g_jobs[i].hProcess);
                g_jobs[i].hProcess = NULL;
                if (shell && (shell->notify || shell->interactive)) {
                    wprintf(L"[%d]+ Done %ls\n", g_jobs[i].id, g_jobs[i].command);
                }
            }
        }
    }
}

void Shell_UpdateJobs(void) {
    Shell_UpdateJobsNotify(NULL);
}

typedef LONG (NTAPI *PFN_NtSuspendProcess)(HANDLE ProcessHandle);
typedef LONG (NTAPI *PFN_NtResumeProcess)(HANDLE ProcessHandle);
static PFN_NtSuspendProcess s_pfnNtSuspendProcess = NULL;
static PFN_NtResumeProcess s_pfnNtResumeProcess = NULL;
static bool s_ntdll_procs_loaded = false;

static void EnsureNtProcs(void) {
    if (s_ntdll_procs_loaded) return;
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        s_pfnNtSuspendProcess = (PFN_NtSuspendProcess)GetProcAddress(hNtdll, "NtSuspendProcess");
        s_pfnNtResumeProcess = (PFN_NtResumeProcess)GetProcAddress(hNtdll, "NtResumeProcess");
    }
    s_ntdll_procs_loaded = true;
}

static bool Ksh_SuspendProcess(HANDLE hProcess) {
    EnsureNtProcs();
    return s_pfnNtSuspendProcess && hProcess && (s_pfnNtSuspendProcess(hProcess) >= 0);
}

static bool Ksh_ResumeProcess(HANDLE hProcess) {
    EnsureNtProcs();
    return s_pfnNtResumeProcess && hProcess && (s_pfnNtResumeProcess(hProcess) >= 0);
}

static void TerminateProcessTree(DWORD pid, UINT exitCode) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { 0 };
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ParentProcessID == pid && pe.th32ProcessID != 0) {
                    TerminateProcessTree(pe.th32ProcessID, exitCode);
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    HANDLE hp = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hp) {
        TerminateProcess(hp, exitCode);
        CloseHandle(hp);
    }
}

void Shell_TerminateActiveJobs(void) {
    for (int i = 0; i < g_job_count; i++) {
        if ((g_jobs[i].running || g_jobs[i].stopped) && !g_jobs[i].disowned && g_jobs[i].pid != 0) {
            TerminateProcessTree(g_jobs[i].pid, 1);
        }
    }
}

ShellJob* ResolveJobSpec(const wchar_t* spec) {
    if (!spec) return NULL;
    if (spec[0] != L'%') {
        int id = _wtoi(spec);
        for (int k = 0; k < g_job_count; k++) {
            if (g_jobs[k].id == id || g_jobs[k].pid == (DWORD)id) return &g_jobs[k];
        }
        return NULL;
    }
    const wchar_t* p = spec + 1;
    if (*p == L'%' || *p == L'+' || *p == L'\0') {
        for (int k = g_job_count - 1; k >= 0; k--) {
            if (g_jobs[k].running || g_jobs[k].stopped) return &g_jobs[k];
        }
        return g_job_count > 0 ? &g_jobs[g_job_count - 1] : NULL;
    }
    if (*p == L'-') {
        int found = 0;
        for (int k = g_job_count - 1; k >= 0; k--) {
            if (g_jobs[k].running || g_jobs[k].stopped) {
                found++;
                if (found == 2) return &g_jobs[k];
            }
        }
        return NULL;
    }
    if (*p == L'?') {
        const wchar_t* needle = p + 1;
        for (int k = g_job_count - 1; k >= 0; k--) {
            if (wcsstr(g_jobs[k].command, needle)) return &g_jobs[k];
        }
        return NULL;
    }
    if (iswdigit(*p)) {
        int id = _wtoi(p);
        for (int k = 0; k < g_job_count; k++) {
            if (g_jobs[k].id == id) return &g_jobs[k];
        }
        return NULL;
    }
    size_t plen = wcslen(p);
    for (int k = g_job_count - 1; k >= 0; k--) {
        if (_wcsnicmp(g_jobs[k].command, p, plen) == 0) return &g_jobs[k];
    }
    return NULL;
}

bool Ksh_HasPseudoConsoleSupport(void) {
    HMODULE hMod = GetModuleHandleW(L"kernel32.dll");
    return hMod && GetProcAddress(hMod, "CreatePseudoConsole") != NULL;
}

static DWORD g_last_background_pid = 0;

DWORD Shell_LastBackgroundPid(void) {
    return g_last_background_pid;
}

int Shell_AddJob(DWORD pid, HANDLE hProcess, const wchar_t* command) {
    Shell_UpdateJobs();
    g_last_background_pid = pid;
    int slot = -1;
    for (int i = 0; i < g_job_count; i++) {
        if (!g_jobs[i].running && g_jobs[i].hProcess == NULL) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        if (g_job_count < 64) slot = g_job_count++;
        else {
            slot = 0;
            if (g_jobs[0].hProcess) {
                CloseHandle(g_jobs[0].hProcess);
                g_jobs[0].hProcess = NULL;
            }
        }
    }
    g_jobs[slot].id = slot + 1;
    g_jobs[slot].pid = pid;
    g_jobs[slot].hProcess = hProcess;
    wcsncpy_s(g_jobs[slot].command, _countof(g_jobs[slot].command), command ? command : L"", _TRUNCATE);
    g_jobs[slot].running = true;
    g_jobs[slot].exit_code = 0;
    return g_jobs[slot].id;
}

bool Builtin_Jobs(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    Shell_UpdateJobs();
    bool opt_p = false, opt_l = false, opt_r = false, opt_s = false;
    for (int i = 1; i < cmd->argc; i++) {
        if (wcscmp(cmd->argv[i], L"-p") == 0) opt_p = true;
        else if (wcscmp(cmd->argv[i], L"-l") == 0) opt_l = true;
        else if (wcscmp(cmd->argv[i], L"-r") == 0) opt_r = true;
        else if (wcscmp(cmd->argv[i], L"-s") == 0) opt_s = true;
    }
    for (int i = 0; i < g_job_count; i++) {
        if (!g_jobs[i].hProcess && !g_jobs[i].running && !g_jobs[i].stopped && g_jobs[i].exit_code == 0 && g_jobs[i].pid == 0) continue;
        if (opt_r && !g_jobs[i].running) continue;
        if (opt_s && !g_jobs[i].stopped) continue;
        const wchar_t* stateStr = g_jobs[i].stopped ? L"Stopped" : (g_jobs[i].running ? L"Running" : L"Done");
        if (opt_p) {
            wprintf(L"%lu\n", g_jobs[i].pid);
        } else if (opt_l) {
            wprintf(L"[%d]+ %lu %-16ls %ls\n", g_jobs[i].id, g_jobs[i].pid,
                stateStr, g_jobs[i].command);
        } else {
            wprintf(L"[%d]+  %-16ls %ls\n", g_jobs[i].id,
                stateStr, g_jobs[i].command);
        }
    }
    *code = 0;
    return true;
}

bool Builtin_Bg(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    Shell_UpdateJobs();
    ShellJob* job = (cmd->argc > 1) ? ResolveJobSpec(cmd->argv[1]) : ResolveJobSpec(L"%+");
    if (!job) {
        fwprintf(stderr, L"ksh: bg: no current job\n");
        *code = 1;
        return true;
    }
    if (job->stopped && job->hProcess) {
        Ksh_ResumeProcess(job->hProcess);
        job->stopped = false;
        job->running = true;
    }
    wprintf(L"[%d]+ %ls &\n", job->id, job->command);
    *code = 0;
    return true;
}

bool Builtin_Fg(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    Shell_UpdateJobs();
    ShellJob* job = (cmd->argc > 1) ? ResolveJobSpec(cmd->argv[1]) : ResolveJobSpec(L"%+");
    if (!job) {
        fwprintf(stderr, L"ksh: fg: no current job\n");
        *code = 1;
        return true;
    }
    wprintf(L"%ls\n", job->command);
    if (job->stopped && job->hProcess) {
        Ksh_ResumeProcess(job->hProcess);
        job->stopped = false;
        job->running = true;
    }
    if (job->running && job->hProcess) {
        WaitForSingleObject(job->hProcess, INFINITE);
        DWORD ec = 0;
        GetExitCodeProcess(job->hProcess, &ec);
        job->exit_code = (int)ec;
        job->running = false;
        CloseHandle(job->hProcess);
        job->hProcess = NULL;
    }
    *code = job->exit_code;
    return true;
}

bool Builtin_Wait(IShell* shell, CommandAST* cmd, int* code) {
    Shell_UpdateJobs();
    bool opt_n = false;
    const wchar_t* opt_p_var = NULL;
    int firstArg = 1;
    while (firstArg < cmd->argc && cmd->argv[firstArg][0] == L'-') {
        if (wcscmp(cmd->argv[firstArg], L"-n") == 0) opt_n = true;
        else if (wcscmp(cmd->argv[firstArg], L"-p") == 0 && firstArg + 1 < cmd->argc) {
            opt_p_var = cmd->argv[++firstArg];
        }
        firstArg++;
    }
    if (opt_n) {
        HANDLE handles[64];
        int jobMap[64];
        DWORD count = 0;
        for (int i = 0; i < g_job_count; i++) {
            if (g_jobs[i].running && g_jobs[i].hProcess) {
                handles[count] = g_jobs[i].hProcess;
                jobMap[count] = i;
                count++;
            }
        }
        if (count > 0) {
            DWORD waitRes = WaitForMultipleObjects(count, handles, FALSE, INFINITE);
            if (waitRes >= WAIT_OBJECT_0 && waitRes < WAIT_OBJECT_0 + count) {
                int slot = jobMap[waitRes - WAIT_OBJECT_0];
                DWORD ec = 0;
                GetExitCodeProcess(g_jobs[slot].hProcess, &ec);
                g_jobs[slot].exit_code = (int)ec;
                g_jobs[slot].running = false;
                CloseHandle(g_jobs[slot].hProcess);
                g_jobs[slot].hProcess = NULL;
                if (opt_p_var && shell) {
                    wchar_t pidBuf[32];
                    swprintf_s(pidBuf, _countof(pidBuf), L"%lu", g_jobs[slot].pid);
                    shell->env->Set(shell->env, opt_p_var, pidBuf);
                }
                *code = (int)ec;
                return true;
            }
        }
        *code = 127;
        return true;
    }
    if (firstArg >= cmd->argc) {
        int lastCode = 0;
        for (int i = 0; i < g_job_count; i++) {
            if (g_jobs[i].running && g_jobs[i].hProcess) {
                WaitForSingleObject(g_jobs[i].hProcess, INFINITE);
                DWORD ec = 0;
                GetExitCodeProcess(g_jobs[i].hProcess, &ec);
                g_jobs[i].exit_code = (int)ec;
                g_jobs[i].running = false;
                lastCode = (int)ec;
                CloseHandle(g_jobs[i].hProcess);
                g_jobs[i].hProcess = NULL;
            }
        }
        *code = lastCode;
        return true;
    }
    int lastCode = 0;
    for (int argi = firstArg; argi < cmd->argc; argi++) {
        const wchar_t* a = cmd->argv[argi];
        ShellJob* j = ResolveJobSpec(a);
        if (!j && a[0] != L'%') {
            DWORD pid = (DWORD)_wtoi(a);
            for (int i = 0; i < g_job_count; i++) {
                if (g_jobs[i].pid == pid) { j = &g_jobs[i]; break; }
            }
        }
        if (j && j->running && j->hProcess) {
            WaitForSingleObject(j->hProcess, INFINITE);
            DWORD ec = 0;
            GetExitCodeProcess(j->hProcess, &ec);
            j->exit_code = (int)ec;
            j->running = false;
            lastCode = (int)ec;
            CloseHandle(j->hProcess);
            j->hProcess = NULL;
        } else if (j) {
            lastCode = j->exit_code;
        } else {
            DWORD pid = (DWORD)_wtoi(a);
            HANDLE hp = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, pid);
            if (hp) {
                WaitForSingleObject(hp, INFINITE);
                DWORD ec = 0;
                GetExitCodeProcess(hp, &ec);
                lastCode = (int)ec;
                CloseHandle(hp);
            } else {
                lastCode = 127;
            }
        }
    }
    *code = lastCode;
    return true;
}

typedef struct KillSignal {
    int number;
    const wchar_t* name;
    bool supported;
} KillSignal;

/* Names are listed for kill -l N; only supported signals can be delivered. */
static const KillSignal g_kill_signals[] = {
    { 0, NULL, true },
    { 1, L"HUP", true },
    { 2, L"INT", true },
    { 3, L"QUIT", false },
    { 4, L"ILL", false },
    { 5, L"TRAP", false },
    { 6, L"ABRT", false },
    { 7, L"BUS", false },
    { 8, L"FPE", false },
    { 9, L"KILL", true },
    { 10, L"USR1", false },
    { 11, L"SEGV", false },
    { 12, L"USR2", false },
    { 13, L"PIPE", false },
    { 14, L"ALRM", false },
    { 15, L"TERM", true },
    { 17, L"STOP", true },
    { 18, L"TSTP", true },
    { 19, L"CONT", true },
};

static const KillSignal* FindKillSignalByNumber(int number) {
    for (size_t i = 0; i < _countof(g_kill_signals); ++i) {
        if (g_kill_signals[i].number == number) return &g_kill_signals[i];
    }
    return NULL;
}

static const KillSignal* ParseKillSignal(const wchar_t* spec) {
    const wchar_t* name = spec;
    if (_wcsnicmp(name, L"SIG", 3) == 0) name += 3;
    bool numeric = *name && wcsspn(name, L"0123456789") == wcslen(name);
    for (size_t i = 0; i < _countof(g_kill_signals); ++i) {
        const KillSignal* signal = &g_kill_signals[i];
        if (numeric && _wtoi(name) == signal->number) return signal;
        if (signal->name && _wcsicmp(name, signal->name) == 0) return signal;
    }
    return NULL;
}

static bool ResolveKillSignal(const wchar_t* spec, const KillSignal** signal,
    int* code) {
    const KillSignal* found = ParseKillSignal(spec);
    if (!found) {
        fwprintf(stderr, L"ksh: kill: %ls: invalid signal specification\n", spec);
        *code = 2;
        return false;
    }
    if (!found->supported) {
        fwprintf(stderr, L"ksh: kill: %ls: signal not supported on Windows\n", spec);
        *code = 2;
        return false;
    }
    *signal = found;
    return true;
}

bool Builtin_Kill(CommandAST* cmd, int* code) {
    if (cmd->argc < 2) {
        fwprintf(stderr, L"ksh: kill: usage: kill [-s signame | -signum | -sigspec] pid | kill -l [exit_status]\n");
        *code = 1;
        return true;
    }
    if (wcscmp(cmd->argv[1], L"-l") == 0) {
        if (cmd->argc > 2) {
            int signum = _wtoi(cmd->argv[2]);
            if (signum > 128) signum -= 128;
            const KillSignal* found = FindKillSignalByNumber(signum);
            if (!found || !found->name) {
                fwprintf(stderr, L"ksh: kill: %ls: invalid signal number\n", cmd->argv[2]);
                *code = 2;
                return true;
            }
            wprintf(L"%ls\n", found->name);
        } else {
            wprintf(L" 1) SIGHUP      2) SIGINT      9) SIGKILL    15) SIGTERM    17) SIGSTOP    18) SIGTSTP    19) SIGCONT\n");
        }
        *code = 0;
        return true;
    }
    int sig = 15;
    int firstPid = 1;
    if (wcscmp(cmd->argv[1], L"-s") == 0 && cmd->argc > 2) {
        const KillSignal* signal = NULL;
        if (!ResolveKillSignal(cmd->argv[2], &signal, code)) return true;
        sig = signal->number;
        firstPid = 3;
    } else if (cmd->argv[1][0] == L'-' && cmd->argv[1][1]) {
        const KillSignal* signal = NULL;
        if (!ResolveKillSignal(cmd->argv[1] + 1, &signal, code)) return true;
        sig = signal->number;
        firstPid = 2;
    }
    int overall = 0;
    for (int i = firstPid; i < cmd->argc; i++) {
        const wchar_t* a = cmd->argv[i];
        DWORD pid = 0;
        ShellJob* job = NULL;
        if (a[0] == L'%') {
            job = ResolveJobSpec(a);
            if (job) pid = job->pid;
        } else {
            pid = (DWORD)_wtoi(a);
            for (int k = 0; k < g_job_count; k++) {
                if (g_jobs[k].pid == pid) { job = &g_jobs[k]; break; }
            }
        }
        if (pid == 0) {
            fwprintf(stderr, L"ksh: kill: %ls: arguments must be process or job IDs\n", a);
            overall = 1;
            continue;
        }
        if (sig == 0) {
            HANDLE hq = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            DWORD exitCode = 0;
            bool alive = hq && GetExitCodeProcess(hq, &exitCode) && exitCode == STILL_ACTIVE;
            if (hq) CloseHandle(hq);
            if (!alive) {
                fwprintf(stderr, L"ksh: kill: (%ls) - No such process\n", a);
                overall = 1;
            }
            continue;
        }
        if (sig == 17 || sig == 18) {
            /* STOP / TSTP - Process suspension */
            HANDLE hp = job && job->hProcess ? job->hProcess : OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
            if (hp && Ksh_SuspendProcess(hp)) {
                if (job) { job->stopped = true; job->running = false; }
            } else {
                fwprintf(stderr, L"ksh: kill: (%ls) - Cannot suspend process\n", a);
                overall = 1;
            }
            if (!job || !job->hProcess) if (hp) CloseHandle(hp);
            continue;
        }
        if (sig == 19) {
            /* CONT - Process resumption */
            HANDLE hp = job && job->hProcess ? job->hProcess : OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
            if (hp && Ksh_ResumeProcess(hp)) {
                if (job) { job->stopped = false; job->running = true; }
            } else {
                fwprintf(stderr, L"ksh: kill: (%ls) - Cannot resume process\n", a);
                overall = 1;
            }
            if (!job || !job->hProcess) if (hp) CloseHandle(hp);
            continue;
        }
        /* KILL (9), TERM (15), etc. with tree termination */
        HANDLE hq = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        DWORD exitCode = 0;
        bool exists = hq && GetExitCodeProcess(hq, &exitCode) && exitCode == STILL_ACTIVE;
        if (hq) CloseHandle(hq);
        if (!exists) {
            fwprintf(stderr, L"ksh: kill: (%ls) - No such process\n", a);
            overall = 1;
            continue;
        }
        TerminateProcessTree(pid, 128 + sig);
        if (job) {
            job->running = false;
            job->stopped = false;
            job->exit_code = 128 + sig;
            if (job->hProcess) { CloseHandle(job->hProcess); job->hProcess = NULL; }
        }
    }
    *code = overall;
    return true;
}

typedef struct ShellHashEntry {
    wchar_t name[128];
    wchar_t path[MAX_PATH];
    int hits;
    struct ShellHashEntry* next;
} ShellHashEntry;

static ShellHashEntry* g_hashTable = NULL;

bool Builtin_Hash(CommandAST* cmd, int* code) {
    bool opt_r = false;
    for (int i = 1; i < cmd->argc; i++) {
        if (wcscmp(cmd->argv[i], L"-r") == 0) {
            ShellHashEntry* curr = g_hashTable;
            while (curr) {
                ShellHashEntry* next = curr->next;
                free(curr);
                curr = next;
            }
            g_hashTable = NULL;
            opt_r = true;
        }
    }
    if (opt_r && cmd->argc == 2) {
        *code = 0;
        return true;
    }
    int nameStart = 1;
    while (nameStart < cmd->argc && cmd->argv[nameStart][0] == L'-') nameStart++;
    if (nameStart >= cmd->argc) {
        if (!g_hashTable) {
            wprintf(L"ksh: hash: hash table empty\n");
        } else {
            wprintf(L"hits\tcommand\n");
            for (ShellHashEntry* e = g_hashTable; e; e = e->next) {
                wprintf(L"%4d\t%ls (%ls)\n", e->hits, e->name, e->path);
            }
        }
        *code = 0;
        return true;
    }
    int overall = 0;
    for (int i = nameStart; i < cmd->argc; i++) {
        const wchar_t* name = cmd->argv[i];
        wchar_t resolved[MAX_PATH] = {0};
        DWORD res = FindCommandPath(name, resolved, _countof(resolved)) ? 1 : 0;
        if (res == 0) {
            fwprintf(stderr, L"ksh: hash: %ls: not found\n", name);
            overall = 1;
            continue;
        }
        ShellHashEntry* found = NULL;
        for (ShellHashEntry* e = g_hashTable; e; e = e->next) {
            if (_wcsicmp(e->name, name) == 0) { found = e; break; }
        }
        if (found) {
            wcsncpy_s(found->path, _countof(found->path), resolved, _TRUNCATE);
        } else {
            ShellHashEntry* newEntry = (ShellHashEntry*)calloc(1, sizeof(ShellHashEntry));
            if (newEntry) {
                wcsncpy_s(newEntry->name, _countof(newEntry->name), name, _TRUNCATE);
                wcsncpy_s(newEntry->path, _countof(newEntry->path), resolved, _TRUNCATE);
                newEntry->hits = 0;
                newEntry->next = g_hashTable;
                g_hashTable = newEntry;
            }
        }
    }
    *code = overall;
    return true;
}

ShellTrap* g_traps = NULL;

/* Console handler threads only set bits; trap actions run on the shell thread. */
enum { SIGBIT_INT = 1, SIGBIT_QUIT = 2, SIGBIT_HUP = 4 };
static volatile LONG g_trapped_signals = 0;
static volatile LONG g_pending_signals = 0;

static LONG TrapBitForName(const wchar_t* name) {
    if (wcscmp(name, L"INT") == 0) return SIGBIT_INT;
    if (wcscmp(name, L"QUIT") == 0) return SIGBIT_QUIT;
    if (wcscmp(name, L"HUP") == 0) return SIGBIT_HUP;
    return 0;
}

static void Trap_UpdateMask(void) {
    LONG mask = 0;
    for (ShellTrap* t = g_traps; t; t = t->next) {
        if (t->action && *t->action) mask |= TrapBitForName(t->sig);
    }
    InterlockedExchange(&g_trapped_signals, mask);
}

volatile LONG g_interrupted = 0;
HANDLE g_foreground_process = NULL;
IShell* g_active_shell = NULL;

static BOOL WINAPI Shell_CtrlHandler(DWORD event) {
    LONG bit;
    switch (event) {
    case CTRL_C_EVENT: bit = SIGBIT_INT; break;
    case CTRL_BREAK_EVENT: bit = SIGBIT_QUIT; break;
    case CTRL_CLOSE_EVENT: bit = SIGBIT_HUP; break;
    default: return FALSE;
    }

    if (g_trapped_signals & bit) {
        InterlockedOr(&g_pending_signals, bit);
        return TRUE;
    }

    if (event == CTRL_CLOSE_EVENT) {
        return FALSE;
    }

    /* Prevent untrapped Ctrl-C / Ctrl-Break from terminating the shell process. */
    InterlockedExchange(&g_interrupted, 1);
    InterlockedOr(&g_pending_signals, bit);

    if (g_active_shell) {
        g_active_shell->last_status = 130;
        if (g_active_shell->env) g_active_shell->env->lastStatus = 130;
        if (!g_active_shell->interactive && !g_foreground_process) {
            g_active_shell->running = false;
        }
    }

    return TRUE;
}

void Shell_InstallSignalHandler(void) {
    SetConsoleCtrlHandler(Shell_CtrlHandler, TRUE);
}

void Shell_DispatchPendingSignals(IShell* shell) {
    LONG pending = InterlockedExchange(&g_pending_signals, 0);
    if (!pending) return;
    if (pending & SIGBIT_INT) Shell_FireTrap(shell, L"INT");
    if (pending & SIGBIT_QUIT) Shell_FireTrap(shell, L"QUIT");
    if (pending & SIGBIT_HUP) Shell_FireTrap(shell, L"HUP");
}

void Shell_FireTrap(IShell* shell, const wchar_t* sig) {
    if (!shell || !sig) return;
    for (ShellTrap* t = g_traps; t; t = t->next) {
        if (_wcsicmp(t->sig, sig) == 0 || (_wcsicmp(sig, L"EXIT") == 0 && wcscmp(t->sig, L"0") == 0)) {
            if (t->action && *t->action) {
                wchar_t* act = SafeStrDup(t->action);
                if (_wcsicmp(sig, L"EXIT") == 0 || wcscmp(t->sig, L"0") == 0) {
                    free(t->action);
                    t->action = NULL;
                }
                bool savedRunning = shell->running;
                shell->running = true;
                shell->ExecuteLine(shell, act);
                shell->running = savedRunning;
                free(act);
            }
            break;
        }
    }
}

static void Trap_PrintQuoted(const wchar_t* s) {
    for (; *s; s++) {
        if (*s == L'\'') fputws(L"'\\''", stdout);
        else putwchar(*s);
    }
}

bool Builtin_Trap(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    int first = 1;
    if (cmd->argc > 1 && wcscmp(cmd->argv[1], L"--") == 0) first = 2;
    if (first >= cmd->argc || (cmd->argc == 2 && wcscmp(cmd->argv[1], L"-p") == 0)) {
        for (ShellTrap* t = g_traps; t; t = t->next) {
            if (t->action) {
                fputws(L"trap -- '", stdout);
                Trap_PrintQuoted(t->action);
                wprintf(L"' %ls\n", t->sig);
            }
        }
        *code = 0;
        return true;
    }
    const wchar_t* action = cmd->argv[first];
    bool reset = (wcscmp(action, L"-") == 0);
    if (first + 1 >= cmd->argc) {
        if (!reset) {
            fwprintf(stderr, L"ksh: trap: usage: trap [-- action] signal [signal ...]\n");
            *code = 2;
            return true;
        }
        for (ShellTrap* t = g_traps; t; t = t->next) {
            free(t->action);
            t->action = NULL;
        }
        Trap_UpdateMask();
        *code = 0;
        return true;
    }
    for (int i = first + 1; i < cmd->argc; i++) {
        const wchar_t* sname = cmd->argv[i];
        wchar_t normSig[32];
        if (_wcsnicmp(sname, L"SIG", 3) == 0) sname += 3;
        wcsncpy_s(normSig, _countof(normSig), sname, _TRUNCATE);
        _wcsupr_s(normSig, _countof(normSig));
        ShellTrap* found = NULL;
        for (ShellTrap* t = g_traps; t; t = t->next) {
            if (_wcsicmp(t->sig, normSig) == 0) { found = t; break; }
        }
        if (!found) {
            found = (ShellTrap*)calloc(1, sizeof(ShellTrap));
            if (found) {
                wcsncpy_s(found->sig, _countof(found->sig), normSig, _TRUNCATE);
                found->next = g_traps;
                g_traps = found;
            }
        }
        if (found) {
            free(found->action);
            found->action = reset ? NULL : SafeStrDup(action);
        }
    }
    Trap_UpdateMask();
    *code = 0;
    return true;
}

void Shell_FreeTraps(void) {
    ShellTrap* trap = g_traps;
    while (trap) {
        ShellTrap* next = trap->next;
        free(trap->action);
        free(trap);
        trap = next;
    }
    g_traps = NULL;
    InterlockedExchange(&g_trapped_signals, 0);
}

bool Builtin_Break(IShell* shell, CommandAST* cmd, int* code) {
    int n = 1;
    if (cmd->argc > 1) {
        if (_wcsicmp(cmd->argv[1], L"/?") == 0 ||
            _wcsicmp(cmd->argv[1], L"on") == 0 ||
            _wcsicmp(cmd->argv[1], L"off") == 0) {
            *code = Batch_ExecuteBreak(cmd->argv[1]);
            return true;
        }
        n = _wtoi(cmd->argv[1]);
        if (n <= 0) {
            fwprintf(stderr, L"ksh: break: %ls: bad number\n", cmd->argv[1]);
            *code = 1;
            return true;
        }
    }
    if (shell) shell->loopBreak = (n > 0 ? n : 1);
    *code = 0;
    return true;
}

bool Builtin_Continue(IShell* shell, CommandAST* cmd, int* code) {
    int n = 1;
    if (cmd->argc > 1) {
        n = _wtoi(cmd->argv[1]);
        if (n <= 0) {
            fwprintf(stderr, L"ksh: continue: %ls: bad number\n", cmd->argv[1]);
            *code = 1;
            return true;
        }
    }
    if (shell) shell->loopContinue = (n > 0 ? n : 1);
    *code = 0;
    return true;
}

