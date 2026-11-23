#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

bool Builtin_Chcp(CommandAST* cmd, int* code) {
    wchar_t rest[MAX_LINE_LEN] = {0};
    for (int i = 1; i < cmd->argc; i++) {
        if (i > 1) wcscat_s(rest, _countof(rest), L" ");
        wcscat_s(rest, _countof(rest), cmd->argv[i]);
    }
    *code = Batch_ExecuteCodePage(rest);
    return true;
}

bool Builtin_Color(IShell* shell, CommandAST* cmd, int* code) {
    wchar_t rest[MAX_LINE_LEN] = {0};
    for (int i = 1; i < cmd->argc; i++) {
        if (i > 1) wcscat_s(rest, _countof(rest), L" ");
        wcscat_s(rest, _countof(rest), cmd->argv[i]);
    }
    *code = Batch_ExecuteColor(shell, rest);
    return true;
}

bool Builtin_Title(CommandAST* cmd, int* code) {
    wchar_t rest[MAX_LINE_LEN] = {0};
    for (int i = 1; i < cmd->argc; i++) {
        if (i > 1) wcscat_s(rest, _countof(rest), L" ");
        wcscat_s(rest, _countof(rest), cmd->argv[i]);
    }
    *code = Batch_ExecuteTitle(rest);
    return true;
}

bool Builtin_Vol(CommandAST* cmd, int* code) {
    wchar_t rest[MAX_LINE_LEN] = {0};
    for (int i = 1; i < cmd->argc; i++) {
        if (i > 1) wcscat_s(rest, _countof(rest), L" ");
        wcscat_s(rest, _countof(rest), cmd->argv[i]);
    }
    *code = Batch_ExecuteVolume(rest);
    return true;
}

bool Builtin_Assoc(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Displays or modifies file extension associations\n\nASSOC [.ext[=[fileType]]]\n\n  .ext      Specifies the file extension to associate the file type with\n  fileType  Specifies the file type to associate with the file extension\n\nType ASSOC without parameters to display the current file associations.\n");
        *code = 0;
        return true;
    }
    if (cmd->argc == 1) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, NULL, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD index = 0;
            wchar_t subkey[256];
            DWORD subkeyLen = _countof(subkey);
            while (RegEnumKeyExW(hKey, index++, subkey, &subkeyLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                if (subkey[0] == L'.') {
                    wchar_t val[512] = {0};
                    DWORD valBytes = sizeof(val);
                    if (RegGetValueW(hKey, subkey, NULL, RRF_RT_REG_SZ, NULL, val, &valBytes) == ERROR_SUCCESS) {
                        if (val[0]) {
                            wprintf(L"%ls=%ls\n", subkey, val);
                        }
                    }
                }
                subkeyLen = _countof(subkey);
            }
            RegCloseKey(hKey);
        }
        *code = 0;
        return true;
    }
    const wchar_t* arg = cmd->argv[1];
    const wchar_t* eq = wcschr(arg, L'=');
    if (!eq) {
        wchar_t val[512] = {0};
        DWORD valBytes = sizeof(val);
        if (RegGetValueW(HKEY_CLASSES_ROOT, arg, NULL, RRF_RT_REG_SZ, NULL, val, &valBytes) == ERROR_SUCCESS && val[0]) {
            wprintf(L"%ls=%ls\n", arg, val);
            *code = 0;
        } else {
            fwprintf(stderr, L"File association not found for extension %ls\n", arg);
            *code = 1;
        }
        return true;
    }
    wchar_t ext[256] = {0};
    size_t extLen = eq - arg;
    if (extLen >= _countof(ext)) extLen = _countof(ext) - 1;
    wcsncpy_s(ext, _countof(ext), arg, extLen);
    const wchar_t* ftype = eq + 1;
    if (!*ftype) {
        if (RegDeleteKeyW(HKEY_CLASSES_ROOT, ext) != ERROR_SUCCESS) {
            wchar_t userSub[512];
            swprintf_s(userSub, _countof(userSub), L"Software\\Classes\\%ls", ext);
            RegDeleteKeyW(HKEY_CURRENT_USER, userSub);
        }
        *code = 0;
        return true;
    }
    HKEY hSubKey = NULL;
    LSTATUS res = RegCreateKeyExW(HKEY_CLASSES_ROOT, ext, 0, NULL, 0, KEY_WRITE, NULL, &hSubKey, NULL);
    if (res != ERROR_SUCCESS) {
        wchar_t userSub[512];
        swprintf_s(userSub, _countof(userSub), L"Software\\Classes\\%ls", ext);
        res = RegCreateKeyExW(HKEY_CURRENT_USER, userSub, 0, NULL, 0, KEY_WRITE, NULL, &hSubKey, NULL);
    }
    if (res == ERROR_SUCCESS && hSubKey) {
        RegSetValueExW(hSubKey, NULL, 0, REG_SZ, (const BYTE*)ftype, (DWORD)((wcslen(ftype) + 1) * sizeof(wchar_t)));
        RegCloseKey(hSubKey);
        wprintf(L"%ls=%ls\n", ext, ftype);
        *code = 0;
    } else {
        fwprintf(stderr, L"Access is denied.\n");
        *code = 1;
    }
    return true;
}

bool Builtin_Ftype(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Displays or modifies file types used in file extension associations\n\nFTYPE [fileType[=[openCommandString]]]\n\n  fileType           Specifies the file type to examine or change\n  openCommandString  Specifies the open command to use when launching files\n                     of this type.\n\nType FTYPE without parameters to display current file types.\n");
        *code = 0;
        return true;
    }
    if (cmd->argc == 1) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, NULL, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD index = 0;
            wchar_t subkey[256];
            DWORD subkeyLen = _countof(subkey);
            while (RegEnumKeyExW(hKey, index++, subkey, &subkeyLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                if (subkey[0] != L'.') {
                    wchar_t fullSub[512];
                    swprintf_s(fullSub, _countof(fullSub), L"%ls\\shell\\open\\command", subkey);
                    wchar_t val[1024] = {0};
                    DWORD valBytes = sizeof(val);
                    if (RegGetValueW(hKey, fullSub, NULL, RRF_RT_REG_SZ, NULL, val, &valBytes) == ERROR_SUCCESS && val[0]) {
                        wprintf(L"%ls=%ls\n", subkey, val);
                    }
                }
                subkeyLen = _countof(subkey);
            }
            RegCloseKey(hKey);
        }
        *code = 0;
        return true;
    }
    const wchar_t* arg = cmd->argv[1];
    const wchar_t* eq = wcschr(arg, L'=');
    if (!eq) {
        wchar_t fullSub[512];
        swprintf_s(fullSub, _countof(fullSub), L"%ls\\shell\\open\\command", arg);
        wchar_t val[1024] = {0};
        DWORD valBytes = sizeof(val);
        if (RegGetValueW(HKEY_CLASSES_ROOT, fullSub, NULL, RRF_RT_REG_SZ, NULL, val, &valBytes) == ERROR_SUCCESS && val[0]) {
            wprintf(L"%ls=%ls\n", arg, val);
            *code = 0;
        } else {
            fwprintf(stderr, L"File type '%ls' not found or no open command associated with it.\n", arg);
            *code = 1;
        }
        return true;
    }
    wchar_t ftype[256] = {0};
    size_t ftypeLen = eq - arg;
    if (ftypeLen >= _countof(ftype)) ftypeLen = _countof(ftype) - 1;
    wcsncpy_s(ftype, _countof(ftype), arg, ftypeLen);
    const wchar_t* openCmd = eq + 1;
    wchar_t fullSub[512];
    swprintf_s(fullSub, _countof(fullSub), L"%ls\\shell\\open\\command", ftype);
    if (!*openCmd) {
        if (RegDeleteKeyW(HKEY_CLASSES_ROOT, fullSub) != ERROR_SUCCESS) {
            wchar_t userSub[512];
            swprintf_s(userSub, _countof(userSub), L"Software\\Classes\\%ls", fullSub);
            RegDeleteKeyW(HKEY_CURRENT_USER, userSub);
        }
        *code = 0;
        return true;
    }
    HKEY hSubKey = NULL;
    LSTATUS res = RegCreateKeyExW(HKEY_CLASSES_ROOT, fullSub, 0, NULL, 0, KEY_WRITE, NULL, &hSubKey, NULL);
    if (res != ERROR_SUCCESS) {
        wchar_t userSub[512];
        swprintf_s(userSub, _countof(userSub), L"Software\\Classes\\%ls", fullSub);
        res = RegCreateKeyExW(HKEY_CURRENT_USER, userSub, 0, NULL, 0, KEY_WRITE, NULL, &hSubKey, NULL);
    }
    if (res == ERROR_SUCCESS && hSubKey) {
        RegSetValueExW(hSubKey, NULL, 0, REG_SZ, (const BYTE*)openCmd, (DWORD)((wcslen(openCmd) + 1) * sizeof(wchar_t)));
        RegCloseKey(hSubKey);
        wprintf(L"%ls=%ls\n", ftype, openCmd);
        *code = 0;
    } else {
        fwprintf(stderr, L"Access is denied.\n");
        *code = 1;
    }
    return true;
}

static const wchar_t* const kDateMonthNames[] = { L"January", L"February", L"March", L"April", L"May", L"June",
    L"July", L"August", L"September", L"October", L"November", L"December" };
static const wchar_t* const kDateDayNames[] = { L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday",
    L"Friday", L"Saturday" };

static bool DateIsSpace(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == L'\v' || c == L'\f';
}

static bool DateIsDigit(wchar_t c) {
    return c >= L'0' && c <= L'9';
}

