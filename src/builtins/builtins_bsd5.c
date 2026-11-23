#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <time.h>
#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

/* ========================================================================= */
/*                          BSD Core Utilities - Part 5                      */
/*                  sysctl, renice, arp, host, getent, srm, pr, banner       */
/* ========================================================================= */

/* ------------------------------------------------------------------------- */
/* 1. SYSCTL (FreeBSD / OpenBSD System State Query)                         */
/* ------------------------------------------------------------------------- */

bool Builtin_Sysctl(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: sysctl [-a] [-n] [name...]\n"
                L"Get or display system state and hardware tunables.\n\n"
                L"  -a          Display all currently available variables\n"
                L"  -n          Suppress printing of the field name\n");
        *code = 0;
        return true;
    }

    bool opt_a = false;
    bool opt_n = false;
    int arg_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1]) {
            for (int j = 1; arg[j]; j++) {
                if (arg[j] == L'a') opt_a = true;
                else if (arg[j] == L'n') opt_n = true;
            }
            arg_start = i + 1;
        } else {
            arg_start = i;
            break;
        }
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);

    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);

    wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD cnLen = _countof(computerName);
    GetComputerNameW(computerName, &cnLen);

    OSVERSIONINFOEXW osvi;
    memset(&osvi, 0, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    typedef NTSTATUS(WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOEXW);
    HMODULE hNt = GetModuleHandleW(L"ntdll.dll");
    if (hNt) {
        RtlGetVersionFn fn = (RtlGetVersionFn)GetProcAddress(hNt, "RtlGetVersion");
        if (fn) fn(&osvi);
    }

    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    double uptime_sec = (freq.QuadPart > 0) ? ((double)counter.QuadPart / (double)freq.QuadPart) : 0.0;

    struct SysctlEntry {
        const wchar_t* name;
        wchar_t val[256];
    } entries[12];
    int count = 0;

    entries[count].name = L"hw.ncpu"; swprintf_s(entries[count].val, 256, L"%u", si.dwNumberOfProcessors); count++;
    entries[count].name = L"hw.pagesize"; swprintf_s(entries[count].val, 256, L"%u", si.dwPageSize); count++;
    entries[count].name = L"hw.physmem"; swprintf_s(entries[count].val, 256, L"%llu", (unsigned long long)ms.ullTotalPhys); count++;
    entries[count].name = L"hw.usermem"; swprintf_s(entries[count].val, 256, L"%llu", (unsigned long long)ms.ullAvailPhys); count++;
    entries[count].name = L"hw.machine"; swprintf_s(entries[count].val, 256, L"%ls", (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) ? L"x86_64" : (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) ? L"arm64" : L"x86"); count++;
    entries[count].name = L"kern.ostype"; swprintf_s(entries[count].val, 256, L"Windows_NT"); count++;
    entries[count].name = L"kern.osrelease"; swprintf_s(entries[count].val, 256, L"%lu.%lu.%lu", osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber); count++;
    entries[count].name = L"kern.hostname"; swprintf_s(entries[count].val, 256, L"%ls", computerName); count++;
    entries[count].name = L"kern.uptime"; swprintf_s(entries[count].val, 256, L"%.2f", uptime_sec); count++;
    entries[count].name = L"vm.loadavg"; swprintf_s(entries[count].val, 256, L"{ 0.00 0.00 0.00 }"); count++;
    entries[count].name = L"vm.memory_load"; swprintf_s(entries[count].val, 256, L"%lu%%", ms.dwMemoryLoad); count++;

    if (opt_a || arg_start >= cmd->argc) {
        for (int i = 0; i < count; i++) {
            if (opt_n) wprintf(L"%ls\n", entries[i].val);
            else wprintf(L"%ls: %ls\n", entries[i].name, entries[i].val);
        }
        *code = 0;
        return true;
    }

    int ret = 0;
    for (int k = arg_start; k < cmd->argc; k++) {
        const wchar_t* q = cmd->argv[k];
        bool found = false;
        for (int i = 0; i < count; i++) {
            if (_wcsicmp(q, entries[i].name) == 0) {
                if (opt_n) wprintf(L"%ls\n", entries[i].val);
                else wprintf(L"%ls: %ls\n", entries[i].name, entries[i].val);
                found = true;
                break;
            }
        }
        if (!found) {
            fwprintf(stderr, L"sysctl: unknown oid '%ls'\n", q);
            ret = 1;
        }
    }
    *code = ret;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 2. RENICE (BSD Process Priority Adjustment)                               */
/* ------------------------------------------------------------------------- */

bool Builtin_Renice(CommandAST* cmd, int* code) {
    if (cmd->argc < 2 || (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: renice [-n] priority [[-p] pid...]\n"
                L"Alter priority of running processes.\n\n"
                L"  -n          Priority increment/level (-20 to 20)\n"
                L"  -p          Specify target process IDs\n");
        *code = (cmd->argc < 2) ? 1 : 0;
        return true;
    }

    int priority_val = 0;
    int pid_start = -1;

    for (int i = 1; i < cmd->argc; i++) {
        if (wcscmp(cmd->argv[i], L"-n") == 0 && i + 1 < cmd->argc) {
            priority_val = _wtoi(cmd->argv[++i]);
        } else if (wcscmp(cmd->argv[i], L"-p") == 0 && i + 1 < cmd->argc) {
            pid_start = i + 1;
            break;
        } else if (pid_start == -1 && (cmd->argv[i][0] == L'-' || iswdigit(cmd->argv[i][0]))) {
            if (i == 1) priority_val = _wtoi(cmd->argv[i]);
            else { pid_start = i; break; }
        } else if (pid_start == -1) {
            pid_start = i;
            break;
        }
    }

    if (pid_start == -1 || pid_start >= cmd->argc) {
        fwprintf(stderr, L"renice: missing target process ID\n");
        *code = 1;
        return true;
    }

    DWORD dwClass = NORMAL_PRIORITY_CLASS;
    if (priority_val <= -10) dwClass = HIGH_PRIORITY_CLASS;
    else if (priority_val < 0) dwClass = ABOVE_NORMAL_PRIORITY_CLASS;
    else if (priority_val == 0) dwClass = NORMAL_PRIORITY_CLASS;
    else if (priority_val <= 10) dwClass = BELOW_NORMAL_PRIORITY_CLASS;
    else dwClass = IDLE_PRIORITY_CLASS;

    int ret = 0;
    for (int i = pid_start; i < cmd->argc; i++) {
        DWORD pid = (DWORD)_wtoi(cmd->argv[i]);
        if (pid == 0) continue;
        HANDLE hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
        if (!hProc) {
            fwprintf(stderr, L"renice: %u: failed to open process (error %lu)\n", pid, GetLastError());
            ret = 1;
            continue;
        }
        if (!SetPriorityClass(hProc, dwClass)) {
            fwprintf(stderr, L"renice: %u: failed to set priority (error %lu)\n", pid, GetLastError());
            ret = 1;
        } else {
            wprintf(L"%u: priority set to %d\n", pid, priority_val);
        }
        CloseHandle(hProc);
    }
    *code = ret;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 3. ARP (Address Resolution Protocol Cache)                                */
/* ------------------------------------------------------------------------- */

bool Builtin_Arp(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: arp [-a] [-n] [hostname/ip]\n"
                L"Display and inspect the kernel IPv4 ARP neighbor cache.\n\n"
                L"  -a          Display current ARP entries\n"
                L"  -n          Show numerical addresses\n");
        *code = 0;
        return true;
    }

    PMIB_IPNET_TABLE2 pTable = NULL;
    if (GetIpNetTable2(AF_INET, &pTable) != NO_ERROR || !pTable) {
        GetIpNetTable2(AF_UNSPEC, &pTable);
    }

    wprintf(L"  Internet Address      Physical Address      Type\n");
    if (pTable) {
        for (ULONG i = 0; i < pTable->NumEntries; i++) {
            char ipBuf[INET6_ADDRSTRLEN] = {0};
            if (pTable->Table[i].Address.si_family == AF_INET) {
                inet_ntop(AF_INET, &(pTable->Table[i].Address.Ipv4.sin_addr), ipBuf, sizeof(ipBuf));
            } else if (pTable->Table[i].Address.si_family == AF_INET6) {
                inet_ntop(AF_INET6, &(pTable->Table[i].Address.Ipv6.sin6_addr), ipBuf, sizeof(ipBuf));
            }
            if (!ipBuf[0]) continue;

            wchar_t macBuf[32] = {0};
            if (pTable->Table[i].PhysicalAddressLength > 0) {
                swprintf_s(macBuf, _countof(macBuf), L"%02x-%02x-%02x-%02x-%02x-%02x",
                         pTable->Table[i].PhysicalAddress[0], pTable->Table[i].PhysicalAddress[1],
                         pTable->Table[i].PhysicalAddress[2], pTable->Table[i].PhysicalAddress[3],
                         pTable->Table[i].PhysicalAddress[4], pTable->Table[i].PhysicalAddress[5]);
            } else {
                wcscpy_s(macBuf, _countof(macBuf), L"(incomplete)");
            }

            const wchar_t* typeStr = L"dynamic";
            if (pTable->Table[i].State == NlnsPermanent) typeStr = L"static";

            wprintf(L"  %-20hs  %-20ls  %-10ls\n", ipBuf, macBuf, typeStr);
        }
        FreeMibTable(pTable);
    }
    *code = 0;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 4. HOST (BSD DNS Lookup Utility)                                          */
/* ------------------------------------------------------------------------- */

bool Builtin_Host(CommandAST* cmd, int* code) {
    if (cmd->argc < 2 || (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: host [-t type] <name_or_ip>\n"
                L"DNS lookup utility for resolving host names and IP addresses.\n\n"
                L"  -t type     Specify query type (A, AAAA, PTR, ANY)\n");
        *code = (cmd->argc < 2) ? 1 : 0;
        return true;
    }

    const wchar_t* target = NULL;
    for (int i = 1; i < cmd->argc; i++) {
        if (wcscmp(cmd->argv[i], L"-t") == 0 && i + 1 < cmd->argc) {
            i++; /* skip type */
        } else if (cmd->argv[i][0] != L'-') {
            target = cmd->argv[i];
            break;
        }
    }

    if (!target) {
        fwprintf(stderr, L"host: missing host name or IP address\n");
        *code = 1;
        return true;
    }

    char targetA[256];
    size_t converted = 0;
    wcstombs_s(&converted, targetA, sizeof(targetA), target, _TRUNCATE);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(targetA, NULL, &hints, &res) != 0 || !res) {
        fwprintf(stderr, L"Host %ls not found: 3(NXDOMAIN)\n", target);
        *code = 1;
        return true;
    }

    bool printed = false;
    for (struct addrinfo* p = res; p != NULL; p = p->ai_next) {
        char ipBuf[INET6_ADDRSTRLEN] = {0};
        if (p->ai_family == AF_INET) {
            struct sockaddr_in* ipv4 = (struct sockaddr_in*)p->ai_addr;
            inet_ntop(AF_INET, &(ipv4->sin_addr), ipBuf, sizeof(ipBuf));
            wprintf(L"%ls has address %hs\n", target, ipBuf);
            printed = true;
        } else if (p->ai_family == AF_INET6) {
            struct sockaddr_in6* ipv6 = (struct sockaddr_in6*)p->ai_addr;
            inet_ntop(AF_INET6, &(ipv6->sin6_addr), ipBuf, sizeof(ipBuf));
            wprintf(L"%ls has IPv6 address %hs\n", target, ipBuf);
            printed = true;
        }
    }
    freeaddrinfo(res);
    *code = printed ? 0 : 1;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 5. GETENT (BSD/POSIX System Database Query)                               */
/* ------------------------------------------------------------------------- */

bool Builtin_Getent(CommandAST* cmd, int* code) {
    if (cmd->argc < 2 || (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: getent <database> [key...]\n"
                L"Get entries from administrative databases.\n\n"
                L"Databases: passwd, group, hosts, services\n");
        *code = (cmd->argc < 2) ? 1 : 0;
        return true;
    }

    const wchar_t* db = cmd->argv[1];
    if (_wcsicmp(db, L"passwd") == 0) {
        wchar_t curUser[256] = {0};
        DWORD uLen = _countof(curUser);
        GetUserNameW(curUser, &uLen);
        if (cmd->argc > 2) {
            for (int i = 2; i < cmd->argc; i++) {
                wprintf(L"%ls:*:%u:%u:%ls:C:\\Users\\%ls:/bin/ksh\n",
                        cmd->argv[i], 1000, 1000, cmd->argv[i], cmd->argv[i]);
            }
        } else {
            wprintf(L"%ls:*:%u:%u:%ls:C:\\Users\\%ls:/bin/ksh\n",
                    curUser[0] ? curUser : L"User", 1000, 1000,
                    curUser[0] ? curUser : L"User", curUser[0] ? curUser : L"User");
            wprintf(L"Administrator:*:500:513:Built-in Administrator:C:\\Users\\Administrator:/bin/ksh\n");
            wprintf(L"SYSTEM:*:18:18:Local System:C:\\Windows:/bin/ksh\n");
        }
        *code = 0;
        return true;
    }

    if (_wcsicmp(db, L"group") == 0) {
        wprintf(L"Administrators:*:544:\n");
        wprintf(L"Users:*:545:\n");
        wprintf(L"Guests:*:546:\n");
        *code = 0;
        return true;
    }

    if (_wcsicmp(db, L"hosts") == 0) {
        if (cmd->argc > 2) {
            for (int i = 2; i < cmd->argc; i++) {
                char targetA[256];
                size_t converted = 0;
                wcstombs_s(&converted, targetA, sizeof(targetA), cmd->argv[i], _TRUNCATE);
                struct addrinfo hints, *res = NULL;
                memset(&hints, 0, sizeof(hints));
                hints.ai_family = AF_INET;
                if (getaddrinfo(targetA, NULL, &hints, &res) == 0 && res) {
                    struct sockaddr_in* in = (struct sockaddr_in*)res->ai_addr;
                    char ipStr[32];
                    inet_ntop(AF_INET, &in->sin_addr, ipStr, sizeof(ipStr));
                    wprintf(L"%-16hs %ls\n", ipStr, cmd->argv[i]);
                    freeaddrinfo(res);
                }
            }
        } else {
            wprintf(L"127.0.0.1        localhost\n");
            wprintf(L"::1              localhost\n");
        }
        *code = 0;
        return true;
    }

    if (_wcsicmp(db, L"services") == 0) {
        if (cmd->argc > 2) {
            for (int i = 2; i < cmd->argc; i++) {
                int port = _wtoi(cmd->argv[i]);
                struct servent* se = (port > 0) ? getservbyport(htons((u_short)port), NULL) : NULL;
                if (se) {
                    wprintf(L"%-16hs %u/%hs\n", se->s_name, ntohs(se->s_port), se->s_proto);
                } else {
                    wprintf(L"%-16ls %ls/tcp\n", cmd->argv[i], cmd->argv[i]);
                }
            }
        } else {
            wprintf(L"echo             7/tcp\n");
            wprintf(L"ftp              21/tcp\n");
            wprintf(L"ssh              22/tcp\n");
            wprintf(L"telnet           23/tcp\n");
            wprintf(L"http             80/tcp\n");
            wprintf(L"https            443/tcp\n");
        }
        *code = 0;
        return true;
    }

    fwprintf(stderr, L"getent: Unknown database: %ls\n", db);
    *code = 1;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 6. SRM / SHRED (Secure File Deletion)                                     */
/* ------------------------------------------------------------------------- */

bool Builtin_Srm(CommandAST* cmd, int* code) {
    if (cmd->argc < 2 || (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: srm [-f] [-z] [-n passes] file...\n"
                L"Securely overwrite and remove files.\n\n"
                L"  -f, --force      Ignore nonexistent files\n"
                L"  -z, --zero       Add a final overwrite with zeros\n"
                L"  -n passes        Number of overwrite passes (default: 3)\n");
        *code = (cmd->argc < 2) ? 1 : 0;
        return true;
    }

    bool force = false;
    bool zero_pass = false;
    int passes = 3;
    int file_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1]) {
            if (wcscmp(arg, L"-f") == 0 || wcscmp(arg, L"--force") == 0) force = true;
            else if (wcscmp(arg, L"-z") == 0 || wcscmp(arg, L"--zero") == 0) zero_pass = true;
            else if (wcscmp(arg, L"-n") == 0 && i + 1 < cmd->argc) passes = _wtoi(cmd->argv[++i]);
            file_start = i + 1;
        } else {
            file_start = i;
            break;
        }
    }

    if (passes < 1) passes = 1;
    int ret = 0;
    char buffer[4096];

    for (int i = file_start; i < cmd->argc; i++) {
        const wchar_t* path = cmd->argv[i];
        FILE* fp = _wfsopen(path, L"r+b", _SH_DENYWR);
        if (!fp) {
            if (!force) {
                fwprintf(stderr, L"srm: cannot open '%ls': No such file or directory\n", path);
                ret = 1;
            }
            continue;
        }

        _fseeki64(fp, 0, SEEK_END);
        __int64 fsize = _ftelli64(fp);

        srand((unsigned int)time(NULL));
        for (int p = 0; p < passes; p++) {
            _fseeki64(fp, 0, SEEK_SET);
            __int64 rem = fsize;
            while (rem > 0) {
                size_t chunk = (rem > (long long)sizeof(buffer)) ? sizeof(buffer) : (size_t)rem;
                for (size_t b = 0; b < chunk; b++) buffer[b] = (char)(rand() & 0xFF);
                fwrite(buffer, 1, chunk, fp);
                rem -= chunk;
            }
            fflush(fp);
        }

        if (zero_pass) {
            _fseeki64(fp, 0, SEEK_SET);
            memset(buffer, 0, sizeof(buffer));
            __int64 rem = fsize;
            while (rem > 0) {
                size_t chunk = (rem > (long long)sizeof(buffer)) ? sizeof(buffer) : (size_t)rem;
                fwrite(buffer, 1, chunk, fp);
                rem -= chunk;
            }
            fflush(fp);
        }

        HANDLE hFile = (HANDLE)_get_osfhandle(_fileno(fp));
        if (hFile != INVALID_HANDLE_VALUE) FlushFileBuffers(hFile);
        fclose(fp);

        if (!DeleteFileW(path)) {
            fwprintf(stderr, L"srm: failed to delete '%ls'\n", path);
            ret = 1;
        }
    }

    *code = ret;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 7. PR (POSIX / BSD Print Paginator)                                       */
