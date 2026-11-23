#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

/* ========================================================================= */
/*               Category 2: Native Windows Process & System Built-ins        */
/* ========================================================================= */

bool Builtin_Tasklist(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: tasklist [/FO format] [/NH] [/FI filter]\n"
                L"Displays a list of currently running processes.\n\n"
                L"  /FO format    Specifies the output format: TABLE, LIST, CSV\n"
                L"  /NH           Specifies that the Column Header should not be displayed\n"
                L"  /FI filter    Filter running tasks (e.g. \"IMAGENAME eq notepad.exe\")\n");
        *code = 0;
        return true;
    }

    enum { FO_TABLE, FO_CSV, FO_LIST } format = FO_TABLE;
    bool no_header = false;
    const wchar_t* filter_name = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (_wcsicmp(arg, L"/NH") == 0) no_header = true;
        else if (_wcsicmp(arg, L"/FO") == 0 && i + 1 < cmd->argc) {
            i++;
            if (_wcsicmp(cmd->argv[i], L"CSV") == 0) format = FO_CSV;
            else if (_wcsicmp(cmd->argv[i], L"LIST") == 0) format = FO_LIST;
            else format = FO_TABLE;
        } else if (_wcsicmp(arg, L"/FI") == 0 && i + 1 < cmd->argc) {
            const wchar_t* f = cmd->argv[++i];
            const wchar_t* eq = wcsstr(f, L"eq ");
            if (eq) filter_name = eq + 3;
            else filter_name = f;
        }
    }

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        *code = 1;
        return true;
    }

    if (!no_header) {
        if (format == FO_TABLE) {
            wprintf(L"Image Name                     PID Session Name        Session#    Mem Usage\n");
            wprintf(L"========================= ======== ================ =========== ============\n");
        } else if (format == FO_CSV) {
            wprintf(L"\"Image Name\",\"PID\",\"Session Name\",\"Session#\",\"Mem Usage\"\n");
        }
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (filter_name && _wcsicmp(pe.szExeFile, filter_name) != 0) {
                continue;
            }

            DWORD mem_kb = 0;
            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (hProc) {
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(hProc, &pmc, sizeof(pmc))) {
                    mem_kb = (DWORD)(pmc.WorkingSetSize / 1024);
                }
                CloseHandle(hProc);
            }

            DWORD session_id = 1;
            ProcessIdToSessionId(pe.th32ProcessID, &session_id);

            if (format == FO_TABLE) {
                wprintf(L"%-25.25ls %8lu %-16ls %11lu %10lu K\n",
                        pe.szExeFile, pe.th32ProcessID, L"Console", session_id, mem_kb);
            } else if (format == FO_CSV) {
                wprintf(L"\"%ls\",\"%lu\",\"Console\",\"%lu\",\"%lu K\"\n",
                        pe.szExeFile, pe.th32ProcessID, session_id, mem_kb);
            } else {
                wprintf(L"Image Name:   %ls\nPID:          %lu\nSession Name: Console\nSession#:     %lu\nMem Usage:    %lu K\n\n",
                        pe.szExeFile, pe.th32ProcessID, session_id, mem_kb);
            }
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);
    *code = 0;
    return true;
}

static void KillProcessTreeRecursive(DWORD pid) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ParentProcessID == pid && pe.th32ProcessID != pid) {
                    KillProcessTreeRecursive(pe.th32ProcessID);
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProc) {
        TerminateProcess(hProc, 1);
        CloseHandle(hProc);
    }
}