static int DateDaysInMonth(int y, int m) {
    static const int dim[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dim[m - 1];
}

/* Sakamoto's algorithm: 0 = Sunday. */
static int DateDayOfWeek(int y, int m, int d) {
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

/* Validates ranges and fills in the weekday. */
static bool DateFinalize(SYSTEMTIME* st) {
    if (st->wYear < 1601 || st->wMonth < 1 || st->wMonth > 12 || st->wDay < 1 ||
        st->wDay > DateDaysInMonth(st->wYear, st->wMonth) || st->wHour > 23 ||
        st->wMinute > 59 || st->wSecond > 59) {
        return false;
    }
    st->wDayOfWeek = (WORD)DateDayOfWeek(st->wYear, st->wMonth, st->wDay);
    st->wMilliseconds = 0;
    return true;
}

static bool DateReadNum(const wchar_t** in, int maxDigits, int* out) {
    const wchar_t* p = *in;
    int v = 0, n = 0;
    while (n < maxDigits && DateIsDigit(*p)) {
        v = v * 10 + (*p - L'0');
        p++;
        n++;
    }
    if (n == 0) return false;
    *out = v;
    *in = p;
    return true;
}

/* Matches a full name or its 3-letter abbreviation; returns the index or -1. */
static int DateMatchName(const wchar_t** in, const wchar_t* const names[], int count) {
    for (int i = 0; i < count; i++) {
        size_t len = wcslen(names[i]);
        if (_wcsnicmp(*in, names[i], len) == 0) { *in += len; return i; }
        if (_wcsnicmp(*in, names[i], 3) == 0) { *in += 3; return i; }
    }
    return -1;
}

typedef struct { int ampm; } DateScanState; /* 0 = none, 1 = AM, 2 = PM */

/* Scans text against a strptime-style format, updating st. */
static bool DateScanFormat(const wchar_t* fmt, const wchar_t** in, SYSTEMTIME* st, DateScanState* s) {
    const wchar_t* p = *in;
    for (; *fmt; fmt++) {
        if (DateIsSpace(*fmt)) {
            while (DateIsSpace(*p)) p++;
            continue;
        }
        if (*fmt != L'%' || !fmt[1]) {
            if (*p != *fmt) return false;
            p++;
            continue;
        }
        fmt++;
        int v = 0;
        int idx = -1;
        switch (*fmt) {
            case L'Y': if (!DateReadNum(&p, 4, &v)) return false; st->wYear = (WORD)v; break;
            case L'y': if (!DateReadNum(&p, 2, &v)) return false; st->wYear = (WORD)(v < 69 ? 2000 + v : 1900 + v); break;
            case L'm': if (!DateReadNum(&p, 2, &v)) return false; st->wMonth = (WORD)v; break;
            case L'd': if (!DateReadNum(&p, 2, &v)) return false; st->wDay = (WORD)v; break;
            case L'e':
                while (*p == L' ') p++;
                if (!DateReadNum(&p, 2, &v)) return false;
                st->wDay = (WORD)v;
                break;
            case L'H': case L'I': if (!DateReadNum(&p, 2, &v)) return false; st->wHour = (WORD)v; break;
            case L'M': if (!DateReadNum(&p, 2, &v)) return false; st->wMinute = (WORD)v; break;
            case L'S': if (!DateReadNum(&p, 2, &v)) return false; st->wSecond = (WORD)v; break;
            case L'p':
                if (_wcsnicmp(p, L"AM", 2) == 0) s->ampm = 1;
                else if (_wcsnicmp(p, L"PM", 2) == 0) s->ampm = 2;
                else return false;
                p += 2;
                break;
            case L'T': if (!DateScanFormat(L"%H:%M:%S", &p, st, s)) return false; break;
            case L'R': if (!DateScanFormat(L"%H:%M", &p, st, s)) return false; break;
            case L'F': if (!DateScanFormat(L"%Y-%m-%d", &p, st, s)) return false; break;
            case L'b': case L'h': case L'B':
                idx = DateMatchName(&p, kDateMonthNames, 12);
                if (idx < 0) return false;
                st->wMonth = (WORD)(idx + 1);
                break;
            case L'a': case L'A':
                if (DateMatchName(&p, kDateDayNames, 7) < 0) return false;
                break;
            case L'n': case L't':
                while (DateIsSpace(*p)) p++;
                break;
            case L'%':
                if (*p != L'%') return false;
                p++;
                break;
            default:
                return false;
        }
    }
    *in = p;
    return true;
}

/* Parses operand text using a -f input format. */
static bool DateParseFormatted(const wchar_t* fmt, const wchar_t* text, SYSTEMTIME* st) {
    DateScanState s = { 0 };
    const wchar_t* p = text;
    if (!DateScanFormat(fmt, &p, st, &s)) return false;
    while (DateIsSpace(*p)) p++;
    if (*p) return false;
    if (s.ampm == 2 && st->wHour < 12) st->wHour += 12;
    else if (s.ampm == 1 && st->wHour == 12) st->wHour = 0;
    return DateFinalize(st);
}

/* Parses BSD [[cc]yy]mmddhhmm[.ss]; missing year fields keep the current year. */
static bool DateParseStamp(const wchar_t* text, SYSTEMTIME* st) {
    const wchar_t* dot = wcschr(text, L'.');
    size_t len = dot ? (size_t)(dot - text) : wcslen(text);
    int sec = 0;
    if (dot) {
        if (wcslen(dot + 1) != 2 || !DateIsDigit(dot[1]) || !DateIsDigit(dot[2])) return false;
        sec = (dot[1] - L'0') * 10 + (dot[2] - L'0');
    }
    if (len != 8 && len != 10 && len != 12) return false;
    for (size_t i = 0; i < len; i++) {
        if (!DateIsDigit(text[i])) return false;
    }

    const wchar_t* p = text;
    int v = 0;
    if (len == 12) {
        DateReadNum(&p, 2, &v);
        int cc = v;
        DateReadNum(&p, 2, &v);
        st->wYear = (WORD)(cc * 100 + v);
    } else if (len == 10) {
        DateReadNum(&p, 2, &v);
        st->wYear = (WORD)(v < 69 ? 2000 + v : 1900 + v);
    }
    DateReadNum(&p, 2, &v); st->wMonth = (WORD)v;
    DateReadNum(&p, 2, &v); st->wDay = (WORD)v;
    DateReadNum(&p, 2, &v); st->wHour = (WORD)v;
    DateReadNum(&p, 2, &v); st->wMinute = (WORD)v;
    st->wSecond = (WORD)sec;
    return DateFinalize(st);
}

/* An operand such as 1113161426.30 is a BSD set-the-clock stamp, not a CMD-style date. */
static bool DateLooksLikeStamp(const wchar_t* s) {
    size_t len = 0;
    for (; s[len]; len++) {
        if (!DateIsDigit(s[len]) && s[len] != L'.') return false;
    }
    return len >= 8 && DateIsDigit(s[0]);
}

/* Admins hold SeSystemtimePrivilege but it is disabled by default; enable it before setting the clock. */
static void DateEnableSetTimePrivilege(void) {
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return;
    LUID luid;
    if (LookupPrivilegeValueW(NULL, SE_SYSTEMTIME_NAME, &luid)) {
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL);
    }
    CloseHandle(token);
}

bool Builtin_Date_POSIX(CommandAST* cmd, int* code) {
    bool utc = false;
    const wchar_t* fmt = NULL;
    int64_t ref_time = -1;
    bool rfc = false;
    bool iso = false;
    bool noSet = false;
    const wchar_t* inFmt = NULL;
    const wchar_t* operand = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'+') {
            fmt = arg + 1;
        } else if (wcscmp(arg, L"-j") == 0) {
            noSet = true;
        } else if (wcscmp(arg, L"-f") == 0 && i + 1 < cmd->argc) {
            inFmt = cmd->argv[++i];
        } else if (arg[0] != L'-') {
            operand = arg;
        } else if (wcscmp(arg, L"-u") == 0 || wcscmp(arg, L"--utc") == 0 || wcscmp(arg, L"--universal") == 0) {
            utc = true;
        } else if (wcscmp(arg, L"-R") == 0 || wcscmp(arg, L"--rfc-email") == 0) {
            rfc = true;
        } else if (wcsncmp(arg, L"-I", 2) == 0 || wcscmp(arg, L"--iso-8601") == 0) {
            iso = true;
        } else if (wcscmp(arg, L"-r") == 0 && i + 1 < cmd->argc) {
            ref_time = _wcstoi64(cmd->argv[++i], NULL, 10);
        }
    }

    SYSTEMTIME st;
    if (ref_time >= 0) {
        uint64_t ft64 = 116444736000000000ULL + (uint64_t)ref_time * 10000000ULL;
        FILETIME ft;
        ft.dwLowDateTime = (DWORD)(ft64 & 0xFFFFFFFF);
        ft.dwHighDateTime = (DWORD)(ft64 >> 32);
        if (utc) {
            FileTimeToSystemTime(&ft, &st);
        } else {
            FILETIME lft;
            FileTimeToLocalFileTime(&ft, &lft);
            FileTimeToSystemTime(&lft, &st);
        }
    } else {
        if (utc) GetSystemTime(&st);
        else GetLocalTime(&st);
    }

    if (operand) {
        bool parsed = inFmt ? DateParseFormatted(inFmt, operand, &st) : DateParseStamp(operand, &st);
        if (!parsed) {
            fwprintf(stderr, L"date: illegal time format\nusage: date [-jnRu] [-f input_fmt] [+output_fmt] [[[[[[cc]yy]mm]dd]HH]MM[.ss]]\n");
            *code = 1;
            return true;
        }
        if (!noSet) {
            DateEnableSetTimePrivilege();
            BOOL setOk = utc ? SetSystemTime(&st) : SetLocalTime(&st);
            if (!setOk) {
                fwprintf(stderr, L"date: cannot set date (error %lu)\n", GetLastError());
                *code = 1;
                return true;
            }
        }
    }

    if (rfc) {
        static const wchar_t* days[] = { L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat" };
        static const wchar_t* months[] = { L"", L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec" };
        wprintf(L"%ls, %02d %ls %04d %02d:%02d:%02d %ls\n",
                days[st.wDayOfWeek % 7], st.wDay, months[(st.wMonth >= 1 && st.wMonth <= 12) ? st.wMonth : 1],
                st.wYear, st.wHour, st.wMinute, st.wSecond, utc ? L"+0000" : L"-0000");
        *code = 0;
        return true;
    }

    if (iso) {
        wprintf(L"%04d-%02d-%02d\n", st.wYear, st.wMonth, st.wDay);
        *code = 0;
        return true;
    }

    if (!fmt) {
        static const wchar_t* days[] = { L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat" };
        static const wchar_t* months[] = { L"", L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec" };
        wprintf(L"%ls %ls %2d %02d:%02d:%02d %ls%04d\n",
                days[st.wDayOfWeek % 7], months[(st.wMonth >= 1 && st.wMonth <= 12) ? st.wMonth : 1],
                st.wDay, st.wHour, st.wMinute, st.wSecond, utc ? L"UTC " : L"", st.wYear);
        *code = 0;
        return true;
    }

    for (int j = 0; fmt[j]; j++) {
        if (fmt[j] == L'%' && fmt[j + 1]) {
            j++;
            switch (fmt[j]) {
                case L'Y': wprintf(L"%04d", st.wYear); break;
                case L'y': wprintf(L"%02d", st.wYear % 100); break;
                case L'm': wprintf(L"%02d", st.wMonth); break;
                case L'd': wprintf(L"%02d", st.wDay); break;
                case L'H': wprintf(L"%02d", st.wHour); break;
                case L'M': wprintf(L"%02d", st.wMinute); break;
                case L'S': wprintf(L"%02d", st.wSecond); break;
                case L'F': wprintf(L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay); break;
                case L'T': wprintf(L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond); break;
                case L'R': wprintf(L"%02d:%02d", st.wHour, st.wMinute); break;
                case L's': {
                    SYSTEMTIME ust = st;
                    if (!utc) TzSpecificLocalTimeToSystemTime(NULL, &st, &ust);
                    FILETIME ft;
                    SystemTimeToFileTime(&ust, &ft);
                    uint64_t t64 = (((uint64_t)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
                    int64_t sec = (int64_t)((t64 - 116444736000000000ULL) / 10000000ULL);
                    wprintf(L"%lld", sec);
                    break;
                }
                case L'a': {
                    static const wchar_t* d[] = { L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat" };
                    wprintf(L"%ls", d[st.wDayOfWeek % 7]);
                    break;
                }
                case L'A': {
                    static const wchar_t* d[] = { L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday" };
                    wprintf(L"%ls", d[st.wDayOfWeek % 7]);
                    break;
                }
                case L'b':
                case L'h': {
                    static const wchar_t* m[] = { L"", L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec" };
                    wprintf(L"%ls", m[(st.wMonth >= 1 && st.wMonth <= 12) ? st.wMonth : 1]);
                    break;
                }
                case L'B': {
                    static const wchar_t* m[] = { L"", L"January", L"February", L"March", L"April", L"May", L"June", L"July", L"August", L"September", L"October", L"November", L"December" };
                    wprintf(L"%ls", m[(st.wMonth >= 1 && st.wMonth <= 12) ? st.wMonth : 1]);
                    break;
                }
                case L'u': wprintf(L"%d", st.wDayOfWeek == 0 ? 7 : st.wDayOfWeek); break;
                case L'w': wprintf(L"%d", st.wDayOfWeek); break;
                case L'n': fputwc(L'\n', stdout); break;
                case L't': fputwc(L'\t', stdout); break;
                case L'%': fputwc(L'%', stdout); break;
                default: wprintf(L"%%%lc", fmt[j]); break;
            }
        } else {
            fputwc(fmt[j], stdout);
        }
    }
    fputwc(L'\n', stdout);
    *code = 0;
    return true;
}

bool Builtin_Date(CommandAST* cmd, int* code) {
    for (int i = 1; i < cmd->argc; i++) {
        if (cmd->argv[i][0] == L'+' || wcscmp(cmd->argv[i], L"-u") == 0 ||
            wcscmp(cmd->argv[i], L"--utc") == 0 || wcscmp(cmd->argv[i], L"-r") == 0 ||
            wcscmp(cmd->argv[i], L"-R") == 0 || wcsncmp(cmd->argv[i], L"-I", 2) == 0 ||
            wcscmp(cmd->argv[i], L"-j") == 0 || wcscmp(cmd->argv[i], L"-f") == 0 ||
            DateLooksLikeStamp(cmd->argv[i])) {
            return Builtin_Date_POSIX(cmd, code);
        }
    }
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Displays or sets the date.\n\nDATE [/T | date]\n      date [-u] [+FORMAT] [-r SECONDS] [-R] [-I]\n\nType DATE without parameters to display the current date setting and\na prompt for a new one.  Press ENTER to keep the same date.\n");
        *code = 0;
        return true;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/T") == 0) {
        wprintf(L"%02d/%02d/%04d\n", st.wMonth, st.wDay, st.wYear);
        *code = 0;
        return true;
    }
    if (cmd->argc > 1 && cmd->argv[1][0] != L'/') {
        int m = 0, d = 0, y = 0;
        if (swscanf_s(cmd->argv[1], L"%d-%d-%d", &m, &d, &y) == 3 ||
            swscanf_s(cmd->argv[1], L"%d/%d/%d", &m, &d, &y) == 3) {
            if (y < 100) y += 2000;
            st.wMonth = (WORD)m;
            st.wDay = (WORD)d;
            st.wYear = (WORD)y;
            if (SetLocalTime(&st)) {
                *code = 0;
                return true;
            }
        }
        fwprintf(stderr, L"The system cannot accept the date entered.\n");
        *code = 1;
        return true;
    }
    static const wchar_t* dayNames[] = { L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat" };
    wprintf(L"The current date is: %ls %02d/%02d/%04d\n",
        (st.wDayOfWeek < 7) ? dayNames[st.wDayOfWeek] : L"",
        st.wMonth, st.wDay, st.wYear);
    if (_isatty(_fileno(stdin))) {
        wprintf(L"Enter the new date: (mm-dd-yy) ");
        fflush(stdout);
        wchar_t buf[64];
        if (fgetws(buf, _countof(buf), stdin)) {
            int m = 0, d = 0, y = 0;
            if (swscanf_s(buf, L"%d-%d-%d", &m, &d, &y) == 3 ||
                swscanf_s(buf, L"%d/%d/%d", &m, &d, &y) == 3) {
                if (y < 100) y += 2000;
                st.wMonth = (WORD)m;
                st.wDay = (WORD)d;
                st.wYear = (WORD)y;
                SetLocalTime(&st);
            }
        }
    }
    *code = 0;
    return true;
}

bool Builtin_Time(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Displays or sets the system time, or times a command.\n\nTIME [/T | time]\nTIME [-p] [command [arguments...]]\n\nType TIME without parameters to display current time.\n");
        *code = 0;
        return true;
    }
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/T") == 0) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wprintf(L"%02d:%02d\n", st.wHour, st.wMinute);
        *code = 0;
        return true;
    }
    bool isPosixTime = false;
    int cmdIdx = 1;
    if (cmd->argc > 1) {
        if (wcscmp(cmd->argv[1], L"-p") == 0) {
            isPosixTime = true;
            cmdIdx = 2;
        } else if (cmd->argv[1][0] != L'/' && !wcschr(cmd->argv[1], L':')) {
            isPosixTime = true;
            cmdIdx = 1;
        }
    }
    if (isPosixTime && cmdIdx < cmd->argc) {
        LARGE_INTEGER freq, start, end;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        FILETIME createTime, exitTime, kernelStart, userStart, kernelEnd, userEnd;
        GetProcessTimes(GetCurrentProcess(), &createTime, &exitTime, &kernelStart, &userStart);

        wchar_t subcmd[MAX_LINE_LEN] = {0};
        for (int i = cmdIdx; i < cmd->argc; i++) {
            /* Arguments are already expanded; quote them so ExecuteLine can't expand them again. */
            if (!Ksh_AppendShellWord(subcmd, _countof(subcmd), cmd->argv[i])) {
                fwprintf(stderr, L"time: command too long\n");
                *code = 1;
                return true;
            }
        }
        int status = shell->ExecuteLine(shell, subcmd);
        QueryPerformanceCounter(&end);
        GetProcessTimes(GetCurrentProcess(), &createTime, &exitTime, &kernelEnd, &userEnd);

        double realSec = (double)(end.QuadPart - start.QuadPart) / (double)freq.QuadPart;
        ULARGE_INTEGER ku1, ku2, uu1, uu2;
        ku1.LowPart = kernelStart.dwLowDateTime; ku1.HighPart = kernelStart.dwHighDateTime;
        ku2.LowPart = kernelEnd.dwLowDateTime; ku2.HighPart = kernelEnd.dwHighDateTime;
        uu1.LowPart = userStart.dwLowDateTime; uu1.HighPart = userStart.dwHighDateTime;
        uu2.LowPart = userEnd.dwLowDateTime; uu2.HighPart = userEnd.dwHighDateTime;
        double sysSec = (double)(ku2.QuadPart - ku1.QuadPart) / 10000000.0;
        double userSec = (double)(uu2.QuadPart - uu1.QuadPart) / 10000000.0;
        fwprintf(stderr, L"real %0.2f\nuser %0.2f\nsys %0.2f\n", realSec, userSec, sysSec);
        *code = status;
        return true;
    }
    if (cmd->argc > 1 && wcschr(cmd->argv[1], L':')) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        int h = 0, m = 0, s = 0;
        if (swscanf_s(cmd->argv[1], L"%d:%d:%d", &h, &m, &s) >= 2) {
            st.wHour = (WORD)h;
            st.wMinute = (WORD)m;
            st.wSecond = (WORD)s;
            st.wMilliseconds = 0;
            if (SetLocalTime(&st)) {
                *code = 0;
                return true;
            }
        }
        fwprintf(stderr, L"The system cannot accept the time entered.\n");
        *code = 1;
        return true;
    }
    SYSTEMTIME st;
    GetLocalTime(&st);
    wprintf(L"The current time is: %02d:%02d:%02d.%02d\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds / 10);
    if (_isatty(_fileno(stdin))) {
        wprintf(L"Enter the new time: ");
        fflush(stdout);
        wchar_t buf[64];
        if (fgetws(buf, _countof(buf), stdin)) {
            int h = 0, m = 0, s = 0;
            if (swscanf_s(buf, L"%d:%d:%d", &h, &m, &s) >= 2) {
                st.wHour = (WORD)h;
                st.wMinute = (WORD)m;
                st.wSecond = (WORD)s;
                st.wMilliseconds = 0;
                SetLocalTime(&st);
            }
        }
    }
    *code = 0;
    return true;
}

bool Builtin_Prompt(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Changes the cmd.exe command prompt.\n\nPROMPT [text]\n\n  text    Specifies a new command prompt.\n\nPrompt can be made up of normal characters and the following special codes:\n  $P   Current drive and path\n  $G   > (greater-than sign)\n  $$   $ (dollar sign)\n\nType PROMPT without parameters to reset the prompt to the default setting.\n");
        *code = 0;
        return true;
    }
    if (cmd->argc == 1) {
        shell->env->Set(shell->env, L"PROMPT", L"$P$G");
    } else {
        wchar_t buf[MAX_LINE_LEN] = {0};
        for (int i = 1; i < cmd->argc; i++) {
            if (i > 1) wcscat_s(buf, _countof(buf), L" ");
            wcscat_s(buf, _countof(buf), cmd->argv[i]);
        }
        shell->env->Set(shell->env, L"PROMPT", buf);
    }
    *code = 0;
    return true;
}

bool Builtin_Doskey(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Edits command lines, recalls Windows commands, and creates macros.\n\nDOSKEY [/REINSTALL] [/MACROS] [/HISTORY] [macroname=[text]]\n");
        *code = 0;
        return true;
    }
    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* a = cmd->argv[i];
        if (_wcsicmp(a, L"/history") == 0) {
            continue;
        }
        if (_wcsicmp(a, L"/macros") == 0 || _wcsicmp(a, L"/macros:all") == 0) {
            for (ShellAlias* alias = shell->aliases; alias; alias = alias->next) {
                wprintf(L"%ls=%ls\n", alias->name, alias->value);
            }
            continue;
        }
        if (_wcsicmp(a, L"/reinstall") == 0) {
            while (shell->aliases) {
                ShellAlias* next = shell->aliases->next;
                free(shell->aliases->name);
                free(shell->aliases->value);
                free(shell->aliases);
                shell->aliases = next;
            }
            continue;
        }
        const wchar_t* eq = wcschr(a, L'=');
        if (eq) {
            wchar_t mname[128] = {0};
            size_t mlen = eq - a;
            if (mlen >= _countof(mname)) mlen = _countof(mname) - 1;
            wcsncpy_s(mname, _countof(mname), a, mlen);
            const wchar_t* mval = eq + 1;
            Shell_SetAlias(shell, mname, mval);
        }
    }
    *code = 0;
    return true;
}

bool Builtin_Mklink(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Creates a symbolic link.\n\nMKLINK [[/D] | [/H] | [/J]] Link Target\n\n        /D      Creates a directory symbolic link. Default is a file\n                symbolic link.\n        /H      Creates a hard link instead of a symbolic link.\n        /J      Creates a Directory Junction.\n        Link    Specifies the new symbolic link name.\n        Target  Specifies the path (relative or absolute) that the new link\n                refers to.\n");
        *code = 0;
        return true;
    }
    bool opt_d = false, opt_h = false, opt_j = false;
    int argIdx = 1;
    while (argIdx < cmd->argc && cmd->argv[argIdx][0] == L'/') {
        if (_wcsicmp(cmd->argv[argIdx], L"/d") == 0) opt_d = true;
        else if (_wcsicmp(cmd->argv[argIdx], L"/h") == 0) opt_h = true;
        else if (_wcsicmp(cmd->argv[argIdx], L"/j") == 0) opt_j = true;
        argIdx++;
    }
    if (argIdx + 1 >= cmd->argc) {
        fwprintf(stderr, L"The syntax of the command is incorrect.\n");
        *code = 1;
        return true;
    }
    const wchar_t* link = cmd->argv[argIdx];
    const wchar_t* target = cmd->argv[argIdx + 1];
    BOOL ok = FALSE;
    if (opt_h) {
        ok = CreateHardLinkW(link, target, NULL);
        if (ok) {
            wprintf(L"Hardlink created for %ls <<===>> %ls\n", link, target);
        }
    } else if (opt_d || opt_j) {
        DWORD flags = 1;
        ok = CreateSymbolicLinkW(link, target, flags);
        if (ok) {
            if (opt_j) wprintf(L"Junction created for %ls <<===>> %ls\n", link, target);
            else wprintf(L"symbolic link created for %ls <<===>> %ls\n", link, target);
        }
    } else {
        ok = CreateSymbolicLinkW(link, target, 0);
        if (ok) {
            wprintf(L"symbolic link created for %ls <<===>> %ls\n", link, target);
        }
    }
    if (!ok) {
        DWORD err = GetLastError();
        fwprintf(stderr, L"ksh: mklink: cannot create link (error %lu)\n", err);
        *code = 1;
    } else {
        *code = 0;
    }
    return true;
}



static bool WildcardMatch(const wchar_t* pat, const wchar_t* str, bool ignore_case) {
    if (!pat || !str) return false;
    while (*pat) {
        if (*pat == L'*') {
            pat++;
            if (!*pat) return true;
            while (*str) {
                if (WildcardMatch(pat, str, ignore_case)) return true;
                str++;
            }
            return false;
        } else if (*pat == L'?') {
            if (!*str) return false;
            pat++;
            str++;
        } else {
            wchar_t cp = ignore_case ? towlower(*pat) : *pat;
            wchar_t cs = ignore_case ? towlower(*str) : *str;
            if (cp != cs) return false;
            pat++;
            str++;
        }
    }
    return *str == L'\0';
}

typedef struct {
    const wchar_t* name_pattern;
    bool iname;
    wchar_t type_filter;
    int maxdepth;
    int mindepth;
    bool empty_only;
    int size_cmp;
    int64_t size_val;
    bool print0;
    bool do_delete;
} BsdFindContext;

static void WalkBsdFind(const wchar_t* current_path, int depth, const BsdFindContext* ctx) {
    DWORD attr = GetFileAttributesW(current_path);
    if (attr == INVALID_FILE_ATTRIBUTES) return;

    bool is_dir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    bool is_link = (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;

    const wchar_t* bname = wcsrchr(current_path, L'\\');
    const wchar_t* bname2 = wcsrchr(current_path, L'/');
    if (bname2 && (!bname || bname2 > bname)) bname = bname2;
    bname = bname ? bname + 1 : current_path;

    if (depth >= ctx->mindepth) {
        bool match = true;
        if (ctx->name_pattern && !WildcardMatch(ctx->name_pattern, bname, ctx->iname)) {
            match = false;
        }
        if (match && ctx->type_filter) {
            if (ctx->type_filter == L'd' && !is_dir) match = false;
            else if (ctx->type_filter == L'f' && is_dir) match = false;
            else if (ctx->type_filter == L'l' && !is_link) match = false;
        }
        if (match && ctx->empty_only) {
            if (is_dir) {
                wchar_t smask[KSH_PATH_MAX];
                _snwprintf_s(smask, _countof(smask), _TRUNCATE, L"%ls\\*", current_path);
                WIN32_FIND_DATAW fd;
                HANDLE h = FindFirstFileW(smask, &fd);
                bool has_any = false;
                if (h != INVALID_HANDLE_VALUE) {
                    do {
                        if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) {
                            has_any = true;
                            break;
                        }
                    } while (FindNextFileW(h, &fd));
                    FindClose(h);
                }
                if (has_any) match = false;
            } else {
                WIN32_FILE_ATTRIBUTE_DATA fad;
                if (GetFileAttributesExW(current_path, GetFileExInfoStandard, &fad)) {
                    if (fad.nFileSizeLow != 0 || fad.nFileSizeHigh != 0) match = false;
                }
            }
        }
        if (match && ctx->size_cmp != 0) {
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if (GetFileAttributesExW(current_path, GetFileExInfoStandard, &fad)) {
                int64_t fsz = (((int64_t)fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                if (ctx->size_cmp < 0 && !(fsz < ctx->size_val)) match = false;
                else if (ctx->size_cmp > 0 && !(fsz > ctx->size_val)) match = false;
                else if (ctx->size_cmp == 2 && !(fsz == ctx->size_val)) match = false;
            }
        }

        if (match) {
            if (ctx->do_delete) {
                if (is_dir) RemoveDirectoryW(current_path);
                else DeleteFileW(current_path);
            } else if (ctx->print0) {
                wprintf(L"%ls%lc", current_path, L'\0');
            } else {
                wprintf(L"%ls\n", current_path);
            }
        }
    }

    if (is_dir && !is_link && (ctx->maxdepth < 0 || depth < ctx->maxdepth)) {
        wchar_t smask[KSH_PATH_MAX];
        _snwprintf_s(smask, _countof(smask), _TRUNCATE, L"%ls\\*", current_path);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(smask, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                wchar_t child[KSH_PATH_MAX];
                _snwprintf_s(child, _countof(child), _TRUNCATE, L"%ls\\%ls", current_path, fd.cFileName);
                WalkBsdFind(child, depth + 1, ctx);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
}

bool Builtin_Find_BSD(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && wcscmp(cmd->argv[1], L"--help") == 0) {
        wprintf(L"Usage: find [path...] [expression]\n"
                L"Search for files in a directory hierarchy.\n\n"
                L"  -name PATTERN     Base of file name matches shell pattern\n"
                L"  -iname PATTERN    Like -name, but the match is case-insensitive\n"
                L"  -type [f|d|l]     File type (f: regular file, d: directory, l: symbolic link)\n"
                L"  -maxdepth LEVELS  Descend at most LEVELS directory levels\n"
                L"  -mindepth LEVELS  Do not apply any tests at levels less than LEVELS\n"
                L"  -empty            File is empty and is either a regular file or directory\n"
                L"  -size N[c|k|M]    File size comparison\n"
                L"  -delete           Delete files; true if removal succeeded\n"
                L"  -print            Print the full file name followed by a newline\n"
                L"  -print0           Print the full file name followed by a null character\n");
        *code = 0;
        return true;
    }

    BsdFindContext ctx;
    ctx.name_pattern = NULL;
    ctx.iname = false;
    ctx.type_filter = 0;
    ctx.maxdepth = -1;
    ctx.mindepth = 0;
    ctx.empty_only = false;
    ctx.size_cmp = 0;
    ctx.size_val = 0;
    ctx.print0 = false;
    ctx.do_delete = false;

    const wchar_t* paths[64];
    int path_count = 0;

    int i = 1;
    while (i < cmd->argc && cmd->argv[i][0] != L'-') {
        if (path_count < 64) paths[path_count++] = cmd->argv[i];
        i++;
    }

    if (path_count == 0) {
        paths[path_count++] = L".";
    }

    while (i < cmd->argc) {
        const wchar_t* arg = cmd->argv[i];
        if (wcscmp(arg, L"-name") == 0 && i + 1 < cmd->argc) {
            ctx.name_pattern = cmd->argv[++i];
            ctx.iname = false;
        } else if (wcscmp(arg, L"-iname") == 0 && i + 1 < cmd->argc) {
            ctx.name_pattern = cmd->argv[++i];
            ctx.iname = true;
        } else if (wcscmp(arg, L"-type") == 0 && i + 1 < cmd->argc) {
            ctx.type_filter = cmd->argv[++i][0];
        } else if (wcscmp(arg, L"-maxdepth") == 0 && i + 1 < cmd->argc) {
            ctx.maxdepth = (int)wcstol(cmd->argv[++i], NULL, 10);
        } else if (wcscmp(arg, L"-mindepth") == 0 && i + 1 < cmd->argc) {
            ctx.mindepth = (int)wcstol(cmd->argv[++i], NULL, 10);
        } else if (wcscmp(arg, L"-empty") == 0) {
            ctx.empty_only = true;
        } else if (wcscmp(arg, L"-delete") == 0) {
            ctx.do_delete = true;
        } else if (wcscmp(arg, L"-print0") == 0) {
            ctx.print0 = true;
        } else if (wcscmp(arg, L"-print") == 0) {
            /* default action */
        }
        i++;
    }

    for (int p = 0; p < path_count; p++) {
        WalkBsdFind(paths[p], 0, &ctx);
    }

    *code = 0;
    return true;
}

bool Builtin_Find(CommandAST* cmd, int* code) {
    bool has_cmd_switch = false;
    bool has_bsd_switch = false;
    for (int i = 1; i < cmd->argc; i++) {
        if (_wcsicmp(cmd->argv[i], L"/v") == 0 || _wcsicmp(cmd->argv[i], L"/c") == 0 ||
            _wcsicmp(cmd->argv[i], L"/n") == 0 || _wcsicmp(cmd->argv[i], L"/i") == 0 ||
            _wcsnicmp(cmd->argv[i], L"/off", 4) == 0 || _wcsicmp(cmd->argv[i], L"/?") == 0) {
            has_cmd_switch = true;
            break;
        }
        if (cmd->argv[i][0] == L'-' && cmd->argv[i][1] != L'\0') {
            has_bsd_switch = true;
            break;
        }
    }
    if (has_bsd_switch || (!has_cmd_switch && cmd->argc == 2 && (wcscmp(cmd->argv[1], L".") == 0 || wcscmp(cmd->argv[1], L"..") == 0))) {
        return Builtin_Find_BSD(cmd, code);
    }

    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Searches for a text string in a file or files.\n\nFIND [/V] [/C] [/N] [/I] [/OFF[LINE]] \"string\" [[drive:][path]filename[ ...]]\n\n  /V         Displays all lines NOT containing the specified string.\n  /C         Displays only the count of lines containing the string.\n  /N         Displays line numbers with the displayed lines.\n  /I         Ignores the case of characters when searching for the string.\n  \"string\"   Specifies the text string to find.\n  [drive:][path]filename\n             Specifies a file or files to search.\n");
        *code = 0;
        return true;
    }
    bool opt_v = false, opt_c = false, opt_n = false, opt_i = false;
    const wchar_t* pattern = NULL;
    int fileStart = -1;
    for (int i = 1; i < cmd->argc; i++) {
        if (_wcsicmp(cmd->argv[i], L"/v") == 0) opt_v = true;
        else if (_wcsicmp(cmd->argv[i], L"/c") == 0) opt_c = true;
        else if (_wcsicmp(cmd->argv[i], L"/n") == 0) opt_n = true;
        else if (_wcsicmp(cmd->argv[i], L"/i") == 0) opt_i = true;
        else if (_wcsnicmp(cmd->argv[i], L"/off", 4) == 0) { }
        else if (!pattern) {
            pattern = cmd->argv[i];
        } else {
            if (fileStart == -1) fileStart = i;
        }
    }
    if (!pattern) {
        fwprintf(stderr, L"FIND: Parameter format not correct\n");
        *code = 2;
        return true;
    }
    wchar_t cleanPattern[512];
    wcsncpy_s(cleanPattern, _countof(cleanPattern), pattern, _TRUNCATE);
    size_t plen = wcslen(cleanPattern);
    if (plen >= 2 && cleanPattern[0] == L'"' && cleanPattern[plen - 1] == L'"') {
        cleanPattern[plen - 1] = L'\0';
        wmemmove(cleanPattern, cleanPattern + 1, plen);
    }

    int matchTotal = 0;
    if (fileStart == -1) {
        wchar_t line[4096];
        int lineNum = 0;
        int count = 0;
        while (fgetws(line, _countof(line), stdin)) {
            lineNum++;
            size_t len = wcslen(line);
            if (len > 0 && line[len - 1] == L'\n') line[len - 1] = L'\0';
            if (len > 1 && line[len - 2] == L'\r') line[len - 2] = L'\0';
            bool match = StrContainsW(line, cleanPattern, opt_i);
            if (opt_v) match = !match;
            if (match) {
                count++;
                matchTotal++;
                if (!opt_c) {
                    if (opt_n) wprintf(L"[%d]%ls\n", lineNum, line);
                    else wprintf(L"%ls\n", line);
                }
            }
        }
        if (opt_c) wprintf(L"%d\n", count);
    } else {
        for (int fi = fileStart; fi < cmd->argc; fi++) {
            const wchar_t* fname = cmd->argv[fi];
            FILE* fp = NULL;
            if (_wfopen_s(&fp, fname, L"r, ccs=UTF-8") != 0 || !fp) {
                if (_wfopen_s(&fp, fname, L"r") != 0 || !fp) {
                    fwprintf(stderr, L"File not found - %ls\n", fname);
                    continue;
                }
            }
            wchar_t upperFname[MAX_PATH];
            wcsncpy_s(upperFname, _countof(upperFname), fname, _TRUNCATE);
            _wcsupr_s(upperFname, _countof(upperFname));
            wprintf(L"---------- %ls\n", upperFname);
            wchar_t line[4096];
            int lineNum = 0;
            int count = 0;
            while (fgetws(line, _countof(line), fp)) {
                lineNum++;
                size_t len = wcslen(line);
                if (len > 0 && line[len - 1] == L'\n') line[len - 1] = L'\0';
                if (len > 1 && line[len - 2] == L'\r') line[len - 2] = L'\0';
                bool match = StrContainsW(line, cleanPattern, opt_i);
                if (opt_v) match = !match;
                if (match) {
                    count++;
                    matchTotal++;
                    if (!opt_c) {
                        if (opt_n) wprintf(L"[%d]%ls\n", lineNum, line);
                        else wprintf(L"%ls\n", line);
                    }
                }
            }
            if (opt_c) wprintf(L"%d\n", count);
            fclose(fp);
        }
    }
    *code = (matchTotal > 0) ? 0 : 1;
    return true;
}

static void FindstrSearchFile(const wchar_t* path, const wchar_t* pattern,
    bool opt_b, bool opt_e, bool opt_i, bool opt_x, bool opt_v, bool opt_n,
    bool opt_m, bool opt_o, bool showFilename, int* matchTotal) {
    FILE* fp = NULL;
    if (_wfopen_s(&fp, path, L"r, ccs=UTF-8") != 0 || !fp) {
        if (_wfopen_s(&fp, path, L"r") != 0 || !fp) return;
    }
    wchar_t line[4096];
    int lineNum = 0;
    long charOffset = 0;
    size_t plen = wcslen(pattern);
    while (fgetws(line, _countof(line), fp)) {
        lineNum++;
        long currOffset = charOffset;
        charOffset += (long)wcslen(line);
        size_t len = wcslen(line);
        if (len > 0 && line[len - 1] == L'\n') line[len - 1] = L'\0';
        if (len > 1 && line[len - 2] == L'\r') line[len - 2] = L'\0';
        len = wcslen(line);

        bool match = false;
        if (opt_x) {
            match = (opt_i ? _wcsicmp(line, pattern) : wcscmp(line, pattern)) == 0;
        } else if (opt_b) {
            match = (opt_i ? _wcsnicmp(line, pattern, plen) : wcsncmp(line, pattern, plen)) == 0;
        } else if (opt_e) {
            if (len >= plen) {
                match = (opt_i ? _wcsicmp(line + (len - plen), pattern) : wcscmp(line + (len - plen), pattern)) == 0;
            }
        } else {
            match = StrContainsW(line, pattern, opt_i);
        }
        if (opt_v) match = !match;
        if (match) {
            (*matchTotal)++;
            if (opt_m) {
                wprintf(L"%ls\n", path);
                break;
            }
            if (showFilename) wprintf(L"%ls:", path);
            if (opt_n) wprintf(L"%d:", lineNum);
            if (opt_o) wprintf(L"%ld:", currOffset);
            wprintf(L"%ls\n", line);
        }
    }
    fclose(fp);
}

static void FindstrSearchDir(const wchar_t* dir, const wchar_t* mask, const wchar_t* pattern,
    bool opt_b, bool opt_e, bool opt_i, bool opt_x, bool opt_v, bool opt_n,
    bool opt_m, bool opt_o, bool opt_s, int* matchTotal) {
    wchar_t searchPath[MAX_PATH];
    swprintf_s(searchPath, _countof(searchPath), L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        wchar_t fullPath[MAX_PATH];
        swprintf_s(fullPath, _countof(fullPath), L"%ls\\%ls", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (opt_s) {
                FindstrSearchDir(fullPath, mask, pattern, opt_b, opt_e, opt_i, opt_x, opt_v, opt_n, opt_m, opt_o, opt_s, matchTotal);
            }
        } else {
            FindstrSearchFile(fullPath, pattern, opt_b, opt_e, opt_i, opt_x, opt_v, opt_n, opt_m, opt_o, true, matchTotal);
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

bool Builtin_Findstr(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Searches for strings in files.\n\nFINDSTR [/B] [/E] [/L] [/R] [/S] [/I] [/X] [/V] [/N] [/M] [/O] [/P] [/C:string] [strings] [[drive:][path]filename[ ...]]\n\n  /B         Matches pattern if at the beginning of a line.\n  /E         Matches pattern if at the end of a line.\n  /L         Uses search strings literally.\n  /R         Uses search strings as regular expressions.\n  /S         Searches for matching files in the current directory and all\n             subdirectories.\n  /I         Specifies that the search is not to be case-sensitive.\n  /X         Prints lines that match exactly.\n  /V         Prints only lines that do not contain a match.\n  /N         Prints the line number before each line that matches.\n  /M         Prints only the filename if a file contains a match.\n  /O         Prints character offset before each matching line.\n  /C:string  Uses specified string as a literal search string.\n");
        *code = 0;
        return true;
    }
    bool opt_b = false, opt_e = false, opt_s = false, opt_i = false;
    bool opt_x = false, opt_v = false, opt_n = false, opt_m = false, opt_o = false;
    const wchar_t* pattern = NULL;
    int fileStart = -1;
    for (int i = 1; i < cmd->argc; i++) {
        if (_wcsicmp(cmd->argv[i], L"/b") == 0) opt_b = true;
        else if (_wcsicmp(cmd->argv[i], L"/e") == 0) opt_e = true;
        else if (_wcsicmp(cmd->argv[i], L"/s") == 0) opt_s = true;
        else if (_wcsicmp(cmd->argv[i], L"/i") == 0) opt_i = true;
        else if (_wcsicmp(cmd->argv[i], L"/x") == 0) opt_x = true;
        else if (_wcsicmp(cmd->argv[i], L"/v") == 0) opt_v = true;
        else if (_wcsicmp(cmd->argv[i], L"/n") == 0) opt_n = true;
        else if (_wcsicmp(cmd->argv[i], L"/m") == 0) opt_m = true;
        else if (_wcsicmp(cmd->argv[i], L"/o") == 0) opt_o = true;
        else if (_wcsnicmp(cmd->argv[i], L"/c:", 3) == 0) {
            pattern = cmd->argv[i] + 3;
        } else if (!pattern) {
            pattern = cmd->argv[i];
        } else {
            if (fileStart == -1) fileStart = i;
        }
    }
    if (!pattern) {
        fwprintf(stderr, L"FINDSTR: Bad command line\n");
        *code = 2;
        return true;
    }
    wchar_t cleanPattern[512];
    wcsncpy_s(cleanPattern, _countof(cleanPattern), pattern, _TRUNCATE);
    size_t plen = wcslen(cleanPattern);
    if (plen >= 2 && cleanPattern[0] == L'"' && cleanPattern[plen - 1] == L'"') {
        cleanPattern[plen - 1] = L'\0';
        wmemmove(cleanPattern, cleanPattern + 1, plen);
    }
    int matchTotal = 0;
    if (fileStart == -1) {
        wchar_t line[4096];
        int lineNum = 0;
        long charOffset = 0;
        size_t cplen = wcslen(cleanPattern);
        while (fgetws(line, _countof(line), stdin)) {
            lineNum++;
            long currOffset = charOffset;
            charOffset += (long)wcslen(line);
            size_t len = wcslen(line);
            if (len > 0 && line[len - 1] == L'\n') line[len - 1] = L'\0';
            if (len > 1 && line[len - 2] == L'\r') line[len - 2] = L'\0';
            len = wcslen(line);
            bool match = false;
            if (opt_x) match = (opt_i ? _wcsicmp(line, cleanPattern) : wcscmp(line, cleanPattern)) == 0;
            else if (opt_b) match = (opt_i ? _wcsnicmp(line, cleanPattern, cplen) : wcsncmp(line, cleanPattern, cplen)) == 0;
            else if (opt_e) {
                if (len >= cplen) match = (opt_i ? _wcsicmp(line + (len - cplen), cleanPattern) : wcscmp(line + (len - cplen), cleanPattern)) == 0;
            } else match = StrContainsW(line, cleanPattern, opt_i);
            if (opt_v) match = !match;
            if (match) {
                matchTotal++;
                if (opt_n) wprintf(L"%d:", lineNum);
                if (opt_o) wprintf(L"%ld:", currOffset);
                wprintf(L"%ls\n", line);
            }
        }
    } else {
        bool multi = (cmd->argc - fileStart > 1) || opt_s;
        for (int fi = fileStart; fi < cmd->argc; fi++) {
            const wchar_t* fname = cmd->argv[fi];
            if (wcschr(fname, L'*') || wcschr(fname, L'?')) {
                FindstrSearchDir(L".", fname, cleanPattern, opt_b, opt_e, opt_i, opt_x, opt_v, opt_n, opt_m, opt_o, opt_s, &matchTotal);
            } else {
                FindstrSearchFile(fname, cleanPattern, opt_b, opt_e, opt_i, opt_x, opt_v, opt_n, opt_m, opt_o, multi, &matchTotal);
            }
        }
    }
    *code = (matchTotal > 0) ? 0 : 1;
    return true;
}

bool Builtin_FC(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Compares two files or sets of files and displays the differences between them\n\nFC [/A] [/B] [/C] [/L] [/LBn] [/N] [/OFF[LINE]] [/T] [/U] [/W] [/nnnn]\n   [drive1:][path1]filename1 [drive2:][path2]filename2\n\n  /A         Displays only first and last lines for each set of differences.\n  /B         Performs a binary comparison.\n  /C         Disregards the case of letters.\n  /L         Compares files as ASCII text.\n  /N         Displays the line numbers on an ASCII comparison.\n  /W         Compresses white space (tabs and spaces) for comparison.\n");
        *code = 0;
        return true;
    }
    bool isPosixFC = false;
    for (int i = 1; i < cmd->argc; i++) {
        if (cmd->argv[i][0] == L'-' && wcschr(L"elnrs", cmd->argv[i][1])) {
            isPosixFC = true;
            break;
        }
    }
    if (isPosixFC || cmd->argc == 1) {
        bool opt_l = false, opt_n = false, opt_r = false, opt_s = false;
        (void)opt_l;
        const wchar_t* opt_editor = NULL;
        int first_idx = -1, last_idx = -1;
        const wchar_t* sub_spec = NULL;

        for (int i = 1; i < cmd->argc; i++) {
            if (cmd->argv[i][0] == L'-') {
                if (wcschr(cmd->argv[i], L'l')) opt_l = true;
                if (wcschr(cmd->argv[i], L'n')) opt_n = true;
                if (wcschr(cmd->argv[i], L'r')) opt_r = true;
                if (wcschr(cmd->argv[i], L's')) opt_s = true;
                if (wcschr(cmd->argv[i], L'e') && i + 1 < cmd->argc) {
                    opt_editor = cmd->argv[++i];
                    if (wcscmp(opt_editor, L"-") == 0) opt_s = true;
                }
            } else if (wcschr(cmd->argv[i], L'=')) {
                sub_spec = cmd->argv[i];
            } else {
                int val = _wtoi(cmd->argv[i]);
                if (first_idx == -1) first_idx = val;
                else if (last_idx == -1) last_idx = val;
            }
        }
        if (g_history_count == 0) {
            wprintf(L"ksh: fc: history list is not active\n");
            *code = 0;
            return true;
        }

        /* Resolve indices */
        int start = (first_idx <= 0) ? (g_history_count > 16 ? g_history_count - 16 : 0) : first_idx - 1;
        int end = (last_idx <= 0) ? g_history_count - 1 : last_idx - 1;
        if (start < 0) start = 0;
        if (end >= g_history_count) end = g_history_count - 1;
        if (start > end) { int tmp = start; start = end; end = tmp; }

        if (opt_s) {
            /* Re-execute command with optional replacement */
            const wchar_t* target = g_history[g_history_count - 1];
            if (sub_spec) {
                const wchar_t* eq = wcschr(sub_spec, L'=');
                size_t oldLen = (size_t)(eq - sub_spec);
                wchar_t oldPat[128];
                wcsncpy_s(oldPat, _countof(oldPat), sub_spec, oldLen);
                const wchar_t* newPat = eq + 1;
                const wchar_t* found = wcsstr(target, oldPat);
                if (found) {
                    wchar_t repBuf[MAX_LINE_LEN];
                    size_t preLen = (size_t)(found - target);
                    wcsncpy_s(repBuf, _countof(repBuf), target, preLen);
                    wcscat_s(repBuf, _countof(repBuf), newPat);
                    wcscat_s(repBuf, _countof(repBuf), found + oldLen);
                    wprintf(L"%ls\n", repBuf);
                    *code = shell ? shell->ExecuteLine(shell, repBuf) : 0;
                    return true;
                }
            }
            wprintf(L"%ls\n", target);
            *code = shell ? shell->ExecuteLine(shell, target) : 0;
            return true;
        }

        if (opt_editor && shell) {
            /* Write commands to temp file and launch editor */
            wchar_t tempPath[MAX_PATH], tempFile[MAX_PATH];
            GetTempPathW(MAX_PATH, tempPath);
            GetTempFileNameW(tempPath, L"kfc", 0, tempFile);
            FILE* tf = NULL;
            if (_wfopen_s(&tf, tempFile, L"w, ccs=UTF-8") == 0 && tf) {
                for (int i = start; i <= end; i++) fwprintf(tf, L"%ls\n", g_history[i]);
                fclose(tf);
                wchar_t editCmd[MAX_LINE_LEN];
                swprintf_s(editCmd, _countof(editCmd), L"%ls \"%ls\"", opt_editor, tempFile);
                int editRes = shell->ExecuteLine(shell, editCmd);
                if (editRes == 0) {
                    shell->RunScript(shell, tempFile);
                }
                _wremove(tempFile);
                *code = editRes;
                return true;
            }
        }

        /* Default: list history */
        if (opt_r) {
            for (int i = end; i >= start; i--) {
                if (opt_n) wprintf(L"%ls\n", g_history[i]);
                else wprintf(L"%d\t%ls\n", i + 1, g_history[i]);
            }
        } else {
            for (int i = start; i <= end; i++) {
                if (opt_n) wprintf(L"%ls\n", g_history[i]);
                else wprintf(L"%d\t%ls\n", i + 1, g_history[i]);
            }
        }
        *code = 0;
        return true;
    }
    bool opt_b = false, opt_c = false, opt_n = false;
    const wchar_t* file1 = NULL;
    const wchar_t* file2 = NULL;
    for (int i = 1; i < cmd->argc; i++) {
        if (_wcsicmp(cmd->argv[i], L"/b") == 0) opt_b = true;
        else if (_wcsicmp(cmd->argv[i], L"/c") == 0) opt_c = true;
        else if (_wcsicmp(cmd->argv[i], L"/l") == 0) { }
        else if (_wcsicmp(cmd->argv[i], L"/n") == 0) opt_n = true;
        else if (_wcsicmp(cmd->argv[i], L"/w") == 0) { }
        else if (cmd->argv[i][0] == L'/') { }
        else if (!file1) file1 = cmd->argv[i];
        else if (!file2) file2 = cmd->argv[i];
    }
    if (!file1 || !file2) {
        fwprintf(stderr, L"FC: Insufficient number of file specifications\n");
        *code = 2;
        return true;
    }
    wprintf(L"Comparing files %ls and %ls\n", file1, file2);
    if (opt_b) {
        FILE *f1 = NULL, *f2 = NULL;
        if (_wfopen_s(&f1, file1, L"rb") != 0 || !f1) {
            fwprintf(stderr, L"FC: cannot open %ls - No such file or directory\n", file1);
            *code = 2; return true;
        }
        if (_wfopen_s(&f2, file2, L"rb") != 0 || !f2) {
            fclose(f1);
            fwprintf(stderr, L"FC: cannot open %ls - No such file or directory\n", file2);
            *code = 2; return true;
        }
        unsigned long offset = 0;
        int diffs = 0;
        while (1) {
            int b1 = fgetc(f1);
            int b2 = fgetc(f2);
            if (b1 == EOF && b2 == EOF) break;
            if (b1 != b2) {
                diffs++;
                if (b1 == EOF) { wprintf(L"%08lX: -- %02X\n", offset, b2); break; }
                else if (b2 == EOF) { wprintf(L"%08lX: %02X --\n", offset, b1); break; }
                else wprintf(L"%08lX: %02X %02X\n", offset, b1, b2);
            }
            offset++;
        }
        fclose(f1);
        fclose(f2);
        if (diffs == 0) {
            wprintf(L"FC: no differences encountered\n");
            *code = 0;
        } else {
            *code = 1;
        }
        return true;
    } else {
        FILE *f1 = NULL, *f2 = NULL;
        if (_wfopen_s(&f1, file1, L"r") != 0 || !f1) {
            fwprintf(stderr, L"FC: cannot open %ls - No such file or directory\n", file1);
            *code = 2; return true;
        }
        if (_wfopen_s(&f2, file2, L"r") != 0 || !f2) {
            fclose(f1);
            fwprintf(stderr, L"FC: cannot open %ls - No such file or directory\n", file2);
            *code = 2; return true;
        }
        wchar_t line1[4096], line2[4096];
        int lnum = 0;
        int diffs = 0;
        while (1) {
            wchar_t* r1 = fgetws(line1, _countof(line1), f1);
            wchar_t* r2 = fgetws(line2, _countof(line2), f2);
            if (!r1 && !r2) break;
            lnum++;
            bool match = false;
            if (r1 && r2) {
                size_t l1 = wcslen(line1); if (l1 && line1[l1-1] == L'\n') line1[l1-1] = L'\0';
                size_t l2 = wcslen(line2); if (l2 && line2[l2-1] == L'\n') line2[l2-1] = L'\0';
                match = (opt_c ? _wcsicmp(line1, line2) : wcscmp(line1, line2)) == 0;
            }
            if (!match) {
                diffs++;
                wprintf(L"***** %ls\n", file1);
                if (r1) { if (opt_n) wprintf(L"%5d:  %ls\n", lnum, line1); else wprintf(L"%ls\n", line1); }
                wprintf(L"***** %ls\n", file2);
                if (r2) { if (opt_n) wprintf(L"%5d:  %ls\n", lnum, line2); else wprintf(L"%ls\n", line2); }
                wprintf(L"*****\n\n");
            }
        }
        fclose(f1);
        fclose(f2);
        if (diffs == 0) {
            wprintf(L"FC: no differences encountered\n");
            *code = 0;
        } else {
            *code = 1;
        }
        return true;
    }
}

bool Builtin_Replace(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Replaces files.\n\nREPLACE [drive1:][path1]filename [drive2:][path2] [/A] [/P] [/R] [/S] [/U] [/W]\n\n  /A  Adds new files to destination directory instead of replacing existing ones.\n  /P  Prompts for confirmation before replacing a file or adding a source file.\n  /R  Replaces read-only files as well as unprotected files.\n  /S  Replaces files in all subdirectories of the destination directory.\n  /U  Updates (replaces) only files that are older than source files.\n  /W  Waits for you to insert a disk before beginning.\n");
        *code = 0;
        return true;
    }
    bool opt_a = false, opt_p = false, opt_r = false, opt_u = false;
    const wchar_t* src = NULL;
    const wchar_t* dst = NULL;
    for (int i = 1; i < cmd->argc; i++) {
        if (_wcsicmp(cmd->argv[i], L"/a") == 0) opt_a = true;
        else if (_wcsicmp(cmd->argv[i], L"/p") == 0) opt_p = true;
        else if (_wcsicmp(cmd->argv[i], L"/r") == 0) opt_r = true;
        else if (_wcsicmp(cmd->argv[i], L"/u") == 0) opt_u = true;
        else if (cmd->argv[i][0] == L'/') { }
        else if (!src) src = cmd->argv[i];
        else if (!dst) dst = cmd->argv[i];
    }
    if (!src) {
        fwprintf(stderr, L"Required parameter missing\n");
        *code = 2;
        return true;
    }
    wchar_t dstDir[MAX_PATH] = L".";
    if (dst) wcsncpy_s(dstDir, _countof(dstDir), dst, _TRUNCATE);

    wchar_t srcDir[MAX_PATH] = L".";
    const wchar_t* lastSlash = wcsrchr(src, L'\\');
    if (!lastSlash) lastSlash = wcsrchr(src, L'/');
    if (lastSlash) {
        size_t dlen = lastSlash - src;
        wcsncpy_s(srcDir, _countof(srcDir), src, dlen);
    }

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(src, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"No files found - %ls\n", src);
        *code = 1;
        return true;
    }
    int fileCount = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wchar_t targetPath[MAX_PATH];
        swprintf_s(targetPath, _countof(targetPath), L"%ls\\%ls", dstDir, fd.cFileName);
        wchar_t fullSrc[MAX_PATH];
        swprintf_s(fullSrc, _countof(fullSrc), L"%ls\\%ls", srcDir, fd.cFileName);

        WIN32_FIND_DATAW targetFd;
        HANDLE hTarget = FindFirstFileW(targetPath, &targetFd);
        bool dstExists = (hTarget != INVALID_HANDLE_VALUE);
        if (dstExists) FindClose(hTarget);

        if (opt_a && dstExists) continue;
        if (!opt_a && !dstExists) continue;
        if (opt_u && dstExists) {
            if (CompareFileTime(&fd.ftLastWriteTime, &targetFd.ftLastWriteTime) <= 0) continue;
        }

        if (opt_p) {
            wprintf(L"%ls %ls? (Y/N) ", opt_a ? L"Add" : L"Replace", targetPath);
            fflush(stdout);
            wchar_t ans[16];
            if (!fgetws(ans, _countof(ans), stdin) || (ans[0] != L'y' && ans[0] != L'Y')) continue;
        }
        if (dstExists && (targetFd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)) {
            if (opt_r) SetFileAttributesW(targetPath, targetFd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            else continue;
        }
        if (CopyFileW(fullSrc, targetPath, FALSE)) {
            wprintf(L"%ls %ls\n", opt_a ? L"Adding" : L"Replacing", targetPath);
            fileCount++;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    if (opt_a) wprintf(L"%d file(s) added\n", fileCount);
    else wprintf(L"%d file(s) replaced\n", fileCount);
    *code = 0;
    return true;
}

bool Builtin_Start(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Starts a separate window to run a specified program or command.\n\nSTART [\"title\"] [/D path] [/I] [/MIN] [/MAX] [/WAIT] [/B] [command/program] [parameters]\n\n  \"title\"     Title to display in the window title bar.\n  path        Starting directory.\n  /B          Start application without creating a new window.\n  /I          The new environment is the original environment passed to cmd.exe.\n  /MIN        Start window minimized.\n  /MAX        Start window maximized.\n  /WAIT       Start application and wait for it to terminate.\n  command     Specifies the command or program to run.\n  parameters  Specifies parameters to pass to the command or program.\n");
        *code = 0;
        return true;
    }
    const wchar_t* title = NULL;
    const wchar_t* workDir = NULL;
    bool opt_min = false, opt_max = false, opt_wait = false, opt_b = false;
    int argIdx = 1;
    if (argIdx < cmd->argc && cmd->argv[argIdx][0] == L'"') {
        title = cmd->argv[argIdx++];
    }
    while (argIdx < cmd->argc &&
           (cmd->argv[argIdx][0] == L'/' || _wcsicmp(cmd->argv[argIdx], L"-b") == 0)) {
        if (_wcsicmp(cmd->argv[argIdx], L"/min") == 0) opt_min = true;
        else if (_wcsicmp(cmd->argv[argIdx], L"/max") == 0) opt_max = true;
        else if (_wcsicmp(cmd->argv[argIdx], L"/wait") == 0) opt_wait = true;
        else if (_wcsicmp(cmd->argv[argIdx], L"/b") == 0 || _wcsicmp(cmd->argv[argIdx], L"-b") == 0) opt_b = true;
        else if (_wcsicmp(cmd->argv[argIdx], L"/d") == 0 && argIdx + 1 < cmd->argc) {
            workDir = cmd->argv[++argIdx];
        }
        argIdx++;
    }
    if (argIdx >= cmd->argc) {
        STARTUPINFOW si = { 0 };
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = { 0 };
        wchar_t shellPath[MAX_PATH];
        GetModuleFileNameW(NULL, shellPath, _countof(shellPath));
        if (CreateProcessW(shellPath, NULL, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, workDir, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        *code = 0;
        return true;
    }
    wchar_t* cmdline = BuildWin32CommandLine(cmd->argv + argIdx, cmd->argc - argIdx);
    if (!cmdline) {
        fwprintf(stderr, L"ksh: start: out of memory\n");
        *code = 1;
        return true;
    }
    STARTUPINFOW si = { 0 };
    si.cb = sizeof(si);
    if (title) {
        si.lpTitle = (LPWSTR)title;
    }
    if (opt_min || opt_max) {
        si.dwFlags |= STARTF_USESHOWWINDOW;
        si.wShowWindow = opt_min ? SW_SHOWMINIMIZED : SW_SHOWMAXIMIZED;
    }
    DWORD flags = opt_b ? 0 : CREATE_NEW_CONSOLE;
    PROCESS_INFORMATION pi = { 0 };
    if (CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, flags, NULL, workDir, &si, &pi)) {
        if (opt_wait) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD ec = 0;
            GetExitCodeProcess(pi.hProcess, &ec);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            *code = (int)ec;
        } else if (opt_b) {
            int jid = Shell_AddJob(pi.dwProcessId, pi.hProcess, cmdline);
            CloseHandle(pi.hThread);
            if (shell->interactive) wprintf(L"[%d] %lu\n", jid, pi.dwProcessId);
            *code = 0;
        } else {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            *code = 0;
        }
    } else {
        fwprintf(stderr, L"ksh: start: cannot start '%ls'\n", cmdline);
        *code = 1;
    }
    free(cmdline);
    return true;
}

static void TreePrintBranch(const wchar_t* prefix, const wchar_t* path, bool opt_f, bool opt_a, int depth) {
    if (depth > 64) return;
    wchar_t searchPath[MAX_PATH];
    swprintf_s(searchPath, _countof(searchPath), L"%ls\\*", path);
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    typedef struct TreeItem {
        wchar_t name[MAX_PATH];
        bool isDir;
    } TreeItem;
    TreeItem* items = (TreeItem*)malloc(512 * sizeof(TreeItem));
    if (!items) {
        FindClose(hFind);
        return;
    }
    int count = 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!isDir && !opt_f) continue;
        if (count < 512) {
            wcsncpy_s(items[count].name, _countof(items[count].name), fd.cFileName, _TRUNCATE);
            items[count].isDir = isDir;
            count++;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    for (int i = 0; i < count; i++) {
        bool isLast = (i == count - 1);
        const wchar_t* connector = opt_a ? (isLast ? L"\\---" : L"+---") : (isLast ? L"└───" : L"├───");
        wprintf(L"%ls%ls%ls\n", prefix, connector, items[i].name);
        if (items[i].isDir && depth < 64) {
            wchar_t subPath[MAX_PATH];
            swprintf_s(subPath, _countof(subPath), L"%ls\\%ls", path, items[i].name);
            wchar_t nextPrefix[MAX_PATH];
            const wchar_t* ext = opt_a ? (isLast ? L"    " : L"|   ") : (isLast ? L"    " : L"│   ");
            swprintf_s(nextPrefix, _countof(nextPrefix), L"%ls%ls", prefix, ext);
            TreePrintBranch(nextPrefix, subPath, opt_f, opt_a, depth + 1);
        }
    }
    free(items);
}

bool Builtin_Tree(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Graphically displays the folder structure of a drive or path.\n\nTREE [drive:][path] [/F] [/A]\n\n   /F   Display the names of the files in each folder.\n   /A   Use ASCII instead of extended characters.\n");
        *code = 0;
        return true;
    }
    bool opt_f = false, opt_a = false;
    const wchar_t* targetPath = L".";
    for (int i = 1; i < cmd->argc; i++) {
        if (_wcsicmp(cmd->argv[i], L"/f") == 0) opt_f = true;
        else if (_wcsicmp(cmd->argv[i], L"/a") == 0) opt_a = true;
        else if (cmd->argv[i][0] != L'/') targetPath = cmd->argv[i];
    }
    wchar_t fullPath[MAX_PATH];
    if (_wfullpath(fullPath, targetPath, _countof(fullPath)) == NULL) {
        wcsncpy_s(fullPath, _countof(fullPath), targetPath, _TRUNCATE);
    }
    wchar_t rootPath[4] = { fullPath[0], L':', L'\\', L'\0' };
    wchar_t volumeName[MAX_PATH] = { 0 };
    DWORD serial = 0;
    GetVolumeInformationW(rootPath, volumeName, _countof(volumeName), &serial, NULL, NULL, NULL, 0);
    wprintf(L"Folder PATH listing for volume %ls\n", volumeName[0] ? volumeName : L"");
    wprintf(L"Volume serial number is %04X-%04X\n", (unsigned int)(serial >> 16), (unsigned int)(serial & 0xffff));
    wprintf(L"%ls\n", fullPath);
    TreePrintBranch(L"", fullPath, opt_f, opt_a, 0);
    *code = 0;
    return true;
}

bool Builtin_Copy(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Copies one or more files to another location.\n\nCOPY [/D] [/V] [/N] [/Y | /-Y] [/Z] [/A | /B ] source [/A | /B]\n     [+ source [/A | /B] [+ ...]] [destination [/A | /B]]\n\n  source       Specifies the file or files to be copied.\n  /A           Indicates an ASCII text file.\n  /B           Indicates a binary file.\n  /D           Allows the destination file to be created decrypted\n  destination  Specifies the directory and/or filename for the new file(s).\n  /V           Verifies that new files are written correctly.\n  /N           Uses short filename, if available, when copying a file with a\n               non-8dot3 name.\n  /Y           Suppresses prompting to confirm you want to overwrite an\n               existing destination file.\n  /-Y          Causes prompting to confirm you want to overwrite an\n               existing destination file.\n  /Z           Copies networked files in restartable mode.\n");
        *code = 0;
        return true;
    }
    wchar_t rest[MAX_LINE_LEN] = {0};
    for (int i = 1; i < cmd->argc; i++) {
        if (i > 1) wcscat_s(rest, _countof(rest), L" ");
        wcscat_s(rest, _countof(rest), cmd->argv[i]);
    }
    *code = Batch_CopyMove(shell, rest, false);
    return true;
}

bool Builtin_Move(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Moves files and renames files and directories.\n\nTo move one or more files:\nMOVE [/Y | /-Y] [drive:][path]filename1[,...] destination\n\nTo rename a directory:\nMOVE [/Y | /-Y] [drive:][path]dirname1 dirname2\n\n  [drive:][path]filename1  Specifies the location and name of the file\n                           or files you want to move.\n  destination              Specifies the new location of the file. Destination\n                           can consist of a drive letter and colon, a\n                           directory name, or a combination.\n  /Y                       Suppresses prompting to confirm you want to\n                           overwrite an existing destination file.\n  /-Y                      Causes prompting to confirm you want to overwrite\n                           an existing destination file.\n");
        *code = 0;
        return true;
    }
    wchar_t rest[MAX_LINE_LEN] = {0};
    for (int i = 1; i < cmd->argc; i++) {
        if (i > 1) wcscat_s(rest, _countof(rest), L" ");
        wcscat_s(rest, _countof(rest), cmd->argv[i]);
    }
    *code = Batch_CopyMove(shell, rest, true);
    return true;
}

