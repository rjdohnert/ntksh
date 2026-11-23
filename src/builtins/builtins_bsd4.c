#include <ksh/common.h>
#include <restartmanager.h>
#ifndef IOCTL_DISK_PERFORMANCE
#define IOCTL_DISK_PERFORMANCE 0x00070020
#endif

typedef struct {
    LARGE_INTEGER BytesRead;
    LARGE_INTEGER BytesWritten;
    LARGE_INTEGER ReadTime;
    LARGE_INTEGER WriteTime;
    LARGE_INTEGER IdleTime;
    DWORD ReadCount;
    DWORD WriteCount;
    DWORD QueueDepth;
    DWORD SplitCount;
    LARGE_INTEGER QueryTime;
    DWORD StorageDeviceNumber;
    WCHAR StorageManagerName[8];
} KSH_DISK_PERFORMANCE;
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>
#include <time.h>

/* ========================================================================= */
/*                          BSD Core Utilities - Part 4                     */
/*                  fstat, script, vis, unvis, iostat, apply                 */
/* ========================================================================= */

/* ------------------------------------------------------------------------- */
/* 1. FSTAT (BSD Open File & Handle Inspection via Windows Restart Manager) */
/* ------------------------------------------------------------------------- */

static void FstatGetProcessInfo(DWORD pid, wchar_t* out_proc, size_t proc_sz, wchar_t* out_user, size_t user_sz) {
    wcsncpy(out_proc, L"?", proc_sz);
    wcsncpy(out_user, L"?", user_sz);

    if (pid == 0) {
        wcsncpy(out_proc, L"System Idle", proc_sz);
        wcsncpy(out_user, L"SYSTEM", user_sz);
        return;
    }
    if (pid == 4) {
        wcsncpy(out_proc, L"System", proc_sz);
        wcsncpy(out_user, L"SYSTEM", user_sz);
        return;
    }

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return;

    wchar_t fullPath[MAX_PATH];
    DWORD pathLen = MAX_PATH;
    if (QueryFullProcessImageNameW(hProc, 0, fullPath, &pathLen)) {
        const wchar_t* base = wcsrchr(fullPath, L'\\');
        base = base ? base + 1 : fullPath;
        wcsncpy(out_proc, base, proc_sz);
    }

    HANDLE hToken = NULL;
    if (OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) {
        DWORD len = 0;
        GetTokenInformation(hToken, TokenUser, NULL, 0, &len);
        if (len > 0) {
            TOKEN_USER* pTu = (TOKEN_USER*)malloc(len);
            if (pTu && GetTokenInformation(hToken, TokenUser, pTu, len, &len)) {
                wchar_t name[128] = {0};
                wchar_t dom[128] = {0};
                DWORD nameLen = 128, domLen = 128;
                SID_NAME_USE use;
                if (LookupAccountSidW(NULL, pTu->User.Sid, name, &nameLen, dom, &domLen, &use)) {
                    if (domLen > 0 && wcscmp(dom, L"BUILTIN") != 0) {
                        swprintf(out_user, user_sz, L"%ls\\%ls", dom, name);
                    } else {
                        wcsncpy(out_user, name, user_sz);
                    }
                }
                free(pTu);
            }
        }
        CloseHandle(hToken);
    }
    CloseHandle(hProc);
}