/* ------------------------------------------------------------------------- */

bool Builtin_Pr(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: pr [-h header] [-l length] [-t] [file...]\n"
                L"Paginate or format files for printing.\n\n"
                L"  -h header   Use header instead of filename in page header\n"
                L"  -l length   Page length in lines (default: 66)\n"
                L"  -t          Omit page headers and trailers\n");
        *code = 0;
        return true;
    }

    int page_len = 66;
    bool omit_headers = false;
    const wchar_t* custom_header = NULL;
    int file_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1]) {
            if (wcscmp(arg, L"-t") == 0) omit_headers = true;
            else if (wcscmp(arg, L"-l") == 0 && i + 1 < cmd->argc) page_len = _wtoi(cmd->argv[++i]);
            else if (wcscmp(arg, L"-h") == 0 && i + 1 < cmd->argc) custom_header = cmd->argv[++i];
            file_start = i + 1;
        } else {
            file_start = i;
            break;
        }
    }

    if (page_len < 10) page_len = 10;
    int body_lines = omit_headers ? page_len : (page_len - 10);
    if (body_lines < 1) body_lines = 1;

    int file_count = cmd->argc - file_start;
    for (int fi = (file_count > 0 ? file_start : 0); (file_count > 0 ? fi < cmd->argc : fi == 0); fi++) {
        FILE* fp = (file_count == 0 || wcscmp(cmd->argv[fi], L"-") == 0) ? stdin : _wfsopen(cmd->argv[fi], L"r, ccs=UTF-8", _SH_DENYNO);
        if (!fp) fp = _wfsopen(cmd->argv[fi], L"r", _SH_DENYNO);
        if (!fp) {
            fwprintf(stderr, L"pr: cannot open %ls\n", cmd->argv[fi]);
            continue;
        }

        const wchar_t* title = custom_header ? custom_header : (file_count > 0 ? cmd->argv[fi] : L"");
        int page_num = 1;
        int line_in_page = 0;
        wchar_t line[4096];

        time_t t = time(NULL);
        struct tm tm_buf;
        localtime_s(&tm_buf, &t);
        wchar_t dateStr[64];
        wcsftime(dateStr, _countof(dateStr), L"%b %d %H:%M %Y", &tm_buf);

        while (fgetws(line, _countof(line), fp)) {
            if (!omit_headers && line_in_page == 0) {
                wprintf(L"\n\n%ls  %ls  Page %d\n\n\n", dateStr, title, page_num);
            }
            fputws(line, stdout);
            line_in_page++;
            if (line_in_page >= body_lines) {
                if (!omit_headers) wprintf(L"\n\n\n\n\n");
                line_in_page = 0;
                page_num++;
            }
        }
        if (!omit_headers && line_in_page > 0) {
            for (int k = line_in_page; k < page_len - 5; k++) putwchar(L'\n');
        }

        if (fp != stdin) fclose(fp);
    }
    *code = 0;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 8. BANNER (Classic BSD ASCII Art Block Banner)                           */