bool Builtin_Del(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Deletes one or more files.\n\nDEL [/P] [/F] [/S] [/Q] [/A[[:]attributes]] names\nERASE [/P] [/F] [/S] [/Q] [/A[[:]attributes]] names\n\n  names         Specifies a list of one or more files or patterns.\n  /P            Prompts for confirmation before deleting each file.\n  /F            Force deleting of read-only files.\n  /S            Delete specified files from all subdirectories.\n  /Q            Quiet mode, do not ask if ok to delete on global wildcard\n  /A            Selects files to be deleted based on attributes\n");
        *code = 0;
        return true;
    }
    wchar_t rest[MAX_LINE_LEN] = {0};
    for (int i = 1; i < cmd->argc; i++) {
        if (i > 1) wcscat_s(rest, _countof(rest), L" ");
        wcscat_s(rest, _countof(rest), cmd->argv[i]);
    }
    *code = Batch_DeleteFiles(rest);
    return true;
}

bool Builtin_Md(CommandAST* cmd, int* code) {
    if (cmd->argc < 2) {
        fwprintf(stderr, L"The syntax of the command is incorrect.\n");
        *code = 1;
        return true;
    }
    if (_wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Creates a directory.\n\nMKDIR [drive:]path\nMD [drive:]path\n");
        *code = 0;
        return true;
    }
    bool p_flag = false;
    for (int i = 1; i < cmd->argc; i++) {
        if (_wcsicmp(cmd->argv[i], L"-p") == 0 || _wcsicmp(cmd->argv[i], L"--parents") == 0) p_flag = true;
    }
    int overall = 0;
    for (int i = 1; i < cmd->argc; i++) {
        if (_wcsicmp(cmd->argv[i], L"-p") == 0 || _wcsicmp(cmd->argv[i], L"--parents") == 0) continue;
        if (p_flag) {
            DWORD attr = GetFileAttributesW(cmd->argv[i]);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
        }
        if (!Batch_CreateDirectoryPath(cmd->argv[i])) {
            DWORD attr = GetFileAttributesW(cmd->argv[i]);
            if (p_flag && attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
            fwprintf(stderr, L"ksh: md: cannot create directory %ls\n", cmd->argv[i]);
            overall = 1;
        }
    }
    *code = overall;
    return true;
}

bool Builtin_Rd(CommandAST* cmd, int* code) {
    if (cmd->argc < 2) {
        fwprintf(stderr, L"The syntax of the command is incorrect.\n");
        *code = 1;
        return true;
    }
    bool recursive = false, quiet = false;
    const wchar_t* directory = NULL;
    for (int i = 1; i < cmd->argc; i++) {
        if (_wcsicmp(cmd->argv[i], L"/s") == 0) recursive = true;
        else if (_wcsicmp(cmd->argv[i], L"/q") == 0) quiet = true;
        else if (_wcsicmp(cmd->argv[i], L"/?") == 0) {
            wprintf(L"Removes (deletes) a directory.\n\nRMDIR [/S] [/Q] [drive:]path\nRD [/S] [/Q] [drive:]path\n\n    /S      Removes all directories and files in the specified directory\n            in addition to the directory itself.  Used to remove a directory\n            tree.\n    /Q      Quiet mode, do not ask if ok to remove a directory tree with /S\n");
            *code = 0;
            return true;
        } else if (!directory) {
            directory = cmd->argv[i];
        }
    }
    if (!directory) {
        *code = 2;
        return true;
    }
    if (!recursive) {
        *code = RemoveDirectoryW(directory) ? 0 : 1;
        return true;
    }
    if (Batch_IsProtectedDeleteTarget(directory)) {
        fwprintf(stderr, L"ksh: rd: refusing to recursively remove a volume root or current directory\n");
        *code = 1;
        return true;
    }
    if (!quiet && !Batch_ConfirmRecursiveDelete(directory)) {
        *code = 1;
        return true;
    }
    *code = Batch_DeleteTree(directory) ? 0 : 1;
    return true;
}

bool Builtin_Ren(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Renames a file or files.\n\nRENAME [drive:][path]filename1 filename2\nREN [drive:][path]filename1 filename2\n\nNote that you cannot specify a new drive or path for your destination file.\n");
        *code = 0;
        return true;
    }
    if (cmd->argc != 3) {
        fwprintf(stderr, L"The syntax of the command is incorrect.\n");
        *code = 1;
        return true;
    }
    const wchar_t* src = cmd->argv[1];
    const wchar_t* dst = cmd->argv[2];
    wchar_t fullDst[MAX_PATH];
    const wchar_t* lastSlash = wcsrchr(src, L'\\');
    if (!lastSlash) lastSlash = wcsrchr(src, L'/');
    if (lastSlash && !wcschr(dst, L'\\') && !wcschr(dst, L'/')) {
        size_t dlen = lastSlash - src;
        wcsncpy_s(fullDst, _countof(fullDst), src, dlen);
        fullDst[dlen] = L'\\';
        fullDst[dlen + 1] = L'\0';
        wcscat_s(fullDst, _countof(fullDst), dst);
        dst = fullDst;
    }
    *code = MoveFileW(src, dst) ? 0 : 1;
    return true;
}