bool Builtin_Taskkill(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: taskkill [/PID processid | /IM imagename] [/F] [/T]\n"
                L"Terminate tasks by process id or image name.\n\n"
                L"  /PID  processid  Specifies the PID of the process to be terminated\n"
                L"  /IM   imagename  Specifies the image name of the process to be terminated\n"
                L"  /F               Forcefully terminate the process(es)\n"
                L"  /T               Terminates the specified process and any child processes\n");
        *code = 0;
        return true;
    }

    DWORD target_pid = 0;
    const wchar_t* target_im = NULL;
    bool force = false;
    bool tree = false;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (_wcsicmp(arg, L"/F") == 0) force = true;
        else if (_wcsicmp(arg, L"/T") == 0) tree = true;
        else if (_wcsicmp(arg, L"/PID") == 0 && i + 1 < cmd->argc) {
            target_pid = (DWORD)wcstoul(cmd->argv[++i], NULL, 10);
        } else if (_wcsicmp(arg, L"/IM") == 0 && i + 1 < cmd->argc) {
            target_im = cmd->argv[++i];
        }
    }

    if (target_pid == 0 && !target_im) {
        fwprintf(stderr, L"taskkill: must specify /PID or /IM\n");
        *code = 1;
        return true;
    }
    (void)force;

    int ret = 0;
    if (target_pid != 0) {
        if (tree) {
            KillProcessTreeRecursive(target_pid);
            wprintf(L"SUCCESS: The process with PID %lu and child processes have been terminated.\n", target_pid);
        } else {
            HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, target_pid);
            if (hProc && TerminateProcess(hProc, 1)) {
                wprintf(L"SUCCESS: The process with PID %lu has been terminated.\n", target_pid);
                CloseHandle(hProc);
            } else {
                fwprintf(stderr, L"ERROR: The process with PID %lu could not be terminated.\n", target_pid);
                ret = 1;
            }
        }
    } else if (target_im) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(pe);
            int killed = 0;
            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, target_im) == 0) {
                        if (tree) {
                            KillProcessTreeRecursive(pe.th32ProcessID);
                        } else {
                            HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                            if (hProc) {
                                TerminateProcess(hProc, 1);
                                CloseHandle(hProc);
                            }
                        }
                        wprintf(L"SUCCESS: The process \"%ls\" with PID %lu has been terminated.\n", pe.szExeFile, pe.th32ProcessID);
                        killed++;
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
            if (killed == 0) {
                fwprintf(stderr, L"ERROR: The process \"%ls\" not found.\n", target_im);
                ret = 1;
            }
        }
    }

    *code = ret;
    return true;
}

bool Builtin_Mode(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: mode [CON[:]] [COLS=c] [LINES=n]\n"
                L"Configures system devices or queries console display modes.\n");
        *code = 0;
        return true;
    }

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hOut, &csbi)) {
        wprintf(L"Status for device CON:\n-------------------\n    Lines:          25\n    Columns:        80\n    Code page:      %u\n", GetConsoleOutputCP());
        *code = 0;
        return true;
    }

    int new_cols = 0;
    int new_lines = 0;
    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (_wcsnicmp(arg, L"cols=", 5) == 0) new_cols = (int)wcstol(arg + 5, NULL, 10);
        else if (_wcsnicmp(arg, L"lines=", 6) == 0) new_lines = (int)wcstol(arg + 6, NULL, 10);
    }

    if (new_cols > 0 || new_lines > 0) {
        SHORT cols = new_cols > 0 ? (SHORT)new_cols : csbi.dwSize.X;
        SHORT lines = new_lines > 0 ? (SHORT)new_lines : csbi.dwSize.Y;
        COORD size = { cols, lines };
        SetConsoleScreenBufferSize(hOut, size);
        SMALL_RECT r = { 0, 0, cols - 1, lines - 1 };
        SetConsoleWindowInfo(hOut, TRUE, &r);
    } else {
        wprintf(L"Status for device CON:\n"
                L"-------------------\n"
                L"    Lines:          %d\n"
                L"    Columns:        %d\n"
                L"    Code page:      %u\n",
                csbi.srWindow.Bottom - csbi.srWindow.Top + 1,
                csbi.srWindow.Right - csbi.srWindow.Left + 1,
                GetConsoleOutputCP());
    }

    *code = 0;
    return true;
}