/* ------------------------------------------------------------------------- */

static const char* g_bannerFont[36][7] = {
    /* 'A' */
    {"  ###  ", " #   # ", "#     #", "#######", "#     #", "#     #", "#     #"},
    /* 'B' */
    {"###### ", "#     #", "#     #", "###### ", "#     #", "#     #", "###### "},
    /* 'C' */
    {" ##### ", "#     #", "#      ", "#      ", "#      ", "#     #", " ##### "},
    /* 'D' */
    {"###### ", "#     #", "#     #", "#     #", "#     #", "#     #", "###### "},
    /* 'E' */
    {"#######", "#      ", "#      ", "#####  ", "#      ", "#      ", "#######"},
    /* 'F' */
    {"#######", "#      ", "#      ", "#####  ", "#      ", "#      ", "#      "},
    /* 'G' */
    {" ##### ", "#     #", "#      ", "#  ####", "#     #", "#     #", " ##### "},
    /* 'H' */
    {"#     #", "#     #", "#     #", "#######", "#     #", "#     #", "#     #"},
    /* 'I' */
    {" ##### ", "   #   ", "   #   ", "   #   ", "   #   ", "   #   ", " ##### "},
    /* 'J' */
    {"      #", "      #", "      #", "      #", "#     #", "#     #", " ##### "},
    /* 'K' */
    {"#    # ", "#   #  ", "#  #   ", "###    ", "#  #   ", "#   #  ", "#    # "},
    /* 'L' */
    {"#      ", "#      ", "#      ", "#      ", "#      ", "#      ", "#######"},
    /* 'M' */
    {"#     #", "##   ##", "# # # #", "#  #  #", "#     #", "#     #", "#     #"},
    /* 'N' */
    {"#     #", "##    #", "# #   #", "#  #  #", "#   # #", "#    ##", "#     #"},
    /* 'O' */
    {" ##### ", "#     #", "#     #", "#     #", "#     #", "#     #", " ##### "},
    /* 'P' */
    {"###### ", "#     #", "#     #", "###### ", "#      ", "#      ", "#      "},
    /* 'Q' */
    {" ##### ", "#     #", "#     #", "#     #", "#   # #", "#    # ", " #### #"},
    /* 'R' */
    {"###### ", "#     #", "#     #", "###### ", "#   #  ", "#    # ", "#     #"},
    /* 'S' */
    {" ##### ", "#     #", "#      ", " ##### ", "      #", "#     #", " ##### "},
    /* 'T' */
    {"#######", "   #   ", "   #   ", "   #   ", "   #   ", "   #   ", "   #   "},
    /* 'U' */
    {"#     #", "#     #", "#     #", "#     #", "#     #", "#     #", " ##### "},
    /* 'V' */
    {"#     #", "#     #", "#     #", "#     #", " #   # ", "  # #  ", "   #   "},
    /* 'W' */
    {"#     #", "#     #", "#     #", "#  #  #", "# # # #", "##   ##", "#     #"},
    /* 'X' */
    {"#     #", " #   # ", "  # #  ", "   #   ", "  # #  ", " #   # ", "#     #"},
    /* 'Y' */
    {"#     #", " #   # ", "  # #  ", "   #   ", "   #   ", "   #   ", "   #   "},
    /* 'Z' */
    {"#######", "     # ", "    #  ", "   #   ", "  #    ", " #     ", "#######"},
    /* '0' */
    {" ##### ", "#    ##", "#   # #", "#  #  #", "# #   #", "##    #", " ##### "},
    /* '1' */
    {"   #   ", "  ##   ", " # #   ", "   #   ", "   #   ", "   #   ", " ##### "},
    /* '2' */
    {" ##### ", "#     #", "      #", " ##### ", "#      ", "#      ", "#######"},
    /* '3' */
    {" ##### ", "#     #", "      #", " ##### ", "      #", "#     #", " ##### "},
    /* '4' */
    {"#      ", "#    # ", "#    # ", "#######", "     # ", "     # ", "     # "},
    /* '5' */
    {"#######", "#      ", "###### ", "      #", "      #", "#     #", " ##### "},
    /* '6' */
    {" ##### ", "#      ", "###### ", "#     #", "#     #", "#     #", " ##### "},
    /* '7' */
    {"#######", "     # ", "    #  ", "   #   ", "  #    ", " #     ", "#      "},
    /* '8' */
    {" ##### ", "#     #", "#     #", " ##### ", "#     #", "#     #", " ##### "},
    /* '9' */
    {" ##### ", "#     #", "#     #", " ######", "      #", "     # ", " ##### "}
};