bool Builtin_Ver(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Displays the Windows version.\n\nVER\n");
        *code = 0;
        return true;
    }
    typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFn getVersion = ntdll ?
        (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion") : NULL;
    OSVERSIONINFOW version = { 0 };
    version.dwOSVersionInfoSize = sizeof(version);
    if (!getVersion || getVersion(&version) != 0) {
        *code = 1;
        return true;
    }
    wprintf(L"Microsoft Windows [Version %lu.%lu.%lu]\n",
        version.dwMajorVersion, version.dwMinorVersion,
        version.dwBuildNumber);
    *code = 0;
    return true;
}

bool Builtin_Version(CommandAST* cmd, int* code) {
    (void)cmd;
    wprintf(L"%ls\n", NTKSH_VERSION);
    *code = 0;
    return true;
}

bool Builtin_Verify(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Tells cmd.exe whether to verify that your files are written correctly to a disk.\n\nVERIFY [ON | OFF]\n");
        *code = 0;
        return true;
    }
    if (cmd->argc == 1) {
        wprintf(L"VERIFY is %ls.\n", shell->batchVerify ? L"on" : L"off");
        *code = 0;
        return true;
    }
    if (_wcsicmp(cmd->argv[1], L"on") == 0) {
        shell->batchVerify = true;
        wprintf(L"VERIFY is on.\n");
        *code = 0;
    } else if (_wcsicmp(cmd->argv[1], L"off") == 0) {
        shell->batchVerify = false;
        wprintf(L"VERIFY is off.\n");
        *code = 0;
    } else {
        fwprintf(stderr, L"ksh: verify: usage: VERIFY [ON | OFF]\n");
        *code = 2;
    }
    return true;
}

