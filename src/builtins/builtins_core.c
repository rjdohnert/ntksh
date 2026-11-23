#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

static bool IsCdPathCandidate(const wchar_t* path) {
    if (!*path) return false;
    if (wcscmp(path, L".") == 0 || wcscmp(path, L"..") == 0) return false;
    if (wcsncmp(path, L"./", 2) == 0 || wcsncmp(path, L".\\", 2) == 0) return false;
    if (wcsncmp(path, L"../", 3) == 0 || wcsncmp(path, L"..\\", 3) == 0) return false;
    if (path[0] == L'/' || path[0] == L'\\') return false;
    if (wcslen(path) > 1 && path[1] == L':') return false;
    return true;
}

/* Returns a newly allocated directory path for target found via CDPATH, or NULL.
   Entries are separated by ';' (':' would clash with drive letters). An empty entry
   means the current directory. *viaEntry is set when a non-empty entry matched. */
static wchar_t* FindCdPathDirectory(const wchar_t* cdpath, const wchar_t* target,
    bool* viaEntry) {
    const wchar_t* start = cdpath;
    for (;;) {
        const wchar_t* end = wcschr(start, L';');
        size_t length = end ? (size_t)(end - start) : wcslen(start);
        wchar_t* candidate = (wchar_t*)malloc((length + wcslen(target) + 2) * sizeof(wchar_t));
        if (!candidate) return NULL;
        size_t targetBytes = (wcslen(target) + 1) * sizeof(wchar_t);
        if (length == 0) {
            memcpy(candidate, target, targetBytes);
        } else {
            memcpy(candidate, start, length * sizeof(wchar_t));
            candidate[length] = L'\0';
            if (candidate[length - 1] != L'\\' && candidate[length - 1] != L'/')
                candidate[length++] = L'\\';
            memcpy(candidate + length, target, targetBytes);
        }
        DWORD attributes = GetFileAttributesW(candidate);
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
            *viaEntry = length > 0;
            return candidate;
        }
        free(candidate);
        if (!end) return NULL;
        start = end + 1;
    }
}

bool Builtin_CD(IShell* shell, CommandAST* cmd, int* code) {
    const wchar_t* target = NULL;
    wchar_t* allocatedTarget = NULL;
    wchar_t* cdpathTarget = NULL;
    wchar_t* oldDirectory = GetCurrentDirectoryAlloc();
    bool printDirectory = false;

    if (cmd->argc > 1 && _wcsicmp(cmd->argv[1], L"/?") == 0) {
        wprintf(L"Displays the name of or changes the current directory.\n\nCHDIR [/D] [drive:][path]\nCHDIR [..]\nCD [/D] [drive:][path]\nCD [..]\n\n  /D    Changes current drive in addition to changing current directory for a drive.\n");
        free(oldDirectory);
        *code = 0;
        return true;
    }
    int argIdx = 1;
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/d") == 0 ||
        wcscmp(cmd->argv[1], L"-L") == 0 || wcscmp(cmd->argv[1], L"-P") == 0)) {
        argIdx = 2;
    }
    if (cmd->argc > argIdx + 1) {
        fwprintf(stderr, L"ksh: cd: too many arguments\n");
        free(oldDirectory);
        *code = 2;
        return true;
    }
    if (cmd->argc > argIdx && wcscmp(cmd->argv[argIdx], L"-") == 0) {
        target = shell->env->Get(shell->env, L"OLDPWD");
        if (!target) allocatedTarget = GetEnvironmentValue(L"OLDPWD");
        printDirectory = true;
    } else if (cmd->argc > argIdx &&
               (wcscmp(cmd->argv[argIdx], L"~") == 0 ||
                wcsncmp(cmd->argv[argIdx], L"~/", 2) == 0 ||
                wcsncmp(cmd->argv[argIdx], L"~\\", 2) == 0)) {
        const wchar_t* home = shell->env->Get(shell->env, L"HOME");
        wchar_t* allocatedHome = NULL;
        if (!home) home = allocatedHome = GetEnvironmentValue(L"HOME");
        if (!home) home = allocatedHome = GetEnvironmentValue(L"USERPROFILE");
        if (!home) home = L"\\";

        if (cmd->argv[argIdx][1] == L'\0') {
            if (allocatedHome) {
                allocatedTarget = allocatedHome;
                target = allocatedTarget;
            } else {
                target = home;
            }
        } else {
            size_t hlen = wcslen(home);
            size_t rlen = wcslen(cmd->argv[argIdx] + 1);
            allocatedTarget = (wchar_t*)malloc((hlen + rlen + 1) * sizeof(wchar_t));
            if (allocatedTarget) {
                memcpy(allocatedTarget, home, hlen * sizeof(wchar_t));
                memcpy(allocatedTarget + hlen, cmd->argv[argIdx] + 1, (rlen + 1) * sizeof(wchar_t));
                target = allocatedTarget;
            }
            free(allocatedHome);
        }
    } else if (cmd->argc > argIdx) {
        target = cmd->argv[argIdx];
    } else {
        target = shell->env->Get(shell->env, L"HOME");
        if (!target) allocatedTarget = GetEnvironmentValue(L"HOME");
        if (!target && !allocatedTarget) allocatedTarget = GetEnvironmentValue(L"USERPROFILE");
        if (!target && !allocatedTarget) target = L"\\";
    }
    if (allocatedTarget) target = allocatedTarget;
    if (!target || !*target) {
        fwprintf(stderr, L"ksh: cd: OLDPWD or HOME is not set\n");
        free(allocatedTarget);
        free(oldDirectory);
        *code = 1;
        return true;
    }
    bool fromArgument = cmd->argc > argIdx && wcscmp(cmd->argv[argIdx], L"-") != 0;
    if (fromArgument && IsCdPathCandidate(target)) {
        const wchar_t* cdpath = shell->env->Get(shell->env, L"CDPATH");
        wchar_t* inheritedCdpath = NULL;
        if (!cdpath) cdpath = inheritedCdpath = GetEnvironmentValue(L"CDPATH");
        if (cdpath && *cdpath) {
            bool viaEntry = false;
            cdpathTarget = FindCdPathDirectory(cdpath, target, &viaEntry);
            if (cdpathTarget) {
                target = cdpathTarget;
                printDirectory = viaEntry || printDirectory;
            }
        }
        free(inheritedCdpath);
    }
    if (!SetCurrentDirectoryW(target)) {
        fwprintf(stderr, L"ksh: cd: %ls: %ls\n", target,
            GetLastError() == ERROR_PATH_NOT_FOUND ||
            GetLastError() == ERROR_FILE_NOT_FOUND ?
                L"No such directory" : L"cannot change directory");
        *code = 1;
    } else {
        wchar_t* newDirectory = GetCurrentDirectoryAlloc();
        if (!newDirectory) {
            fwprintf(stderr, L"ksh: cd: cannot determine the new directory\n");
            *code = 1;
        } else {
            if (oldDirectory &&
                !shell->env->Set(shell->env, L"OLDPWD", oldDirectory)) {
                fwprintf(stderr, L"ksh: cd: cannot update OLDPWD\n");
                *code = 1;
            } else if (!shell->env->Set(shell->env, L"PWD", newDirectory)) {
                fwprintf(stderr, L"ksh: cd: cannot update PWD\n");
                *code = 1;
            } else {
                *code = 0;
                if (printDirectory) {
                    PrintPosixPath(newDirectory);
                    putwchar(L'\n');
                }
            }
            free(newDirectory);
        }
    }
    free(cdpathTarget);
    free(allocatedTarget);
    free(oldDirectory);
    return true;
}

bool Builtin_PWD(IShell* shell, CommandAST* cmd, int* code) {
    bool physical = false;
    if (cmd->argc > 2 || (cmd->argc == 2 &&
            wcscmp(cmd->argv[1], L"-L") != 0 &&
            wcscmp(cmd->argv[1], L"-P") != 0)) {
        fwprintf(stderr, L"ksh: pwd: usage: pwd [-L | -P]\n");
        *code = 2;
        return true;
    }
    if (cmd->argc == 2) physical = wcscmp(cmd->argv[1], L"-P") == 0;

    wchar_t* cwd = GetCurrentDirectoryAlloc();
    if (!cwd) {
        fwprintf(stderr, L"ksh: pwd: cannot determine current directory\n");
        *code = 1;
        return true;
    }
    if (!physical) {
        const wchar_t* logical = shell->env->Get(shell->env, L"PWD");
        wchar_t* allocatedLogical = logical ? NULL :
            GetEnvironmentValue(L"PWD");
        if (!logical) logical = allocatedLogical;
        if (logical && SameDirectory(logical, cwd)) {
            free(cwd);
            cwd = SafeStrDup(logical);
            if (!cwd) {
                free(allocatedLogical);
                fwprintf(stderr, L"ksh: pwd: out of memory\n");
                *code = 1;
                return true;
            }
        }
        free(allocatedLogical);
    }
    if (physical) {
        HANDLE directory = CreateFileW(cwd, 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
            OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (directory == INVALID_HANDLE_VALUE) {
            fwprintf(stderr, L"ksh: pwd: cannot resolve current directory (error %lu)\n",
                GetLastError());
            free(cwd);
            *code = 1;
            return true;
        }
        DWORD required = GetFinalPathNameByHandleW(directory, NULL, 0,
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        wchar_t* resolved = required ?
            (wchar_t*)malloc((size_t)(required + 1) * sizeof(wchar_t)) : NULL;
        DWORD length = resolved ? GetFinalPathNameByHandleW(directory,
            resolved, required + 1,
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS) : 0;
        DWORD error = GetLastError();
        CloseHandle(directory);
        if (!length || length > required) {
            fwprintf(stderr, L"ksh: pwd: cannot resolve current directory (error %lu)\n",
                error);
            free(resolved);
            free(cwd);
            *code = 1;
            return true;
        }
        free(cwd);
        cwd = resolved;
    }
    if (wcsncmp(cwd, L"\\\\?\\UNC\\", 8) == 0) {
        fputws(L"//", stdout);
        PrintPosixPath(cwd + 8);
        putwchar(L'\n');
    } else if (wcsncmp(cwd, L"\\\\?\\", 4) == 0) {
        PrintPosixPath(cwd + 4);
        putwchar(L'\n');
    } else {
        PrintPosixPath(cwd);
        putwchar(L'\n');
    }
    *code = ferror(stdout) ? 1 : 0;
    free(cwd);
    return true;
}

bool Builtin_Local(IShell* shell, CommandAST* cmd, int* code) {
    if (shell->functionDepth <= 0 || !shell->localScope) {
        fwprintf(stderr, L"ksh: local: not in a function\n");
        *code = 2;
        return true;
    }
    if (cmd->argc < 2) {
        fwprintf(stderr, L"ksh: local: usage: local name[=value] ...\n");
        *code = 2;
        return true;
    }
    for (int i = 1; i < cmd->argc; ++i) {
        const wchar_t* equal = wcschr(cmd->argv[i], L'=');
        size_t nameLength = equal ? (size_t)(equal - cmd->argv[i]) :
            wcslen(cmd->argv[i]);
        wchar_t* name = (wchar_t*)malloc((nameLength + 1) * sizeof(wchar_t));
        if (!name) {
            fwprintf(stderr, L"ksh: local: out of memory\n");
            *code = 1;
            return true;
        }
        memcpy(name, cmd->argv[i], nameLength * sizeof(wchar_t));
        name[nameLength] = L'\0';
        if (!IsValidIdentifier(name)) {
            fwprintf(stderr, L"ksh: local: '%ls': not a valid identifier\n",
                cmd->argv[i]);
            free(name);
            *code = 2;
            return true;
        }
        const wchar_t* value = equal ? equal + 1 : L"";
        if (!Shell_AddLocal(shell, name, value)) {
            fwprintf(stderr, L"ksh: local: cannot create '%ls'\n", name);
            free(name);
            *code = 1;
            return true;
        }
        free(name);
    }
    *code = 0;
    return true;
}

bool Builtin_Pushd(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc != 2) {
        fwprintf(stderr, L"ksh: pushd: usage: pushd directory\n");
        *code = 2;
        return true;
    }
    DWORD length = GetCurrentDirectoryW(0, NULL);
    if (!length || length >= 32768) {
        fwprintf(stderr, L"ksh: pushd: cannot determine current directory\n");
        *code = 1;
        return true;
    }
    wchar_t* previous = (wchar_t*)malloc((size_t)length * sizeof(wchar_t));
    DirectoryStackEntry* entry =
        (DirectoryStackEntry*)calloc(1, sizeof(DirectoryStackEntry));
    if (!previous || !entry ||
        !GetCurrentDirectoryW(length, previous) ||
        !(entry->path = SafeStrDup(previous))) {
        free(previous);
        if (entry) {
            free(entry->path);
            free(entry);
        }
        fwprintf(stderr, L"ksh: pushd: out of memory or path unavailable\n");
        *code = 1;
        return true;
    }
    free(previous);

    const wchar_t* rawTarget = cmd->argv[1];
    wchar_t mappedTarget[MAX_PATH * 2] = { 0 };
    const wchar_t* navTarget = rawTarget;

    /* UNC auto-mapping support matching Windows CMD pushd behavior */
    if ((rawTarget[0] == L'\\' && rawTarget[1] == L'\\') ||
        (rawTarget[0] == L'/' && rawTarget[1] == L'/')) {
        wchar_t norm[MAX_PATH * 2];
        wcsncpy_s(norm, _countof(norm), rawTarget, _TRUNCATE);
        for (wchar_t* p = norm; *p; p++) if (*p == L'/') *p = L'\\';

        wchar_t* serverStart = norm + 2;
        wchar_t* shareStart = wcschr(serverStart, L'\\');
        if (shareStart && shareStart[1]) {
            shareStart++;
            wchar_t* shareEnd = wcschr(shareStart, L'\\');
            wchar_t remoteShare[MAX_PATH * 2] = { 0 };
            const wchar_t* subDir = shareEnd ? shareEnd : L"\\";
            if (shareEnd) {
                size_t shareLen = (size_t)(shareEnd - norm);
                wcsncpy_s(remoteShare, _countof(remoteShare), norm, shareLen);
            } else {
                wcscpy_s(remoteShare, _countof(remoteShare), norm);
            }

            DWORD drives = GetLogicalDrives();
            wchar_t freeDrive[4] = { 0 };
            for (int d = 25; d >= 3; d--) {
                if (!(drives & (1 << d))) {
                    freeDrive[0] = (wchar_t)(L'A' + d);
                    freeDrive[1] = L':';
                    freeDrive[2] = L'\0';
                    break;
                }
            }

            if (freeDrive[0]) {
                typedef struct {
                    DWORD dwScope;
                    DWORD dwType;
                    DWORD dwDisplayType;
                    DWORD dwUsage;
                    LPWSTR lpLocalName;
                    LPWSTR lpRemoteName;
                    LPWSTR lpComment;
                    LPWSTR lpProvider;
                } KSH_NETRES;
                typedef DWORD (APIENTRY *PFN_WNetAddConn)(KSH_NETRES*, LPCWSTR, LPCWSTR, DWORD);
                HMODULE hMpr = LoadLibraryW(L"mpr.dll");
                if (hMpr) {
                    PFN_WNetAddConn pfnAdd = (PFN_WNetAddConn)GetProcAddress(hMpr, "WNetAddConnection2W");
                    if (pfnAdd) {
                        KSH_NETRES nr = { 0 };
                        nr.dwType = 1; /* RESOURCETYPE_DISK */
                        nr.lpLocalName = freeDrive;
                        nr.lpRemoteName = remoteShare;
                        if (pfnAdd(&nr, NULL, NULL, 0) == 0) {
                            wcscpy_s(entry->mappedDrive, _countof(entry->mappedDrive), freeDrive);
                            swprintf_s(mappedTarget, _countof(mappedTarget), L"%ls%ls", freeDrive, subDir);
                            navTarget = mappedTarget;
                        }
                    }
                    FreeLibrary(hMpr);
                }
            }
        }
    }

    CommandAST changeDirectory = { 0 };
    changeDirectory.argc = 2;
    changeDirectory.argv[0] = L"cd";
    changeDirectory.argv[1] = (wchar_t*)navTarget;
    (void)Builtin_CD(shell, &changeDirectory, code);
    if (*code != 0) {
        if (entry->mappedDrive[0]) {
            HMODULE hMpr = LoadLibraryW(L"mpr.dll");
            if (hMpr) {
                typedef DWORD (APIENTRY *PFN_WNetCancelConn)(LPCWSTR, DWORD, BOOL);
                PFN_WNetCancelConn pfnCancel = (PFN_WNetCancelConn)GetProcAddress(hMpr, "WNetCancelConnection2W");
                if (pfnCancel) pfnCancel(entry->mappedDrive, 0, TRUE);
                FreeLibrary(hMpr);
            }
        }
        free(entry->path);
        free(entry);
        return true;
    }
    entry->next = shell->directoryStack;
    shell->directoryStack = entry;
    return true;
}

bool Builtin_Popd(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc != 1) {
        fwprintf(stderr, L"ksh: popd: usage: popd\n");
        *code = 2;
        return true;
    }
    DirectoryStackEntry* entry = shell->directoryStack;
    if (!entry) {
        fwprintf(stderr, L"ksh: popd: directory stack is empty\n");
        *code = 1;
        return true;
    }
    CommandAST changeDirectory = { 0 };
    changeDirectory.argc = 2;
    changeDirectory.argv[0] = L"cd";
    changeDirectory.argv[1] = entry->path;
    (void)Builtin_CD(shell, &changeDirectory, code);
    if (*code != 0) return true;

    if (entry->mappedDrive[0]) {
        HMODULE hMpr = LoadLibraryW(L"mpr.dll");
        if (hMpr) {
            typedef DWORD (APIENTRY *PFN_WNetCancelConn)(LPCWSTR, DWORD, BOOL);
            PFN_WNetCancelConn pfnCancel = (PFN_WNetCancelConn)GetProcAddress(hMpr, "WNetCancelConnection2W");
            if (pfnCancel) pfnCancel(entry->mappedDrive, 0, TRUE);
            FreeLibrary(hMpr);
        }
    }

    shell->directoryStack = entry->next;
    free(entry->path);
    free(entry);
    return true;
}