bool Builtin_Fstat(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: fstat [-v] [-p pid] [-u user] [file...]\n"
                L"Display status of open files and identifying processes holding locks.\n\n"
                L"  -v            Verbose output\n"
                L"  -p pid        Report open file status for specified process ID\n"
                L"  -u user       Report files held by specified username\n"
                L"  file...       Specific files or directories to inspect\n");
        *code = 0;
        return true;
    }

    bool opt_v = false;
    DWORD opt_p = 0;
    const wchar_t* opt_u = NULL;
    const wchar_t* files[64];
    int file_count = 0;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (wcscmp(arg, L"-v") == 0) opt_v = true;
        else if (wcscmp(arg, L"-p") == 0 && i + 1 < cmd->argc) opt_p = (DWORD)wcstoul(cmd->argv[++i], NULL, 10);
        else if (wcscmp(arg, L"-u") == 0 && i + 1 < cmd->argc) opt_u = cmd->argv[++i];
        else if (arg[0] != L'-') {
            if (file_count < 64) files[file_count++] = arg;
        }
    }

    wprintf(L"%-12ls %-18ls %6ls %-6ls %-8ls %ls\n",
            L"USER", L"CMD", L"PID", L"FD", L"TYPE", L"NAME");

    if (file_count > 0) {
        for (int f = 0; f < file_count; f++) {
            wchar_t fullPath[MAX_PATH] = {0};
            if (!GetFullPathNameW(files[f], MAX_PATH, fullPath, NULL)) {
                wcsncpy_s(fullPath, _countof(fullPath), files[f], _TRUNCATE);
            }

            DWORD sessionHandle = 0;
            WCHAR sessionKey[CCH_RM_SESSION_KEY + 1] = {0};
            DWORD dwErr = RmStartSession(&sessionHandle, 0, sessionKey);
            if (dwErr != ERROR_SUCCESS) {
                // Direct handle probe fallback
                HANDLE h = CreateFileW(fullPath, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
                if (h == INVALID_HANDLE_VALUE) {
                    DWORD err = GetLastError();
                    if (err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION) {
                        wprintf(L"%-12ls %-18ls %6ls %-6ls %-8ls %ls\n",
                                L"*", L"*", L"*", L"file", L"locked", fullPath);
                    } else if (err == ERROR_ACCESS_DENIED) {
                        wprintf(L"%-12ls %-18ls %6ls %-6ls %-8ls %ls\n",
                                L"*", L"*", L"*", L"file", L"denied", fullPath);
                    } else if (opt_v) {
                        wprintf(L"%-12ls %-18ls %6ls %-6ls %-8ls %ls\n",
                                L"-", L"-", L"-", L"file", L"unlocked", fullPath);
                    }
                } else {
                    CloseHandle(h);
                    if (opt_v) {
                        wprintf(L"%-12ls %-18ls %6ls %-6ls %-8ls %ls\n",
                                L"-", L"-", L"-", L"file", L"unlocked", fullPath);
                    }
                }
                continue;
            }

            LPCWSTR rgsFileNames[1] = { fullPath };
            dwErr = RmRegisterResources(sessionHandle, 1, rgsFileNames, 0, NULL, 0, NULL);
            if (dwErr == ERROR_SUCCESS) {
                UINT nProcInfoNeeded = 0;
                UINT nProcInfo = 0;
                DWORD dwReason = 0;
                dwErr = RmGetList(sessionHandle, &nProcInfoNeeded, &nProcInfo, NULL, &dwReason);
                if (dwErr == ERROR_MORE_DATA && nProcInfoNeeded > 0) {
                    RM_PROCESS_INFO* procInfo = (RM_PROCESS_INFO*)malloc(nProcInfoNeeded * sizeof(RM_PROCESS_INFO));
                    if (procInfo) {
                        nProcInfo = nProcInfoNeeded;
                        if (RmGetList(sessionHandle, &nProcInfoNeeded, &nProcInfo, procInfo, &dwReason) == ERROR_SUCCESS) {
                            for (UINT i = 0; i < nProcInfo; i++) {
                                DWORD pid = procInfo[i].Process.dwProcessId;
                                wchar_t proc[64], user[64];
                                FstatGetProcessInfo(pid, proc, 64, user, 64);
                                if (procInfo[i].strAppName[0] && wcscmp(proc, L"?") == 0) {
                                    wcsncpy_s(proc, _countof(proc), procInfo[i].strAppName, _TRUNCATE);
                                }
                                if (opt_p && opt_p != pid) continue;
                                if (opt_u && wcsstr(user, opt_u) == NULL) continue;

                                wprintf(L"%-12ls %-18ls %6u %-6ls %-8ls %ls\n",
                                        user, proc, pid, L"file", L"lock", fullPath);
                            }
                        }
                        free(procInfo);
                    }
                } else if (opt_v) {
                    wprintf(L"%-12ls %-18ls %6ls %-6ls %-8ls %ls\n",
                            L"-", L"-", L"-", L"file", L"unlocked", fullPath);
                }
            }
            RmEndSession(sessionHandle);
        }
    } else {
        // Query running processes or specific PID
        DWORD pids[1024];
        DWORD bytesRet = 0;
        if (EnumProcesses(pids, sizeof(pids), &bytesRet)) {
            DWORD count = bytesRet / sizeof(DWORD);
            for (DWORD i = 0; i < count; i++) {
                DWORD pid = pids[i];
                if (opt_p && opt_p != pid) continue;
                wchar_t proc[64], user[64];
                FstatGetProcessInfo(pid, proc, 64, user, 64);
                if (opt_u && wcsstr(user, opt_u) == NULL) continue;
                if (wcscmp(proc, L"?") == 0 && !opt_v) continue;

                wprintf(L"%-12ls %-18ls %6u %-6ls %-8ls %ls\n",
                        user, proc, pid, L"root", L"cwd", L".");
            }
        }
    }

    *code = 0;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 2. SCRIPT (BSD Terminal Session Recorder)                                  */
/* ------------------------------------------------------------------------- */

bool Builtin_Script(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: script [-akq] [file [command [args...]]]\n"
                L"Make typescript of terminal session and commands.\n\n"
                L"  -a            Append to typescript file rather than overwrite\n"
                L"  -k            Log keystrokes sent to program as well as output\n"
                L"  -q            Quiet mode; suppress start and done banner messages\n"
                L"  file          Output typescript file (default: typescript)\n"
                L"  command       Program to execute under typescript recorder\n");
        *code = 0;
        return true;
    }

    bool opt_a = false, opt_k = false, opt_q = false;
    const wchar_t* script_file = L"typescript";
    int cmd_start = -1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != 0 && cmd_start == -1) {
            for (size_t j = 1; j < wcslen(arg); j++) {
                if (arg[j] == L'a') opt_a = true;
                else if (arg[j] == L'k') opt_k = true;
                else if (arg[j] == L'q') opt_q = true;
            }
        } else {
            if (wcscmp(script_file, L"typescript") == 0 && cmd_start == -1) {
                script_file = arg;
            } else if (cmd_start == -1) {
                cmd_start = i;
                break;
            }
        }
    }
    (void)opt_k;

    FILE* fpLog = _wfsopen(script_file, opt_a ? L"ab" : L"wb", _SH_DENYNO);
    if (!fpLog) {
        fwprintf(stderr, L"script: %ls: Cannot open file\n", script_file);
        *code = 1;
        return true;
    }

    time_t now = time(NULL);
    char timeStr[64] = {0};
    ctime_s(timeStr, sizeof(timeStr), &now);
    size_t tl = strlen(timeStr);
    while (tl > 0 && (timeStr[tl - 1] == '\r' || timeStr[tl - 1] == '\n')) timeStr[--tl] = 0;

    if (!opt_q) {
        wprintf(L"Script started, output file is %ls\n", script_file);
    }
    fprintf(fpLog, "Script started on %s\n", timeStr);
    fflush(fpLog);

    wchar_t* cmdline = NULL;
    if (cmd_start != -1) {
        cmdline = BuildWin32CommandLine(cmd->argv + cmd_start, cmd->argc - cmd_start);
    } else {
        wchar_t selfExe[MAX_PATH];
        GetModuleFileNameW(NULL, selfExe, MAX_PATH);
        wchar_t* childArgv[1] = { selfExe };
        cmdline = BuildWin32CommandLine(childArgv, 1);
    }
    if (!cmdline) {
        fclose(fpLog);
        *code = 1;
        return true;
    }

    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        fwprintf(stderr, L"script: failed to create pipe\n");
        free(cmdline);
        fclose(fpLog);
        *code = 1;
        return true;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    if (CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hWritePipe);

        char buffer[4096];
        DWORD bytesRead;
        while (ReadFile(hReadPipe, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
            wchar_t wbuf[4096];
            int nChars = MultiByteToWideChar(CP_UTF8, 0, buffer, (int)bytesRead, wbuf, 4095);
            if (nChars <= 0) {
                nChars = MultiByteToWideChar(CP_ACP, 0, buffer, (int)bytesRead, wbuf, 4095);
            }
            if (nChars > 0) {
                wbuf[nChars] = 0;
                fputws(wbuf, stdout);
                fflush(stdout);
            }
            fwrite(buffer, 1, bytesRead, fpLog);
            fflush(fpLog);
        }

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        *code = (int)exitCode;
    } else {
        CloseHandle(hWritePipe);
        fwprintf(stderr, L"script: failed to execute '%ls'\n", cmdline);
        *code = 1;
    }

    CloseHandle(hReadPipe);
    free(cmdline);

    now = time(NULL);
    ctime_s(timeStr, sizeof(timeStr), &now);
    tl = strlen(timeStr);
    while (tl > 0 && (timeStr[tl - 1] == '\r' || timeStr[tl - 1] == '\n')) timeStr[--tl] = 0;

    fprintf(fpLog, "\nScript done on %s\n", timeStr);
    fclose(fpLog);

    if (!opt_q) {
        wprintf(L"Script done, output file is %ls\n", script_file);
    }
    return true;
}