bool Builtin_Path(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Displays or sets a search path for executable files.\n\nPATH [[drive:]path[;...][;%PATH%]]\nPATH ;\n\nType PATH ; to clear all search-path settings.\nType PATH without parameters to display the current path.\n");
        *code = 0;
        return true;
    }
    if (cmd->argc == 1) {
        wchar_t* path = GetEnvironmentValue(L"PATH");
        wprintf(L"PATH=%ls\n", path ? path : L"");
        free(path);
        *code = 0;
        return true;
    }
    if (cmd->argc == 2 && wcscmp(cmd->argv[1], L";") == 0) {
        shell->env->Set(shell->env, L"PATH", L"");
        *code = 0;
        return true;
    }
    wchar_t buf[MAX_LINE_LEN] = {0};
    for (int i = 1; i < cmd->argc; i++) {
        if (i > 1) wcscat_s(buf, _countof(buf), L" ");
        wcscat_s(buf, _countof(buf), cmd->argv[i]);
    }
    shell->env->Set(shell->env, L"PATH", buf);
    *code = 0;
    return true;
}

bool Builtin_Dirs(IShell* shell, CommandAST* cmd, int* code) {
    bool opt_c = false, opt_p = false, opt_v = false;
    for (int i = 1; i < cmd->argc; i++) {
        if (wcscmp(cmd->argv[i], L"-c") == 0) opt_c = true;
        else if (wcscmp(cmd->argv[i], L"-l") == 0) { }
        else if (wcscmp(cmd->argv[i], L"-p") == 0) opt_p = true;
        else if (wcscmp(cmd->argv[i], L"-v") == 0) opt_v = true;
    }
    if (opt_c) {
        DirectoryStackEntry* cur = shell->directoryStack;
        while (cur) {
            DirectoryStackEntry* n = cur->next;
            free(cur->path);
            free(cur);
            cur = n;
        }
        shell->directoryStack = NULL;
        *code = 0;
        return true;
    }
    wchar_t* cwd = GetCurrentDirectoryAlloc();
    if (opt_v) {
        int idx = 0;
        wprintf(L"%d\t%ls\n", idx++, cwd ? cwd : L"");
        for (DirectoryStackEntry* e = shell->directoryStack; e; e = e->next) {
            wprintf(L"%d\t%ls\n", idx++, e->path);
        }
    } else if (opt_p) {
        wprintf(L"%ls\n", cwd ? cwd : L"");
        for (DirectoryStackEntry* e = shell->directoryStack; e; e = e->next) {
            wprintf(L"%ls\n", e->path);
        }
    } else {
        wprintf(L"%ls", cwd ? cwd : L"");
        for (DirectoryStackEntry* e = shell->directoryStack; e; e = e->next) {
            wprintf(L" %ls", e->path);
        }
        wprintf(L"\n");
    }
    free(cwd);
    *code = 0;
    return true;
}

bool Builtin_Sleep(CommandAST* cmd, int* code) {
    if (cmd->argc < 2) {
        fwprintf(stderr, L"ksh: sleep: missing operand\n");
        *code = 1;
        return true;
    }
    double sec = _wtof(cmd->argv[1]);
    if (sec < 0) {
        fwprintf(stderr, L"ksh: sleep: invalid interval '%ls'\n", cmd->argv[1]);
        *code = 1;
        return true;
    }
    DWORD ms = (DWORD)(sec * 1000.0);
    Sleep(ms);
    *code = 0;
    return true;
}

bool Builtin_History(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    if (cmd->argc > 1 && wcscmp(cmd->argv[1], L"-c") == 0) {
        for (int i = 0; i < g_history_count; i++) {
            if (g_history[i]) free(g_history[i]);
            g_history[i] = NULL;
        }
        g_history_count = 0;
        *code = 0;
        return true;
    }
    CommandAST fcCmd = {0};
    fcCmd.argc = 2;
    fcCmd.argv[0] = L"fc";
    fcCmd.argv[1] = L"-l";
    return Builtin_FC(shell, &fcCmd, code);
}

bool Builtin_Disown(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    Shell_UpdateJobs();
    bool opt_a = false, opt_r = false, opt_h = false;
    int first_job_arg = 0;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* a = cmd->argv[i];
        if (a[0] == L'-' && a[1] != L'\0') {
            for (int j = 1; a[j]; j++) {
                if (a[j] == L'a') opt_a = true;
                else if (a[j] == L'r') opt_r = true;
                else if (a[j] == L'h') opt_h = true;
                else {
                    fwprintf(stderr, L"ksh: disown: -%c: unknown option\n", a[j]);
                    *code = 1;
                    return true;
                }
            }
        } else {
            if (first_job_arg == 0) first_job_arg = i;
        }
    }

    *code = 0;
    if (first_job_arg == 0) {
        if (opt_a) {
            for (int i = 0; i < g_job_count; i++) {
                if (!g_jobs[i].running && !g_jobs[i].stopped && g_jobs[i].hProcess == NULL) continue;
                if (opt_r && !g_jobs[i].running) continue;
                if (opt_h) {
                    g_jobs[i].disowned = true;
                } else {
                    if (g_jobs[i].hProcess) {
                        CloseHandle(g_jobs[i].hProcess);
                        g_jobs[i].hProcess = NULL;
                    }
                    g_jobs[i].running = false;
                    g_jobs[i].stopped = false;
                    g_jobs[i].pid = 0;
                    g_jobs[i].command[0] = L'\0';
                }
            }
        } else {
            ShellJob* job = ResolveJobSpec(L"%%");
            if (!job) {
                fwprintf(stderr, L"ksh: disown: no current job\n");
                *code = 1;
                return true;
            }
            if (opt_r && !job->running) {
                *code = 0;
                return true;
            }
            if (opt_h) {
                job->disowned = true;
            } else {
                if (job->hProcess) {
                    CloseHandle(job->hProcess);
                    job->hProcess = NULL;
                }
                job->running = false;
                job->stopped = false;
                job->pid = 0;
                job->command[0] = L'\0';
            }
        }
        return true;
    }

    for (int i = first_job_arg; i < cmd->argc; i++) {
        const wchar_t* spec = cmd->argv[i];
        if (spec[0] == L'-') continue;
        ShellJob* job = ResolveJobSpec(spec);
        if (!job) {
            fwprintf(stderr, L"ksh: disown: %ls: no such job\n", spec);
            *code = 1;
            continue;
        }
        if (opt_r && !job->running) continue;
        if (opt_h) {
            job->disowned = true;
        } else {
            if (job->hProcess) {
                CloseHandle(job->hProcess);
                job->hProcess = NULL;
            }
            job->running = false;
            job->stopped = false;
            job->pid = 0;
            job->command[0] = L'\0';
        }
    }
    return true;
}

bool Builtin_Integer(IShell* shell, CommandAST* cmd, int* code) {
    CommandAST tsCmd = *cmd;
    wchar_t* newArgv[MAX_ARGS];
    newArgv[0] = L"typeset";
    newArgv[1] = L"-i";
    for (int i = 1; i < cmd->argc && i + 1 < MAX_ARGS; i++) {
        newArgv[i + 1] = cmd->argv[i];
    }
    tsCmd.argc = cmd->argc + 1;
    for (int i = 0; i < tsCmd.argc; i++) tsCmd.argv[i] = newArgv[i];
    return Builtin_Typeset(shell, &tsCmd, code);
}

bool Builtin_Float(IShell* shell, CommandAST* cmd, int* code) {
    CommandAST tsCmd = *cmd;
    wchar_t* newArgv[MAX_ARGS];
    newArgv[0] = L"typeset";
    newArgv[1] = L"-E";
    for (int i = 1; i < cmd->argc && i + 1 < MAX_ARGS; i++) {
        newArgv[i + 1] = cmd->argv[i];
    }
    tsCmd.argc = cmd->argc + 1;
    for (int i = 0; i < tsCmd.argc; i++) tsCmd.argv[i] = newArgv[i];
    return Builtin_Typeset(shell, &tsCmd, code);
}

bool Builtin_Functions(IShell* shell, CommandAST* cmd, int* code) {
    CommandAST tsCmd = *cmd;
    wchar_t* newArgv[MAX_ARGS];
    newArgv[0] = L"typeset";
    newArgv[1] = L"-f";
    for (int i = 1; i < cmd->argc && i + 1 < MAX_ARGS; i++) {
        newArgv[i + 1] = cmd->argv[i];
    }
    tsCmd.argc = cmd->argc + 1;
    for (int i = 0; i < tsCmd.argc; i++) tsCmd.argv[i] = newArgv[i];
    return Builtin_Typeset(shell, &tsCmd, code);
}

bool Builtin_Autoload(IShell* shell, CommandAST* cmd, int* code) {
    CommandAST tsCmd = *cmd;
    wchar_t* newArgv[MAX_ARGS];
    newArgv[0] = L"typeset";
    newArgv[1] = L"-fu";
    for (int i = 1; i < cmd->argc && i + 1 < MAX_ARGS; i++) {
        newArgv[i + 1] = cmd->argv[i];
    }
    tsCmd.argc = cmd->argc + 1;
    for (int i = 0; i < tsCmd.argc; i++) tsCmd.argv[i] = newArgv[i];
    return Builtin_Typeset(shell, &tsCmd, code);
}

bool Builtin_Nameref(IShell* shell, CommandAST* cmd, int* code) {
    CommandAST tsCmd = *cmd;
    wchar_t* newArgv[MAX_ARGS];
    newArgv[0] = L"typeset";
    newArgv[1] = L"-n";
    for (int i = 1; i < cmd->argc && i + 1 < MAX_ARGS; i++) {
        newArgv[i + 1] = cmd->argv[i];
    }
    tsCmd.argc = cmd->argc + 1;
    for (int i = 0; i < tsCmd.argc; i++) tsCmd.argv[i] = newArgv[i];
    return Builtin_Typeset(shell, &tsCmd, code);
}

bool Builtin_Colon(CommandAST* cmd, int* code) {
    (void)cmd;
    *code = 0;
    return true;
}

bool Builtin_True(CommandAST* cmd, int* code) {
    (void)cmd;
    *code = 0;
    return true;
}

bool Builtin_False(CommandAST* cmd, int* code) {
    (void)cmd;
    *code = 1;
    return true;
}

bool Builtin_Hist(IShell* shell, CommandAST* cmd, int* code) {
    CommandAST fcCmd = *cmd;
    wchar_t* newArgv[MAX_ARGS];
    newArgv[0] = L"fc";
    newArgv[1] = L"-l";
    for (int i = 1; i < cmd->argc && i + 1 < MAX_ARGS; i++) {
        newArgv[i + 1] = cmd->argv[i];
    }
    fcCmd.argc = cmd->argc + 1;
    for (int i = 0; i < fcCmd.argc; i++) fcCmd.argv[i] = newArgv[i];
    return Builtin_FC(shell, &fcCmd, code);
}

bool Builtin_R(IShell* shell, CommandAST* cmd, int* code) {
    if (g_history_count == 0) {
        fwprintf(stderr, L"ksh: r: no previous command\n");
        *code = 1;
        return true;
    }
    const wchar_t* old_pat = NULL;
    const wchar_t* new_pat = NULL;
    wchar_t pat_buf[MAX_LINE_LEN] = {0};
    const wchar_t* target_spec = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* eq = wcschr(cmd->argv[i], L'=');
        if (eq && !old_pat) {
            size_t nlen = (size_t)(eq - cmd->argv[i]);
            if (nlen >= MAX_LINE_LEN) nlen = MAX_LINE_LEN - 1;
            wcsncpy_s(pat_buf, MAX_LINE_LEN, cmd->argv[i], nlen);
            old_pat = pat_buf;
            new_pat = eq + 1;
        } else if (!target_spec) {
            target_spec = cmd->argv[i];
        }
    }

    int found_idx = -1;
    if (target_spec) {
        wchar_t* endp = NULL;
        long num = wcstol(target_spec, &endp, 10);
        if (endp && *endp == L'\0') {
            if (num < 0) {
                int idx = g_history_count + (int)num;
                if (idx >= 0 && idx < g_history_count) found_idx = idx;
            } else if (num > 0 && num <= g_history_count) {
                found_idx = (int)num - 1;
            }
        } else {
            size_t plen = wcslen(target_spec);
            for (int i = g_history_count - 1; i >= 0; i--) {
                if (wcsncmp(g_history[i], target_spec, plen) == 0) {
                    found_idx = i;
                    break;
                }
            }
        }
    } else {
        found_idx = g_history_count - 1;
    }

    if (found_idx < 0 || found_idx >= g_history_count) {
        fwprintf(stderr, L"ksh: r: command not found in history\n");
        *code = 1;
        return true;
    }

    const wchar_t* src_cmd = g_history[found_idx];
    wchar_t expanded[MAX_LINE_LEN] = {0};
    if (old_pat && new_pat) {
        const wchar_t* match = wcsstr(src_cmd, old_pat);
        if (match) {
            size_t prefix_len = (size_t)(match - src_cmd);
            wcsncpy_s(expanded, MAX_LINE_LEN, src_cmd, prefix_len);
            wcscat_s(expanded, MAX_LINE_LEN, new_pat);
            wcscat_s(expanded, MAX_LINE_LEN, match + wcslen(old_pat));
        } else {
            wcscpy_s(expanded, MAX_LINE_LEN, src_cmd);
        }
    } else {
        wcscpy_s(expanded, MAX_LINE_LEN, src_cmd);
    }

    wprintf(L"%ls\n", expanded);
    fflush(stdout);
    History_Add(expanded);
    *code = shell->ExecuteLine(shell, expanded);
    return true;
}

bool Builtin_Basename(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Strip directory and suffix from filenames.\n\n"
                L"basename string [suffix]\n"
                L"basename [-a] [-z] [-s suffix] string...\n\n"
                L"  -a, --multiple       Support multiple arguments\n"
                L"  -s, --suffix=SUFFIX  Remove a trailing SUFFIX\n"
                L"  -z, --zero           End each output line with NUL, not newline\n");
        *code = 0;
        return true;
    }
    if (cmd->argc == 1) {
        fwprintf(stderr, L"ksh: basename: missing operand\n");
        *code = 1;
        return true;
    }
    bool multiple = false;
    bool zero_delim = false;
    const wchar_t* suffix = NULL;
    int first = 1;
    while (first < cmd->argc) {
        if (wcscmp(cmd->argv[first], L"-a") == 0 || wcscmp(cmd->argv[first], L"--multiple") == 0) {
            multiple = true;
            first++;
        } else if (wcscmp(cmd->argv[first], L"-z") == 0 || wcscmp(cmd->argv[first], L"--zero") == 0) {
            zero_delim = true;
            first++;
        } else if (wcscmp(cmd->argv[first], L"-s") == 0 && first + 1 < cmd->argc) {
            multiple = true;
            suffix = cmd->argv[first + 1];
            first += 2;
        } else if (wcsncmp(cmd->argv[first], L"--suffix=", 9) == 0) {
            multiple = true;
            suffix = cmd->argv[first] + 9;
            first++;
        } else if (wcscmp(cmd->argv[first], L"--") == 0) {
            first++;
            break;
        } else {
            break;
        }
    }
    if (first >= cmd->argc) {
        fwprintf(stderr, L"ksh: basename: missing operand\n");
        *code = 1;
        return true;
    }
    if (!multiple && (cmd->argc - first) == 2 && !suffix) {
        suffix = cmd->argv[first + 1];
    }

    for (int i = first; i < cmd->argc; i++) {
        if (!multiple && i > first) break;
        const wchar_t* str = cmd->argv[i];
        size_t slen = wcslen(str);
        if (slen == 0) {
            wprintf(L"%lc", zero_delim ? L'\0' : L'\n');
            continue;
        }
        while (slen > 1 && (str[slen - 1] == L'/' || str[slen - 1] == L'\\')) {
            if (slen == 3 && str[1] == L':') break;
            slen--;
        }
        wchar_t buf[MAX_PATH];
        size_t copyLen = slen < MAX_PATH ? slen : MAX_PATH - 1;
        wcsncpy_s(buf, MAX_PATH, str, copyLen);
        buf[copyLen] = L'\0';

        wchar_t* p = buf + copyLen;
        while (p > buf && *(p - 1) != L'/' && *(p - 1) != L'\\' && *(p - 1) != L':') {
            p--;
        }
        if (suffix && wcslen(p) > wcslen(suffix)) {
            size_t plen = wcslen(p);
            size_t suflen = wcslen(suffix);
            if (wcscmp(p + plen - suflen, suffix) == 0) {
                p[plen - suflen] = L'\0';
            }
        }
        wprintf(L"%ls%lc", p, zero_delim ? L'\0' : L'\n');
    }
    *code = 0;
    return true;
}

bool Builtin_Dirname(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Strip last component from file name.\n\ndirname [-z] [string ...]\n\n  -z, --zero  End each output line with NUL, not newline\n");
        *code = 0;
        return true;
    }
    if (cmd->argc == 1) {
        fwprintf(stderr, L"ksh: dirname: missing operand\n");
        *code = 1;
        return true;
    }
    bool zero_delim = false;
    int first = 1;
    while (first < cmd->argc && cmd->argv[first][0] == L'-') {
        if (wcscmp(cmd->argv[first], L"-z") == 0 || wcscmp(cmd->argv[first], L"--zero") == 0) zero_delim = true;
        else if (wcscmp(cmd->argv[first], L"--") == 0) { first++; break; }
        first++;
    }

    for (int i = first; i < cmd->argc; i++) {
        const wchar_t* str = cmd->argv[i];
        size_t slen = wcslen(str);
        if (slen == 0) {
            wprintf(L".%lc", zero_delim ? L'\0' : L'\n');
            continue;
        }
        while (slen > 1 && (str[slen - 1] == L'/' || str[slen - 1] == L'\\')) {
            if (slen == 3 && str[1] == L':') break;
            slen--;
        }
        wchar_t buf[MAX_PATH];
        size_t copyLen = slen < MAX_PATH ? slen : MAX_PATH - 1;
        wcsncpy_s(buf, MAX_PATH, str, copyLen);
        buf[copyLen] = L'\0';

        wchar_t* p = buf + copyLen;
        while (p > buf && *(p - 1) != L'/' && *(p - 1) != L'\\' && *(p - 1) != L':') {
            p--;
        }
        if (p == buf) {
            if (buf[1] == L':' && buf[2] != L'\0') {
                buf[2] = L'\0';
                wprintf(L"%ls%lc", buf, zero_delim ? L'\0' : L'\n');
            } else {
                wprintf(L".%lc", zero_delim ? L'\0' : L'\n');
            }
        } else {
            while (p > buf + 1 && (*(p - 1) == L'/' || *(p - 1) == L'\\')) {
                if (p == buf + 3 && buf[1] == L':') break;
                p--;
            }
            *p = L'\0';
            wprintf(L"%ls%lc", buf, zero_delim ? L'\0' : L'\n');
        }
    }
    *code = 0;
    return true;
}