bool Builtin_Echo(CommandAST* cmd, int* code) {
    /* XSI behavior: backslash escapes are interpreted by default. -E disables them. */
    bool no_newline = false;
    bool escapes = true;
    int start_idx = 1;

    while (start_idx < cmd->argc && cmd->argv[start_idx][0] == L'-' &&
        cmd->argv[start_idx][1] && wcscmp(cmd->argv[start_idx], L"--") != 0) {
        bool valid = true;
        for (const wchar_t* option = cmd->argv[start_idx] + 1; *option;
            ++option) {
            if (*option == L'n') no_newline = true;
            else if (*option == L'e') escapes = true;
            else if (*option == L'E') escapes = false;
            else {
                valid = false;
                break;
            }
        }
        if (!valid) break;
        start_idx++;
    }
    if (start_idx < cmd->argc && wcscmp(cmd->argv[start_idx], L"--") == 0)
        start_idx++;

    for (int i = start_idx; i < cmd->argc; i++) {
        if (i > start_idx) putwchar(L' ');
        if (!escapes) {
            fputws(cmd->argv[i], stdout);
            continue;
        }
        for (const wchar_t* p = cmd->argv[i]; *p; ++p) {
            if (*p != L'\\' || !p[1]) {
                putwchar(*p);
                continue;
            }
            switch (*++p) {
            case L'a': putwchar(L'\a'); break;
            case L'b': putwchar(L'\b'); break;
            case L'c': *code = ferror(stdout) ? 1 : 0; return true;
            case L'e': putwchar(27); break;
            case L'f': putwchar(L'\f'); break;
            case L'n': putwchar(L'\n'); break;
            case L'r': putwchar(L'\r'); break;
            case L't': putwchar(L'\t'); break;
            case L'v': putwchar(L'\v'); break;
            case L'\\': putwchar(L'\\'); break;
            default:
                putwchar(L'\\');
                putwchar(*p);
                break;
            }
        }
    }
    if (!no_newline) putwchar(L'\n');
    fflush(stdout);
    *code = ferror(stdout) ? 1 : 0;
    return true;
}

static bool PrintPrintfEscapes(const wchar_t* text, bool* stop) {
    for (size_t i = 0; text[i]; ++i) {
        if (text[i] != L'\\' || !text[i + 1]) {
            if (putwchar(text[i]) == WEOF) return false;
            continue;
        }
        wchar_t escaped = text[++i];
        switch (escaped) {
        case L'a': if (putwchar(L'\a') == WEOF) return false; break;
        case L'b': if (putwchar(L'\b') == WEOF) return false; break;
        case L'f': if (putwchar(L'\f') == WEOF) return false; break;
        case L'n': if (putwchar(L'\n') == WEOF) return false; break;
        case L'r': if (putwchar(L'\r') == WEOF) return false; break;
        case L't': if (putwchar(L'\t') == WEOF) return false; break;
        case L'v': if (putwchar(L'\v') == WEOF) return false; break;
        case L'\\': if (putwchar(L'\\') == WEOF) return false; break;
        case L'c': *stop = true; return true;
        case L'0': {
            unsigned value = 0;
            int digits = 0;
            while (digits < 3 && text[i + 1] >= L'0' &&
                text[i + 1] <= L'7') {
                value = value * 8 + (unsigned)(text[++i] - L'0');
                digits++;
            }
            if (putwchar((wchar_t)value) == WEOF) return false;
            break;
        }
        default:
            if (putwchar(L'\\') == WEOF || putwchar(escaped) == WEOF)
                return false;
            break;
        }
    }
    return true;
}

static bool PrintPrintfFormatEscape(const wchar_t* format, size_t* position,
    bool* stop) {
    size_t i = *position + 1;
    if (!format[i]) {
        *position = i;
        return putwchar(L'\\') != WEOF;
    }
    wchar_t escaped = format[i++];
    switch (escaped) {
    case L'a': if (putwchar(L'\a') == WEOF) return false; break;
    case L'b': if (putwchar(L'\b') == WEOF) return false; break;
    case L'f': if (putwchar(L'\f') == WEOF) return false; break;
    case L'n': if (putwchar(L'\n') == WEOF) return false; break;
    case L'r': if (putwchar(L'\r') == WEOF) return false; break;
    case L't': if (putwchar(L'\t') == WEOF) return false; break;
    case L'v': if (putwchar(L'\v') == WEOF) return false; break;
    case L'\\': if (putwchar(L'\\') == WEOF) return false; break;
    case L'c': *stop = true; break;
    case L'0': {
        unsigned value = 0;
        int digits = 0;
        while (digits < 3 && format[i] >= L'0' && format[i] <= L'7') {
            value = value * 8 + (unsigned)(format[i++] - L'0');
            digits++;
        }
        if (putwchar((wchar_t)value) == WEOF) return false;
        break;
    }
    default:
        if (putwchar(L'\\') == WEOF || putwchar(escaped) == WEOF)
            return false;
        break;
    }
    *position = i;
    return true;
}

bool Builtin_Printf(CommandAST* cmd, int* code) {
    int first = 1;
    if (first < cmd->argc && wcscmp(cmd->argv[first], L"--") == 0) first++;
    if (first >= cmd->argc) {
        fwprintf(stderr, L"ksh: printf: missing format operand\n");
        *code = 2;
        return true;
    }

    const wchar_t* format = cmd->argv[first++];
    int argument = first;
    bool stop = false;
    bool outputError = false;
    bool repeat;

    do {
        bool consumedArgument = false;
        repeat = false;
        for (size_t i = 0; format[i] && !stop;) {
            if (format[i] == L'\\') {
                if (!PrintPrintfFormatEscape(format, &i, &stop)) {
                    outputError = true;
                    break;
                }
                continue;
            }
            if (format[i] != L'%') {
                if (putwchar(format[i++]) == WEOF) outputError = true;
                if (outputError) break;
                continue;
            }
            i++;
            if (format[i] == L'%') {
                if (putwchar(L'%') == WEOF) outputError = true;
                i++;
                if (outputError) break;
                continue;
            }
            wchar_t specifier[128] = L"%";
            size_t specLength = 1;
            wchar_t flags[16] = {0};
            size_t flagsLength = 0;
            while (wcschr(L"-+ #0'", format[i]) && format[i]) {
                if (flagsLength + 1 >= sizeof(flags) / sizeof(flags[0])) {
                    outputError = true;
                    break;
                }
                flags[flagsLength++] = format[i++];
            }
            flags[flagsLength] = L'\0';
            for (size_t f = 0; f < flagsLength; ++f)
                specifier[specLength++] = flags[f];
            if (outputError) break;

            wchar_t widthText[16] = {0};
            if (format[i] == L'*') {
                long width = 0;
                if (argument < cmd->argc) {
                    wchar_t* end = NULL;
                    errno = 0;
                    width = wcstol(cmd->argv[argument++], &end, 10);
                    consumedArgument = true;
                    if (errno || end == cmd->argv[argument - 1] || *end) {
                        fwprintf(stderr, L"ksh: printf: invalid field width\n");
                        outputError = true;
                        break;
                    }
                }
                if (width < 0) {
                    if (!wcschr(flags, L'-') && specLength + 1 < 128)
                        specifier[specLength++] = L'-';
                    width = -width;
                }
                if (width > 4096) width = 4096;
                int written = swprintf_s(widthText,
                    sizeof(widthText) / sizeof(widthText[0]), L"%ld", width);
                if (written < 0 || specLength + (size_t)written >= 128) {
                    outputError = true;
                    break;
                }
                memcpy(specifier + specLength, widthText,
                    (size_t)written * sizeof(wchar_t));
                specLength += (size_t)written;
                i++;
            } else {
                while (iswdigit(format[i])) {
                    if (specLength + 1 >= 128) {
                        outputError = true;
                        break;
                    }
                    specifier[specLength++] = format[i++];
                }
            }
            if (outputError) break;
            if (format[i] == L'.') {
                if (specLength + 1 >= 128) {
                    outputError = true;
                    break;
                }
                specifier[specLength++] = format[i++];
                if (format[i] == L'*') {
                    long precision = 0;
                    if (argument < cmd->argc) {
                        wchar_t* end = NULL;
                        errno = 0;
                        precision = wcstol(cmd->argv[argument++], &end, 10);
                        consumedArgument = true;
                        if (errno || end == cmd->argv[argument - 1] || *end) {
                            fwprintf(stderr, L"ksh: printf: invalid precision\n");
                            outputError = true;
                            break;
                        }
                    }
                    if (precision < 0) {
                        specLength--;
                    } else {
                        if (precision > 4096) precision = 4096;
                        int written = swprintf_s(widthText,
                            sizeof(widthText) / sizeof(widthText[0]), L"%ld",
                            precision);
                        if (written < 0 ||
                            specLength + (size_t)written >= 128) {
                            outputError = true;
                            break;
                        }
                        memcpy(specifier + specLength, widthText,
                            (size_t)written * sizeof(wchar_t));
                        specLength += (size_t)written;
                    }
                    i++;
                } else {
                    while (iswdigit(format[i])) {
                        if (specLength + 1 >= 128) {
                            outputError = true;
                            break;
                        }
                        specifier[specLength++] = format[i++];
                    }
                }
            }
            if (outputError) break;
            while (wcschr(L"hljztL", format[i]) && format[i]) i++;
            wchar_t conversion = format[i];
            if (!conversion) {
                fwprintf(stderr, L"ksh: printf: incomplete format specifier\n");
                outputError = true;
                break;
            }
            i++;
            if (conversion == L'n') {
                fwprintf(stderr, L"ksh: printf: %%n is not supported\n");
                outputError = true;
                break;
            }
            if (!wcschr(L"diouxXfFeEgGaAcspbq", conversion)) {
                fwprintf(stderr, L"ksh: printf: invalid conversion specifier\n");
                outputError = true;
                break;
            }
            if (argument < cmd->argc) {
                consumedArgument = true;
                repeat = true;
            }
            const wchar_t* value = argument < cmd->argc ?
                cmd->argv[argument++] : L"";
            if (conversion == L'b') {
                bool escapedStop = false;
                if (!PrintPrintfEscapes(value, &escapedStop))
                    outputError = true;
                stop = stop || escapedStop;
                continue;
            }
            if (conversion == L'q') {
                if (putwchar(L'\'') == WEOF) outputError = true;
                for (const wchar_t* p = value; *p && !outputError; ++p) {
                    if (*p == L'\'') {
                        if (fputws(L"'\\''", stdout) == WEOF)
                            outputError = true;
                    } else if (putwchar(*p) == WEOF) {
                        outputError = true;
                    }
                }
                if (putwchar(L'\'') == WEOF) outputError = true;
                continue;
            }

            if (conversion == L's' || conversion == L'c') {
                if (specLength + 2 >= 128) {
                    outputError = true;
                    break;
                }
                specifier[specLength++] = L'l';
                specifier[specLength++] = conversion;
                specifier[specLength] = L'\0';
                int result = conversion == L's' ?
                    wprintf(specifier, value) :
                    wprintf(specifier, *value ? *value : L'\0');
                if (result < 0) outputError = true;
            } else if (wcschr(L"diouxX", conversion)) {
                wchar_t* end = NULL;
                errno = 0;
                bool isUnsigned = wcschr(L"ouxX", conversion) != NULL;
                unsigned long long unsignedValue = isUnsigned ?
                    wcstoull(value, &end, conversion == L'o' ? 8 :
                        (conversion == L'x' || conversion == L'X') ? 16 : 10) :
                    (unsigned long long)wcstoll(value, &end, 0);
                if (*value && (errno || end == value || *end)) {
                    fwprintf(stderr, L"ksh: printf: '%ls' is not numeric\n",
                        value);
                    outputError = true;
                    break;
                }
                if (specLength + 3 >= 128) {
                    outputError = true;
                    break;
                }
                specifier[specLength++] = L'l';
                specifier[specLength++] = L'l';
                specifier[specLength++] = conversion;
                specifier[specLength] = L'\0';
                int result = isUnsigned ?
                    wprintf(specifier, unsignedValue) :
                    wprintf(specifier, (long long)unsignedValue);
                if (result < 0) outputError = true;
            } else if (conversion == L'p') {
                wchar_t* end = NULL;
                errno = 0;
                unsigned long long pointerValue = wcstoull(value, &end, 0);
                if (*value && (errno || end == value || *end)) {
                    fwprintf(stderr, L"ksh: printf: '%ls' is not numeric\n",
                        value);
                    outputError = true;
                    break;
                }
                if (specLength + 1 >= 128) {
                    outputError = true;
                    break;
                }
                specifier[specLength++] = L'p';
                specifier[specLength] = L'\0';
                if (wprintf(specifier,
                        (void*)(uintptr_t)pointerValue) < 0)
                    outputError = true;
            } else {
                wchar_t* end = NULL;
                errno = 0;
                double numericValue = wcstod(value, &end);
                if (*value && (errno || end == value || *end)) {
                    fwprintf(stderr, L"ksh: printf: '%ls' is not numeric\n",
                        value);
                    outputError = true;
                    break;
                }
                if (specLength + 1 >= 128) {
                    outputError = true;
                    break;
                }
                specifier[specLength++] = conversion;
                specifier[specLength] = L'\0';
                if (wprintf(specifier, numericValue) < 0)
                    outputError = true;
            }
            if (outputError) break;
        }
        if (!consumedArgument || argument >= cmd->argc) repeat = false;
    } while (repeat && !stop && !outputError);

    *code = outputError || ferror(stdout) ? 1 : 0;
    return true;
}