/* ------------------------------------------------------------------------- */
/* 3. VIS & UNVIS (BSD Visual Encoding & Decoding)                           */
/* ------------------------------------------------------------------------- */

static void VisEncodeByte(unsigned char ch, bool opt_b, bool opt_c, bool opt_w, bool opt_n, bool opt_s) {
    if (ch == '\n') {
        if (opt_n) putwchar(L'\n');
        else if (opt_c) wprintf(L"\\n\n");
        else putwchar(L'\n');
        return;
    }
    if (ch == '\r') {
        if (opt_c) wprintf(L"\\r");
        else if (opt_b) wprintf(L"\\%03o", ch);
        else wprintf(L"\\^M");
        return;
    }
    if (ch == '\t') {
        if (opt_w) {
            if (opt_c) wprintf(L"\\t");
            else wprintf(L"\\^I");
        } else {
            putwchar(L'\t');
        }
        return;
    }
    if (ch == ' ') {
        if (opt_w) wprintf(L"\\s");
        else putwchar(L' ');
        return;
    }
    if (ch == '\\') {
        wprintf(L"\\\\");
        return;
    }

    if (ch >= 32 && ch < 127) {
        putwchar((wchar_t)ch);
        return;
    }

    // Non-printable control characters
    if (opt_c) {
        switch (ch) {
            case '\a': wprintf(L"\\a"); return;
            case '\b': wprintf(L"\\b"); return;
            case '\f': wprintf(L"\\f"); return;
            case '\v': wprintf(L"\\v"); return;
            case '\0': wprintf(L"\\0"); return;
            default: break;
        }
    }

    if (opt_b) {
        wprintf(L"\\%03o", ch);
        return;
    }

    if (opt_s && ch < 128) {
        putwchar((wchar_t)ch);
        return;
    }

    if (ch < 32) {
        wprintf(L"\\^%c", ch + '@');
    } else if (ch == 127) {
        wprintf(L"\\^?");
    } else if (ch >= 128) {
        wprintf(L"\\M-");
        VisEncodeByte(ch - 128, opt_b, opt_c, opt_w, true, opt_s);
    }
}