bool Builtin_Env(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Displays, sets, or removes environment variables, or runs a utility with an altered environment.\n\n"
                L"env [-i] [-u name] [name=value ...] [utility [arg ...]]\n\n"
                L"  -i, -       Start with an empty environment\n"
                L"  -u name     Remove variable from environment\n"
                L"  name=value  Set variable in utility's environment\n");
        *code = 0;
        return true;
    }
    int first = 1;
    bool ignore_env = false;

    typedef struct EnvPair {
        wchar_t name[256];
        wchar_t* val;
        wchar_t* orig_val;
        bool was_set;
    } EnvPair;
    EnvPair pairs[128];
    int pair_count = 0;

    wchar_t unsets[32][256];
    int unset_count = 0;

    while (first < cmd->argc) {
        if (wcscmp(cmd->argv[first], L"-i") == 0 || wcscmp(cmd->argv[first], L"-") == 0) {
            ignore_env = true;
            first++;
        } else if (wcscmp(cmd->argv[first], L"-u") == 0 && first + 1 < cmd->argc) {
            if (unset_count < 32) {
                wcsncpy_s(unsets[unset_count++], 256, cmd->argv[first + 1], _TRUNCATE);
            }
            first += 2;
        } else if (wcscmp(cmd->argv[first], L"--") == 0) {
            first++;
            break;
        } else {
            const wchar_t* eq = wcschr(cmd->argv[first], L'=');
            if (eq && eq != cmd->argv[first]) {
                if (pair_count < 128) {
                    size_t nlen = (size_t)(eq - cmd->argv[first]);
                    if (nlen >= 256) nlen = 255;
                    wcsncpy_s(pairs[pair_count].name, 256, cmd->argv[first], nlen);
                    pairs[pair_count].val = (wchar_t*)(eq + 1);
                    const wchar_t* cur = shell->env->Get(shell->env, pairs[pair_count].name);
                    pairs[pair_count].was_set = (cur != NULL);
                    pairs[pair_count].orig_val = cur ? _wcsdup(cur) : NULL;
                    pair_count++;
                }
                first++;
            } else {
                break;
            }
        }
    }

    if (first >= cmd->argc) {
        if (ignore_env) {
            for (int i = 0; i < pair_count; i++) {
                wprintf(L"%ls=%ls\n", pairs[i].name, pairs[i].val);
            }
        } else {
            for (int i = 0; i < pair_count; i++) {
                shell->env->Set(shell->env, pairs[i].name, pairs[i].val);
            }
            for (int i = 0; i < unset_count; i++) {
                shell->env->Unset(shell->env, unsets[i]);
            }
            for (EnvEntry* e = shell->env->head; e; e = e->next) {
                wprintf(L"%ls=%ls\n", e->key, e->value);
            }
            for (int i = 0; i < pair_count; i++) {
                if (pairs[i].was_set) {
                    shell->env->Set(shell->env, pairs[i].name, pairs[i].orig_val);
                    free(pairs[i].orig_val);
                } else {
                    shell->env->Unset(shell->env, pairs[i].name);
                }
            }
        }
        *code = 0;
        return true;
    }

    for (int i = 0; i < pair_count; i++) {
        shell->env->Set(shell->env, pairs[i].name, pairs[i].val);
    }
    for (int i = 0; i < unset_count; i++) {
        shell->env->Unset(shell->env, unsets[i]);
    }

    CommandAST subcmd = {0};
    subcmd.argc = cmd->argc - first;
    for (int i = 0; i < subcmd.argc && i < MAX_ARGS; i++) {
        subcmd.argv[i] = cmd->argv[first + i];
    }
    int sub_code = 0;
    if (IsBuiltinCommand(&subcmd)) {
        (void)TryExecBuiltin(shell, &subcmd, &sub_code);
    } else {
        sub_code = ExecuteSingleCommand(shell, &subcmd, NULL, NULL);
    }
    *code = sub_code;

    for (int i = 0; i < pair_count; i++) {
        if (pairs[i].was_set) {
            shell->env->Set(shell->env, pairs[i].name, pairs[i].orig_val);
            free(pairs[i].orig_val);
        } else {
            shell->env->Unset(shell->env, pairs[i].name);
        }
    }
    return true;
}

bool Builtin_Printenv(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Prints the values of the specified environment variable(s).\n\nprintenv [-0] [name ...]\n\n  -0, --null  End each output line with NUL, not newline\n");
        *code = 0;
        return true;
    }
    bool null_delim = false;
    int first = 1;
    if (first < cmd->argc && (wcscmp(cmd->argv[first], L"-0") == 0 || wcscmp(cmd->argv[first], L"--null") == 0)) {
        null_delim = true;
        first++;
    }
    if (first >= cmd->argc) {
        for (EnvEntry* e = shell->env->head; e; e = e->next) {
            wprintf(L"%ls=%ls%lc", e->key, e->value, null_delim ? L'\0' : L'\n');
        }
        *code = 0;
        return true;
    }
    bool any_missing = false;
    for (int i = first; i < cmd->argc; i++) {
        const wchar_t* val = shell->env->Get(shell->env, cmd->argv[i]);
        if (val) {
            wprintf(L"%ls%lc", val, null_delim ? L'\0' : L'\n');
        } else {
            any_missing = true;
        }
    }
    *code = any_missing ? 1 : 0;
    return true;
}

bool Builtin_Uname(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Print system information.\n\nuname [-a|-s|-n|-r|-v|-m|-p|-o]\n\n"
                L"  -a, --all                Print all information\n"
                L"  -s, --kernel-name        Print the kernel name (Windows_NT)\n"
                L"  -n, --nodename           Print the network node hostname\n"
                L"  -r, --kernel-release     Print the operating system release\n"
                L"  -v, --kernel-version     Print the operating system version\n"
                L"  -m, --machine            Print the machine hardware name\n"
                L"  -p, --processor          Print the processor type\n"
                L"  -o, --operating-system   Print the operating system (Windows_NT)\n");
        *code = 0;
        return true;
    }
    bool opt_s = false, opt_n = false, opt_r = false, opt_v = false, opt_m = false, opt_p = false, opt_o = false;
    bool any_opt = false;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-') {
            if (wcscmp(arg, L"-a") == 0 || wcscmp(arg, L"--all") == 0) {
                opt_s = opt_n = opt_r = opt_v = opt_m = opt_p = opt_o = true;
                any_opt = true;
            } else if (wcscmp(arg, L"-s") == 0 || wcscmp(arg, L"--kernel-name") == 0) {
                opt_s = true; any_opt = true;
            } else if (wcscmp(arg, L"-n") == 0 || wcscmp(arg, L"--nodename") == 0) {
                opt_n = true; any_opt = true;
            } else if (wcscmp(arg, L"-r") == 0 || wcscmp(arg, L"--kernel-release") == 0) {
                opt_r = true; any_opt = true;
            } else if (wcscmp(arg, L"-v") == 0 || wcscmp(arg, L"--kernel-version") == 0) {
                opt_v = true; any_opt = true;
            } else if (wcscmp(arg, L"-m") == 0 || wcscmp(arg, L"--machine") == 0) {
                opt_m = true; any_opt = true;
            } else if (wcscmp(arg, L"-p") == 0 || wcscmp(arg, L"--processor") == 0) {
                opt_p = true; any_opt = true;
            } else if (wcscmp(arg, L"-o") == 0 || wcscmp(arg, L"--operating-system") == 0) {
                opt_o = true; any_opt = true;
            } else {
                for (const wchar_t* p = arg + 1; *p; p++) {
                    if (*p == L'a') opt_s = opt_n = opt_r = opt_v = opt_m = opt_p = opt_o = true;
                    else if (*p == L's') opt_s = true;
                    else if (*p == L'n') opt_n = true;
                    else if (*p == L'r') opt_r = true;
                    else if (*p == L'v') opt_v = true;
                    else if (*p == L'm') opt_m = true;
                    else if (*p == L'p') opt_p = true;
                    else if (*p == L'o') opt_o = true;
                    any_opt = true;
                }
            }
        }
    }
    if (!any_opt) opt_s = true;

    wchar_t sysname[64] = L"Windows_NT";
    wchar_t nodename[MAX_COMPUTERNAME_LENGTH + 1] = L"localhost";
    DWORD nodeSize = _countof(nodename);
    GetComputerNameW(nodename, &nodeSize);

    wchar_t release[64] = L"10.0";
    wchar_t version[128] = L"Build 26100";
    OSVERSIONINFOEXW osvi = { 0 };
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    typedef LONG (WINAPI *RtlGetVersionPtr)(OSVERSIONINFOEXW*);
    HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
    if (hNtDll) {
        RtlGetVersionPtr pRtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hNtDll, "RtlGetVersion");
        if (pRtlGetVersion && pRtlGetVersion(&osvi) == 0) {
            swprintf_s(release, _countof(release), L"%lu.%lu", osvi.dwMajorVersion, osvi.dwMinorVersion);
            swprintf_s(version, _countof(version), L"Build %lu", osvi.dwBuildNumber);
        }
    }

    wchar_t machine[32] = L"x86_64";
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) wcscpy_s(machine, 32, L"x86_64");
    else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) wcscpy_s(machine, 32, L"aarch64");
    else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) wcscpy_s(machine, 32, L"i686");
    else if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM) wcscpy_s(machine, 32, L"arm");

    bool space = false;
    if (opt_s) { wprintf(L"%ls%ls", space ? L" " : L"", sysname); space = true; }
    if (opt_n) { wprintf(L"%ls%ls", space ? L" " : L"", nodename); space = true; }
    if (opt_r) { wprintf(L"%ls%ls", space ? L" " : L"", release); space = true; }
    if (opt_v) { wprintf(L"%ls%ls", space ? L" " : L"", version); space = true; }
    if (opt_m) { wprintf(L"%ls%ls", space ? L" " : L"", machine); space = true; }
    if (opt_p && !opt_m) { wprintf(L"%ls%ls", space ? L" " : L"", machine); space = true; }
    if (opt_o && !opt_s) { wprintf(L"%ls%ls", space ? L" " : L"", sysname); space = true; }
    wprintf(L"\n");
    *code = 0;
    return true;
}

bool Builtin_Which(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Locate a command in PATH or builtins.\n\nwhich [-a] [-s] name...\n\n  -a    Print all matching pathnames of each argument\n  -s    Silent mode; return 0 if all found, 1 if missing\n");
        *code = 0;
        return true;
    }
    if (cmd->argc == 1) {
        *code = 1;
        return true;
    }
    bool all = false;
    bool silent = false;
    int first = 1;
    while (first < cmd->argc && cmd->argv[first][0] == L'-') {
        if (wcscmp(cmd->argv[first], L"-a") == 0) all = true;
        else if (wcscmp(cmd->argv[first], L"-s") == 0) silent = true;
        else if (wcscmp(cmd->argv[first], L"--") == 0) { first++; break; }
        first++;
    }
    bool any_missing = false;
    for (int i = first; i < cmd->argc; i++) {
        const wchar_t* name = cmd->argv[i];
        bool found = false;
        wchar_t path[MAX_PATH];
        if (all) {
            CommandAST dummy = { .argc = 1, .argv = { (wchar_t*)name } };
            if (IsBuiltinCommand(&dummy) || Shell_HasFunction(shell, name)) {
                if (!silent) wprintf(L"%ls: shell built-in command\n", name);
                found = true;
            }
            wchar_t pathBuf[32768];
            DWORD pathLen = GetEnvironmentVariableW(L"PATH", pathBuf, _countof(pathBuf));
            if (pathLen > 0) {
                wchar_t* ctx = NULL;
                wchar_t* dir = wcstok_s(pathBuf, L";", &ctx);
                while (dir) {
                    static const wchar_t* exts[] = { L"", L".exe", L".cmd", L".bat", L".com" };
                    for (size_t e = 0; e < sizeof(exts)/sizeof(exts[0]); e++) {
                        wchar_t candidate[MAX_PATH];
                        swprintf_s(candidate, MAX_PATH, L"%ls\\%ls%ls", dir, name, exts[e]);
                        DWORD attrs = GetFileAttributesW(candidate);
                        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                            if (!silent) wprintf(L"%ls\n", candidate);
                            found = true;
                        }
                    }
                    dir = wcstok_s(NULL, L";", &ctx);
                }
            }
        } else {
            if (FindCommandPath(name, path, _countof(path))) {
                if (!silent) wprintf(L"%ls\n", path);
                found = true;
            } else {
                CommandAST dummy = { .argc = 1, .argv = { (wchar_t*)name } };
                if (IsBuiltinCommand(&dummy) || Shell_HasFunction(shell, name)) {
                    if (!silent) wprintf(L"%ls: shell built-in command\n", name);
                    found = true;
                }
            }
        }
        if (!found) any_missing = true;
    }
    *code = any_missing ? 1 : 0;
    return true;
}

bool Builtin_Cat(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Concatenate FILE(s) to standard output.\n\n"
                L"cat [-n] [-b] [-s] [-u] [-E] [-T] [FILE...]\n\n"
                L"  -n, --number             number all output lines\n"
                L"  -b, --number-nonblank    number nonempty output lines, overrides -n\n"
                L"  -s, --squeeze-blank      suppress repeated empty output lines\n"
                L"  -u                       (ignored, unbuffered output)\n"
                L"  -E, --show-ends          display $ at end of each line\n"
                L"  -T, --show-tabs          display TAB characters as ^I\n");
        *code = 0;
        return true;
    }
    bool opt_n = false, opt_b = false, opt_s = false, opt_E = false, opt_T = false;
    int first = 1;
    while (first < cmd->argc && cmd->argv[first][0] == L'-' && cmd->argv[first][1]) {
        if (wcscmp(cmd->argv[first], L"--") == 0) { first++; break; }
        if (wcscmp(cmd->argv[first], L"--number") == 0) opt_n = true;
        else if (wcscmp(cmd->argv[first], L"--number-nonblank") == 0) opt_b = true;
        else if (wcscmp(cmd->argv[first], L"--squeeze-blank") == 0) opt_s = true;
        else if (wcscmp(cmd->argv[first], L"--show-ends") == 0) opt_E = true;
        else if (wcscmp(cmd->argv[first], L"--show-tabs") == 0) opt_T = true;
        else {
            for (const wchar_t* p = cmd->argv[first] + 1; *p; p++) {
                if (*p == L'n') opt_n = true;
                else if (*p == L'b') opt_b = true;
                else if (*p == L's') opt_s = true;
                else if (*p == L'E') opt_E = true;
                else if (*p == L'T') opt_T = true;
                else if (*p == L'u') { /* unbuffered */ }
                else break;
            }
        }
        first++;
    }

    int file_count = cmd->argc - first;
    int line_num = 1;
    bool last_was_empty = false;
    bool any_error = false;

    for (int fi = (file_count > 0 ? first : 0); (file_count > 0 ? fi < cmd->argc : fi == 0); fi++) {
        FILE* fp = NULL;
        bool is_stdin = (file_count == 0 || wcscmp(cmd->argv[fi], L"-") == 0);
        if (is_stdin) {
            fp = stdin;
        } else {
            if (_wfopen_s(&fp, cmd->argv[fi], L"r, ccs=UTF-8") != 0 || !fp) {
                if (_wfopen_s(&fp, cmd->argv[fi], L"r") != 0 || !fp) {
                    fwprintf(stderr, L"cat: %ls: No such file or directory\n", cmd->argv[fi]);
                    any_error = true;
                    continue;
                }
            }
        }

        wchar_t line_buf[4096];
        while (fgetws(line_buf, _countof(line_buf), fp)) {
            size_t len = wcslen(line_buf);
            bool has_nl = (len > 0 && line_buf[len - 1] == L'\n');
            if (has_nl) {
                line_buf[--len] = L'\0';
                if (len > 0 && line_buf[len - 1] == L'\r') line_buf[--len] = L'\0';
            }
            bool is_empty = (len == 0);
            if (opt_s && is_empty && last_was_empty) continue;
            last_was_empty = is_empty;

            if (opt_b) {
                if (!is_empty) {
                    wprintf(L"%6d\t", line_num++);
                }
            } else if (opt_n) {
                wprintf(L"%6d\t", line_num++);
            }

            if (opt_T) {
                for (size_t c = 0; c < len; c++) {
                    if (line_buf[c] == L'\t') fputws(L"^I", stdout);
                    else putwchar(line_buf[c]);
                }
            } else {
                fputws(line_buf, stdout);
            }
            if (opt_E) putwchar(L'$');
            if (has_nl) putwchar(L'\n');
        }

        if (!is_stdin) fclose(fp);
    }
    *code = any_error ? 1 : 0;
    return true;
}

bool Builtin_Head(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Output the first part of files.\n\n"
                L"head [-n count] [-c bytes] [-q] [-v] [FILE...]\n\n"
                L"  -n, --lines=COUNT    print the first COUNT lines instead of 10\n"
                L"  -c, --bytes=BYTES    print the first BYTES bytes\n"
                L"  -q, --quiet          never print headers giving file names\n"
                L"  -v, --verbose        always print headers giving file names\n");
        *code = 0;
        return true;
    }
    int count = 10;
    int bytes = -1;
    bool quiet = false, verbose = false;
    int first = 1;
    while (first < cmd->argc && cmd->argv[first][0] == L'-' && cmd->argv[first][1]) {
        if (wcscmp(cmd->argv[first], L"--") == 0) { first++; break; }
        if (iswdigit(cmd->argv[first][1])) {
            count = _wtoi(cmd->argv[first] + 1);
        } else if (wcscmp(cmd->argv[first], L"-n") == 0 && first + 1 < cmd->argc) {
            count = _wtoi(cmd->argv[++first]);
        } else if (wcsncmp(cmd->argv[first], L"--lines=", 8) == 0) {
            count = _wtoi(cmd->argv[first] + 8);
        } else if (wcscmp(cmd->argv[first], L"-c") == 0 && first + 1 < cmd->argc) {
            bytes = _wtoi(cmd->argv[++first]);
        } else if (wcsncmp(cmd->argv[first], L"--bytes=", 8) == 0) {
            bytes = _wtoi(cmd->argv[first] + 8);
        } else if (wcscmp(cmd->argv[first], L"-q") == 0 || wcscmp(cmd->argv[first], L"--quiet") == 0) {
            quiet = true;
        } else if (wcscmp(cmd->argv[first], L"-v") == 0 || wcscmp(cmd->argv[first], L"--verbose") == 0) {
            verbose = true;
        }
        first++;
    }

    int file_count = cmd->argc - first;
    bool multi = (file_count > 1);
    bool any_error = false;
    for (int fi = (file_count > 0 ? first : 0); (file_count > 0 ? fi < cmd->argc : fi == 0); fi++) {
        FILE* fp = NULL;
        bool is_stdin = (file_count == 0 || wcscmp(cmd->argv[fi], L"-") == 0);
        const wchar_t* fname = is_stdin ? L"standard input" : cmd->argv[fi];
        if (is_stdin) {
            fp = stdin;
        } else {
            if (_wfopen_s(&fp, cmd->argv[fi], bytes >= 0 ? L"rb" : L"r, ccs=UTF-8") != 0 || !fp) {
                if (_wfopen_s(&fp, cmd->argv[fi], bytes >= 0 ? L"rb" : L"r") != 0 || !fp) {
                    fwprintf(stderr, L"head: cannot open '%ls' for reading: No such file or directory\n", cmd->argv[fi]);
                    any_error = true;
                    continue;
                }
            }
        }

        if ((multi && !quiet) || verbose) {
            wprintf(L"==> %ls <==\n", fname);
        }

        if (bytes >= 0) {
            int byte_c = 0;
            int ch;
            while (byte_c < bytes && (ch = fgetc(fp)) != EOF) {
                fputc(ch, stdout);
                byte_c++;
            }
        } else {
            int lines_read = 0;
            wchar_t line_buf[4096];
            while (lines_read < count && fgetws(line_buf, _countof(line_buf), fp)) {
                fputws(line_buf, stdout);
                lines_read++;
            }
        }

        if (!is_stdin) fclose(fp);
    }
    *code = any_error ? 1 : 0;
    return true;
}