static bool PrintShellEscapes(FILE* output, const wchar_t* text,
    bool* stopOutput) {
    for (size_t i = 0; text[i]; ++i) {
        if (text[i] != L'\\' || !text[i + 1]) {
            if (fputwc(text[i], output) == WEOF) return false;
            continue;
        }
        wchar_t escaped = text[++i];
        switch (escaped) {
        case L'a': if (fputwc(L'\a', output) == WEOF) return false; break;
        case L'b': if (fputwc(L'\b', output) == WEOF) return false; break;
        case L'c': *stopOutput = true; return true;
        case L'e':
        case L'E': if (fputwc(27, output) == WEOF) return false; break;
        case L'f': if (fputwc(L'\f', output) == WEOF) return false; break;
        case L'n': if (fputwc(L'\n', output) == WEOF) return false; break;
        case L'r': if (fputwc(L'\r', output) == WEOF) return false; break;
        case L't': if (fputwc(L'\t', output) == WEOF) return false; break;
        case L'v': if (fputwc(L'\v', output) == WEOF) return false; break;
        case L'\\': if (fputwc(L'\\', output) == WEOF) return false; break;
        case L'0': {
            unsigned value = 0;
            unsigned digits = 0;
            while (digits < 3 && text[i + 1] >= L'0' &&
                text[i + 1] <= L'7') {
                value = value * 8 + (unsigned)(text[++i] - L'0');
                digits++;
            }
            if (fputwc((wchar_t)value, output) == WEOF) return false;
            break;
        }
        default:
            if (fputwc(L'\\', output) == WEOF ||
                fputwc(escaped, output) == WEOF)
                return false;
            break;
        }
    }
    return true;
}

bool Builtin_Print(IShell* shell, CommandAST* cmd, int* code) {
    bool noNewline = false;
    bool raw = false;
    bool lineByLine = false;
    bool toCoprocess = false;
    FILE* output = stdout;
    int index = 1;
    while (index < cmd->argc && cmd->argv[index][0] == L'-' &&
        cmd->argv[index][1] && wcscmp(cmd->argv[index], L"--") != 0) {
        const wchar_t* option = cmd->argv[index] + 1;
        if (option[0] == L'u') {
            const wchar_t* numStr = NULL;
            if (option[1] != L'\0') {
                numStr = option + 1;
            } else if (++index < cmd->argc) {
                numStr = cmd->argv[index];
            } else {
                fwprintf(stderr, L"ksh: print: -u requires a file descriptor\n");
                *code = 2;
                return true;
            }
            wchar_t* end = NULL;
            errno = 0;
            long descriptor = wcstol(numStr, &end, 10);
            if (errno || end == numStr || *end ||
                (descriptor != 1 && descriptor != 2)) {
                fwprintf(stderr,
                    L"ksh: print: only writable descriptors 1 and 2 are available\n");
                *code = 2;
                return true;
            }
            output = descriptor == 1 ? stdout : stderr;
            index++;
            continue;
        }
        if (wcscmp(option, L"f") == 0) {
            if (output != stdout || noNewline || raw) {
                fwprintf(stderr,
                    L"ksh: print: -f cannot be combined with -n, -r, -R, or -u\n");
                *code = 2;
                return true;
            }
            if (index + 1 >= cmd->argc) {
                fwprintf(stderr, L"ksh: print: -f requires a format\n");
                *code = 2;
                return true;
            }
            CommandAST formatted = { 0 };
            formatted.argc = cmd->argc - index;
            formatted.argv[0] = cmd->argv[0];
            for (int argument = index + 1;
                argument < cmd->argc; ++argument)
                formatted.argv[argument - index] = cmd->argv[argument];
            (void)shell;
            return Builtin_Printf(&formatted, code);
        }
        bool valid = true;
        for (const wchar_t* current = option; *current; ++current) {
            if (*current == L'n') noNewline = true;
            else if (*current == L'r' || *current == L'R') raw = true;
            else if (*current == L'l') lineByLine = true;
            else if (*current == L'p') toCoprocess = true;
            else if (*current == L's') {
                fwprintf(stderr,
                    L"ksh: print: -s requires history support, which is not available\n");
                *code = 2;
                return true;
            } else {
                valid = false;
                break;
            }
        }
        if (!valid) break;
        index++;
    }
    if (index < cmd->argc && wcscmp(cmd->argv[index], L"--") == 0) index++;
    if (toCoprocess) {
        if (!shell || !shell->coproc_active || !shell->coproc_in_write) {
            fwprintf(stderr, L"ksh: print: -p requires active coprocess/history support\n");
            *code = 2;
            return true;
        }
        wchar_t pBuf[MAX_LINE_LEN];
        pBuf[0] = L'\0';
        for (int argument = index; argument < cmd->argc; ++argument) {
            if (argument > index) wcscat_s(pBuf, _countof(pBuf), lineByLine ? L"\n" : L" ");
            wcscat_s(pBuf, _countof(pBuf), cmd->argv[argument]);
        }
        if (!noNewline) wcscat_s(pBuf, _countof(pBuf), L"\n");
        char u8Buf[MAX_LINE_LEN * 4];
        int u8len = WideCharToMultiByte(CP_UTF8, 0, pBuf, -1, u8Buf, sizeof(u8Buf), NULL, NULL);
        if (u8len > 1) {
            DWORD written = 0;
            WriteFile(shell->coproc_in_write, u8Buf, (DWORD)(u8len - 1), &written, NULL);
            FlushFileBuffers(shell->coproc_in_write);
        }
        *code = 0;
        return true;
    }
    bool stopOutput = false;
    for (int argument = index; argument < cmd->argc && !stopOutput;
        ++argument) {
        if (argument > index && !lineByLine && fputwc(L' ', output) == WEOF) {
            *code = 1;
            return true;
        }
        if (raw) {
            if (fputws(cmd->argv[argument], output) == WEOF) {
                *code = 1;
                return true;
            }
        } else if (!PrintShellEscapes(output, cmd->argv[argument],
                &stopOutput)) {
            *code = 1;
            return true;
        }
        if (lineByLine && fputwc(L'\n', output) == WEOF) {
            *code = 1;
            return true;
        }
    }
    if (!lineByLine && !noNewline && !stopOutput && fputwc(L'\n', output) == WEOF) {
        *code = 1;
        return true;
    }
    fflush(output);
    *code = ferror(output) ? 1 : 0;
    return true;
}

bool Builtin_Let(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc == 1) {
        *code = 0;
        return true;
    }
    size_t capacity = 1;
    for (int i = 1; i < cmd->argc; ++i) {
        size_t length = wcslen(cmd->argv[i]);
        if (length > SIZE_MAX - capacity - 1) {
            fwprintf(stderr, L"ksh: let: expression is too large\n");
            *code = 1;
            return true;
        }
        capacity += length + (i > 1 ? 1 : 0);
    }
    wchar_t* expression = (wchar_t*)malloc(capacity * sizeof(wchar_t));
    if (!expression) {
        fwprintf(stderr, L"ksh: let: out of memory\n");
        *code = 1;
        return true;
    }
    expression[0] = L'\0';
    for (int i = 1; i < cmd->argc; ++i) {
        if (i > 1) wcscat_s(expression, capacity, L",");
        wcscat_s(expression, capacity, cmd->argv[i]);
    }
    long long value = 0;
    bool valid = Batch_EvaluateArithmetic(shell, NULL, expression, &value);
    free(expression);
    if (!valid) {
        fwprintf(stderr, L"ksh: let: invalid arithmetic expression\n");
        *code = 2;
        return true;
    }
    *code = value != 0 ? 0 : 1;
    return true;
}

bool Builtin_Setvar(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc != 3 || !IsValidIdentifier(cmd->argv[1])) {
        fwprintf(stderr, L"ksh: setvar: usage: setvar name value\n");
        *code = 2;
        return true;
    }
    if (!shell->env->Set(shell->env, cmd->argv[1], cmd->argv[2])) {
        fwprintf(stderr, L"ksh: setvar: cannot set '%ls'\n", cmd->argv[1]);
        *code = 1;
        return true;
    }
    *code = 0;
    return true;
}

bool FindCommandPath(const wchar_t* name, wchar_t* path,
    size_t pathCapacity) {
    static const wchar_t* extensions[] = {
        L".exe", L".com", L".bat", L".cmd", NULL
    };
    if (wcschr(name, L'\\') || wcschr(name, L'/') ||
        (wcslen(name) > 1 && name[1] == L':')) {
        DWORD length = GetFullPathNameW(name, (DWORD)pathCapacity, path, NULL);
        return length > 0 && length < pathCapacity &&
            GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    }
    /* Search PATH only: an unqualified name must not resolve to the current directory
       (binary planting). Use ./name to run a program from the current directory. */
    DWORD plen = GetEnvironmentVariableW(L"PATH", NULL, 0);
    wchar_t* searchDirs = plen ? (wchar_t*)malloc(plen * sizeof(wchar_t)) : NULL;
    if (!searchDirs || !GetEnvironmentVariableW(L"PATH", searchDirs, plen)) {
        free(searchDirs);
        return false;
    }
    for (size_t i = 0; extensions[i]; ++i) {
        DWORD length = SearchPathW(searchDirs, name, extensions[i],
            (DWORD)pathCapacity, path, NULL);
        if (length > 0 && length < pathCapacity) { free(searchDirs); return true; }
    }
    free(searchDirs);
    return false;
}

static bool DescribeCommand(const wchar_t* name, bool pathOnly,
    bool verbose) {
    wchar_t path[MAX_PATH];
    bool builtin = IsBuiltinCommand(&(CommandAST){.argc = 1, .argv = {(wchar_t*)name}});
    bool foundPath = FindCommandPath(name, path,
        sizeof(path) / sizeof(path[0]));
    if (pathOnly) {
        if (foundPath) return wprintf(L"%ls\n", path) >= 0;
        return true;
    }
    if (builtin) {
        return verbose ? wprintf(L"%ls is a shell builtin\n", name) >= 0 :
            wprintf(L"%ls\n", name) >= 0;
    }
    if (foundPath) {
        return verbose ? wprintf(L"%ls is %ls\n", name, path) >= 0 :
            wprintf(L"%ls\n", path) >= 0;
    }
    return false;
}

typedef struct DefaultPathScope {
    wchar_t* savedPath;
    bool hadPath;
} DefaultPathScope;

static bool EnterDefaultPath(IShell* shell, DefaultPathScope* scope) {
    wchar_t systemDir[MAX_PATH];
    wchar_t windowsDir[MAX_PATH];
    wchar_t defaultPath[4096];
    scope->savedPath = NULL;
    scope->hadPath = false;
    if (!GetSystemDirectoryW(systemDir, MAX_PATH) ||
        !GetWindowsDirectoryW(windowsDir, MAX_PATH)) {
        return false;
    }
    if (swprintf(defaultPath, _countof(defaultPath),
            L"%ls;%ls;%ls\\Wbem;%ls\\WindowsPowerShell\\v1.0",
            systemDir, windowsDir, systemDir, systemDir) < 0) {
        return false;
    }
    const wchar_t* current = shell->env->Get(shell->env, L"PATH");
    wchar_t* inherited = NULL;
    if (!current) current = inherited = GetEnvironmentValue(L"PATH");
    if (current) {
        scope->hadPath = true;
        scope->savedPath = SafeStrDup(current);
        free(inherited);
        if (!scope->savedPath) return false;
    }
    if (!shell->env->Set(shell->env, L"PATH", defaultPath)) {
        free(scope->savedPath);
        scope->savedPath = NULL;
        return false;
    }
    return true;
}

static void LeaveDefaultPath(IShell* shell, DefaultPathScope* scope) {
    if (scope->hadPath) {
        shell->env->Set(shell->env, L"PATH", scope->savedPath);
    } else {
        shell->env->Unset(shell->env, L"PATH");
    }
    free(scope->savedPath);
    scope->savedPath = NULL;
}

bool Builtin_Exec(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc == 1) {
        *code = ApplyFdRedirects(shell, cmd, true) ? 0 : 1;
        return true;
    }
    if (cmd->next_in_pipeline) {
        fwprintf(stderr, L"ksh: exec: cannot replace a pipeline process\n");
        *code = 2;
        return true;
    }
    CommandAST child = { 0 };
    child.argc = cmd->argc - 1;
    for (int i = 0; i < child.argc; ++i)
        child.argv[i] = cmd->argv[i + 1];
    child.argv[child.argc] = NULL;
    child.in_redirect = cmd->in_redirect;
    child.out_redirect = cmd->out_redirect;
    child.err_redirect = cmd->err_redirect;
    child.append_out = cmd->append_out;
    child.append_err = cmd->append_err;
    child.err_to_out = cmd->err_to_out;
    child.out_to_err = cmd->out_to_err;
    memcpy(child.fds, cmd->fds, sizeof(child.fds));

    bool isBuiltin = IsBuiltinCommand(&child) || IsBatchFileName(child.argv[0]) ||
        Shell_HasFunction(shell, child.argv[0]);
    wchar_t path[MAX_PATH];
    bool found = isBuiltin || FindCommandPath(child.argv[0], path, _countof(path));
    if (!found) {
        fwprintf(stderr, L"ksh: exec: %ls: not found\n", child.argv[0]);
        *code = 127;
        if (!shell->interactive) shell->running = false;
        return true;
    }
    if (!isBuiltin) {
        const wchar_t* extension = wcsrchr(path, L'.');
        bool executable = extension &&
            (_wcsicmp(extension, L".exe") == 0 ||
                _wcsicmp(extension, L".com") == 0 ||
                _wcsicmp(extension, L".bat") == 0 ||
                _wcsicmp(extension, L".cmd") == 0);
        if (!executable) {
            fwprintf(stderr, L"ksh: exec: %ls: cannot execute\n", child.argv[0]);
            *code = 126;
            if (!shell->interactive) shell->running = false;
            return true;
        }
    }
    *code = ExecuteSingleCommand(shell, &child, NULL, NULL);
    shell->running = false;
    shell->execReplaced = true;
    return true;
}


bool Builtin_Eval(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc == 1) {
        *code = 0;
        return true;
    }
    size_t length = 1;
    for (int i = 1; i < cmd->argc; ++i) {
        size_t partLength = wcslen(cmd->argv[i]);
        if (partLength > SIZE_MAX - length - 1) {
            fwprintf(stderr, L"ksh: eval: command is too large\n");
            *code = 1;
            return true;
        }
        length += partLength + (i > 1 ? 1 : 0);
    }
    wchar_t* command = (wchar_t*)malloc(length * sizeof(wchar_t));
    if (!command) {
        fwprintf(stderr, L"ksh: eval: out of memory\n");
        *code = 1;
        return true;
    }
    command[0] = L'\0';
    for (int i = 1; i < cmd->argc; ++i) {
        if (i > 1) wcscat_s(command, length, L" ");
        wcscat_s(command, length, cmd->argv[i]);
    }
    *code = shell->ExecuteLine(shell, command);
    free(command);
    return true;
}

bool Builtin_TypeLike(IShell* shell, CommandAST* cmd, int* code) {
    int first = 1;
    bool all = false;
    bool pathOnly = false;
    bool verbose = true;
    while (first < cmd->argc && cmd->argv[first][0] == L'-') {
        if (wcscmp(cmd->argv[first], L"--") == 0) {
            first++;
            break;
        }
        for (const wchar_t* option = cmd->argv[first] + 1; *option;
            ++option) {
            if (*option == L'a') all = true;
            else if (*option == L'p' || *option == L'P') pathOnly = true;
            else if (*option == L'v' || *option == L'f') verbose = true;
            else {
                fwprintf(stderr, L"ksh: %ls: invalid option -%lc\n",
                    cmd->argv[0], *option);
                *code = 2;
                return true;
            }
        }
        first++;
    }
    if (first == cmd->argc) {
        fwprintf(stderr, L"ksh: %ls: missing command name\n", cmd->argv[0]);
        *code = 2;
        return true;
    }
    bool success = true;
    for (int i = first; i < cmd->argc; ++i) {
        bool described = false;
        if (Shell_HasFunction(shell, cmd->argv[i]) && !pathOnly) {
            if (verbose)
                wprintf(L"%ls is a function\n", cmd->argv[i]);
            else
                wprintf(L"%ls\n", cmd->argv[i]);
            described = true;
        } else {
            described = DescribeCommand(cmd->argv[i], pathOnly, verbose);
        }
        if (!described) {
            fwprintf(stderr, L"ksh: %ls: not found\n", cmd->argv[i]);
            success = false;
        }
        if (all) {
            wchar_t path[MAX_PATH];
            if (FindCommandPath(cmd->argv[i], path,
                    sizeof(path) / sizeof(path[0])) &&
                (!pathOnly || !described))
                wprintf(L"%ls\n", path);
        }
    }
    *code = success ? 0 : 1;
    return true;
}