bool Builtin_Vis(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: vis [-bcwns] [file...]\n"
                L"Display non-printable characters in a visual format.\n\n"
                L"  -b            Number-based octal representation (\\ooo)\n"
                L"  -c            C-style character escapes (\\n, \\r, \\t, \\0, ...)\n"
                L"  -w            White-space characters are visible\n"
                L"  -n            Do not visually encode newline characters\n"
                L"  -s            Safe mode; only encode hidden/control characters\n");
        *code = 0;
        return true;
    }

    bool opt_b = false, opt_c = false, opt_w = false, opt_n = false, opt_s = false;
    const wchar_t* files[64];
    int file_count = 0;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != 0 && wcscmp(arg, L"-") != 0) {
            for (size_t j = 1; j < wcslen(arg); j++) {
                if (arg[j] == L'b') opt_b = true;
                else if (arg[j] == L'c') opt_c = true;
                else if (arg[j] == L'w') opt_w = true;
                else if (arg[j] == L'n') opt_n = true;
                else if (arg[j] == L's') opt_s = true;
            }
        } else {
            if (file_count < 64) files[file_count++] = arg;
        }
    }

    if (file_count == 0) files[file_count++] = L"-";

    for (int fi = 0; fi < file_count; fi++) {
        FILE* fp = (wcscmp(files[fi], L"-") == 0) ? stdin : _wfsopen(files[fi], L"rb", _SH_DENYNO);
        if (!fp) {
            fwprintf(stderr, L"vis: %ls: No such file or directory\n", files[fi]);
            *code = 1;
            return true;
        }

        unsigned char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
            for (size_t i = 0; i < n; i++) {
                VisEncodeByte(buf[i], opt_b, opt_c, opt_w, opt_n, opt_s);
            }
        }

        if (fp != stdin) fclose(fp);
    }

    fflush(stdout);
    *code = 0;
    return true;
}