bool Builtin_More(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: more [/C] [/P] [/S] [/T[n]] [+n] [FILE...]\n"
                L"Displays output one screen at a time.\n\n"
                L"  /C            Clear screen before displaying page\n"
                L"  /P            Expand FormFeed characters\n"
                L"  /S            Squeeze multiple blank lines into a single blank line\n"
                L"  /T[n]         Expand tabs to n spaces (default 8)\n"
                L"  +n            Start displaying the first file at line n\n");
        *code = 0;
        return true;
    }

    bool clear_scr = false;
    bool squeeze = false;
    int tabstop = 8;
    int start_line = 1;
    int file_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (_wcsicmp(arg, L"/C") == 0) clear_scr = true;
        else if (_wcsicmp(arg, L"/S") == 0) squeeze = true;
        else if (_wcsnicmp(arg, L"/T", 2) == 0) {
            if (arg[2] != L'\0') tabstop = (int)wcstol(arg + 2, NULL, 10);
            else if (i + 1 < cmd->argc) tabstop = (int)wcstol(cmd->argv[++i], NULL, 10);
        } else if (arg[0] == L'+' && iswdigit(arg[1])) {
            start_line = (int)wcstol(arg + 1, NULL, 10);
        } else if (arg[0] != L'/') {
            file_start = i;
            break;
        }
        file_start = i + 1;
    }

    int files_count = cmd->argc - file_start;
    const wchar_t* dummy_stdin = L"-";
    const wchar_t* const* files = (files_count > 0) ? (const wchar_t* const*)(cmd->argv + file_start) : &dummy_stdin;
    if (files_count <= 0) files_count = 1;
    (void)tabstop;

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    bool is_console = GetConsoleScreenBufferInfo(hOut, &csbi);
    int page_height = is_console ? (csbi.srWindow.Bottom - csbi.srWindow.Top) : 24;
    if (page_height < 5) page_height = 24;

    wchar_t line[4096];
    for (int fi = 0; fi < files_count; fi++) {
        FILE* fp = wcscmp(files[fi], L"-") == 0 ? stdin : _wfsopen(files[fi], L"r", _SH_DENYNO);
        if (!fp) continue;

        if (clear_scr && is_console) {
            COORD coord = { 0, 0 };
            DWORD written = 0;
            DWORD bufSize = (DWORD)csbi.dwSize.X * (DWORD)csbi.dwSize.Y;
            FillConsoleOutputCharacterW(hOut, L' ', bufSize, coord, &written);
            FillConsoleOutputAttribute(hOut, csbi.wAttributes, bufSize, coord, &written);
            SetConsoleCursorPosition(hOut, coord);
        }

        int cur_line = 0;
        int lines_displayed = 0;
        bool last_was_blank = false;

        while (fgetws(line, sizeof(line) / sizeof(line[0]), fp)) {
            cur_line++;
            if (cur_line < start_line) continue;

            size_t len = wcslen(line);
            while (len > 0 && (line[len - 1] == L'\r' || line[len - 1] == L'\n')) line[--len] = L'\0';

            bool is_blank = (len == 0);
            if (squeeze && is_blank && last_was_blank) continue;
            last_was_blank = is_blank;

            wprintf(L"%ls\n", line);
            lines_displayed++;

            if (is_console && lines_displayed >= page_height) {
                wprintf(L"-- More --");
                int ch = _getch();
                wprintf(L"\r          \r");
                if (ch == 'q' || ch == 'Q') break;
                else if (ch == '\r' || ch == '\n') lines_displayed = page_height - 1;
                else lines_displayed = 0;
            }
        }
        if (fp != stdin) fclose(fp);
    }

    *code = 0;
    return true;
}