bool Builtin_Command(IShell* shell, CommandAST* cmd, int* code) {
    int first = 1;
    bool lookup = false;
    bool verbose = false;
    bool useDefaultPath = false;
    while (first < cmd->argc && cmd->argv[first][0] == L'-') {
        if (wcscmp(cmd->argv[first], L"--") == 0) {
            first++;
            break;
        }
        for (const wchar_t* option = cmd->argv[first] + 1; *option;
            ++option) {
            if (*option == L'v') lookup = true;
            else if (*option == L'V') lookup = verbose = true;
            else if (*option == L'p') useDefaultPath = true;
            else {
                fwprintf(stderr, L"ksh: command: invalid option -%lc\n",
                    *option);
                *code = 2;
                return true;
            }
        }
        first++;
    }
    if (first == cmd->argc) {
        *code = 0;
        return true;
    }
    DefaultPathScope pathScope;
    if (useDefaultPath && !EnterDefaultPath(shell, &pathScope)) {
        fwprintf(stderr, L"ksh: command: cannot set default search path\n");
        *code = 1;
        return true;
    }
    if (lookup) {
        bool success = true;
        for (int i = first; i < cmd->argc; ++i) {
            if (Shell_HasFunction(shell, cmd->argv[i])) {
                if (verbose)
                    wprintf(L"%ls is a function\n", cmd->argv[i]);
                else
                    wprintf(L"%ls\n", cmd->argv[i]);
            } else if (!DescribeCommand(cmd->argv[i], false, verbose)) {
                if (verbose)
                    fwprintf(stderr, L"ksh: command: %ls: not found\n",
                        cmd->argv[i]);
                success = false;
            }
        }
        if (useDefaultPath) LeaveDefaultPath(shell, &pathScope);
        *code = success ? 0 : 1;
        return true;
    }

    CommandAST delegated = *cmd;
    delegated.argc = cmd->argc - first;
    for (int i = 0; i < delegated.argc; ++i)
        delegated.argv[i] = cmd->argv[first + i];
    delegated.argv[delegated.argc] = NULL;
    if (IsBuiltinCommand(&delegated)) {
        (void)TryExecBuiltin(shell, &delegated, code);
    } else {
        *code = ExecuteSingleCommand(shell, &delegated, NULL, NULL);
    }
    if (useDefaultPath) LeaveDefaultPath(shell, &pathScope);
    return true;
}

static bool PrintExportedVariable(const wchar_t* key, const wchar_t* value) {
    if (fwprintf(stdout, L"export %ls='", key) < 0) return false;
    for (const wchar_t* p = value; *p; ++p) {
        if (*p == L'\'') {
            if (fputws(L"'\\''", stdout) == WEOF) return false;
        } else if (putwchar(*p) == WEOF) {
            return false;
        }
    }
    return fputws(L"'\n", stdout) != WEOF;
}

bool Builtin_Export(IShell* shell, CommandAST* cmd, int* code) {
    int first = 1;
    if (cmd->argc > 1 && wcscmp(cmd->argv[1], L"-p") == 0) first++;
    if (first == cmd->argc) {
        EnvEntry* curr = shell->env->head;
        bool success = true;
        while (curr) {
            if (!PrintExportedVariable(curr->key, curr->value))
                success = false;
            curr = curr->next;
        }
        *code = success && !ferror(stdout) ? 0 : 1;
        return true;
    }
    for (int i = first; i < cmd->argc; i++) {
        const wchar_t* equal = wcschr(cmd->argv[i], L'=');
        size_t nameLength = equal ? (size_t)(equal - cmd->argv[i]) :
            wcslen(cmd->argv[i]);
        wchar_t* name = (wchar_t*)malloc((nameLength + 1) * sizeof(wchar_t));
        if (!name) {
            fwprintf(stderr, L"ksh: export: out of memory\n");
            *code = 1;
            return true;
        }
        memcpy(name, cmd->argv[i], nameLength * sizeof(wchar_t));
        name[nameLength] = L'\0';
        if (!IsValidIdentifier(name)) {
            fwprintf(stderr, L"ksh: export: '%ls': not a valid identifier\n",
                cmd->argv[i]);
            free(name);
            *code = 2;
            return true;
        }
        wchar_t* inherited = NULL;
        const wchar_t* value = equal ? equal + 1 :
            (inherited = GetEnvironmentValue(name));
        if (!value) value = L"";
        bool success = shell->env->Set(shell->env, name, value);
        free(inherited);
        free(name);
        if (!success) {
            fwprintf(stderr, L"ksh: export: cannot set environment variable\n");
            *code = 1;
            return true;
        }
    }
    *code = 0;
    return true;
}

static bool PrintReadonlyVariable(const EnvEntry* entry) {
    if (fwprintf(stdout, L"readonly %ls='", entry->key) < 0) return false;
    for (const wchar_t* p = entry->value; *p; ++p) {
        if (*p == L'\'') {
            if (fputws(L"'\\''", stdout) == WEOF) return false;
        } else if (putwchar(*p) == WEOF) {
            return false;
        }
    }
    return fputws(L"'\n", stdout) != WEOF;
}

bool Builtin_Readonly(IShell* shell, CommandAST* cmd, int* code) {
    int first = 1;
    if (cmd->argc > 1 && wcscmp(cmd->argv[1], L"-p") == 0) first++;
    if (first == cmd->argc) {
        bool success = true;
        for (EnvEntry* entry = shell->env->head; entry; entry = entry->next) {
            if (entry->readonly && !PrintReadonlyVariable(entry))
                success = false;
        }
        *code = success && !ferror(stdout) ? 0 : 1;
        return true;
    }
    for (int i = first; i < cmd->argc; ++i) {
        const wchar_t* equal = wcschr(cmd->argv[i], L'=');
        size_t nameLength = equal ? (size_t)(equal - cmd->argv[i]) :
            wcslen(cmd->argv[i]);
        wchar_t* name = (wchar_t*)malloc((nameLength + 1) * sizeof(wchar_t));
        if (!name) {
            fwprintf(stderr, L"ksh: readonly: out of memory\n");
            *code = 1;
            return true;
        }
        memcpy(name, cmd->argv[i], nameLength * sizeof(wchar_t));
        name[nameLength] = L'\0';
        if (!IsValidIdentifier(name)) {
            fwprintf(stderr, L"ksh: readonly: '%ls': not a valid identifier\n",
                cmd->argv[i]);
            free(name);
            *code = 2;
            return true;
        }
        if (equal && !shell->env->Set(shell->env, name, equal + 1)) {
            fwprintf(stderr, L"ksh: readonly: cannot assign '%ls'\n", name);
            free(name);
            *code = 1;
            return true;
        }
        if (!equal && !shell->env->Get(shell->env, name)) {
            wchar_t* inherited = GetEnvironmentValue(name);
            if (inherited) {
                bool imported = shell->env->Set(shell->env, name, inherited);
                free(inherited);
                if (!imported) {
                    fwprintf(stderr, L"ksh: readonly: cannot import '%ls'\n",
                        name);
                    free(name);
                    *code = 1;
                    return true;
                }
            }
        }
        if (!Env_MakeReadonly(shell->env, name)) {
            fwprintf(stderr, L"ksh: readonly: cannot mark '%ls' readonly\n",
                name);
            free(name);
            *code = 1;
            return true;
        }
        free(name);
    }
    *code = 0;
    return true;
}

static bool PrintTypesetEntry(const EnvEntry* entry) {
    fputws(L"typeset", stdout);
    if (entry->is_float) {
        if (entry->float_exponential) {
            if (entry->float_precision > 0) wprintf(L" -E%u", entry->float_precision);
            else fputws(L" -E", stdout);
        } else {
            if (entry->float_precision > 0) wprintf(L" -F%u", entry->float_precision);
            else fputws(L" -F", stdout);
        }
    }
    if (entry->integer) {
        if (entry->integerBase == 10) fputws(L" -i", stdout);
        else wprintf(L" -i%u", entry->integerBase);
    }
    if (entry->lowercase) fputws(L" -l", stdout);
    if (entry->uppercase) fputws(L" -u", stdout);
    if (entry->width) {
        wprintf(L" -%lc%u", entry->alignment, entry->width);
    }
    if (entry->readonly) fputws(L" -r", stdout);
    wprintf(L" %ls='", entry->key);
    for (const wchar_t* p = entry->value; *p; ++p) {
        if (*p == L'\'') fputws(L"'\\''", stdout);
        else putwchar(*p);
    }
    return fputws(L"'\n", stdout) != WEOF && !ferror(stdout);
}

static bool PrintShellQuoted(const wchar_t* value) {
    if (putwchar(L'\'') == WEOF) return false;
    for (const wchar_t* p = value; *p; ++p) {
        if (*p == L'\'') {
            if (fputws(L"'\\''", stdout) == WEOF) return false;
        } else if (putwchar(*p) == WEOF) {
            return false;
        }
    }
    return putwchar(L'\'') != WEOF;
}

static bool PrintAlias(const ShellAlias* alias) {
    return fwprintf(stdout, L"alias %ls=", alias->name) >= 0 &&
        PrintShellQuoted(alias->value) &&
        putwchar(L'\n') != WEOF && !ferror(stdout);
}

bool Builtin_Alias(IShell* shell, CommandAST* cmd, int* code) {
    int first = 1;
    if (first < cmd->argc && wcscmp(cmd->argv[first], L"-p") == 0)
        first++;
    else if (first < cmd->argc && cmd->argv[first][0] == L'-') {
        fwprintf(stderr, L"ksh: alias: invalid option %ls\n",
            cmd->argv[first]);
        *code = 2;
        return true;
    }

    bool success = true;
    if (first == cmd->argc) {
        for (ShellAlias* alias = shell->aliases; alias; alias = alias->next)
            if (!PrintAlias(alias)) success = false;
        *code = success ? 0 : 1;
        return true;
    }

    for (int i = first; i < cmd->argc; ++i) {
        const wchar_t* equal = wcschr(cmd->argv[i], L'=');
        if (!equal) {
            ShellAlias* alias = Shell_FindAlias(shell, cmd->argv[i]);
            if (!alias) {
                fwprintf(stderr, L"ksh: alias: %ls: not found\n",
                    cmd->argv[i]);
                success = false;
            } else if (!PrintAlias(alias)) {
                success = false;
            }
            continue;
        }
        size_t nameLength = (size_t)(equal - cmd->argv[i]);
        wchar_t* name = (wchar_t*)malloc((nameLength + 1) *
            sizeof(wchar_t));
        if (!name) {
            fwprintf(stderr, L"ksh: alias: out of memory\n");
            *code = 1;
            return true;
        }
        memcpy(name, cmd->argv[i], nameLength * sizeof(wchar_t));
        name[nameLength] = L'\0';
        if (!IsValidIdentifier(name)) {
            fwprintf(stderr, L"ksh: alias: %ls: invalid alias name\n", name);
            free(name);
            success = false;
            continue;
        }
        if (!Shell_SetAlias(shell, name, equal + 1)) {
            fwprintf(stderr, L"ksh: alias: cannot set %ls\n", name);
            success = false;
        }
        free(name);
    }
    *code = success ? 0 : 1;
    return true;
}

bool Builtin_Unalias(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc == 2 && wcscmp(cmd->argv[1], L"-a") == 0) {
        while (shell->aliases) {
            ShellAlias* next = shell->aliases->next;
            free(shell->aliases->name);
            free(shell->aliases->value);
            free(shell->aliases);
            shell->aliases = next;
        }
        *code = 0;
        return true;
    }
    if (cmd->argc < 2) {
        fwprintf(stderr, L"ksh: unalias: usage: unalias [-a] name ...\n");
        *code = 2;
        return true;
    }
    bool success = true;
    for (int i = 1; i < cmd->argc; ++i) {
        ShellAlias** link = &shell->aliases;
        while (*link && wcscmp((*link)->name, cmd->argv[i]) != 0)
            link = &(*link)->next;
        if (!*link) {
            fwprintf(stderr, L"ksh: unalias: %ls: not found\n",
                cmd->argv[i]);
            success = false;
            continue;
        }
        ShellAlias* removed = *link;
        *link = removed->next;
        free(removed->name);
        free(removed->value);
        free(removed);
    }
    *code = success ? 0 : 1;
    return true;
}

static bool PrintTypesetArray(const ShellArray* array) {
    if (fwprintf(stdout, L"typeset -%lc %ls\nset -A %ls",
            array->associative ? L'A' : L'a',
            array->name, array->name) < 0)
        return false;
    for (const ArrayElement* element = array->elements; element;
        element = element->next) {
        if (putwchar(L' ') == WEOF) return false;
        if (array->associative) {
            if (fwprintf(stdout, L"%ls=", element->key) < 0) return false;
        }
        if (!PrintShellQuoted(element->value)) return false;
    }
    return putwchar(L'\n') != WEOF && !ferror(stdout);
}