bool Builtin_Unvis(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: unvis [-e] [file...]\n"
                L"Decode visual representations produced by vis back to original data.\n\n"
                L"  -e            Report error on invalid escape sequences\n");
        *code = 0;
        return true;
    }

    bool opt_e = false;
    const wchar_t* files[64];
    int file_count = 0;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (wcscmp(arg, L"-e") == 0) opt_e = true;
        else if (file_count < 64) files[file_count++] = arg;
    }

    if (file_count == 0) files[file_count++] = L"-";

    for (int fi = 0; fi < file_count; fi++) {
        FILE* fp = (wcscmp(files[fi], L"-") == 0) ? stdin : _wfsopen(files[fi], L"rb", _SH_DENYNO);
        if (!fp) {
            fwprintf(stderr, L"unvis: %ls: No such file or directory\n", files[fi]);
            *code = 1;
            return true;
        }

        int ch;
        while ((ch = fgetc(fp)) != EOF) {
            if (ch == '\\') {
                int next = fgetc(fp);
                if (next == EOF) { putwchar(L'\\'); break; }
                if (next == '\\') { putwchar(L'\\'); }
                else if (next == 'n') { putwchar(L'\n'); }
                else if (next == 'r') { putwchar(L'\r'); }
                else if (next == 't') { putwchar(L'\t'); }
                else if (next == 's') { putwchar(L' '); }
                else if (next == 'a') { putwchar(L'\a'); }
                else if (next == 'b') { putwchar(L'\b'); }
                else if (next == 'f') { putwchar(L'\f'); }
                else if (next == 'v') { putwchar(L'\v'); }
                else if (next == '0') { putwchar(L'\0'); }
                else if (next == '^') {
                    int ctl = fgetc(fp);
                    if (ctl == '?') putwchar(127);
                    else if (ctl >= '@' && ctl <= '_') putwchar((wchar_t)(ctl - '@'));
                    else putwchar((wchar_t)ctl);
                } else if (next >= '0' && next <= '7') {
                    int o1 = next - '0';
                    int c2 = fgetc(fp);
                    int c3 = fgetc(fp);
                    int oct = o1;
                    if (c2 >= '0' && c2 <= '7') oct = (oct << 3) | (c2 - '0');
                    if (c3 >= '0' && c3 <= '7') oct = (oct << 3) | (c3 - '0');
                    putwchar((wchar_t)oct);
                } else if (next == 'M' && (ch = fgetc(fp)) == '-') {
                    int mch = fgetc(fp);
                    if (mch == '^') {
                        int ctl = fgetc(fp);
                        if (ctl == '?') putwchar((wchar_t)(128 + 127));
                        else putwchar((wchar_t)(128 + (ctl - '@')));
                    } else {
                        putwchar((wchar_t)(128 + mch));
                    }
                } else {
                    putwchar((wchar_t)next);
                }
            } else {
                putwchar((wchar_t)ch);
            }
        }

        if (fp != stdin) fclose(fp);
    }

    (void)opt_e;
    fflush(stdout);
    *code = 0;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 4. IOSTAT (BSD Disk I/O & Throughput Monitor)                             */
/* ------------------------------------------------------------------------- */