bool Builtin_Shutdown(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: shutdown [/s | /r | /l | /a] [/f] [/t xxx] [/c \"comment\"]\n"
                L"Shutdown, restart, or log off the current Windows machine.\n\n"
                L"  /s            Shutdown the computer\n"
                L"  /r            Restart the computer\n"
                L"  /l            Log off\n"
                L"  /a            Abort a system shutdown\n"
                L"  /t xxx        Set the time-out period before shutdown to xxx seconds\n"
                L"  /c \"comment\"  Comment on the reason for the restart or shutdown\n"
                L"  /f            Force running applications to close without warning\n");
        *code = 0;
        return true;
    }

    bool do_shutdown = false;
    bool do_restart = false;
    bool do_logoff = false;
    bool do_abort = false;
    bool force = false;
    DWORD timeout = 30;
    const wchar_t* comment = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (_wcsicmp(arg, L"/s") == 0) do_shutdown = true;
        else if (_wcsicmp(arg, L"/r") == 0) do_restart = true;
        else if (_wcsicmp(arg, L"/l") == 0) do_logoff = true;
        else if (_wcsicmp(arg, L"/a") == 0) do_abort = true;
        else if (_wcsicmp(arg, L"/f") == 0) force = true;
        else if (_wcsicmp(arg, L"/t") == 0 && i + 1 < cmd->argc) timeout = (DWORD)wcstoul(cmd->argv[++i], NULL, 10);
        else if (_wcsicmp(arg, L"/c") == 0 && i + 1 < cmd->argc) comment = cmd->argv[++i];
    }

    if (!do_shutdown && !do_restart && !do_logoff && !do_abort) {
        wprintf(L"shutdown: must specify /s, /r, /l, or /a\n");
        *code = 1;
        return true;
    }

    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid);
        AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
        CloseHandle(hToken);
    }

    if (do_abort) {
        if (AbortSystemShutdownW(NULL)) {
            wprintf(L"The shutdown was aborted.\n");
            *code = 0;
        } else {
            fwprintf(stderr, L"Unable to abort shutdown.\n");
            *code = 1;
        }
        return true;
    }

    if (do_logoff) {
        ExitWindowsEx(EWX_LOGOFF | (force ? EWX_FORCE : 0), 0);
        *code = 0;
        return true;
    }

    if (InitiateSystemShutdownExW(NULL, (wchar_t*)comment, timeout, force, do_restart, SHTDN_REASON_FLAG_PLANNED)) {
        *code = 0;
    } else {
        fwprintf(stderr, L"Failed to initiate %ls.\n", do_restart ? L"restart" : L"shutdown");
        *code = 1;
    }

    return true;
}

bool Builtin_Systeminfo(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: systeminfo [/FO format] [/NH]\n"
                L"Displays detailed configuration information about a computer and its operating system.\n\n"
                L"  /FO format    Specifies the format in which the output is displayed (TABLE, LIST, CSV)\n"
                L"  /NH           Specifies that the \"Column Header\" should not be displayed\n");
        *code = 0;
        return true;
    }

    wchar_t hostname[MAX_COMPUTERNAME_LENGTH + 1] = L"";
    DWORD hlen = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(hostname, &hlen);

    typedef LONG (WINAPI *RtlGetVersionPtr)(OSVERSIONINFOEXW*);
    OSVERSIONINFOEXW vi = { 0 };
    vi.dwOSVersionInfoSize = sizeof(vi);
    HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
    if (hNt) {
        RtlGetVersionPtr pRtl = (RtlGetVersionPtr)GetProcAddress(hNt, "RtlGetVersion");
        if (pRtl) pRtl(&vi);
    }

    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    const wchar_t* arch = L"x64-based PC";
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) arch = L"ARM64-based PC";
    else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) arch = L"x86-based PC";

    MEMORYSTATUSEX ms = { 0 };
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);

    wchar_t win_dir[MAX_PATH] = L"";
    GetWindowsDirectoryW(win_dir, MAX_PATH);

    wchar_t sys_dir[MAX_PATH] = L"";
    GetSystemDirectoryW(sys_dir, MAX_PATH);

    TIME_ZONE_INFORMATION tzi;
    GetTimeZoneInformation(&tzi);

    ULONGLONG uptime_ms = GetTickCount64();
    ULONGLONG uptime_sec = uptime_ms / 1000;
    ULONGLONG days = uptime_sec / 86400;
    ULONGLONG hours = (uptime_sec % 86400) / 3600;
    ULONGLONG minutes = (uptime_sec % 3600) / 60;
    ULONGLONG seconds = uptime_sec % 60;

    wprintf(L"Host Name:                 %ls\n", hostname);
    wprintf(L"OS Name:                   Microsoft Windows NT\n");
    wprintf(L"OS Version:                %lu.%lu.%lu N/A Build %lu\n", vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber, vi.dwBuildNumber);
    wprintf(L"OS Manufacturer:           Microsoft Corporation\n");
    wprintf(L"System Type:               %ls\n", arch);
    wprintf(L"System Boot Time:          Up %llu days, %llu hours, %llu minutes, %llu seconds\n", days, hours, minutes, seconds);
    wprintf(L"Windows Directory:         %ls\n", win_dir);
    wprintf(L"System Directory:          %ls\n", sys_dir);
    wprintf(L"Time Zone:                 %ls\n", tzi.StandardName);
    wprintf(L"Total Physical Memory:     %llu MB\n", ms.ullTotalPhys / (1024 * 1024));
    wprintf(L"Available Physical Memory: %llu MB\n", ms.ullAvailPhys / (1024 * 1024));
    wprintf(L"Virtual Memory: Max Size:  %llu MB\n", ms.ullTotalVirtual / (1024 * 1024));
    wprintf(L"Virtual Memory: Available: %llu MB\n", ms.ullAvailVirtual / (1024 * 1024));

    *code = 0;
    return true;
}