bool Builtin_Banner(CommandAST* cmd, int* code) {
    if (cmd->argc < 2 || (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: banner string...\n"
                L"Print large ASCII art banner strings.\n");
        *code = (cmd->argc < 2) ? 1 : 0;
        return true;
    }

    wchar_t joined[512] = {0};
    for (int i = 1; i < cmd->argc; i++) {
        if (i > 1) wcscat_s(joined, _countof(joined), L" ");
        wcscat_s(joined, _countof(joined), cmd->argv[i]);
    }

    for (int row = 0; row < 7; row++) {
        for (size_t c = 0; joined[c]; c++) {
            wchar_t ch = towupper(joined[c]);
            if (ch >= L'A' && ch <= L'Z') {
                wprintf(L"%hs  ", g_bannerFont[ch - L'A'][row]);
            } else if (ch >= L'0' && ch <= L'9') {
                wprintf(L"%hs  ", g_bannerFont[26 + (ch - L'0')][row]);
            } else if (ch == L' ') {
                wprintf(L"       ");
            } else if (ch == L'!') {
                wprintf(L"%hs  ", (row == 5) ? "   " : ((row == 6) ? " # " : " # "));
            } else if (ch == L'-') {
                wprintf(L"%hs  ", (row == 3) ? "#####" : "     ");
            } else if (ch == L'.') {
                wprintf(L"%hs  ", (row == 6) ? " # " : "   ");
            } else {
                wprintf(L"       ");
            }
        }
        wprintf(L"\n");
    }
    wprintf(L"\n");
    *code = 0;
    return true;
}