bool Builtin_Typeset(IShell* shell, CommandAST* cmd, int* code) {
    bool print = false;
    bool listFunctions = false;
    bool functionNamesOnly = false;
    bool sawInteger = false;
    bool integerValue = false;
    unsigned int integerBase = 10;
    bool sawFloat = false;
    bool floatValue = false;
    bool floatExponential = false;
    unsigned int floatPrecision = 0;
    bool sawCase = false;
    wchar_t caseMode = L'\0';
    bool sawAlignment = false;
    wchar_t alignment = L'R';
    unsigned int width = 0;
    bool sawReadonly = false;
    bool readonlyValue = false;
    bool sawArray = false;
    bool arrayAssociative = false;
    bool sawNameref = false;
    bool namerefValue = false;
    bool sawF = false;
    int first = 1;
    while (first < cmd->argc && cmd->argv[first][0] != L'\0' &&
        (cmd->argv[first][0] == L'-' || cmd->argv[first][0] == L'+')) {
        const wchar_t* option = cmd->argv[first];
        if (wcscmp(option, L"--") == 0) {
            first++;
            break;
        }
        bool enabling = option[0] == L'-';
        for (const wchar_t* flag = option + 1; *flag; ++flag) {
            switch (*flag) {
            case L'p':
                if (!enabling) goto invalid_typeset_option;
                print = true;
                break;
            case L'f':
                if (!enabling) goto invalid_typeset_option;
                listFunctions = true;
                break;
            case L'E': {
                sawFloat = true;
                floatValue = enabling;
                floatExponential = true;
                if (enabling && iswdigit(flag[1])) {
                    wchar_t* end = NULL;
                    floatPrecision = (unsigned int)wcstoul(flag + 1, &end, 10);
                    flag = end - 1;
                }
                break;
            }
            case L'F': {
                if (enabling && iswdigit(flag[1])) {
                    sawFloat = true;
                    floatValue = enabling;
                    floatExponential = false;
                    wchar_t* end = NULL;
                    floatPrecision = (unsigned int)wcstoul(flag + 1, &end, 10);
                    flag = end - 1;
                } else {
                    if (!enabling) goto invalid_typeset_option;
                    sawF = true;
                }
                break;
            }
            case L'i': {
                sawInteger = true;
                integerValue = enabling;
                if (enabling && iswdigit(flag[1])) {
                    wchar_t* end = NULL;
                    unsigned long parsed = wcstoul(flag + 1, &end, 10);
                    if (end == flag + 1 || parsed < 2 || parsed > 36) {
                        fwprintf(stderr, L"ksh: typeset: invalid integer base\n");
                        *code = 2;
                        return true;
                    }
                    integerBase = (unsigned int)parsed;
                    flag = end - 1;
                }
                break;
            }
            case L'l':
            case L'u':
                sawCase = true;
                caseMode = enabling ? *flag : L'\0';
                break;
            case L'L':
            case L'R':
            case L'Z':
                if (!enabling) {
                    sawAlignment = true;
                    alignment = L'R';
                    width = 0;
                    break;
                }
                sawAlignment = true;
                alignment = *flag;
                if (iswdigit(flag[1])) {
                    wchar_t* end = NULL;
                    unsigned long parsed = wcstoul(flag + 1, &end, 10);
                    if (parsed > MAX_LINE_LEN) {
                        fwprintf(stderr, L"ksh: typeset: width is too large\n");
                        *code = 2;
                        return true;
                    }
                    width = (unsigned int)parsed;
                    flag = end - 1;
                } else {
                    width = 0;
                }
                break;
            case L'r':
                sawReadonly = true;
                readonlyValue = enabling;
                if (!enabling) {
                    fwprintf(stderr, L"ksh: typeset: cannot remove readonly attribute\n");
                    *code = 2;
                    return true;
                }
                break;
            case L'x':
                if (!enabling) {
                    fwprintf(stderr, L"ksh: typeset: variables remain exported\n");
                    *code = 2;
                    return true;
                }
                break;
            case L'a':
                if (!enabling) goto invalid_typeset_option;
                sawArray = true;
                arrayAssociative = false;
                break;
            case L'A':
                if (!enabling) goto invalid_typeset_option;
                sawArray = true;
                arrayAssociative = true;
                break;
            case L'T': {
                if (first + 1 >= cmd->argc) {
                    *code = 0;
                    return true;
                }
                const wchar_t* spec = cmd->argv[++first];
                wchar_t typeName[128];
                const wchar_t* eq = wcschr(spec, L'=');
                if (eq) {
                    size_t tnLen = (size_t)(eq - spec);
                    wcsncpy_s(typeName, _countof(typeName), spec, tnLen);
                } else {
                    wcsncpy_s(typeName, _countof(typeName), spec, _TRUNCATE);
                }
                ShellTypeMember* head = NULL;
                ShellTypeMember** tail = &head;
                for (int mIdx = first + 1; mIdx < cmd->argc; mIdx++) {
                    const wchar_t* marg = cmd->argv[mIdx];
                    if (wcscmp(marg, L")") == 0) break;
                    if (wcscmp(marg, L"function") == 0 && mIdx + 2 < cmd->argc) {
                        const wchar_t* fnName = cmd->argv[++mIdx];
                        if (mIdx + 1 < cmd->argc && wcscmp(cmd->argv[mIdx + 1], L"{") == 0) mIdx += 2;
                        else mIdx++;
                        wchar_t body[MAX_LINE_LEN] = {0};
                        while (mIdx < cmd->argc && wcscmp(cmd->argv[mIdx], L"}") != 0 && wcscmp(cmd->argv[mIdx], L")") != 0) {
                            if (body[0]) wcscat_s(body, _countof(body), L" ");
                            wcscat_s(body, _countof(body), cmd->argv[mIdx++]);
                        }
                        ShellTypeMember* mem = (ShellTypeMember*)calloc(1, sizeof(ShellTypeMember));
                        if (mem) {
                            wcsncpy_s(mem->name, _countof(mem->name), fnName, _TRUNCATE);
                            mem->isFunction = true;
                            mem->functionBody = SafeStrDup(body);
                            *tail = mem;
                            tail = &mem->next;
                        }
                    } else if (wcschr(marg, L'=')) {
                        const wchar_t* meq = wcschr(marg, L'=');
                        ShellTypeMember* mem = (ShellTypeMember*)calloc(1, sizeof(ShellTypeMember));
                        if (mem) {
                            size_t mnLen = (size_t)(meq - marg);
                            wcsncpy_s(mem->name, _countof(mem->name), marg, mnLen);
                            wcsncpy_s(mem->defaultValue, _countof(mem->defaultValue), meq + 1, _TRUNCATE);
                            size_t dvLen = wcslen(mem->defaultValue);
                            if (dvLen > 0 && mem->defaultValue[dvLen - 1] == L')') mem->defaultValue[dvLen - 1] = L'\0';
                            *tail = mem;
                            tail = &mem->next;
                        }
                    }
                }
                Shell_RegisterType(shell, typeName, head);
                *code = 0;
                return true;
            }
            case L'n':
                sawNameref = true;
                namerefValue = enabling;
                break;
            case L't':
                fwprintf(stderr, L"ksh: typeset: attribute -t (tracing) is not supported\n");
                *code = 2;
                return true;
            default:
                goto invalid_typeset_option;
            }
        }
        first++;
    }
    if (sawF) {
        if (first == cmd->argc || listFunctions) {
            listFunctions = true;
            functionNamesOnly = true;
        } else {
            bool anyFunc = false;
            bool anyAssign = false;
            for (int i = first; i < cmd->argc; ++i) {
                if (wcschr(cmd->argv[i], L'=')) {
                    anyAssign = true;
                    break;
                }
                if (Shell_FindFunction(shell, cmd->argv[i])) {
                    anyFunc = true;
                }
            }
            if (!anyAssign && anyFunc) {
                listFunctions = true;
                functionNamesOnly = true;
            } else {
                sawFloat = true;
                floatValue = true;
                floatExponential = false;
                floatPrecision = 6;
            }
        }
    }
    if (listFunctions) {
        bool success = true;
        if (first == cmd->argc) {
            for (ShellFunction* function = shell->functions; function;
                function = function->next) {
                if (functionNamesOnly) {
                    if (fwprintf(stdout, L"%ls\n", function->name) < 0)
                        success = false;
                } else {
                    if (fwprintf(stdout, L"%ls() {\n%ls", function->name,
                            function->body) < 0)
                        success = false;
                    if (*function->body &&
                        function->body[wcslen(function->body) - 1] != L'\n' &&
                        putwchar(L'\n') == WEOF)
                        success = false;
                    if (fputws(L"}\n", stdout) == WEOF) success = false;
                }
            }
        } else {
            for (int i = first; i < cmd->argc; ++i) {
                ShellFunction* function = Shell_FindFunction(shell,
                    cmd->argv[i]);
                if (!function) {
                    fwprintf(stderr, L"ksh: typeset: %ls: function not found\n",
                        cmd->argv[i]);
                    success = false;
                    continue;
                }
                if (functionNamesOnly) {
                    if (fwprintf(stdout, L"%ls\n", function->name) < 0)
                        success = false;
                } else {
                    if (fwprintf(stdout, L"%ls() {\n%ls", function->name,
                            function->body) < 0)
                        success = false;
                    if (*function->body &&
                        function->body[wcslen(function->body) - 1] != L'\n' &&
                        putwchar(L'\n') == WEOF)
                        success = false;
                    if (fputws(L"}\n", stdout) == WEOF) success = false;
                }
            }
        }
        *code = success ? 0 : 1;
        return true;
    }
    if (first == cmd->argc || print) {
        bool success = true;
        if (first == cmd->argc) {
            for (EnvEntry* entry = shell->env->head; entry; entry = entry->next) {
                if (!PrintTypesetEntry(entry)) success = false;
            }
            for (ShellArray* array = shell->env->arrays; array;
                array = array->next) {
                if (!PrintTypesetArray(array)) success = false;
            }
        } else {
            for (int i = first; i < cmd->argc; ++i) {
                EnvEntry* entry = Env_FindEntry(shell->env, cmd->argv[i]);
                ShellArray* array = Env_FindArray(shell->env, cmd->argv[i]);
                if (!entry && !array) {
                    fwprintf(stderr, L"ksh: typeset: %ls: not found\n",
                        cmd->argv[i]);
                    success = false;
                } else if (entry && !PrintTypesetEntry(entry)) {
                    success = false;
                } else if (array && !PrintTypesetArray(array)) {
                    success = false;
                }
            }
        }
        *code = success ? 0 : 1;
        return true;
    }

    for (int i = first; i < cmd->argc; ++i) {
        const wchar_t* equal = wcschr(cmd->argv[i], L'=');
        size_t keyLength = equal ? (size_t)(equal - cmd->argv[i]) :
            wcslen(cmd->argv[i]);
        wchar_t* key = (wchar_t*)malloc((keyLength + 1) * sizeof(wchar_t));
        if (!key) {
            fwprintf(stderr, L"ksh: typeset: out of memory\n");
            *code = 1;
            return true;
        }
        memcpy(key, cmd->argv[i], keyLength * sizeof(wchar_t));
        key[keyLength] = L'\0';
        if (!IsValidIdentifier(key)) {
            fwprintf(stderr, L"ksh: typeset: '%ls': not a valid identifier\n",
                cmd->argv[i]);
            free(key);
            *code = 2;
            return true;
        }
        if (sawArray || Env_FindArray(shell->env, key)) {
            ShellArray* array = Env_CreateArray(shell->env, key,
                sawArray ? arrayAssociative :
                    Env_FindArray(shell->env, key)->associative);
            if (!array) {
                fwprintf(stderr, L"ksh: typeset: cannot declare array '%ls'\n",
                    key);
                free(key);
                *code = 1;
                return true;
            }
            if (equal) {
                if (array->associative) {
                    fwprintf(stderr,
                        L"ksh: typeset: associative array initialization requires set -A\n");
                    free(key);
                    *code = 2;
                    return true;
                }
                if (!Env_ArraySetIndex(array, 0, equal + 1)) {
                    fwprintf(stderr, L"ksh: typeset: cannot assign array '%ls'\n",
                        key);
                    free(key);
                    *code = 1;
                    return true;
                }
            }
            free(key);
            continue;
        }
        EnvEntry* entry = NULL;
        if (shell->functionDepth > 0 && shell->localScope) {
            if (!Shell_AddLocal(shell, key, equal ? equal + 1 : L"")) {
                fwprintf(stderr, L"ksh: typeset: cannot create local '%ls'\n", key);
                free(key);
                *code = 1;
                return true;
            }
            entry = Env_FindEntry(shell->env, key);
        } else {
            entry = Env_FindEntry(shell->env, key);
            if (!entry) {
                wchar_t* inherited = equal ? NULL : GetEnvironmentValue(key);
                bool created = shell->env->Set(shell->env, key,
                    equal ? L"" : (inherited ? inherited : L""));
                free(inherited);
                if (!created) {
                    fwprintf(stderr, L"ksh: typeset: cannot create '%ls'\n", key);
                    free(key);
                    *code = 1;
                    return true;
                }
                entry = Env_FindEntry(shell->env, key);
            }
        }
        if (sawInteger) {
            entry->integer = integerValue;
            if (integerValue) entry->integerBase = integerBase;
        }
        if (sawFloat) {
            entry->is_float = floatValue;
            entry->float_exponential = floatExponential;
            entry->float_precision = floatPrecision;
        }
        if (sawCase) {
            entry->lowercase = caseMode == L'l';
            entry->uppercase = caseMode == L'u';
        }
        if (sawAlignment) {
            entry->alignment = alignment;
            entry->width = width;
        }
        if (sawNameref) {
            entry->is_nameref = namerefValue;
            if (equal) {
                free(entry->value);
                entry->value = SafeStrDup(equal + 1);
            }
        }
        if (equal && !sawNameref && !shell->env->Set(shell->env, key, equal + 1)) {
            fwprintf(stderr, L"ksh: typeset: invalid value for '%ls'\n", key);
            free(key);
            *code = 1;
            return true;
        } else if (!equal && (sawInteger || sawFloat || sawCase || sawAlignment)) {
            wchar_t* oldValue = SafeStrDup(entry->value);
            if (!oldValue || !shell->env->Set(shell->env, key, oldValue)) {
                free(oldValue);
                fwprintf(stderr, L"ksh: typeset: cannot apply attributes to '%ls'\n",
                    key);
                free(key);
                *code = 1;
                return true;
            }
            free(oldValue);
        }
        if (sawReadonly && readonlyValue &&
            !Env_MakeReadonly(shell->env, key)) {
            fwprintf(stderr, L"ksh: typeset: cannot mark '%ls' readonly\n", key);
            free(key);
            *code = 1;
            return true;
        }
        free(key);
    }
    *code = 0;
    return true;

invalid_typeset_option:
    fwprintf(stderr, L"ksh: typeset: invalid option %ls\n", cmd->argv[first]);
    *code = 2;
    return true;
}

bool TestParseInteger(const wchar_t* text, long long* result);

static bool* ShellOptionSlot(IShell* shell, const wchar_t* name) {
    struct {
        const wchar_t* name;
        bool* slot;
    } table[] = {
        { L"allexport", &shell->allexport },
        { L"errexit", &shell->errexit },
        { L"hashall", &shell->hashall },
        { L"monitor", &shell->monitor },
        { L"noclobber", &shell->noclobber },
        { L"noexec", &shell->noexec },
        { L"noglob", &shell->noglob },
        { L"notify", &shell->notify },
        { L"nounset", &shell->env->nounset },
        { L"pipefail", &shell->pipefail },
        { L"verbose", &shell->verbose },
        { L"xtrace", &shell->xtrace },
    };
    for (size_t i = 0; i < _countof(table); i++) {
        if (wcscmp(table[i].name, name) == 0) return table[i].slot;
    }
    return NULL;
}

bool SetShellOption(IShell* shell, const wchar_t* name, bool enabled) {
    if (wcscmp(name, L"emacs") == 0) {
        if (enabled) shell->editMode = EDIT_MODE_EMACS;
        return true;
    }
    if (wcscmp(name, L"vi") == 0) {
        if (enabled) shell->editMode = EDIT_MODE_VI;
        return true;
    }
    bool* slot = ShellOptionSlot(shell, name);
    if (!slot) return false;
    *slot = enabled;
    return true;
}

static const wchar_t* const kShellOptionNames[] = {
    L"allexport", L"emacs", L"errexit", L"hashall", L"monitor", L"noclobber",
    L"noglob", L"noexec", L"notify", L"nounset", L"pipefail", L"verbose",
    L"vi", L"xtrace"
};

static const wchar_t* ShellOptionForFlag(wchar_t flag) {
    switch (flag) {
    case L'a': return L"allexport";
    case L'b': return L"notify";
    case L'C': return L"noclobber";
    case L'e': return L"errexit";
    case L'f': return L"noglob";
    case L'h': return L"hashall";
    case L'm': return L"monitor";
    case L'n': return L"noexec";
    case L'u': return L"nounset";
    case L'v': return L"verbose";
    case L'x': return L"xtrace";
    default: return NULL;
    }
}

void PrintShellOptions(IShell* shell, bool reusable) {
    for (size_t i = 0; i < _countof(kShellOptionNames); i++) {
        const wchar_t* name = kShellOptionNames[i];
        bool on;
        if (wcscmp(name, L"emacs") == 0) on = shell->editMode == EDIT_MODE_EMACS;
        else if (wcscmp(name, L"vi") == 0) on = shell->editMode == EDIT_MODE_VI;
        else on = *ShellOptionSlot(shell, name);
        if (reusable) {
            fwprintf(stdout, L"set %co %ls\n", on ? L'-' : L'+', name);
        } else {
            fwprintf(stdout, L"%ls %ls\n", name, on ? L"on" : L"off");
        }
    }
}