bool Builtin_Subst(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: subst [drive1: [drive2:]path]\n"
                L"       subst drive1: /D\n\n"
                L"Associates a path with a drive letter, or displays current substitutions.\n");
        *code = 0;
        return true;
    }

    if (cmd->argc == 1) {
        wchar_t target[MAX_PATH];
        wchar_t drive[3] = L"A:";
        for (wchar_t c = L'A'; c <= L'Z'; c++) {
            drive[0] = c;
            if (QueryDosDeviceW(drive, target, MAX_PATH)) {
                if (wcsncmp(target, L"\\??\\", 4) == 0) {
                    wprintf(L"%ls\\: => %ls\n", drive, target + 4);
                }
            }
        }
        *code = 0;
        return true;
    }

    const wchar_t* drv = cmd->argv[1];
    if (cmd->argc > 2 && _wcsicmp(cmd->argv[2], L"/D") == 0) {
        if (DefineDosDeviceW(DDD_REMOVE_DEFINITION, drv, NULL)) {
            *code = 0;
        } else {
            fwprintf(stderr, L"subst: failed to remove drive %ls\n", drv);
            *code = 1;
        }
        return true;
    }

    if (cmd->argc > 2) {
        const wchar_t* path = cmd->argv[2];
        wchar_t full_path[MAX_PATH];
        GetFullPathNameW(path, MAX_PATH, full_path, NULL);
        if (DefineDosDeviceW(0, drv, full_path)) {
            *code = 0;
        } else {
            fwprintf(stderr, L"subst: failed to substitute %ls for %ls\n", drv, full_path);
            *code = 1;
        }
        return true;
    }

    *code = 1;
    return true;
}



/* ========================================================================= */
/*                   Category D: System, Uptime, Memory & Console            */
/* ========================================================================= */

bool Builtin_Uptime(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: uptime [options]\n"
                L"Tell how long the system has been running.\n\n"
                L"  -p, --pretty   show uptime in pretty format\n"
                L"  -s, --since    system up since\n");
        *code = 0;
        return true;
    }

    bool pretty = false, since = false;
    for (int i = 1; i < cmd->argc; i++) {
        if (wcscmp(cmd->argv[i], L"-p") == 0 || wcscmp(cmd->argv[i], L"--pretty") == 0) pretty = true;
        else if (wcscmp(cmd->argv[i], L"-s") == 0 || wcscmp(cmd->argv[i], L"--since") == 0) since = true;
    }

    ULONGLONG ticks = GetTickCount64();
    ULONGLONG total_sec = ticks / 1000;
    ULONGLONG days = total_sec / 86400;
    ULONGLONG hours = (total_sec % 86400) / 3600;
    ULONGLONG mins = (total_sec % 3600) / 60;

    SYSTEMTIME st;
    GetLocalTime(&st);

    if (since) {
        FILETIME ft;
        SystemTimeToFileTime(&st, &ft);
        ULARGE_INTEGER uli;
        uli.LowPart = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        uli.QuadPart -= ticks * 10000ULL; /* 100ns units */
        ft.dwLowDateTime = uli.LowPart;
        ft.dwHighDateTime = uli.HighPart;
        SYSTEMTIME boot_st;
        FileTimeToSystemTime(&ft, &boot_st);
        wprintf(L"%04d-%02d-%02d %02d:%02d:%02d\n",
                boot_st.wYear, boot_st.wMonth, boot_st.wDay,
                boot_st.wHour, boot_st.wMinute, boot_st.wSecond);
    } else if (pretty) {
        if (days > 0)
            wprintf(L"up %llu day%ls, %llu hour%ls, %llu minute%ls\n",
                    days, days == 1 ? L"" : L"s",
                    hours, hours == 1 ? L"" : L"s",
                    mins, mins == 1 ? L"" : L"s");
        else if (hours > 0)
            wprintf(L"up %llu hour%ls, %llu minute%ls\n",
                    hours, hours == 1 ? L"" : L"s",
                    mins, mins == 1 ? L"" : L"s");
        else
            wprintf(L"up %llu minute%ls\n", mins, mins == 1 ? L"" : L"s");
    } else {
        wprintf(L" %02d:%02d:%02d up ", st.wHour, st.wMinute, st.wSecond);
        if (days > 0)
            wprintf(L"%llu day%ls, %02llu:%02llu,  1 user,  load average: 0.00, 0.00, 0.00\n",
                    days, days == 1 ? L"" : L"s", hours, mins);
        else
            wprintf(L"%02llu:%02llu,  1 user,  load average: 0.00, 0.00, 0.00\n",
                    hours, mins);
    }
    *code = 0;
    return true;
}