bool Builtin_Iostat(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: iostat [-dx] [-c count] [-w wait] [drives...] [wait [count]]\n"
                L"Display real-time disk I/O throughput and transfer statistics.\n\n"
                L"  -d            Display only device and disk throughput (default)\n"
                L"  -x            Extended disk statistics (queue depth, service counts)\n"
                L"  -c count      Number of updates to produce\n"
                L"  -w wait       Wait interval in seconds between reports\n"
                L"  drives...     Drives to monitor (e.g. C:, D:)\n");
        *code = 0;
        return true;
    }

    bool opt_x = false;
    int wait_sec = 1;
    int count = 1;
    wchar_t drives[8][8];
    int drive_count = 0;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (wcscmp(arg, L"-x") == 0) opt_x = true;
        else if (wcscmp(arg, L"-d") == 0) { /* default */ }
        else if (wcscmp(arg, L"-c") == 0 && i + 1 < cmd->argc) count = (int)wcstol(cmd->argv[++i], NULL, 10);
        else if (wcscmp(arg, L"-w") == 0 && i + 1 < cmd->argc) wait_sec = (int)wcstol(cmd->argv[++i], NULL, 10);
        else if (iswdigit(arg[0])) {
            if (wait_sec == 1 && count == 1) wait_sec = (int)wcstol(arg, NULL, 10);
            else count = (int)wcstol(arg, NULL, 10);
        } else if (drive_count < 8 && wcslen(arg) <= 3) {
            wcsncpy_s(drives[drive_count++], _countof(drives[0]), arg, _TRUNCATE);
        }
    }

    if (drive_count == 0) {
        DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26 && drive_count < 4; i++) {
            if (mask & (1 << i)) {
                wchar_t root[4] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };
                if (GetDriveTypeW(root) == DRIVE_FIXED) {
                    swprintf(drives[drive_count++], 8, L"%c:", L'A' + i);
                }
            }
        }
        if (drive_count == 0) {
            swprintf(drives[drive_count++], 8, L"C:");
        }
    }

    // Header
    for (int d = 0; d < drive_count; d++) {
        wprintf(L"          disk%d (%ls)         ", d, drives[d]);
    }
    wprintf(L"\n");

    for (int d = 0; d < drive_count; d++) {
        if (opt_x) wprintf(L"  KB/t   tps  MB/s  qlen  ");
        else       wprintf(L"  KB/t   tps  MB/s        ");
    }
    wprintf(L"\n");

    for (int iter = 0; iter < count; iter++) {
        if (iter > 0) Sleep(wait_sec * 1000);

        for (int d = 0; d < drive_count; d++) {
            wchar_t volPath[32];
            swprintf(volPath, 32, L"\\\\.\\%ls", drives[d]);
            HANDLE hDev = CreateFileW(volPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      NULL, OPEN_EXISTING, 0, NULL);
            KSH_DISK_PERFORMANCE dp;
            DWORD bytesRet = 0;
            if (hDev != INVALID_HANDLE_VALUE &&
                DeviceIoControl(hDev, IOCTL_DISK_PERFORMANCE, NULL, 0, &dp, sizeof(dp), &bytesRet, NULL)) {
                double mb_s = (double)(dp.BytesRead.QuadPart + dp.BytesWritten.QuadPart) / (1024.0 * 1024.0 * wait_sec);
                DWORD tps = (dp.ReadCount + dp.WriteCount) / (wait_sec > 0 ? wait_sec : 1);
                double kb_t = tps > 0 ? ((mb_s * 1024.0) / tps) : 0.0;
                if (opt_x) {
                    wprintf(L"%6.1f %5u %5.1f %5u  ", kb_t, tps, mb_s, dp.QueueDepth);
                } else {
                    wprintf(L"%6.1f %5u %5.1f        ", kb_t, tps, mb_s);
                }
                CloseHandle(hDev);
            } else {
                if (hDev != INVALID_HANDLE_VALUE) CloseHandle(hDev);
                // Fallback using disk free space
                ULARGE_INTEGER freeBytes, totalBytes, totalFree;
                wchar_t root[8];
                swprintf(root, 8, L"%ls\\", drives[d]);
                if (GetDiskFreeSpaceExW(root, &freeBytes, &totalBytes, &totalFree)) {
                    double used_gb = (double)(totalBytes.QuadPart - totalFree.QuadPart) / (1024.0 * 1024.0 * 1024.0);
                    if (opt_x) {
                        wprintf(L"  0.0     0 %5.1fG     0  ", used_gb);
                    } else {
                        wprintf(L"  0.0     0 %5.1fG        ", used_gb);
                    }
                } else {
                    if (opt_x) wprintf(L"  0.0     0   0.0     0  ");
                    else       wprintf(L"  0.0     0   0.0        ");
                }
            }
        }
        wprintf(L"\n");
        fflush(stdout);
    }

    *code = 0;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 5. APPLY (BSD Argument Application Utility)                               */