bool Builtin_Set(IShell* shell, CommandAST* cmd, int* code) {
    int firstArgument = 1;
    bool sawOption = false;
    bool explicitParameters = false;
    if (cmd->argc > 1 && (wcscmp(cmd->argv[1], L"-A") == 0 ||
            wcscmp(cmd->argv[1], L"+A") == 0)) {
        bool append = cmd->argv[1][0] == L'+';
        if (cmd->argc < 3 || !IsValidIdentifier(cmd->argv[2])) {
            fwprintf(stderr, L"ksh: set: usage: set [-A|+A] array [value...]\n");
            *code = 2;
            return true;
        }
        ShellArray* existing = Env_FindArray(shell->env, cmd->argv[2]);
        ShellArray* array = Env_CreateArray(shell->env, cmd->argv[2],
            existing ? existing->associative : false);
        if (!array) {
            fwprintf(stderr, L"ksh: set: cannot create array '%ls'\n",
                cmd->argv[2]);
            *code = 1;
            return true;
        }
        if (!append) Env_ArrayClear(array);
        size_t index = append ? array->count : 0;
        for (int i = 3; i < cmd->argc; ++i) {
            if (array->associative) {
                const wchar_t* equal = wcschr(cmd->argv[i], L'=');
                if (!equal || equal == cmd->argv[i]) {
                    fwprintf(stderr,
                        L"ksh: set: associative array values must be key=value\n");
                    *code = 2;
                    return true;
                }
                size_t keyLength = (size_t)(equal - cmd->argv[i]);
                wchar_t* key = (wchar_t*)malloc((keyLength + 1) *
                    sizeof(wchar_t));
                if (!key) {
                    fwprintf(stderr, L"ksh: set: out of memory\n");
                    *code = 1;
                    return true;
                }
                memcpy(key, cmd->argv[i], keyLength * sizeof(wchar_t));
                key[keyLength] = L'\0';
                bool set = Env_ArraySet(array, key, equal + 1);
                free(key);
                if (!set) {
                    fwprintf(stderr, L"ksh: set: cannot assign array value\n");
                    *code = 1;
                    return true;
                }
            } else if (!Env_ArraySetIndex(array, index++, cmd->argv[i])) {
                fwprintf(stderr, L"ksh: set: cannot assign array value\n");
                *code = 1;
                return true;
            }
        }
        *code = 0;
        return true;
    }

    while (firstArgument < cmd->argc) {
        const wchar_t* option = cmd->argv[firstArgument];
        if (wcscmp(option, L"--") == 0) {
            firstArgument++;
            explicitParameters = true;
            break;
        }
        if (wcscmp(option, L"-") == 0) {
            shell->errexit = false;
            shell->xtrace = false;
            sawOption = true;
            firstArgument++;
            continue;
        }
        if ((option[0] != L'-' && option[0] != L'+') || !option[1])
            break;

        bool enabled = option[0] == L'-';
        sawOption = true;
        if (wcscmp(option + 1, L"o") == 0) {
            firstArgument++;
            if (firstArgument == cmd->argc) {
                PrintShellOptions(shell, option[0] == L'+');
                *code = ferror(stdout) ? 1 : 0;
                return true;
            }
            if (!SetShellOption(shell, cmd->argv[firstArgument], enabled)) {
                fwprintf(stderr, L"ksh: set: unknown option '%ls'\n",
                    cmd->argv[firstArgument]);
                *code = 2;
                return true;
            }
            firstArgument++;
            continue;
        }
        bool valid = true;
        for (const wchar_t* flag = option + 1; *flag; ++flag) {
            const wchar_t* name = ShellOptionForFlag(*flag);
            if (!name || !SetShellOption(shell, name, enabled)) {
                fwprintf(stderr, L"ksh: set: unsupported option '%lc'\n",
                    *flag);
                valid = false;
                break;
            }
        }
        if (!valid) {
            *code = 2;
            return true;
        }
        firstArgument++;
    }

    if (sawOption && !explicitParameters && firstArgument == cmd->argc) {
        *code = 0;
        return true;
    }

    if (firstArgument < cmd->argc || firstArgument == 2) {
        int count = cmd->argc - firstArgument;
        if (!Env_SetPositional(shell->env,
                shell->env->positionalCount ? shell->env->positional[0] :
                    L"ksh",
                count, &cmd->argv[firstArgument])) {
            fwprintf(stderr, L"ksh: set: unable to set positional parameters\n");
            *code = 1;
            return true;
        }
    } else {
        for (EnvEntry* entry = shell->env->head; entry; entry = entry->next)
            wprintf(L"%ls=%ls\n", entry->key, entry->value);
    }
    *code = 0;
    return true;
}

bool Builtin_Shift(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc > 2) {
        fwprintf(stderr, L"ksh: shift: too many arguments\n");
        *code = 2;
        return true;
    }
    long long amount = 1;
    if (cmd->argc == 2 && !TestParseInteger(cmd->argv[1], &amount)) {
        fwprintf(stderr, L"ksh: shift: '%ls': numeric argument required\n",
            cmd->argv[1]);
        *code = 2;
        return true;
    }
    if (amount < 0 || amount > INT_MAX ||
        !Env_ShiftPositional(shell->env, (int)amount)) {
        fwprintf(stderr, L"ksh: shift: cannot shift that many parameters\n");
        *code = 1;
        return true;
    }
    *code = 0;
    return true;
}

static bool IsIFSCharacter(wchar_t character, const wchar_t* ifs) {
    return character && wcschr(ifs, character) != NULL;
}

static bool IsIFSWhitespace(wchar_t character, const wchar_t* ifs) {
    return (character == L' ' || character == L'\t' ||
        character == L'\n') && IsIFSCharacter(character, ifs);
}

static bool AssignReadArray(IShell* shell, wchar_t* input,
    const wchar_t* name, const wchar_t* ifs) {
    ShellArray* array = Env_CreateArray(shell->env, name, false);
    if (!array) return false;
    Env_ArrayClear(array);
    size_t inputLength = wcslen(input);
    size_t position = 0;
    while (position < inputLength &&
        IsIFSWhitespace(input[position], ifs))
        position++;
    size_t index = 0;
    while (position < inputLength) {
        size_t start = position;
        while (position < inputLength &&
            !IsIFSCharacter(input[position], ifs))
            position++;
        size_t valueLength = position - start;
        wchar_t* value = (wchar_t*)malloc((valueLength + 1) *
            sizeof(wchar_t));
        if (!value) return false;
        memcpy(value, input + start, valueLength * sizeof(wchar_t));
        value[valueLength] = L'\0';
        bool assigned = Env_ArraySetIndex(array, index++, value);
        free(value);
        if (!assigned) return false;
        if (position == inputLength) break;
        bool whitespaceDelimiter = IsIFSWhitespace(input[position], ifs);
        position++;
        if (whitespaceDelimiter) {
            while (position < inputLength &&
                IsIFSWhitespace(input[position], ifs))
                position++;
        } else {
            while (position < inputLength &&
                IsIFSWhitespace(input[position], ifs))
                position++;
            if (position == inputLength) {
                if (!Env_ArraySetIndex(array, index++, L"")) return false;
                break;
            }
        }
    }
    return true;
}

static bool AssignReadFields(IShell* shell, wchar_t* input,
    CommandAST* cmd, int firstName, bool defaultReply, bool arrayMode,
    const wchar_t* arrayName) {
    int nameCount = defaultReply ? 1 : cmd->argc - firstName;
    if (!arrayMode && nameCount <= 0) return false;
    const wchar_t* ifs = shell->env->Get(shell->env, L"IFS");
    wchar_t* inheritedIfs = NULL;
    if (!ifs) {
        inheritedIfs = GetEnvironmentValue(L"IFS");
        ifs = inheritedIfs ? inheritedIfs : L" \t\n";
    }
    if (defaultReply) ifs = L"";
    if (arrayMode) {
        bool success = AssignReadArray(shell, input, arrayName, ifs);
        free(inheritedIfs);
        return success;
    }

    size_t inputLength = wcslen(input);
    size_t position = 0;
    while (position < inputLength &&
        IsIFSWhitespace(input[position], ifs))
        position++;

    bool success = true;
    for (int field = 0; field < nameCount; ++field) {
        const wchar_t* name = defaultReply ? L"REPLY" :
            cmd->argv[firstName + field];
        size_t start = position;
        size_t end = inputLength;
        if (field + 1 < nameCount) {
            while (position < inputLength &&
                !IsIFSCharacter(input[position], ifs))
                position++;
            end = position;
            if (position < inputLength) {
                if (IsIFSWhitespace(input[position], ifs)) {
                    while (position < inputLength &&
                        IsIFSWhitespace(input[position], ifs))
                        position++;
                } else {
                    position++;
                    while (position < inputLength &&
                        IsIFSWhitespace(input[position], ifs))
                        position++;
                }
            }
        } else {
            while (end > start && IsIFSWhitespace(input[end - 1], ifs))
                end--;
            position = inputLength;
        }
        size_t valueLength = end - start;
        wchar_t* value = (wchar_t*)malloc((valueLength + 1) *
            sizeof(wchar_t));
        if (!value) {
            success = false;
            break;
        }
        memcpy(value, input + start, valueLength * sizeof(wchar_t));
        value[valueLength] = L'\0';
        bool assigned = shell->env->Set(shell->env, name, value);
        free(value);
        if (!assigned) {
            fwprintf(stderr, L"ksh: read: cannot assign '%ls'\n", name);
            success = false;
            break;
        }
    }
    free(inheritedIfs);
    return success;
}

bool Builtin_Read(IShell* shell, CommandAST* cmd, int* code) {
    bool raw = false;
    bool silent = false;
    bool fromCoprocess = false;
    bool hasLimit = false;
    bool exactLimit = false;
    bool hasTimeout = false;
    bool hasDelimiter = false;
    size_t characterLimit = 0;
    DWORD timeoutMs = 0;
    wchar_t delimiter = L'\n';
    const wchar_t* prompt = NULL;
    const wchar_t* arrayName = NULL;
    bool arrayMode = false;
    int inputDescriptor = 0;
    int firstName = 1;

    while (firstName < cmd->argc && cmd->argv[firstName][0] == L'-') {
        const wchar_t* option = cmd->argv[firstName];
        if (wcscmp(option, L"--") == 0) {
            firstName++;
            break;
        }
        if (wcscmp(option, L"-r") == 0) raw = true;
        else if (wcscmp(option, L"-s") == 0) silent = true;
        else if (wcscmp(option, L"-p") == 0) {
            if ((shell && shell->coproc_active) || firstName + 1 >= cmd->argc) {
                fromCoprocess = true;
                firstName++;
                continue;
            } else if (firstName + 2 <= cmd->argc && !IsValidIdentifier(cmd->argv[firstName + 1])) {
                prompt = cmd->argv[firstName + 1];
                firstName += 2;
                continue;
            } else {
                fromCoprocess = true;
                firstName++;
                continue;
            }
        }
        else if (wcscmp(option, L"-n") == 0 ||
            wcscmp(option, L"-N") == 0 ||
            wcscmp(option, L"-d") == 0 ||
            wcscmp(option, L"-t") == 0 ||
            wcscmp(option, L"-u") == 0 ||
            wcscmp(option, L"-A") == 0 ||
            wcscmp(option, L"-a") == 0) {
            if (firstName + 1 >= cmd->argc) {
                fwprintf(stderr, L"ksh: read: option %ls requires an operand\n",
                    option);
                *code = 2;
                return true;
            }
            const wchar_t* operand = cmd->argv[firstName + 1];
            if (wcscmp(option, L"-A") == 0 ||
                wcscmp(option, L"-a") == 0) {
                arrayMode = true;
                arrayName = operand;
            } else if (wcscmp(option, L"-n") == 0 ||
                wcscmp(option, L"-N") == 0) {
                wchar_t* end = NULL;
                errno = 0;
                unsigned long long parsed = wcstoull(operand, &end, 10);
                if (errno || end == operand || *end || parsed > MAX_LINE_LEN) {
                    fwprintf(stderr, L"ksh: read: invalid character count\n");
                    *code = 2;
                    return true;
                }
                characterLimit = (size_t)parsed;
                hasLimit = true;
                exactLimit = wcscmp(option, L"-N") == 0;
            } else if (wcscmp(option, L"-d") == 0) {
                delimiter = *operand ? operand[0] : L'\0';
                hasDelimiter = true;
            } else if (wcscmp(option, L"-t") == 0) {
                wchar_t* end = NULL;
                errno = 0;
                double seconds = wcstod(operand, &end);
                if (errno || end == operand || *end || seconds < 0 ||
                    seconds > 86400.0) {
                    fwprintf(stderr, L"ksh: read: invalid timeout\n");
                    *code = 2;
                    return true;
                }
                timeoutMs = (DWORD)(seconds * 1000.0);
                hasTimeout = true;
            } else {
                wchar_t* end = NULL;
                errno = 0;
                long descriptor = wcstol(operand, &end, 10);
                if (errno || end == operand || *end || descriptor < 0 ||
                    descriptor > INT_MAX) {
                    fwprintf(stderr, L"ksh: read: invalid file descriptor\n");
                    *code = 2;
                    return true;
                }
                inputDescriptor = (int)descriptor;
            }
            firstName++;
        } else if (option[1] && !option[2]) {
            fwprintf(stderr, L"ksh: read: unsupported option %ls\n", option);
            *code = 2;
            return true;
        } else {
            for (const wchar_t* flag = option + 1; *flag; ++flag) {
                if (*flag == L'r') raw = true;
                else if (*flag == L's') silent = true;
                else {
                    fwprintf(stderr, L"ksh: read: unsupported option -%lc\n",
                        *flag);
                    *code = 2;
                    return true;
                }
            }
        }
        firstName++;
    }

    if (fromCoprocess) {
        if (!shell || !shell->coproc_active || !shell->coproc_out_read) {
            fwprintf(stderr, L"ksh: read: no coprocess\n");
            *code = 1;
            return true;
        }
        char ch = 0;
        DWORD readBytes = 0;
        char lineBuf[MAX_LINE_LEN];
        size_t llen = 0;
        while (llen + 1 < sizeof(lineBuf) && ReadFile(shell->coproc_out_read, &ch, 1, &readBytes, NULL) && readBytes > 0) {
            if (ch == '\n') break;
            if (ch != '\r') lineBuf[llen++] = ch;
        }
        lineBuf[llen] = '\0';
        wchar_t wBuf[MAX_LINE_LEN];
        MultiByteToWideChar(CP_UTF8, 0, lineBuf, -1, wBuf, _countof(wBuf));
        const wchar_t* varName = (firstName < cmd->argc) ? cmd->argv[firstName] : L"REPLY";
        shell->env->Set(shell->env, varName, wBuf);
        *code = (readBytes == 0 && llen == 0) ? 1 : 0;
        return true;
    }

    if (arrayMode && (!arrayName || !IsValidIdentifier(arrayName) ||
            firstName != cmd->argc)) {
        fwprintf(stderr, L"ksh: read: array mode requires one valid array name\n");
        *code = 2;
        return true;
    }
    for (int i = firstName; i < cmd->argc; ++i) {
        if (!IsValidIdentifier(cmd->argv[i])) {
            fwprintf(stderr, L"ksh: read: '%ls': not a valid variable name\n",
                cmd->argv[i]);
            *code = 2;
            return true;
        }
    }
    if (hasDelimiter && delimiter == L'\0') {
        fwprintf(stderr, L"ksh: read: empty delimiter is not supported\n");
        *code = 2;
        return true;
    }
    if (prompt && fputws(prompt, stdout) == WEOF) {
        fwprintf(stderr, L"ksh: read: cannot display prompt\n");
        *code = 1;
        return true;
    }
    if (prompt) fflush(stdout);

    int duplicatedDescriptor = -1;
    FILE* input = stdin;
    if (inputDescriptor != 0) {
        duplicatedDescriptor = _dup(inputDescriptor);
        if (duplicatedDescriptor < 0 ||
            !(input = _fdopen(duplicatedDescriptor, "r"))) {
            if (duplicatedDescriptor >= 0) _close(duplicatedDescriptor);
            fwprintf(stderr, L"ksh: read: cannot open descriptor %d\n",
                inputDescriptor);
            *code = 1;
            return true;
        }
    }

    HANDLE inputHandle = (HANDLE)_get_osfhandle(_fileno(input));
    DWORD inputType = inputHandle == INVALID_HANDLE_VALUE ?
        FILE_TYPE_UNKNOWN : GetFileType(inputHandle);
    DWORD originalConsoleMode = 0;
    bool restoreConsoleMode = false;
    if (silent && inputHandle != INVALID_HANDLE_VALUE &&
        GetConsoleMode(inputHandle, &originalConsoleMode)) {
        if (SetConsoleMode(inputHandle,
                originalConsoleMode & ~ENABLE_ECHO_INPUT))
            restoreConsoleMode = true;
    }

    wchar_t line[MAX_LINE_LEN];
    size_t length = 0;
    bool terminated = false;
    bool timedOut = false;
    bool reachedEof = false;
    ULONGLONG deadline = GetTickCount64() + timeoutMs;
    while (length + 1 < MAX_LINE_LEN &&
        (!hasLimit || length < characterLimit)) {
        if (hasTimeout && (inputType == FILE_TYPE_CHAR ||
                inputType == FILE_TYPE_PIPE)) {
            ULONGLONG now = GetTickCount64();
            DWORD remaining = now >= deadline ? 0 :
                (DWORD)(deadline - now);
            DWORD wait = WaitForSingleObject(inputHandle, remaining);
            if (wait == WAIT_TIMEOUT) {
                timedOut = true;
                break;
            }
            if (wait != WAIT_OBJECT_0) {
                fwprintf(stderr, L"ksh: read: input wait failed\n");
                *code = 1;
                goto read_cleanup;
            }
        }
        wint_t current = fgetwc(input);
        if (current == WEOF) {
            reachedEof = true;
            break;
        }
        if (length == 0 && (wchar_t)current == 0xFEFF) {
            continue;
        }
        if (!exactLimit && (wchar_t)current == delimiter) {
            terminated = true;
            break;
        }
        if (!raw && (wchar_t)current == L'\\') {
            wint_t next = fgetwc(input);
            if (next == WEOF) {
                reachedEof = true;
                break;
            }
            if ((wchar_t)next == L'\n' || (wchar_t)next == L'\r')
                continue;
            current = next;
        }
        line[length++] = (wchar_t)current;
    }
    line[length] = L'\0';
    if (restoreConsoleMode)
        (void)SetConsoleMode(inputHandle, originalConsoleMode);
    if (input != stdin) fclose(input);

    if (length || terminated) {
        bool defaultReply = !arrayMode && firstName == cmd->argc;
        if (!AssignReadFields(shell, line, cmd, firstName, defaultReply,
                arrayMode, arrayName)) {
            fwprintf(stderr, L"ksh: read: assignment failed\n");
            *code = 1;
            return true;
        }
    }
    *code = timedOut || reachedEof ? 1 : 0;
    return true;

read_cleanup:
    if (restoreConsoleMode)
        (void)SetConsoleMode(inputHandle, originalConsoleMode);
    if (input != stdin) fclose(input);
    return true;
}