static void FormatFreeVal(unsigned __int64 val, unsigned __int64 unit, bool human, wchar_t* buf, size_t sz) {
    if (human) FormatBytesHuman(val, buf, sz);
    else swprintf_s(buf, sz, L"%llu", val / unit);
}

bool Builtin_Free(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: free [options]\n"
                L"Display amount of free and used memory in the system.\n\n"
                L"  -b, --bytes         show output in bytes\n"
                L"  -k, --kibi          show output in kibibytes (default)\n"
                L"  -m, --mebi          show output in mebibytes\n"
                L"  -g, --gibi          show output in gibibytes\n"
                L"  -h, --human         show human-readable output\n"
                L"  -t, --total         show total for RAM + swap\n");
        *code = 0;
        return true;
    }

    bool human = false, show_total = false;
    unsigned __int64 unit = 1024;
    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* a = cmd->argv[i];
        if (wcscmp(a, L"-b") == 0 || wcscmp(a, L"--bytes") == 0) { unit = 1; human = false; }
        else if (wcscmp(a, L"-k") == 0 || wcscmp(a, L"--kibi") == 0) { unit = 1024; human = false; }
        else if (wcscmp(a, L"-m") == 0 || wcscmp(a, L"--mebi") == 0) { unit = 1024 * 1024; human = false; }
        else if (wcscmp(a, L"-g") == 0 || wcscmp(a, L"--gibi") == 0) { unit = 1024ULL * 1024 * 1024; human = false; }
        else if (wcscmp(a, L"-h") == 0 || wcscmp(a, L"--human") == 0) human = true;
        else if (wcscmp(a, L"-t") == 0 || wcscmp(a, L"--total") == 0) show_total = true;
    }

    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) {
        fwprintf(stderr, L"free: failed to query memory status\n");
        *code = 1;
        return true;
    }

    unsigned __int64 mem_total = ms.ullTotalPhys;
    unsigned __int64 mem_avail = ms.ullAvailPhys;
    unsigned __int64 mem_used = (mem_total >= mem_avail) ? (mem_total - mem_avail) : 0;
    unsigned __int64 mem_free = mem_avail;

    unsigned __int64 swap_total = (ms.ullTotalPageFile > ms.ullTotalPhys) ? (ms.ullTotalPageFile - ms.ullTotalPhys) : 0;
    unsigned __int64 swap_avail = (ms.ullAvailPageFile > ms.ullAvailPhys) ? (ms.ullAvailPageFile - ms.ullAvailPhys) : ms.ullAvailPageFile;
    unsigned __int64 swap_used = (swap_total >= swap_avail) ? (swap_total - swap_avail) : 0;
    unsigned __int64 swap_free = swap_avail;

    wchar_t mt[32], mu[32], mf[32], ma[32];
    FormatFreeVal(mem_total, unit, human, mt, _countof(mt));
    FormatFreeVal(mem_used, unit, human, mu, _countof(mu));
    FormatFreeVal(mem_free, unit, human, mf, _countof(mf));
    FormatFreeVal(mem_avail, unit, human, ma, _countof(ma));

    wchar_t st[32], su[32], sf[32];
    FormatFreeVal(swap_total, unit, human, st, _countof(st));
    FormatFreeVal(swap_used, unit, human, su, _countof(su));
    FormatFreeVal(swap_free, unit, human, sf, _countof(sf));

    wprintf(L"               total        used        free      shared  buff/cache   available\n");
    wprintf(L"Mem:    %12ls%12ls%12ls%12ls%12ls%12ls\n", mt, mu, mf, L"0", L"0", ma);
    wprintf(L"Swap:   %12ls%12ls%12ls\n", st, su, sf);

    if (show_total) {
        wchar_t tt[32], tu[32], tf[32];
        FormatFreeVal(mem_total + swap_total, unit, human, tt, _countof(tt));
        FormatFreeVal(mem_used + swap_used, unit, human, tu, _countof(tu));
        FormatFreeVal(mem_free + swap_free, unit, human, tf, _countof(tf));
        wprintf(L"Total:  %12ls%12ls%12ls\n", tt, tu, tf);
    }
    *code = 0;
    return true;
}