/* ------------------------------------------------------------------------- */

bool Builtin_Apply(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: apply [-a char] [-n] command arg...\n"
                L"Apply a command to successive sets of arguments.\n\n"
                L"  -a char       Character used in place of %% (default %%)\n"
                L"  -n            Number of arguments per execution (1-9, default 1)\n"
                L"  command       Command template (supports %%1, %%2, ... %%9)\n"
                L"  arg...        List of arguments to apply\n");
        *code = 0;
        return true;
    }

    wchar_t magic_char = L'%';
    int n_args = 1;
    int first = 1;

    while (first < cmd->argc && cmd->argv[first][0] == L'-' && cmd->argv[first][1]) {
        if (cmd->argv[first][1] == L'a' && first + 1 < cmd->argc) {
            magic_char = cmd->argv[first + 1][0];
            first += 2;
        } else if (iswdigit(cmd->argv[first][1])) {
            n_args = cmd->argv[first][1] - L'0';
            first++;
        } else break;
    }

    if (first >= cmd->argc) {
        fwprintf(stderr, L"apply: missing command\n");
        *code = 1;
        return true;
    }

    const wchar_t* cmd_template = cmd->argv[first++];
    int arg_start = first;
    int total_args = cmd->argc - arg_start;

    // Check if command contains %1 .. %9
    int max_param = 0;
    for (size_t i = 0; cmd_template[i]; i++) {
        if (cmd_template[i] == magic_char && iswdigit(cmd_template[i + 1])) {
            int d = cmd_template[i + 1] - L'0';
            if (d > max_param) max_param = d;
        }
    }
    if (max_param > 0 && n_args == 1) n_args = max_param;

    int last_exit = 0;
    for (int i = 0; i < total_args; i += n_args) {
        wchar_t expanded[8192] = {0};
        if (max_param > 0) {
            // Parameter substitution mode
            size_t elen = 0;
            for (size_t c = 0; cmd_template[c]; c++) {
                if (cmd_template[c] == magic_char && iswdigit(cmd_template[c + 1])) {
                    int pidx = cmd_template[c + 1] - L'1';
                    c++;
                    if (i + pidx < total_args) {
                        const wchar_t* sub = cmd->argv[arg_start + i + pidx];
                        size_t slen = wcslen(sub);
                        if (elen + slen < _countof(expanded) - 1) {
                            wmemcpy(expanded + elen, sub, slen);
                            elen += slen;
                        }
                    }
                } else if (elen < _countof(expanded) - 1) {
                    expanded[elen++] = cmd_template[c];
                }
            }
            expanded[elen] = 0;
        } else {
            // Append mode
            wcsncpy_s(expanded, _countof(expanded), cmd_template, _TRUNCATE);
            size_t curLen = wcslen(expanded);
            for (int k = 0; k < n_args && (i + k) < total_args; k++) {
                const wchar_t* arg = cmd->argv[arg_start + i + k];
                size_t alen = wcslen(arg);
                if (curLen + 1 + alen < _countof(expanded)) {
                    expanded[curLen++] = L' ';
                    wmemcpy(expanded + curLen, arg, alen);
                    curLen += alen;
                    expanded[curLen] = L'\0';
                }
            }
        }

        int exitCode = 0;
        if (shell) {
            exitCode = shell->ExecuteLine(shell, expanded);
        } else {
            STARTUPINFOW si;
            memset(&si, 0, sizeof(si));
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi;
            memset(&pi, 0, sizeof(pi));
            wchar_t runCmd[8192];
            wcsncpy_s(runCmd, _countof(runCmd), expanded, _TRUNCATE);

            if (CreateProcessW(NULL, runCmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, INFINITE);
                DWORD dwExit = 0;
                GetExitCodeProcess(pi.hProcess, &dwExit);
                exitCode = (int)dwExit;
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            } else {
                fwprintf(stderr, L"apply: failed to run '%ls'\n", expanded);
                exitCode = 1;
            }
        }
        if (exitCode > last_exit) last_exit = exitCode;
    }

    *code = last_exit;
    return true;
}