bool Builtin_Tail(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Output the last part of files.\n\n"
                L"tail [-n count] [-c bytes] [-f] [-q] [-v] [FILE...]\n\n"
                L"  -n, --lines=COUNT    output the last COUNT lines, instead of the last 10\n"
                L"  -c, --bytes=BYTES    output the last BYTES bytes\n"
                L"  -f, --follow         output appended data as the file grows\n"
                L"  -q, --quiet          never output headers giving file names\n"
                L"  -v, --verbose        always output headers giving file names\n");
        *code = 0;
        return true;
    }
    int count = 10;
    int bytes = -1;
    bool quiet = false, verbose = false;
    bool follow = false;
    int first = 1;
    while (first < cmd->argc && cmd->argv[first][0] == L'-' && cmd->argv[first][1]) {
        if (wcscmp(cmd->argv[first], L"--") == 0) { first++; break; }
        if (iswdigit(cmd->argv[first][1])) {
            count = _wtoi(cmd->argv[first] + 1);
        } else if (wcscmp(cmd->argv[first], L"-n") == 0 && first + 1 < cmd->argc) {
            count = _wtoi(cmd->argv[++first]);
        } else if (wcsncmp(cmd->argv[first], L"--lines=", 8) == 0) {
            count = _wtoi(cmd->argv[first] + 8);
        } else if (wcscmp(cmd->argv[first], L"-c") == 0 && first + 1 < cmd->argc) {
            bytes = _wtoi(cmd->argv[++first]);
        } else if (wcsncmp(cmd->argv[first], L"--bytes=", 8) == 0) {
            bytes = _wtoi(cmd->argv[first] + 8);
        } else if (wcscmp(cmd->argv[first], L"-f") == 0 || wcscmp(cmd->argv[first], L"--follow") == 0) {
            follow = true;
        } else if (wcscmp(cmd->argv[first], L"-q") == 0 || wcscmp(cmd->argv[first], L"--quiet") == 0) {
            quiet = true;
        } else if (wcscmp(cmd->argv[first], L"-v") == 0 || wcscmp(cmd->argv[first], L"--verbose") == 0) {
            verbose = true;
        }
        first++;
    }

    int file_count = cmd->argc - first;
    bool multi = (file_count > 1);
    bool any_error = false;
    for (int fi = (file_count > 0 ? first : 0); (file_count > 0 ? fi < cmd->argc : fi == 0); fi++) {
        FILE* fp = NULL;
        bool is_stdin = (file_count == 0 || wcscmp(cmd->argv[fi], L"-") == 0);
        const wchar_t* fname = is_stdin ? L"standard input" : cmd->argv[fi];
        if (is_stdin) {
            fp = stdin;
        } else {
            if (_wfopen_s(&fp, cmd->argv[fi], bytes >= 0 ? L"rb" : L"r, ccs=UTF-8") != 0 || !fp) {
                if (_wfopen_s(&fp, cmd->argv[fi], bytes >= 0 ? L"rb" : L"r") != 0 || !fp) {
                    fwprintf(stderr, L"tail: cannot open '%ls' for reading: No such file or directory\n", cmd->argv[fi]);
                    any_error = true;
                    continue;
                }
            }
        }

        if ((multi && !quiet) || verbose) {
            wprintf(L"==> %ls <==\n", fname);
        }

        if (bytes >= 0) {
            if (!is_stdin) {
                _fseeki64(fp, 0, SEEK_END);
                __int64 total_bytes = _ftelli64(fp);
                __int64 start_pos = total_bytes > bytes ? total_bytes - bytes : 0;
                _fseeki64(fp, start_pos, SEEK_SET);
                int ch;
                while ((ch = fgetc(fp)) != EOF) fputc(ch, stdout);
            } else {
                char* bbuf = (char*)malloc(bytes > 0 ? bytes : 1);
                int bcount = 0;
                int ch;
                while ((ch = fgetc(fp)) != EOF) {
                    if (bcount < bytes) bbuf[bcount++] = (char)ch;
                    else {
                        memmove(bbuf, bbuf + 1, bytes - 1);
                        bbuf[bytes - 1] = (char)ch;
                    }
                }
                for (int b = 0; b < bcount; b++) fputc(bbuf[b], stdout);
                free(bbuf);
            }
        } else {
            wchar_t** lines = (wchar_t**)calloc(count > 0 ? count : 1, sizeof(wchar_t*));
            int head_idx = 0;
            int total_lines = 0;
            wchar_t line_buf[4096];
            while (fgetws(line_buf, _countof(line_buf), fp)) {
                if (lines[head_idx]) free(lines[head_idx]);
                lines[head_idx] = _wcsdup(line_buf);
                head_idx = (head_idx + 1) % (count > 0 ? count : 1);
                total_lines++;
            }
            int print_count = total_lines < count ? total_lines : count;
            int start_idx = total_lines < count ? 0 : head_idx;
            for (int p = 0; p < print_count; p++) {
                int idx = (start_idx + p) % count;
                if (lines[idx]) {
                    fputws(lines[idx], stdout);
                    free(lines[idx]);
                    lines[idx] = NULL;
                }
            }
            free(lines);
        }

        if (follow && !is_stdin) {
            fflush(stdout);
            wchar_t fbuf[4096];
            while (true) {
                clearerr(fp);
                bool read_any = false;
                while (fgetws(fbuf, _countof(fbuf), fp)) {
                    fputws(fbuf, stdout);
                    read_any = true;
                }
                if (read_any) fflush(stdout);
                Sleep(100);
                if (_kbhit()) { (void)_getch(); break; }
            }
        }

        if (!is_stdin) fclose(fp);
    }
    *code = any_error ? 1 : 0;
    return true;
}

bool Builtin_Wc(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Print newline, word, and byte counts for each FILE.\n\n"
                L"wc [-l] [-w] [-c] [-m] [-L] [FILE...]\n\n"
                L"  -l, --lines            print the newline counts\n"
                L"  -w, --words            print the word counts\n"
                L"  -c, --bytes            print the byte counts\n"
                L"  -m, --chars            print the character counts\n"
                L"  -L, --max-line-length  print the maximum display width\n");
        *code = 0;
        return true;
    }
    bool opt_l = false, opt_w = false, opt_c = false, opt_m = false, opt_L = false;
    bool any_opt = false;
    int first = 1;
    while (first < cmd->argc && cmd->argv[first][0] == L'-' && cmd->argv[first][1]) {
        if (wcscmp(cmd->argv[first], L"--") == 0) { first++; break; }
        if (wcscmp(cmd->argv[first], L"-l") == 0 || wcscmp(cmd->argv[first], L"--lines") == 0) { opt_l = true; any_opt = true; }
        else if (wcscmp(cmd->argv[first], L"-w") == 0 || wcscmp(cmd->argv[first], L"--words") == 0) { opt_w = true; any_opt = true; }
        else if (wcscmp(cmd->argv[first], L"-c") == 0 || wcscmp(cmd->argv[first], L"--bytes") == 0) { opt_c = true; any_opt = true; }
        else if (wcscmp(cmd->argv[first], L"-m") == 0 || wcscmp(cmd->argv[first], L"--chars") == 0) { opt_m = true; any_opt = true; }
        else if (wcscmp(cmd->argv[first], L"-L") == 0 || wcscmp(cmd->argv[first], L"--max-line-length") == 0) { opt_L = true; any_opt = true; }
        else {
            for (const wchar_t* p = cmd->argv[first] + 1; *p; p++) {
                if (*p == L'l') { opt_l = true; any_opt = true; }
                else if (*p == L'w') { opt_w = true; any_opt = true; }
                else if (*p == L'c') { opt_c = true; any_opt = true; }
                else if (*p == L'm') { opt_m = true; any_opt = true; }
                else if (*p == L'L') { opt_L = true; any_opt = true; }
            }
        }
        first++;
    }
    if (!any_opt) { opt_l = true; opt_w = true; opt_c = true; }

    int file_count = cmd->argc - first;
    __int64 total_l = 0, total_w = 0, total_c = 0, total_m = 0, max_L = 0;
    bool any_error = false;

    for (int fi = (file_count > 0 ? first : 0); (file_count > 0 ? fi < cmd->argc : fi == 0); fi++) {
        FILE* fp = NULL;
        bool is_stdin = (file_count == 0 || wcscmp(cmd->argv[fi], L"-") == 0);
        const wchar_t* fname = is_stdin ? L"" : cmd->argv[fi];
        if (!is_stdin) {
            if (_wfopen_s(&fp, cmd->argv[fi], L"rb") != 0 || !fp) {
                fwprintf(stderr, L"wc: %ls: No such file or directory\n", cmd->argv[fi]);
                any_error = true;
                continue;
            }
        }

        __int64 fl = 0, fw = 0, fc = 0, fm = 0, fL = 0;
        __int64 cur_line_len = 0;
        bool in_word = false;
        if (is_stdin) {
            wint_t ch;
            while ((ch = fgetwc(stdin)) != WEOF) {
                fc++;
                fm++;
                if (ch == L'\n') {
                    fl++;
                    if (cur_line_len > fL) fL = cur_line_len;
                    cur_line_len = 0;
                } else if (ch != L'\r') {
                    cur_line_len++;
                }
                if (iswspace(ch)) {
                    in_word = false;
                } else if (!in_word) {
                    in_word = true;
                    fw++;
                }
            }
        } else {
            int ch;
            while ((ch = fgetc(fp)) != EOF) {
                fc++;
                fm++;
                if (ch == '\n') {
                    fl++;
                    if (cur_line_len > fL) fL = cur_line_len;
                    cur_line_len = 0;
                } else if (ch != '\r') {
                    cur_line_len++;
                }
                if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
                    in_word = false;
                } else if (!in_word) {
                    in_word = true;
                    fw++;
                }
            }
            fclose(fp);
        }
        if (cur_line_len > fL) fL = cur_line_len;

        total_l += fl; total_w += fw; total_c += fc; total_m += fm;
        if (fL > max_L) max_L = fL;

        if (opt_l) wprintf(L"%8lld", fl);
        if (opt_w) wprintf(L"%8lld", fw);
        if (opt_c) wprintf(L"%8lld", fc);
        if (opt_m && !opt_c) wprintf(L"%8lld", fm);
        if (opt_L) wprintf(L"%8lld", fL);
        if (fname[0]) wprintf(L" %ls", fname);
        wprintf(L"\n");
    }

    if (file_count > 1) {
        if (opt_l) wprintf(L"%8lld", total_l);
        if (opt_w) wprintf(L"%8lld", total_w);
        if (opt_c) wprintf(L"%8lld", total_c);
        if (opt_m && !opt_c) wprintf(L"%8lld", total_m);
        if (opt_L) wprintf(L"%8lld", max_L);
        wprintf(L" total\n");
    }
    *code = any_error ? 1 : 0;
    return true;
}

bool Builtin_Tee(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Copy standard input to each FILE, and also to standard output.\n\n"
                L"tee [-a] [-i] [FILE...]\n\n"
                L"  -a, --append   append to the given FILEs, do not overwrite\n"
                L"  -i, --ignore-interrupts ignore interrupt signals\n");
        *code = 0;
        return true;
    }
    bool append = false;
    int first = 1;
    while (first < cmd->argc && cmd->argv[first][0] == L'-' && cmd->argv[first][1]) {
        if (wcscmp(cmd->argv[first], L"-a") == 0 || wcscmp(cmd->argv[first], L"--append") == 0) append = true;
        else if (wcscmp(cmd->argv[first], L"-i") == 0 || wcscmp(cmd->argv[first], L"--ignore-interrupts") == 0) { /* ignore */ }
        else if (wcscmp(cmd->argv[first], L"--") == 0) { first++; break; }
        first++;
    }

    int file_count = cmd->argc - first;
    FILE** fps = (FILE**)calloc(file_count > 0 ? file_count : 1, sizeof(FILE*));
    for (int i = 0; i < file_count; i++) {
        _wfopen_s(&fps[i], cmd->argv[first + i], append ? L"a, ccs=UTF-8" : L"w, ccs=UTF-8");
    }

    wchar_t line_buf[4096];
    while (fgetws(line_buf, _countof(line_buf), stdin)) {
        fputws(line_buf, stdout);
        fflush(stdout);
        for (int i = 0; i < file_count; i++) {
            if (fps[i]) {
                fputws(line_buf, fps[i]);
                fflush(fps[i]);
            }
        }
    }
    for (int i = 0; i < file_count; i++) {
        if (fps[i]) fclose(fps[i]);
    }
    free(fps);
    *code = 0;
    return true;
}

bool Builtin_Seq(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Print numbers from FIRST to LAST, in steps of INCREMENT.\n\n"
                L"seq [-s sep] [-w] [FIRST [INCR]] LAST\n\n"
                L"  -s, --separator=STRING  use STRING to separate numbers (default: \\n)\n"
                L"  -w, --equal-width       equalize width by padding with leading zeroes\n");
        *code = 0;
        return true;
    }
    const wchar_t* sep = L"\n";
    bool equal_width = false;
    int first = 1;
    while (first < cmd->argc && cmd->argv[first][0] == L'-' && cmd->argv[first][1]) {
        if (wcscmp(cmd->argv[first], L"-s") == 0 && first + 1 < cmd->argc) {
            sep = cmd->argv[++first];
        } else if (wcsncmp(cmd->argv[first], L"-s", 2) == 0) {
            sep = cmd->argv[first] + 2;
        } else if (wcsncmp(cmd->argv[first], L"--separator=", 12) == 0) {
            sep = cmd->argv[first] + 12;
        } else if (wcscmp(cmd->argv[first], L"-w") == 0 || wcscmp(cmd->argv[first], L"--equal-width") == 0) {
            equal_width = true;
        } else if (wcscmp(cmd->argv[first], L"--") == 0) {
            first++;
            break;
        } else {
            break;
        }
        first++;
    }

    int operands = cmd->argc - first;
    if (operands < 1 || operands > 3) {
        fwprintf(stderr, L"ksh: seq: missing or excess operands\n");
        *code = 1;
        return true;
    }

    double start = 1.0, step = 1.0, stop = 1.0;
    if (operands == 1) {
        stop = _wtof(cmd->argv[first]);
    } else if (operands == 2) {
        start = _wtof(cmd->argv[first]);
        stop = _wtof(cmd->argv[first + 1]);
    } else {
        start = _wtof(cmd->argv[first]);
        step = _wtof(cmd->argv[first + 1]);
        stop = _wtof(cmd->argv[first + 2]);
    }

    if (step == 0.0) {
        fwprintf(stderr, L"ksh: seq: zero increment\n");
        *code = 1;
        return true;
    }

    int width = 0;
    if (equal_width) {
        wchar_t last_str[64];
        swprintf_s(last_str, _countof(last_str), L"%.0f", stop);
        width = (int)wcslen(last_str);
    }

    bool is_first = true;
    if (step > 0) {
        for (double d = start; d <= stop + 1e-9; d += step) {
            if (!is_first) fputws(sep, stdout);
            if (equal_width) wprintf(L"%0*lld", width, (__int64)d);
            else if (d == (__int64)d) wprintf(L"%lld", (__int64)d);
            else wprintf(L"%g", d);
            is_first = false;
        }
    } else {
        for (double d = start; d >= stop - 1e-9; d += step) {
            if (!is_first) fputws(sep, stdout);
            if (equal_width) wprintf(L"%0*lld", width, (__int64)d);
            else if (d == (__int64)d) wprintf(L"%lld", (__int64)d);
            else wprintf(L"%g", d);
            is_first = false;
        }
    }
    if (!is_first) wprintf(L"\n");
    *code = 0;
    return true;
}

bool Builtin_Expr(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Print the value of EXPRESSION to standard output.\n\n"
                L"expr ARG1 OPERATOR ARG2\n"
                L"Operators: +, -, *, /, %, =, !=, <, <=, >, >=, &, |\n");
        *code = 0;
        return true;
    }
    if (cmd->argc == 1) {
        fwprintf(stderr, L"expr: missing operand\n");
        *code = 2;
        return true;
    }
    if (cmd->argc == 2) {
        wprintf(L"%ls\n", cmd->argv[1]);
        *code = (wcscmp(cmd->argv[1], L"0") == 0 || cmd->argv[1][0] == L'\0') ? 1 : 0;
        return true;
    }
    if (cmd->argc == 4) {
        const wchar_t* a = cmd->argv[1];
        const wchar_t* op = cmd->argv[2];
        const wchar_t* b = cmd->argv[3];
        wchar_t* end1 = NULL;
        wchar_t* end2 = NULL;
        __int64 v1 = _wcstoi64(a, &end1, 10);
        __int64 v2 = _wcstoi64(b, &end2, 10);
        bool num1 = (end1 && *end1 == L'\0');
        bool num2 = (end2 && *end2 == L'\0');

        if (wcscmp(op, L"+") == 0 && num1 && num2) {
            __int64 res = v1 + v2;
            wprintf(L"%lld\n", res);
            *code = (res == 0) ? 1 : 0;
            return true;
        } else if (wcscmp(op, L"-") == 0 && num1 && num2) {
            __int64 res = v1 - v2;
            wprintf(L"%lld\n", res);
            *code = (res == 0) ? 1 : 0;
            return true;
        } else if (wcscmp(op, L"*") == 0 && num1 && num2) {
            __int64 res = v1 * v2;
            wprintf(L"%lld\n", res);
            *code = (res == 0) ? 1 : 0;
            return true;
        } else if (wcscmp(op, L"/") == 0 && num1 && num2) {
            if (v2 == 0) { fwprintf(stderr, L"expr: division by zero\n"); *code = 2; return true; }
            __int64 res = v1 / v2;
            wprintf(L"%lld\n", res);
            *code = (res == 0) ? 1 : 0;
            return true;
        } else if (wcscmp(op, L"%") == 0 && num1 && num2) {
            if (v2 == 0) { fwprintf(stderr, L"expr: division by zero\n"); *code = 2; return true; }
            __int64 res = v1 % v2;
            wprintf(L"%lld\n", res);
            *code = (res == 0) ? 1 : 0;
            return true;
        } else if (wcscmp(op, L"=") == 0 || wcscmp(op, L"==") == 0) {
            bool eq = (num1 && num2) ? (v1 == v2) : (wcscmp(a, b) == 0);
            wprintf(L"%d\n", eq ? 1 : 0);
            *code = eq ? 0 : 1;
            return true;
        } else if (wcscmp(op, L"!=") == 0) {
            bool ne = (num1 && num2) ? (v1 != v2) : (wcscmp(a, b) != 0);
            wprintf(L"%d\n", ne ? 1 : 0);
            *code = ne ? 0 : 1;
            return true;
        } else if (wcscmp(op, L"<") == 0) {
            bool lt = (num1 && num2) ? (v1 < v2) : (wcscmp(a, b) < 0);
            wprintf(L"%d\n", lt ? 1 : 0);
            *code = lt ? 0 : 1;
            return true;
        } else if (wcscmp(op, L"<=") == 0) {
            bool le = (num1 && num2) ? (v1 <= v2) : (wcscmp(a, b) <= 0);
            wprintf(L"%d\n", le ? 1 : 0);
            *code = le ? 0 : 1;
            return true;
        } else if (wcscmp(op, L">") == 0) {
            bool gt = (num1 && num2) ? (v1 > v2) : (wcscmp(a, b) > 0);
            wprintf(L"%d\n", gt ? 1 : 0);
            *code = gt ? 0 : 1;
            return true;
        } else if (wcscmp(op, L">=") == 0) {
            bool ge = (num1 && num2) ? (v1 >= v2) : (wcscmp(a, b) >= 0);
            wprintf(L"%d\n", ge ? 1 : 0);
            *code = ge ? 0 : 1;
            return true;
        } else if (wcscmp(op, L"|") == 0) {
            bool a_null = (!a[0] || (num1 && v1 == 0));
            if (!a_null) { wprintf(L"%ls\n", a); *code = 0; }
            else { wprintf(L"%ls\n", b); *code = (!b[0] || (num2 && v2 == 0)) ? 1 : 0; }
            return true;
        } else if (wcscmp(op, L"&") == 0) {
            bool a_null = (!a[0] || (num1 && v1 == 0));
            bool b_null = (!b[0] || (num2 && v2 == 0));
            if (!a_null && !b_null) { wprintf(L"%ls\n", a); *code = 0; }
            else { wprintf(L"0\n"); *code = 1; }
            return true;
        }
    }
    fwprintf(stderr, L"expr: syntax error\n");
    *code = 2;
    return true;
}

bool Builtin_Cmp(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Compare two files byte by byte.\n\n"
                L"cmp [-l] [-s] FILE1 FILE2\n\n"
                L"  -l, --verbose  output byte numbers and differing byte values\n"
                L"  -s, --quiet    suppress all normal output\n");
        *code = 0;
        return true;
    }
    bool verbose = false, quiet = false;
    int first = 1;
    while (first < cmd->argc && cmd->argv[first][0] == L'-' && cmd->argv[first][1]) {
        if (wcscmp(cmd->argv[first], L"-l") == 0 || wcscmp(cmd->argv[first], L"--verbose") == 0) verbose = true;
        else if (wcscmp(cmd->argv[first], L"-s") == 0 || wcscmp(cmd->argv[first], L"--quiet") == 0 || wcscmp(cmd->argv[first], L"--silent") == 0) quiet = true;
        else if (wcscmp(cmd->argv[first], L"--") == 0) { first++; break; }
        first++;
    }
    if (cmd->argc - first < 2) {
        fwprintf(stderr, L"cmp: missing operand\n");
        *code = 2;
        return true;
    }
    const wchar_t* file1 = cmd->argv[first];
    const wchar_t* file2 = cmd->argv[first + 1];

    FILE *f1 = NULL, *f2 = NULL;
    if (_wfopen_s(&f1, file1, L"rb") != 0 || !f1) {
        fwprintf(stderr, L"cmp: %ls: No such file or directory\n", file1);
        *code = 2; return true;
    }
    if (_wfopen_s(&f2, file2, L"rb") != 0 || !f2) {
        fclose(f1);
        fwprintf(stderr, L"cmp: %ls: No such file or directory\n", file2);
        *code = 2; return true;
    }

    __int64 byte_num = 1;
    __int64 line_num = 1;
    bool diff = false;
    while (1) {
        int b1 = fgetc(f1);
        int b2 = fgetc(f2);
        if (b1 == EOF && b2 == EOF) break;
        if (b1 != b2) {
            diff = true;
            if (verbose) {
                wprintf(L"%6lld %3o %3o\n", byte_num, b1 == EOF ? 0 : b1, b2 == EOF ? 0 : b2);
            } else if (!quiet) {
                if (b1 == EOF) wprintf(L"cmp: EOF on %ls after byte %lld\n", file1, byte_num - 1);
                else if (b2 == EOF) wprintf(L"cmp: EOF on %ls after byte %lld\n", file2, byte_num - 1);
                else wprintf(L"%ls %ls differ: char %lld, line %lld\n", file1, file2, byte_num, line_num);
                break;
            }
        }
        if (b1 == '\n') line_num++;
        byte_num++;
    }
    fclose(f1);
    fclose(f2);
    *code = diff ? 1 : 0;
    return true;
}