static bool FindSourceFile(const wchar_t* filename,
    const wchar_t* searchPath, wchar_t* resolved, size_t capacity) {
    DWORD length = GetFullPathNameW(filename, (DWORD)capacity, resolved, NULL);
    if (length > 0 && length < capacity &&
        GetFileAttributesW(resolved) != INVALID_FILE_ATTRIBUTES)
        return true;
    length = SearchPathW(searchPath, filename, NULL, (DWORD)capacity,
        resolved, NULL);
    return length > 0 && length < capacity &&
        GetFileAttributesW(resolved) != INVALID_FILE_ATTRIBUTES;
}

bool Builtin_Source(IShell* shell, CommandAST* cmd, int* code) {
    bool sourceAlias = wcscmp(cmd->argv[0], L"source") == 0;
    const wchar_t* searchPath = NULL;
    int fileArgument = 1;
    if (sourceAlias && cmd->argc > 1 &&
        wcscmp(cmd->argv[1], L"-p") == 0) {
        if (cmd->argc < 4) {
            fwprintf(stderr, L"ksh: source: -p requires a path and file\n");
            *code = 2;
            return true;
        }
        searchPath = cmd->argv[2];
        fileArgument = 3;
    }
    if (fileArgument >= cmd->argc) {
        fwprintf(stderr, L"ksh: %ls: filename required\n", cmd->argv[0]);
        *code = 2;
        return true;
    }

    wchar_t resolved[MAX_PATH];
    if (!FindSourceFile(cmd->argv[fileArgument], searchPath, resolved,
            sizeof(resolved) / sizeof(resolved[0]))) {
        fwprintf(stderr, L"ksh: %ls: cannot find '%ls'\n", cmd->argv[0],
            cmd->argv[fileArgument]);
        *code = 1;
        return true;
    }

    wchar_t* saved[MAX_ARGS] = {0};
    int savedCount = shell->env->positionalCount;
    for (int i = 0; i < savedCount; ++i) {
        saved[i] = _wcsdup(shell->env->positional[i]);
        if (!saved[i]) {
            for (int j = 0; j < i; ++j) free(saved[j]);
            fwprintf(stderr, L"ksh: %ls: out of memory\n", cmd->argv[0]);
            *code = 1;
            return true;
        }
    }
    if (cmd->argc > fileArgument + 1 &&
        !Env_SetPositional(shell->env,
            savedCount ? saved[0] : L"ksh",
            cmd->argc - fileArgument - 1,
            &cmd->argv[fileArgument + 1])) {
        for (int i = 0; i < savedCount; ++i) free(saved[i]);
        fwprintf(stderr, L"ksh: %ls: cannot set script parameters\n",
            cmd->argv[0]);
        *code = 1;
        return true;
    }

    if (shell->sourceDepth >= 128) {
        fwprintf(stderr, L"ksh: %ls: maximum source nesting depth exceeded\n", cmd->argv[0]);
        for (int i = 0; i < savedCount; ++i) free(saved[i]);
        *code = 1;
        return true;
    }

    bool previousReturn = shell->returnRequested;
    int previousReturnStatus = shell->returnStatus;
    shell->returnRequested = false;
    shell->sourceDepth++;
    int status = shell->RunScript(shell, resolved);
    shell->sourceDepth--;
    if (shell->returnRequested) status = shell->returnStatus;
    shell->returnRequested = previousReturn;
    shell->returnStatus = previousReturnStatus;

    if (savedCount > 0)
        (void)Env_SetPositional(shell->env, saved[0], savedCount - 1,
            &saved[1]);
    for (int i = 0; i < savedCount; ++i) free(saved[i]);
    *code = status;
    return true;
}

bool Builtin_Return(IShell* shell, CommandAST* cmd, int* code) {
    if (shell->sourceDepth <= 0 && shell->functionDepth <= 0) {
        fwprintf(stderr, L"ksh: return: not in a function or sourced script\n");
        *code = 2;
        return true;
    }
    if (cmd->argc > 2) {
        fwprintf(stderr, L"ksh: return: too many arguments\n");
        *code = 2;
        return true;
    }
    long long value = shell->last_status;
    if (cmd->argc == 2 && !TestParseInteger(cmd->argv[1], &value)) {
        fwprintf(stderr, L"ksh: return: '%ls': numeric argument required\n",
            cmd->argv[1]);
        *code = 2;
    } else {
        *code = (int)((unsigned long long)value & 0xffu);
    }
    shell->returnStatus = *code;
    shell->returnRequested = true;
    return true;
}

bool Builtin_Getopts(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc < 3) {
        fwprintf(stderr, L"ksh: getopts: usage: getopts optstring name [args...]\n");
        *code = 2;
        return true;
    }
    const wchar_t* optionString = cmd->argv[1];
    const wchar_t* variableName = cmd->argv[2];
    if (!IsValidIdentifier(variableName)) {
        fwprintf(stderr, L"ksh: getopts: '%ls': not a valid variable name\n",
            variableName);
        *code = 2;
        return true;
    }
    wchar_t* inheritedOptind = NULL;
    const wchar_t* optindValue = shell->env->Get(shell->env, L"OPTIND");
    if (!optindValue)
        optindValue = inheritedOptind = GetEnvironmentValue(L"OPTIND");
    long optind = 1;
    if (optindValue) {
        wchar_t* end = NULL;
        errno = 0;
        optind = wcstol(optindValue, &end, 10);
        if (errno || end == optindValue || *end || optind < 1 ||
            optind > MAX_ARGS) {
            free(inheritedOptind);
            fwprintf(stderr, L"ksh: getopts: OPTIND must be a positive integer\n");
            *code = 2;
            return true;
        }
    }
    wchar_t normalizedOptind[24];
    swprintf_s(normalizedOptind, sizeof(normalizedOptind) /
        sizeof(normalizedOptind[0]), L"%ld", optind);
    free(inheritedOptind);
    if (!shell->env->Set(shell->env, L"OPTIND", normalizedOptind)) {
        fwprintf(stderr, L"ksh: getopts: cannot set OPTIND\n");
        *code = 1;
        return true;
    }

    int argumentCount;
    wchar_t** arguments;
    if (cmd->argc > 3) {
        argumentCount = cmd->argc - 3;
        arguments = &cmd->argv[3];
    } else {
        argumentCount = shell->env->positionalCount > 0 ?
            shell->env->positionalCount - 1 : 0;
        arguments = shell->env->positionalCount > 1 ?
            &shell->env->positional[1] : NULL;
    }
    if (shell->getoptsLastOptind != (int)optind) {
        shell->getoptsOffset = 1;
        shell->getoptsLastOptind = (int)optind;
    }
    int argumentIndex = (int)optind - 1;
    const wchar_t* argument = argumentIndex < argumentCount ?
        arguments[argumentIndex] : NULL;
    if (!argument || argument[0] != L'-' || !argument[1] ||
        wcscmp(argument, L"--") == 0) {
        if (argument && wcscmp(argument, L"--") == 0)
            optind++;
        wchar_t optindBuffer[24];
        swprintf_s(optindBuffer, sizeof(optindBuffer) /
            sizeof(optindBuffer[0]), L"%ld", optind);
        bool success = shell->env->Set(shell->env, L"OPTIND", optindBuffer) &&
            shell->env->Set(shell->env, variableName, L"?") &&
            shell->env->Unset(shell->env, L"OPTARG");
        if (!success) {
            fwprintf(stderr, L"ksh: getopts: cannot update option variables\n");
            *code = 1;
        } else {
            *code = 1;
        }
        return true;
    }

    size_t optionOffset = (size_t)shell->getoptsOffset;
    wchar_t option = argument[optionOffset];
    if (!option) {
        shell->getoptsOffset = 1;
        optind++;
        if (optind - 1 < argumentCount)
            argument = arguments[optind - 1];
        if (!argument || argument[0] != L'-' || !argument[1] ||
            wcscmp(argument, L"--") == 0) {
            wchar_t optindBuffer[24];
            swprintf_s(optindBuffer, sizeof(optindBuffer) /
                sizeof(optindBuffer[0]), L"%ld", optind);
            bool success = shell->env->Set(shell->env, L"OPTIND",
                    optindBuffer) &&
                shell->env->Set(shell->env, variableName, L"?") &&
                shell->env->Unset(shell->env, L"OPTARG");
            if (!success)
                fwprintf(stderr, L"ksh: getopts: cannot update option variables\n");
            *code = 1;
            return true;
        }
        optionOffset = 1;
        option = argument[optionOffset];
    }

    const wchar_t* optionDefinition = wcschr(optionString, option);
    bool silentErrors = optionString[0] == L':';
    if (!optionDefinition || option == L':' || option == L'?') {
        if (argument[optionOffset + 1])
            shell->getoptsOffset = (int)optionOffset + 1;
        else {
            shell->getoptsOffset = 1;
            optind++;
        }
        wchar_t optionValue[2] = {option, L'\0'};
        if (!silentErrors)
            fwprintf(stderr, L"ksh: getopts: illegal option -- %lc\n",
                option);
        wchar_t optindBuffer[24];
        swprintf_s(optindBuffer, sizeof(optindBuffer) /
            sizeof(optindBuffer[0]), L"%ld", optind);
        bool success = shell->env->Set(shell->env, L"OPTIND", optindBuffer) &&
            shell->env->Set(shell->env, variableName, L"?") &&
            shell->env->Set(shell->env, L"OPTARG", optionValue);
        *code = success ? 0 : 1;
        return true;
    }

    bool requiresArgument = optionDefinition[1] == L':';
    wchar_t* optionArgument = NULL;
    if (requiresArgument) {
        if (argument[optionOffset + 1]) {
            optionArgument = _wcsdup(argument + optionOffset + 1);
            shell->getoptsOffset = 1;
            optind++;
        } else if (argumentIndex + 1 < argumentCount) {
            optionArgument = _wcsdup(arguments[argumentIndex + 1]);
            shell->getoptsOffset = 1;
            optind += 2;
        } else {
            shell->getoptsOffset = 1;
            optind++;
            optionArgument = _wcsdup(L"");
            if (!silentErrors)
                fwprintf(stderr,
                    L"ksh: getopts: option requires an argument -- %lc\n",
                    option);
            wchar_t result[2] = {silentErrors ? L':' : L'?', L'\0'};
            wchar_t optionValue[2] = {option, L'\0'};
            wchar_t optindBuffer[24];
            swprintf_s(optindBuffer, sizeof(optindBuffer) /
                sizeof(optindBuffer[0]), L"%ld", optind);
            bool success = optionArgument &&
                shell->env->Set(shell->env, L"OPTIND", optindBuffer) &&
                shell->env->Set(shell->env, variableName, result) &&
                (silentErrors ?
                    shell->env->Set(shell->env, L"OPTARG", optionValue) :
                    shell->env->Unset(shell->env, L"OPTARG"));
            free(optionArgument);
            *code = success ? 0 : 1;
            return true;
        }
        if (!optionArgument) {
            fwprintf(stderr, L"ksh: getopts: out of memory\n");
            *code = 1;
            return true;
        }
    } else if (argument[optionOffset + 1]) {
        shell->getoptsOffset = (int)optionOffset + 1;
    } else {
        shell->getoptsOffset = 1;
        optind++;
    }

    wchar_t result[2] = {option, L'\0'};
    wchar_t optindBuffer[24];
    swprintf_s(optindBuffer, sizeof(optindBuffer) /
        sizeof(optindBuffer[0]), L"%ld", optind);
    bool success = shell->env->Set(shell->env, L"OPTIND", optindBuffer) &&
        shell->env->Set(shell->env, variableName, result);
    if (success) {
        if (optionArgument)
            success = shell->env->Set(shell->env, L"OPTARG", optionArgument);
        else
            success = shell->env->Unset(shell->env, L"OPTARG");
    }
    free(optionArgument);
    *code = success ? 0 : 1;
    if (!success)
        fwprintf(stderr, L"ksh: getopts: cannot update option variables\n");
    return true;
}

bool Builtin_Umask(IShell* shell, CommandAST* cmd, int* code) {
    bool symbolic = false;
    bool portable = false;
    bool changed = false;
    int operand = 1;
    while (operand < cmd->argc && cmd->argv[operand][0] == L'-') {
        if (wcscmp(cmd->argv[operand], L"--") == 0) {
            operand++;
            break;
        }
        for (const wchar_t* option = cmd->argv[operand] + 1; *option;
            ++option) {
            if (*option == L'S') symbolic = true;
            else if (*option == L'p') portable = true;
            else {
                fwprintf(stderr, L"ksh: umask: invalid option -%lc\n",
                    *option);
                *code = 2;
                return true;
            }
        }
        operand++;
    }
    if (cmd->argc - operand > 1) {
        fwprintf(stderr, L"ksh: umask: too many operands\n");
        *code = 2;
        return true;
    }
    if (operand < cmd->argc) {
        const wchar_t* text = cmd->argv[operand];
        if (!*text) {
            fwprintf(stderr, L"ksh: umask: invalid mask\n");
            *code = 2;
            return true;
        }
        unsigned int mask = 0;
        for (const wchar_t* digit = text; *digit; ++digit) {
            if (*digit < L'0' || *digit > L'7' || mask > 0777) {
                fwprintf(stderr, L"ksh: umask: invalid octal mask '%ls'\n",
                    text);
                *code = 2;
                return true;
            }
            mask = mask * 8 + (unsigned int)(*digit - L'0');
        }
        if (mask > 0777) {
            fwprintf(stderr, L"ksh: umask: mask exceeds 0777\n");
            *code = 2;
            return true;
        }
        shell->fileMask = mask;
        changed = true;
        int previousMask;
        if (_umask_s((int)(mask & 0700), &previousMask) != 0) {
            fwprintf(stderr, L"ksh: umask: cannot update file creation mask\n");
            *code = 1;
            return true;
        }
    }

    if (changed && !symbolic && !portable) {
        *code = 0;
        return true;
    }
    if (symbolic) {
        unsigned int permissions = 0777 & ~shell->fileMask;
        wprintf(L"u=%ls%ls%ls,g=%ls%ls%ls,o=%ls%ls%ls\n",
            permissions & 0400 ? L"r" : L"",
            permissions & 0200 ? L"w" : L"",
            permissions & 0100 ? L"x" : L"",
            permissions & 0040 ? L"r" : L"",
            permissions & 0020 ? L"w" : L"",
            permissions & 0010 ? L"x" : L"",
            permissions & 0004 ? L"r" : L"",
            permissions & 0002 ? L"w" : L"",
            permissions & 0001 ? L"x" : L"");
    } else if (portable) {
        wprintf(L"umask %04o\n", shell->fileMask);
    } else {
        wprintf(L"%04o\n", shell->fileMask);
    }
    *code = ferror(stdout) ? 1 : 0;
    return true;
}

ShellLimit* FindShellLimit(IShell* shell, wchar_t option) {
    for (size_t i = 0; i < SHELL_LIMIT_COUNT; ++i) {
        if (shell->limits[i].option == option) return &shell->limits[i];
    }
    return NULL;
}