bool Builtin_Watch(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: watch [options] command\n"
                L"Execute a program periodically, showing output fullscreen.\n\n"
                L"  -n, --interval <secs>  seconds to wait between updates (default 2.0)\n"
                L"  -t, --no-title         turn off header\n"
                L"  -d, --differences      highlight differences between updates\n");
        *code = 0;
        return true;
    }

    double interval = 2.0;
    bool no_title = false;
    int cmd_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        if ((wcscmp(cmd->argv[i], L"-n") == 0 || wcscmp(cmd->argv[i], L"--interval") == 0) && i + 1 < cmd->argc) {
            interval = _wtof(cmd->argv[++i]);
            if (interval < 0.1) interval = 0.1;
        } else if (wcscmp(cmd->argv[i], L"-t") == 0 || wcscmp(cmd->argv[i], L"--no-title") == 0) no_title = true;
        else if (wcscmp(cmd->argv[i], L"-d") == 0 || wcscmp(cmd->argv[i], L"--differences") == 0) { }
        else if (cmd->argv[i][0] != L'-') {
            cmd_start = i;
            break;
        }
    }

    if (cmd_start >= cmd->argc) {
        fwprintf(stderr, L"watch: command missing\n");
        *code = 1;
        return true;
    }

    wchar_t full_cmd[4096] = {0};
    for (int i = cmd_start; i < cmd->argc; i++) {
        /* Arguments are already expanded; quote them so ExecuteLine can't expand them again. */
        if (!Ksh_AppendShellWord(full_cmd, _countof(full_cmd), cmd->argv[i])) {
            fwprintf(stderr, L"watch: command too long\n");
            *code = 1;
            return true;
        }
    }

    bool is_interactive = _isatty(_fileno(stdout)) != 0;
    do {
        if (!no_title) {
            SYSTEMTIME st; GetLocalTime(&st);
            wprintf(L"Every %.1fs: %ls    %04d-%02d-%02d %02d:%02d:%02d\n\n",
                    interval, full_cmd, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        }
        shell->ExecuteLine(shell, full_cmd);
        if (!is_interactive) break;
        Sleep((DWORD)(interval * 1000));
        if (_kbhit()) { (void)_getch(); break; }
    } while (true);

    *code = 0;
    return true;
}

bool Builtin_Stty(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: stty [-a] [size]\n"
                L"Print terminal characteristics.\n\n"
                L"  size        print the number of rows and columns\n"
                L"  -a, --all   print all current settings in human-readable form\n");
        *code = 0;
        return true;
    }

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int rows = 24, cols = 80;
    if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
        rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }

    if (cmd->argc > 1 && wcscmp(cmd->argv[1], L"size") == 0) {
        wprintf(L"%d %d\n", rows, cols);
        *code = 0;
        return true;
    }

    wprintf(L"speed 38400 baud; rows %d; columns %d; line = 0;\n"
            L"intr = ^C; quit = ^\\; erase = ^?; kill = ^U; eof = ^D; eol = <undef>;\n",
            rows, cols);
    *code = 0;
    return true;
}