bool Builtin_Mktemp(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Create a temporary file or directory safely.\n\n"
                L"mktemp [-d] [-u] [-q] [-p dir] [-t prefix] [TEMPLATE]\n\n"
                L"  -d, --directory   create a directory, not a file\n"
                L"  -u, --dry-run     do not create anything; merely print a name\n"
                L"  -q, --quiet       suppress diagnostics about file/dir-creation failure\n"
                L"  -p, --tmpdir=DIR  interpret TEMPLATE relative to DIR\n"
                L"  -t prefix         generate template prefix.XXXXXX\n");
        *code = 0;
        return true;
    }
    bool is_dir = false, dry_run = false, quiet = false;
    const wchar_t* tmp_dir = NULL;
    const wchar_t* prefix = NULL;
    const wchar_t* template_name = NULL;
    int first = 1;
    while (first < cmd->argc && cmd->argv[first][0] == L'-' && cmd->argv[first][1]) {
        if (wcscmp(cmd->argv[first], L"-d") == 0 || wcscmp(cmd->argv[first], L"--directory") == 0) is_dir = true;
        else if (wcscmp(cmd->argv[first], L"-u") == 0 || wcscmp(cmd->argv[first], L"--dry-run") == 0) dry_run = true;
        else if (wcscmp(cmd->argv[first], L"-q") == 0 || wcscmp(cmd->argv[first], L"--quiet") == 0) quiet = true;
        else if (wcscmp(cmd->argv[first], L"-p") == 0 && first + 1 < cmd->argc) tmp_dir = cmd->argv[++first];
        else if (wcsncmp(cmd->argv[first], L"--tmpdir=", 9) == 0) tmp_dir = cmd->argv[first] + 9;
        else if (wcscmp(cmd->argv[first], L"-t") == 0 && first + 1 < cmd->argc) prefix = cmd->argv[++first];
        else if (wcscmp(cmd->argv[first], L"--") == 0) { first++; break; }
        first++;
    }
    if (first < cmd->argc) template_name = cmd->argv[first];

    wchar_t base_dir[MAX_PATH];
    if (tmp_dir) wcscpy_s(base_dir, MAX_PATH, tmp_dir);
    else GetTempPathW(MAX_PATH, base_dir);
    size_t blen = wcslen(base_dir);
    while (blen > 0 && (base_dir[blen - 1] == L'\\' || base_dir[blen - 1] == L'/')) base_dir[--blen] = L'\0';

    wchar_t full_path[MAX_PATH];
    static const wchar_t alphanum[] = L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int attempts = 0; attempts < 1000; attempts++) {
        wchar_t rand_part[7];
        for (int r = 0; r < 6; r++) rand_part[r] = alphanum[rand() % (sizeof(alphanum)/sizeof(alphanum[0]) - 1)];
        rand_part[6] = L'\0';

        if (template_name) {
            swprintf_s(full_path, MAX_PATH, L"%ls\\%ls", base_dir, template_name);
            wchar_t* xs = wcsstr(full_path, L"XXXXXX");
            if (xs) memcpy(xs, rand_part, 6 * sizeof(wchar_t));
        } else if (prefix) {
            swprintf_s(full_path, MAX_PATH, L"%ls\\%ls.%ls", base_dir, prefix, rand_part);
        } else {
            swprintf_s(full_path, MAX_PATH, L"%ls\\tmp.%ls", base_dir, rand_part);
        }

        if (dry_run) {
            wprintf(L"%ls\n", full_path);
            *code = 0;
            return true;
        }

        if (is_dir) {
            if (CreateDirectoryW(full_path, NULL)) {
                wprintf(L"%ls\n", full_path);
                *code = 0;
                return true;
            }
        } else {
            HANDLE h = CreateFileW(full_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                CloseHandle(h);
                wprintf(L"%ls\n", full_path);
                *code = 0;
                return true;
            }
        }
    }
    if (!quiet) fwprintf(stderr, L"mktemp: failed to create file with template '%ls'\n", template_name ? template_name : L"tmp.XXXXXX");
    *code = 1;
    return true;
}

bool Builtin_Sync(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Synchronize cached writes to persistent storage.\n\nsync\n");
        *code = 0;
        return true;
    }
    _flushall();
    *code = 0;
    return true;
}

bool Builtin_Tty(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Print the file name of the terminal connected to standard input.\n\ntty [-s]\n\n  -s, --silent  print nothing, only return an exit status\n");
        *code = 0;
        return true;
    }
    bool silent = (cmd->argc > 1 && (wcscmp(cmd->argv[1], L"-s") == 0 || wcscmp(cmd->argv[1], L"--silent") == 0));
    DWORD mode = 0;
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (GetConsoleMode(hIn, &mode)) {
        if (!silent) wprintf(L"con\n");
        *code = 0;
    } else {
        if (!silent) wprintf(L"not a tty\n");
        *code = 1;
    }
    return true;
}

bool Builtin_Where(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Displays the location of files that match the search pattern.\n\n"
                L"WHERE [/R dir] [/Q] [/T] pattern...\n\n"
                L"  /R dir   Recursively searches and displays the files that match the pattern\n"
                L"           starting at the specified directory.\n"
                L"  /Q       Returns only the exit code, without displaying the list of matched files.\n"
                L"  /T       Displays the file size, last modified date and time for all matched files.\n");
        *code = 0;
        return true;
    }
    if (cmd->argc == 1) {
        fwprintf(stderr, L"ERROR: Invalid syntax. Default option is not allowed more than '0' time(s).\n");
        *code = 2;
        return true;
    }
    const wchar_t* recurse_dir = NULL;
    bool quiet = false, show_time = false;
    int first = 1;
    while (first < cmd->argc && (cmd->argv[first][0] == L'/' || cmd->argv[first][0] == L'-')) {
        if (_wcsicmp(cmd->argv[first], L"/R") == 0 || _wcsicmp(cmd->argv[first], L"-R") == 0) {
            if (first + 1 < cmd->argc) recurse_dir = cmd->argv[++first];
        } else if (_wcsicmp(cmd->argv[first], L"/Q") == 0 || _wcsicmp(cmd->argv[first], L"-Q") == 0) {
            quiet = true;
        } else if (_wcsicmp(cmd->argv[first], L"/T") == 0 || _wcsicmp(cmd->argv[first], L"-T") == 0) {
            show_time = true;
        }
        first++;
    }
    if (first >= cmd->argc) {
        fwprintf(stderr, L"ERROR: Invalid syntax. Value expected for 'pattern'.\n");
        *code = 2;
        return true;
    }

    bool any_matched = false;
    static const wchar_t* exts[] = { L"", L".exe", L".cmd", L".bat", L".com" };

    for (int pi = first; pi < cmd->argc; pi++) {
        const wchar_t* pattern = cmd->argv[pi];
        bool pat_has_ext = (wcsrchr(pattern, L'.') != NULL);
        bool pat_found = false;

        wchar_t searchDirs[512][MAX_PATH];
        int dirCount = 0;
        if (recurse_dir) {
            wcscpy_s(searchDirs[dirCount++], MAX_PATH, recurse_dir);
        } else {
            wcscpy_s(searchDirs[dirCount++], MAX_PATH, L".");
            wchar_t pathBuf[32768];
            DWORD pathLen = GetEnvironmentVariableW(L"PATH", pathBuf, _countof(pathBuf));
            if (pathLen > 0) {
                wchar_t* ctx = NULL;
                wchar_t* d = wcstok_s(pathBuf, L";", &ctx);
                while (d && dirCount < 512) {
                    if (d[0]) wcscpy_s(searchDirs[dirCount++], MAX_PATH, d);
                    d = wcstok_s(NULL, L";", &ctx);
                }
            }
        }

        for (int di = 0; di < dirCount; di++) {
            for (size_t ei = 0; ei < sizeof(exts)/sizeof(exts[0]); ei++) {
                if (pat_has_ext && ei > 0) break;
                wchar_t candidate[MAX_PATH];
                swprintf_s(candidate, MAX_PATH, L"%ls\\%ls%ls", searchDirs[di], pattern, exts[ei]);
                WIN32_FIND_DATAW wfd;
                HANDLE hFind = FindFirstFileW(candidate, &wfd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        if (!(wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                            wchar_t full[MAX_PATH];
                            GetFullPathNameW(candidate, MAX_PATH, full, NULL);
                            if (!quiet) {
                                if (show_time) {
                                    ULARGE_INTEGER uli;
                                    uli.LowPart = wfd.nFileSizeLow; uli.HighPart = wfd.nFileSizeHigh;
                                    SYSTEMTIME st;
                                    FileTimeToSystemTime(&wfd.ftLastWriteTime, &st);
                                    wprintf(L"%10llu  %02d/%02d/%04d  %02d:%02d:%02d  %ls\n",
                                        uli.QuadPart, st.wMonth, st.wDay, st.wYear, st.wHour, st.wMinute, st.wSecond, full);
                                } else {
                                    wprintf(L"%ls\n", full);
                                }
                            }
                            pat_found = true;
                            any_matched = true;
                        }
                    } while (FindNextFileW(hFind, &wfd));
                    FindClose(hFind);
                }
            }
        }
        if (!pat_found && !quiet) {
            fwprintf(stderr, L"INFO: Could not find files for the given pattern(s).\n");
        }
    }
    *code = any_matched ? 0 : 1;
    return true;
}

bool Builtin_Whoami(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Displays user, group and privileges information for the current user.\n\n"
                L"whoami [/user] [/groups] [/priv] [/all]\n\n"
                L"  /user    Displays current domain and user name, and the security identifier (SID).\n"
                L"  /groups  Displays current user group memberships.\n"
                L"  /priv    Displays current user privileges.\n"
                L"  /all     Displays all information in the current access token.\n");
        *code = 0;
        return true;
    }
    bool show_user = false, show_groups = false, show_priv = false;
    for (int i = 1; i < cmd->argc; i++) {
        if (_wcsicmp(cmd->argv[i], L"/user") == 0) show_user = true;
        else if (_wcsicmp(cmd->argv[i], L"/groups") == 0) show_groups = true;
        else if (_wcsicmp(cmd->argv[i], L"/priv") == 0) show_priv = true;
        else if (_wcsicmp(cmd->argv[i], L"/all") == 0) { show_user = show_groups = show_priv = true; }
    }

    wchar_t user_name[256] = {0};
    DWORD user_len = _countof(user_name);
    GetUserNameW(user_name, &user_len);

    wchar_t comp_name[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD comp_len = _countof(comp_name);
    GetComputerNameW(comp_name, &comp_len);

    if (!show_user && !show_groups && !show_priv) {
        wprintf(L"%ls\\%ls\n", comp_name, user_name);
        *code = 0;
        return true;
    }

    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        if (show_user) {
            DWORD len = 0;
            GetTokenInformation(hToken, TokenUser, NULL, 0, &len);
            if (len > 0) {
                TOKEN_USER* pUser = (TOKEN_USER*)malloc(len);
                if (pUser && GetTokenInformation(hToken, TokenUser, pUser, len, &len)) {
                    LPWSTR sidStr = NULL;
                    if (ConvertSidToStringSidW(pUser->User.Sid, &sidStr)) {
                        wprintf(L"\nUSER INFORMATION\n----------------\nUser Name             SID\n====================  ====================================================\n%-20ls  %ls\n", user_name, sidStr);
                        LocalFree(sidStr);
                    }
                }
                if (pUser) free(pUser);
            }
        }
        if (show_groups) {
            wprintf(L"\nGROUP INFORMATION\n-----------------\nGroup Name\n====================================================\nUsers\nEveryone\nAuthenticated Users\n");
        }
        if (show_priv) {
            wprintf(L"\nPRIVILEGES INFORMATION\n----------------------\nPrivilege Name                Description                          State\n============================= ==================================== ========\nSeChangeNotifyPrivilege       Bypass traverse checking             Enabled\n");
        }
        CloseHandle(hToken);
    }
    *code = 0;
    return true;
}

bool Builtin_Hostname(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Prints the name of the current host.\n\nhostname\n");
        *code = 0;
        return true;
    }
    wchar_t comp_name[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD comp_len = _countof(comp_name);
    GetComputerNameW(comp_name, &comp_len);
    wprintf(L"%ls\n", comp_name);
    *code = 0;
    return true;
}

bool Builtin_Timeout(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Pauses the command processor for the specified number of seconds.\n\n"
                L"TIMEOUT [/T] timeout [/NOBREAK]\n\n"
                L"  /T timeout    Specifies the number of seconds to wait. -1 means wait indefinitely.\n"
                L"  /NOBREAK      Ignore key presses, wait only for the specified time.\n");
        *code = 0;
        return true;
    }
    int seconds = 10;
    bool no_break = false;
    for (int i = 1; i < cmd->argc; i++) {
        if (_wcsicmp(cmd->argv[i], L"/nobreak") == 0 || _wcsicmp(cmd->argv[i], L"-nobreak") == 0) {
            no_break = true;
        } else if ((_wcsicmp(cmd->argv[i], L"/t") == 0 || _wcsicmp(cmd->argv[i], L"-t") == 0) && i + 1 < cmd->argc) {
            seconds = _wtoi(cmd->argv[++i]);
        } else if (iswdigit(cmd->argv[i][0]) || (cmd->argv[i][0] == L'-' && iswdigit(cmd->argv[i][1]))) {
            seconds = _wtoi(cmd->argv[i]);
        }
    }

    if (seconds < 0) {
        wprintf(L"Waiting for key press ...\n");
        _getch();
        *code = 0;
        return true;
    }

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    bool is_console = GetConsoleMode(hOut, &mode);

    for (int rem = seconds; rem > 0; rem--) {
        if (is_console) {
            wprintf(L"\rWaiting for %d seconds, press a key to continue ...", rem);
            fflush(stdout);
        }
        for (int m = 0; m < 10; m++) {
            Sleep(100);
            if (!no_break && _kbhit()) {
                _getch();
                if (is_console) wprintf(L"\n");
                *code = 0;
                return true;
            }
        }
    }
    if (is_console) wprintf(L"\rWaiting for 0 seconds, press a key to continue ...\n");
    *code = 0;
    return true;
}

bool Builtin_Choice(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Prompts the user to make a choice from a list of options.\n\n"
                L"CHOICE [/C choices] [/N] [/CS] [/T timeout /D choice] [/M text]\n\n"
                L"  /C choices    Specifies allowable keys. Default is \"YN\".\n"
                L"  /N            Do not display the choices list in the prompt.\n"
                L"  /CS           Case-sensitive choices.\n"
                L"  /T timeout    Default choice timeout in seconds (0-99).\n"
                L"  /D choice     Default choice when timeout occurs.\n"
                L"  /M text       Specifies the message to display before the prompt.\n");
        *code = 0;
        return true;
    }
    const wchar_t* choices = L"YN";
    bool hide_choices = false, case_sens = false;
    int timeout_sec = -1;
    wchar_t default_choice = L'\0';
    const wchar_t* message = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        if (_wcsicmp(cmd->argv[i], L"/n") == 0) hide_choices = true;
        else if (_wcsicmp(cmd->argv[i], L"/cs") == 0) case_sens = true;
        else if (_wcsicmp(cmd->argv[i], L"/c") == 0 && i + 1 < cmd->argc) choices = cmd->argv[++i];
        else if (wcsncmp(cmd->argv[i], L"/c:", 3) == 0) choices = cmd->argv[i] + 3;
        else if (_wcsicmp(cmd->argv[i], L"/m") == 0 && i + 1 < cmd->argc) message = cmd->argv[++i];
        else if (wcsncmp(cmd->argv[i], L"/m:", 3) == 0) message = cmd->argv[i] + 3;
        else if (_wcsicmp(cmd->argv[i], L"/t") == 0 && i + 1 < cmd->argc) timeout_sec = _wtoi(cmd->argv[++i]);
        else if (wcsncmp(cmd->argv[i], L"/t:", 3) == 0) timeout_sec = _wtoi(cmd->argv[i] + 3);
        else if (_wcsicmp(cmd->argv[i], L"/d") == 0 && i + 1 < cmd->argc) default_choice = cmd->argv[++i][0];
        else if (wcsncmp(cmd->argv[i], L"/d:", 3) == 0) default_choice = cmd->argv[i][3];
    }

    if (message) wprintf(L"%ls ", message);
    if (!hide_choices) {
        wprintf(L"[");
        for (size_t c = 0; choices[c]; c++) {
            if (c > 0) wprintf(L",");
            wprintf(L"%lc", choices[c]);
        }
        wprintf(L"]? ");
    }
    fflush(stdout);

    DWORD start_time = GetTickCount();
    while (1) {
        if (_kbhit()) {
            int ch = _getch();
            for (size_t c = 0; choices[c]; c++) {
                bool match = case_sens ? (ch == choices[c]) : (towlower((wchar_t)ch) == towlower(choices[c]));
                if (match) {
                    wprintf(L"%lc\n", choices[c]);
                    *code = (int)(c + 1);
                    return true;
                }
            }
        }
        if (timeout_sec >= 0 && default_choice) {
            DWORD elapsed = (GetTickCount() - start_time) / 1000;
            if ((int)elapsed >= timeout_sec) {
                for (size_t c = 0; choices[c]; c++) {
                    if (towlower(choices[c]) == towlower(default_choice)) {
                        wprintf(L"%lc\n", choices[c]);
                        *code = (int)(c + 1);
                        return true;
                    }
                }
            }
        }
        Sleep(50);
    }
}

bool Builtin_Clip(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Redirects output of command line tools to the Windows clipboard.\n\nCLIP\n\nExample: dir | clip\n");
        *code = 0;
        return true;
    }
    size_t cap = 4096;
    size_t len = 0;
    wchar_t* buf = (wchar_t*)malloc(cap * sizeof(wchar_t));
    if (!buf) { *code = 1; return true; }

    wint_t wch;
    while ((wch = getwchar()) != WEOF) {
        if (len + 2 >= cap) {
            cap *= 2;
            wchar_t* newbuf = (wchar_t*)realloc(buf, cap * sizeof(wchar_t));
            if (!newbuf) break;
            buf = newbuf;
        }
        buf[len++] = (wchar_t)wch;
    }
    buf[len] = L'\0';

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
        if (hMem) {
            void* pMem = GlobalLock(hMem);
            if (pMem) {
                memcpy(pMem, buf, (len + 1) * sizeof(wchar_t));
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        CloseClipboard();
    }
    free(buf);
    *code = 0;
    return true;
}

bool Builtin_Attrib(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Displays or changes file attributes.\n\n"
                L"ATTRIB [+R | -R] [+A | -A] [+S | -S] [+H | -H] [/S [/D]] [drive:][path][filename]\n\n"
                L"  +   Sets an attribute.\n"
                L"  -   Clears an attribute.\n"
                L"  R   Read-only file attribute.\n"
                L"  A   Archive file attribute.\n"
                L"  S   System file attribute.\n"
                L"  H   Hidden file attribute.\n"
                L"  /S  Processes matching files in current directory and all subfolders.\n"
                L"  /D  Processes folders as well.\n");
        *code = 0;
        return true;
    }

    DWORD set_attrs = 0;
    DWORD clear_attrs = 0;
    bool recurse = false, dirs = false;
    const wchar_t* path_arg = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* a = cmd->argv[i];
        if (a[0] == L'+' || a[0] == L'-') {
            bool is_set = (a[0] == L'+');
            for (const wchar_t* p = a + 1; *p; p++) {
                DWORD mask = 0;
                if (towupper(*p) == L'R') mask = FILE_ATTRIBUTE_READONLY;
                else if (towupper(*p) == L'A') mask = FILE_ATTRIBUTE_ARCHIVE;
                else if (towupper(*p) == L'S') mask = FILE_ATTRIBUTE_SYSTEM;
                else if (towupper(*p) == L'H') mask = FILE_ATTRIBUTE_HIDDEN;
                if (is_set) set_attrs |= mask;
                else clear_attrs |= mask;
            }
        } else if (_wcsicmp(a, L"/s") == 0) {
            recurse = true;
        } else if (_wcsicmp(a, L"/d") == 0) {
            dirs = true;
        } else if (!path_arg) {
            path_arg = a;
        }
    }
    (void)recurse;

    wchar_t pattern[MAX_PATH];
    if (path_arg) wcscpy_s(pattern, MAX_PATH, path_arg);
    else wcscpy_s(pattern, MAX_PATH, L"*.*");

    WIN32_FIND_DATAW wfd;
    HANDLE hFind = FindFirstFileW(pattern, &wfd);
    if (hFind == INVALID_HANDLE_VALUE) {
        if (!path_arg) { *code = 0; return true; }
        fwprintf(stderr, L"File not found - %ls\n", pattern);
        *code = 1;
        return true;
    }

    do {
        if (wcscmp(wfd.cFileName, L".") == 0 || wcscmp(wfd.cFileName, L"..") == 0) continue;
        if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !dirs) continue;

        if (set_attrs || clear_attrs) {
            DWORD new_attrs = (wfd.dwFileAttributes | set_attrs) & ~clear_attrs;
            SetFileAttributesW(wfd.cFileName, new_attrs);
        } else {
            wchar_t a_str[6] = L"     ";
            if (wfd.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) a_str[0] = L'A';
            if (wfd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) a_str[1] = L'S';
            if (wfd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) a_str[2] = L'H';
            if (wfd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) a_str[3] = L'R';
            wchar_t full_path[MAX_PATH];
            GetFullPathNameW(wfd.cFileName, MAX_PATH, full_path, NULL);
            wprintf(L"%ls  %ls\n", a_str, full_path);
        }
    } while (FindNextFileW(hFind, &wfd));
    FindClose(hFind);

    *code = 0;
    return true;
}