void PrintShellLimit(const ShellLimit* limit, bool hard) {
    unsigned long long value = hard ? limit->hard : limit->soft;
    if (value == ULLONG_MAX) wprintf(L"%ls\tunlimited", limit->name);
    else wprintf(L"%ls\t%llu", limit->name, value);
    if (limit->option != L'n')
        fputws(L"\t(not enforceable on Windows)", stdout);
    putwchar(L'\n');
}

bool Builtin_Ulimit(IShell* shell, CommandAST* cmd, int* code) {
    bool hard = false;
    bool soft = false;
    bool all = false;
    wchar_t resource = L'\0';
    int operand = 1;
    while (operand < cmd->argc && cmd->argv[operand][0] == L'-') {
        if (wcscmp(cmd->argv[operand], L"--") == 0) {
            operand++;
            break;
        }
        const wchar_t* flags = cmd->argv[operand] + 1;
        if (!*flags) {
            fwprintf(stderr, L"ksh: ulimit: invalid option\n");
            *code = 2;
            return true;
        }
        for (; *flags; ++flags) {
            if (*flags == L'H') hard = true;
            else if (*flags == L'S') soft = true;
            else if (*flags == L'a') all = true;
            else if (FindShellLimit(shell, *flags)) {
                if (resource && resource != *flags) {
                    fwprintf(stderr, L"ksh: ulimit: select only one resource\n");
                    *code = 2;
                    return true;
                }
                resource = *flags;
            } else {
                fwprintf(stderr, L"ksh: ulimit: invalid option -%lc\n",
                    *flags);
                *code = 2;
                return true;
            }
        }
        operand++;
    }
    if (hard && soft) {
        fwprintf(stderr, L"ksh: ulimit: -H and -S are mutually exclusive\n");
        *code = 2;
        return true;
    }
    if (all && resource) {
        fwprintf(stderr, L"ksh: ulimit: -a cannot be combined with a resource\n");
        *code = 2;
        return true;
    }
    if (cmd->argc - operand > 1 || (all && operand < cmd->argc)) {
        fwprintf(stderr, L"ksh: ulimit: invalid operand count\n");
        *code = 2;
        return true;
    }
    if (!resource && !all) resource = L'f';
    if (all) {
        for (size_t i = 0; i < SHELL_LIMIT_COUNT; ++i)
            PrintShellLimit(&shell->limits[i], hard);
        *code = ferror(stdout) ? 1 : 0;
        return true;
    }

    ShellLimit* limit = FindShellLimit(shell, resource);
    if (!limit) {
        fwprintf(stderr, L"ksh: ulimit: unsupported resource\n");
        *code = 2;
        return true;
    }
    if (operand == cmd->argc) {
        if (resource == L'n') {
            limit->soft = (unsigned long long)_getmaxstdio();
            if (limit->hard < limit->soft) limit->hard = limit->soft;
        }
        unsigned long long value = hard ? limit->hard : limit->soft;
        if (value == ULLONG_MAX) wprintf(L"unlimited\n");
        else wprintf(L"%llu\n", value);
        *code = ferror(stdout) ? 1 : 0;
        return true;
    }

    const wchar_t* valueText = cmd->argv[operand];
    unsigned long long value = ULLONG_MAX;
    if (wcscmp(valueText, L"unlimited") != 0 &&
        wcscmp(valueText, L"-1") != 0) {
        wchar_t* end = NULL;
        errno = 0;
        value = wcstoull(valueText, &end, 10);
        if (errno || end == valueText || *end) {
            fwprintf(stderr, L"ksh: ulimit: invalid limit '%ls'\n",
                valueText);
            *code = 2;
            return true;
        }
    }
    if (resource != L'n') {
        fwprintf(stderr,
            L"ksh: ulimit: resource -%lc cannot be enforced on Windows\n",
            resource);
        *code = 1;
        return true;
    }
    if (value == ULLONG_MAX) {
        fwprintf(stderr, L"ksh: ulimit: unlimited open files is unavailable\n");
        *code = 1;
        return true;
    }
    if (value < 3 || value > 2048) {
        fwprintf(stderr,
            L"ksh: ulimit: open-file limit must be between 3 and 2048\n");
        *code = 1;
        return true;
    }
    if (hard) {
        if (value < limit->soft) limit->soft = value;
        limit->hard = value;
    } else {
        if (value > limit->hard) {
            fwprintf(stderr, L"ksh: ulimit: soft limit exceeds hard limit\n");
            *code = 1;
            return true;
        }
        limit->soft = value;
    }
    if (_setmaxstdio((int)limit->soft) < 0) {
        fwprintf(stderr, L"ksh: ulimit: cannot update open-file limit\n");
        *code = 1;
        return true;
    }
    *code = 0;
    return true;
}

double FileTimeSeconds(const FILETIME* time) {
    ULARGE_INTEGER value;
    value.LowPart = time->dwLowDateTime;
    value.HighPart = time->dwHighDateTime;
    return (double)value.QuadPart / 10000000.0;
}

void PrintShellTime(double seconds) {
    unsigned long minutes = (unsigned long)(seconds / 60.0);
    double remainder = seconds - (double)minutes * 60.0;
    wprintf(L"%lum%.2fs", minutes, remainder);
}

bool Builtin_Times(CommandAST* cmd, int* code) {
    if (cmd->argc != 1) {
        fwprintf(stderr, L"ksh: times: takes no arguments\n");
        *code = 2;
        return true;
    }
    FILETIME creation;
    FILETIME exitTime;
    FILETIME kernel;
    FILETIME user;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exitTime,
            &kernel, &user)) {
        fwprintf(stderr, L"ksh: times: cannot obtain process times\n");
        *code = 1;
        return true;
    }
    PrintShellTime(FileTimeSeconds(&user));
    putwchar(L' ');
    PrintShellTime(FileTimeSeconds(&kernel));
    putwchar(L'\n');
    PrintShellTime(0.0);
    putwchar(L' ');
    PrintShellTime(0.0);
    putwchar(L'\n');
    *code = ferror(stdout) ? 1 : 0;
    return true;
}

bool Builtin_Setenv(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc < 2 || cmd->argc > 3) {
        fwprintf(stderr, L"ksh: setenv: usage: setenv name [value]\n");
        *code = 2;
        return true;
    }
    const wchar_t* equal = wcschr(cmd->argv[1], L'=');
    if (equal && cmd->argc == 3) {
        fwprintf(stderr, L"ksh: setenv: use name=value or name value, not both\n");
        *code = 2;
        return true;
    }
    size_t nameLength = equal ? (size_t)(equal - cmd->argv[1]) :
        wcslen(cmd->argv[1]);
    wchar_t* name = (wchar_t*)malloc((nameLength + 1) * sizeof(wchar_t));
    if (!name) {
        fwprintf(stderr, L"ksh: setenv: out of memory\n");
        *code = 1;
        return true;
    }
    memcpy(name, cmd->argv[1], nameLength * sizeof(wchar_t));
    name[nameLength] = L'\0';
    if (!IsValidIdentifier(name)) {
        fwprintf(stderr, L"ksh: setenv: '%ls': not a valid identifier\n",
            cmd->argv[1]);
        free(name);
        *code = 2;
        return true;
    }
    const wchar_t* value = cmd->argc == 3 ? cmd->argv[2] :
        (equal ? equal + 1 : L"");
    bool success = shell->env->Set(shell->env, name, value);
    free(name);
    if (!success) {
        fwprintf(stderr, L"ksh: setenv: cannot set environment variable\n");
        *code = 1;
    } else {
        *code = 0;
    }
    return true;
}

bool Builtin_Cls(CommandAST* cmd, int* code) {
    (void)cmd;
    HANDLE hConsole = (HANDLE)_get_osfhandle(_fileno(stdout));
    COORD coordScreen = { 0, 0 };
    DWORD cCharsWritten;
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    if (hConsole == INVALID_HANDLE_VALUE || !_isatty(_fileno(stdout)) ||
        !GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        fwprintf(stderr, L"ksh: clear: standard output is not a console\n");
        *code = 1;
        return true;
    }
    DWORD bufferSize = (DWORD)csbi.dwSize.X * (DWORD)csbi.dwSize.Y;
    bool success =
        FillConsoleOutputCharacterW(hConsole, L' ', bufferSize, coordScreen,
            &cCharsWritten) &&
        cCharsWritten == bufferSize &&
        FillConsoleOutputAttribute(hConsole, csbi.wAttributes, bufferSize,
            coordScreen, &cCharsWritten) &&
        cCharsWritten == bufferSize &&
        SetConsoleCursorPosition(hConsole, coordScreen);
    if (!success)
        fwprintf(stderr, L"ksh: clear: cannot clear console (error %lu)\n",
            GetLastError());
    *code = success ? 0 : 1;
    return true;
}

static void FormatNumberWithCommas(unsigned long long val, wchar_t* out, size_t outSize) {
    wchar_t temp[32];
    int len = swprintf_s(temp, _countof(temp), L"%llu", val);
    if (len <= 0) {
        if (outSize > 0) out[0] = L'\0';
        return;
    }
    int commas = (len - 1) / 3;
    int newLen = len + commas;
    if ((size_t)newLen >= outSize) {
        wcsncpy_s(out, outSize, temp, _TRUNCATE);
        return;
    }
    out[newLen] = L'\0';
    int src = len - 1;
    int dst = newLen - 1;
    int count = 0;
    while (src >= 0) {
        out[dst--] = temp[src--];
        count++;
        if (count == 3 && src >= 0) {
            out[dst--] = L',';
            count = 0;
        }
    }
}

bool Builtin_Dir(CommandAST* cmd, int* code) {
    const wchar_t* path = NULL;
    bool bare = false;
    bool show_hidden = false;
    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'/' || arg[0] == L'-') {
            for (int k = 1; arg[k]; k++) {
                if (arg[k] == L'b' || arg[k] == L'B') bare = true;
                else if (arg[k] == L'a' || arg[k] == L'A') show_hidden = true;
            }
        } else if (!path) {
            path = arg;
        } else {
            fwprintf(stderr, L"ksh: dir: usage: dir [path]\n");
            *code = 2;
            return true;
        }
    }
    if (!path) path = L".";

    wchar_t fullPath[MAX_PATH * 2];
    if (_wfullpath(fullPath, path, _countof(fullPath)) == NULL) {
        wcsncpy_s(fullPath, _countof(fullPath), path, _TRUNCATE);
    }

    DWORD attributes = GetFileAttributesW(fullPath);
    bool isDirectory = (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY));

    wchar_t dirHeader[MAX_PATH * 2];
    wchar_t searchPath[MAX_PATH * 2];

    if (isDirectory) {
        wcsncpy_s(dirHeader, _countof(dirHeader), fullPath, _TRUNCATE);
        size_t dlen = wcslen(dirHeader);
        while (dlen > 3 && (dirHeader[dlen - 1] == L'\\' || dirHeader[dlen - 1] == L'/')) {
            dirHeader[--dlen] = L'\0';
        }
        swprintf_s(searchPath, _countof(searchPath), L"%ls%ls*",
            dirHeader,
            (dirHeader[dlen - 1] == L'\\' || dirHeader[dlen - 1] == L'/') ? L"" : L"\\");
    } else {
        wcsncpy_s(searchPath, _countof(searchPath), fullPath, _TRUNCATE);
        wcsncpy_s(dirHeader, _countof(dirHeader), fullPath, _TRUNCATE);
        wchar_t* lastSlash = wcsrchr(dirHeader, L'\\');
        wchar_t* lastFSlash = wcsrchr(dirHeader, L'/');
        if (lastFSlash && (!lastSlash || lastFSlash > lastSlash)) lastSlash = lastFSlash;
        if (lastSlash) {
            if (lastSlash == dirHeader + 2 && dirHeader[1] == L':') {
                *(lastSlash + 1) = L'\0';
            } else if (lastSlash == dirHeader) {
                *(lastSlash + 1) = L'\0';
            } else {
                *lastSlash = L'\0';
            }
        } else {
            GetCurrentDirectoryW(_countof(dirHeader), dirHeader);
        }
    }

    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPath, &ffd);

    if (hFind == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"ksh: dir: %ls: cannot list path (error %lu)\n",
            path, GetLastError());
        *code = 1;
        return true;
    }

    wchar_t volumeRoot[MAX_PATH] = { 0 };
    if (!bare) {
        if (GetVolumePathNameW(dirHeader, volumeRoot, _countof(volumeRoot))) {
            wchar_t label[MAX_PATH] = { 0 };
            DWORD serial = 0;
            if (GetVolumeInformationW(volumeRoot, label, _countof(label), &serial, NULL, NULL, NULL, 0)) {
                wchar_t drive = volumeRoot[0];
                if (label[0]) {
                    wprintf(L" Volume in drive %lc is %ls\n", drive, label);
                } else {
                    wprintf(L" Volume in drive %lc has no label.\n", drive);
                }
                wprintf(L" Volume Serial Number is %04X-%04X\n\n",
                    (unsigned int)(serial >> 16), (unsigned int)(serial & 0xffff));
            }
        }
        wprintf(L" Directory of %ls\n\n", dirHeader);
    }

    unsigned long long fileCount = 0;
    unsigned long long dirCount = 0;
    unsigned long long totalFileBytes = 0;
    bool success = true;

    do {
        if (bare) {
            if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0)
                continue;
            if (!show_hidden && (ffd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                continue;
            if (wprintf(L"%ls\n", ffd.cFileName) < 0) success = false;
        } else {
            if (!show_hidden && (ffd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                continue;

            FILETIME localFt;
            FileTimeToLocalFileTime(&ffd.ftLastWriteTime, &localFt);
            SYSTEMTIME st;
            FileTimeToSystemTime(&localFt, &st);
            int hour = st.wHour;
            const wchar_t* ampm = (hour >= 12) ? L"PM" : L"AM";
            int hour12 = hour % 12;
            if (hour12 == 0) hour12 = 12;

            wchar_t dateStr[32];
            swprintf_s(dateStr, _countof(dateStr), L"%02u/%02u/%04u  %02u:%02u %ls",
                st.wMonth, st.wDay, st.wYear, hour12, st.wMinute, ampm);

            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                dirCount++;
                if (wprintf(L"%ls    <DIR>          %ls\n", dateStr, ffd.cFileName) < 0)
                    success = false;
            } else {
                fileCount++;
                ULARGE_INTEGER fileSize;
                fileSize.LowPart = ffd.nFileSizeLow;
                fileSize.HighPart = ffd.nFileSizeHigh;
                totalFileBytes += fileSize.QuadPart;

                wchar_t sizeStr[32];
                FormatNumberWithCommas(fileSize.QuadPart, sizeStr, _countof(sizeStr));
                if (wprintf(L"%ls %17ls %ls\n", dateStr, sizeStr, ffd.cFileName) < 0)
                    success = false;
            }
        }
    } while (FindNextFileW(hFind, &ffd) != 0);

    DWORD enumerationError = GetLastError();
    FindClose(hFind);

    if (enumerationError != ERROR_NO_MORE_FILES) {
        fwprintf(stderr, L"ksh: dir: enumeration failed (error %lu)\n",
            enumerationError);
        success = false;
    }

    if (!bare && success) {
        wchar_t fcStr[32], fbStr[32];
        FormatNumberWithCommas(fileCount, fcStr, _countof(fcStr));
        FormatNumberWithCommas(totalFileBytes, fbStr, _countof(fbStr));
        wprintf(L"%16ls File(s) %14ls bytes\n", fcStr, fbStr);

        ULARGE_INTEGER freeBytes;
        freeBytes.QuadPart = 0;
        GetDiskFreeSpaceExW(volumeRoot, &freeBytes, NULL, NULL);

        wchar_t dcStr[32], dbStr[32];
        FormatNumberWithCommas(dirCount, dcStr, _countof(dcStr));
        FormatNumberWithCommas(freeBytes.QuadPart, dbStr, _countof(dbStr));
        wprintf(L"%16ls Dir(s)  %14ls bytes free\n", dcStr, dbStr);
    }

    *code = success && !ferror(stdout) ? 0 : 1;
    return true;
}

