#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

wchar_t* BuildWin32CommandLine(wchar_t** argv, int argc) {
    size_t est_len = 1;
    for (int i = 0; i < argc; i++) {
        est_len += wcslen(argv[i]) * 2 + 4;
    }
    wchar_t* cmdline = (wchar_t*)malloc(est_len * sizeof(wchar_t));
    if (!cmdline) return NULL;
    cmdline[0] = L'\0';

    for (int i = 0; i < argc; i++) {
        const wchar_t* arg = argv[i];
        /* Quote whenever the MSVCRT argv parser would otherwise split or unescape the argument. */
        bool need_quotes = (*arg == L'\0' || wcspbrk(arg, L" \t\n\v\"") != NULL);
        if (i > 0) wcscat_s(cmdline, est_len, L" ");

        if (!need_quotes) {
            wcscat_s(cmdline, est_len, arg);
        } else {
            wcscat_s(cmdline, est_len, L"\"");
            for (const wchar_t* p = arg; *p; p++) {
                int backslashes = 0;
                while (*p == L'\\') {
                    backslashes++;
                    p++;
                }
                if (*p == L'\0') {
                    for (int b = 0; b < backslashes * 2; b++) wcscat_s(cmdline, est_len, L"\\");
                    break;
                } else if (*p == L'\"') {
                    for (int b = 0; b < backslashes * 2 + 1; b++) wcscat_s(cmdline, est_len, L"\\");
                    wcscat_s(cmdline, est_len, L"\"");
                } else {
                    for (int b = 0; b < backslashes; b++) wcscat_s(cmdline, est_len, L"\\");
                    wchar_t ch[2] = { *p, 0 };
                    wcscat_s(cmdline, est_len, ch);
                }
            }
            wcscat_s(cmdline, est_len, L"\"");
        }
    }
    return cmdline;
}



static int OpenDevSocketDescriptor(const wchar_t* path, int flags) {
    bool isTcp = (_wcsnicmp(path, L"/dev/tcp/", 9) == 0);
    bool isUdp = (_wcsnicmp(path, L"/dev/udp/", 9) == 0);
    if (!isTcp && !isUdp) return -1;

    const wchar_t* hostStart = path + 9;
    const wchar_t* portSlash = wcschr(hostStart, L'/');
    if (!portSlash || !*(portSlash + 1)) return -1;

    wchar_t hostW[256];
    wcsncpy_s(hostW, _countof(hostW), hostStart, (size_t)(portSlash - hostStart));
    const wchar_t* portW = portSlash + 1;

    char hostA[256];
    char portA[32];
    WideCharToMultiByte(CP_UTF8, 0, hostW, -1, hostA, sizeof(hostA), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, portW, -1, portA, sizeof(portA), NULL, NULL);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = isTcp ? SOCK_STREAM : SOCK_DGRAM;
    hints.ai_protocol = isTcp ? IPPROTO_TCP : IPPROTO_UDP;

    struct addrinfo* res = NULL;
    if (getaddrinfo(hostA, portA, &hints, &res) != 0 || !res) {
        return -1;
    }

    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) {
        freeaddrinfo(res);
        return -1;
    }

    if (connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) {
        closesocket(s);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    int crtMode = (flags & (_O_WRONLY | _O_RDWR)) ? _O_RDWR : _O_RDONLY;
    int fd = _open_osfhandle((intptr_t)s, crtMode | _O_BINARY);
    if (fd < 0) closesocket(s);
    return fd;
}

int OpenWideDescriptor(const wchar_t* path, int flags, int mode) {
    if (!path) return -1;
    if (_wcsnicmp(path, L"/dev/tcp/", 9) == 0 || _wcsnicmp(path, L"/dev/udp/", 9) == 0) {
        return OpenDevSocketDescriptor(path, flags);
    }
    if (_wcsicmp(path, L"/dev/null") == 0 || _wcsicmp(path, L"nul") == 0) {
        path = L"NUL";
    }
    int descriptor = -1;
    if (_wsopen_s(&descriptor, path, flags, _SH_DENYNO, mode) != 0)
        return -1;
    return descriptor;
}

static int BindHandleToDescriptor(HANDLE handle, int descriptor) {
    HANDLE duplicate = NULL;
    int fileDescriptor;
    if (!handle || handle == INVALID_HANDLE_VALUE) return -1;
    if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(),
            &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS))
        return -1;
    fileDescriptor = _open_osfhandle((intptr_t)duplicate,
        (descriptor == _fileno(stdin) ? _O_RDONLY : _O_WRONLY) | _O_BINARY);
    if (fileDescriptor < 0) CloseHandle(duplicate);
    return fileDescriptor;
}

static bool NoclobberBlocks(IShell* shell, const wchar_t* path, bool append) {
    if (!shell->noclobber || append) return false;
    if (_wcsicmp(path, L"/dev/null") == 0 || _wcsicmp(path, L"nul") == 0) return false;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return false;
    fwprintf(stderr, L"ksh: %ls: cannot overwrite existing file\n", path);
    return true;
}

int ExecuteBuiltinWithRedirections(IShell* shell, CommandAST* cmd,
    HANDLE hIn, HANDLE hOut, int* outCode) {
    int savedInput = -1;
    int savedOutput = -1;
    int savedErr = -1;
    int inputFd = -1;
    int outputFd = -1;
    int errFd = -1;
    int savedInputMode = -1;
    int savedOutputMode = -1;
    int savedErrMode = -1;
    int status = 1;
    bool inputChanged = false;
    bool outputChanged = false;
    bool errChanged = false;
    bool inputModeChanged = false;
    bool outputModeChanged = false;
    bool errModeChanged = false;

    bool needIn = (cmd->in_redirect != NULL || hIn != NULL);
    bool needOut = (cmd->out_redirect != NULL || hOut != NULL || cmd->out_to_err);
    bool needErr = (cmd->err_redirect != NULL || cmd->err_to_out);

    if (needIn) {
        fflush(stdin);
        savedInput = _dup(_fileno(stdin));
    }
    if (needOut) {
        fflush(stdout);
        savedOutput = _dup(_fileno(stdout));
    }
    if (needErr) {
        fflush(stderr);
        savedErr = _dup(_fileno(stderr));
    }

    if (cmd->in_redirect) {
        if ((wcscmp(cmd->in_redirect, L"p") == 0 || wcscmp(cmd->in_redirect, L"&p") == 0) &&
            shell && shell->coproc_active && shell->coproc_out_read) {
            inputFd = BindHandleToDescriptor(shell->coproc_out_read, _fileno(stdin));
        } else {
            inputFd = OpenWideDescriptor(cmd->in_redirect,
                _O_RDONLY | _O_BINARY, 0);
        }
    } else if (hIn) {
        inputFd = BindHandleToDescriptor(hIn, _fileno(stdin));
    }
    if (cmd->in_redirect || hIn) {
        savedInputMode = _setmode(_fileno(stdin), _O_U8TEXT);
        if (savedInputMode < 0) {
            fwprintf(stderr, L"ksh: cannot set redirected input encoding\n");
            goto cleanup;
        }
        inputModeChanged = true;
        if (inputFd < 0 || _dup2(inputFd, _fileno(stdin)) < 0) {
            fwprintf(stderr, L"ksh: cannot redirect standard input\n");
            goto cleanup;
        }
        inputChanged = true;
        if (_setmode(_fileno(stdin), _O_U8TEXT) < 0) {
            fwprintf(stderr, L"ksh: cannot set redirected input encoding\n");
            goto cleanup;
        }
    }

    if (cmd->err_to_out && cmd->err_to_out_before_out) {
        if (_dup2(_fileno(stdout), _fileno(stderr)) < 0) {
            fwprintf(stderr, L"ksh: cannot redirect standard error\n");
            goto cleanup;
        }
        errChanged = true;
    } else if (cmd->out_to_err && cmd->out_to_err_before_err) {
        if (_dup2(_fileno(stderr), _fileno(stdout)) < 0) {
            fwprintf(stderr, L"ksh: cannot redirect standard output\n");
            goto cleanup;
        }
        outputChanged = true;
    }

    if (cmd->out_redirect) {
        if ((wcscmp(cmd->out_redirect, L"p") == 0 || wcscmp(cmd->out_redirect, L"&p") == 0) &&
            shell && shell->coproc_active && shell->coproc_in_write) {
            outputFd = BindHandleToDescriptor(shell->coproc_in_write, _fileno(stdout));
        } else {
            int flags = _O_WRONLY | _O_CREAT | _O_BINARY |
                (cmd->append_out ? _O_APPEND : _O_TRUNC);
            outputFd = NoclobberBlocks(shell, cmd->out_redirect, cmd->append_out) ? -1 :
                OpenWideDescriptor(cmd->out_redirect, flags, _S_IREAD | _S_IWRITE);
        }
    } else if (hOut) {
        outputFd = BindHandleToDescriptor(hOut, _fileno(stdout));
    }
    if (cmd->out_redirect || hOut) {
        savedOutputMode = _setmode(_fileno(stdout), _O_U8TEXT);
        if (savedOutputMode < 0) {
            fwprintf(stderr, L"ksh: cannot set redirected output encoding\n");
            goto cleanup;
        }
        outputModeChanged = true;
        if (outputFd < 0 || _dup2(outputFd, _fileno(stdout)) < 0) {
            fwprintf(stderr, L"ksh: cannot redirect standard output\n");
            goto cleanup;
        }
        outputChanged = true;
        if (_setmode(_fileno(stdout), _O_U8TEXT) < 0) {
            fwprintf(stderr, L"ksh: cannot set redirected output encoding\n");
            goto cleanup;
        }
    }

    if (cmd->err_to_out && !cmd->err_to_out_before_out) {
        if (_dup2(_fileno(stdout), _fileno(stderr)) < 0) {
            fwprintf(stderr, L"ksh: cannot redirect standard error\n");
            goto cleanup;
        }
        errChanged = true;
    } else if (cmd->out_to_err && !cmd->out_to_err_before_err) {
        if (_dup2(_fileno(stderr), _fileno(stdout)) < 0) {
            fwprintf(stderr, L"ksh: cannot redirect standard output\n");
            goto cleanup;
        }
        outputChanged = true;
    } else if (cmd->err_redirect) {
        int flags = _O_WRONLY | _O_CREAT | _O_BINARY |
            (cmd->append_err ? _O_APPEND : _O_TRUNC);
        errFd = NoclobberBlocks(shell, cmd->err_redirect, cmd->append_err) ? -1 :
            OpenWideDescriptor(cmd->err_redirect, flags, _S_IREAD | _S_IWRITE);
        if (errFd < 0) {
            fwprintf(stderr, L"ksh: cannot redirect standard error\n");
            goto cleanup;
        }
        savedErrMode = _setmode(_fileno(stderr), _O_U8TEXT);
        if (savedErrMode >= 0) errModeChanged = true;
        if (_dup2(errFd, _fileno(stderr)) < 0) {
            fwprintf(stderr, L"ksh: cannot redirect standard error\n");
            goto cleanup;
        }
        errChanged = true;
        if (_setmode(_fileno(stderr), _O_U8TEXT) < 0) {
            fwprintf(stderr, L"ksh: cannot set redirected error encoding\n");
            goto cleanup;
        }
    }

    if (IsBatchFileName(cmd->argv[0])) {
        *outCode = Shell_RunBatch(shell, cmd->argv[0], cmd->argc - 1,
            cmd->argc > 1 ? &cmd->argv[1] : NULL);
        status = 0;
    } else if (Shell_HasFunction(shell, cmd->argv[0])) {
        *outCode = Shell_InvokeFunction(shell, cmd);
        status = 0;
    } else {
        status = TryExecBuiltin(shell, cmd, outCode) ? 0 : 1;
    }

cleanup:
    fflush(stdout);
    fflush(stderr);
    if (inputChanged && savedInput >= 0) {
        if (_dup2(savedInput, _fileno(stdin)) < 0) {
            fwprintf(stderr, L"ksh: cannot restore standard input\n");
            status = 1;
        }
    }
    if (inputModeChanged && _setmode(_fileno(stdin), savedInputMode) < 0) {
        fwprintf(stderr, L"ksh: cannot restore standard input encoding\n");
        status = 1;
    }
    if (outputChanged && savedOutput >= 0) {
        if (_dup2(savedOutput, _fileno(stdout)) < 0) {
            fwprintf(stderr, L"ksh: cannot restore standard output\n");
            status = 1;
        }
    }
    if (outputModeChanged && _setmode(_fileno(stdout), savedOutputMode) < 0) {
        fwprintf(stderr, L"ksh: cannot restore standard output encoding\n");
        status = 1;
    }
    if (errChanged && savedErr >= 0) {
        if (_dup2(savedErr, _fileno(stderr)) < 0) {
            fwprintf(stderr, L"ksh: cannot restore standard error\n");
            status = 1;
        }
    }
    if (errModeChanged && _setmode(_fileno(stderr), savedErrMode) < 0) {
        fwprintf(stderr, L"ksh: cannot restore standard error encoding\n");
        status = 1;
    }
    if (savedInput >= 0) _close(savedInput);
    if (savedOutput >= 0) _close(savedOutput);
    if (savedErr >= 0) _close(savedErr);
    if (inputFd >= 0) _close(inputFd);
    if (outputFd >= 0) _close(outputFd);
    if (errFd >= 0) _close(errFd);
    return status ? status : *outCode;
}

static void ProcessCompoundToken(IShell* shell, ShellArray* array, size_t* arrIdx, wchar_t prefixStack[16][256], int* prefixTop, const wchar_t* tok) {
    if (!tok || !*tok) return;
    if (wcscmp(tok, L")") == 0) {
        if (*prefixTop > 0) (*prefixTop)--;
        return;
    }
    const wchar_t* subeq = wcschr(tok, L'=');
    if (subeq && *(subeq + 1) == L'(') {
        wchar_t fieldName[128];
        wcsncpy_s(fieldName, _countof(fieldName), tok, (size_t)(subeq - tok));
        if (*prefixTop < 15) {
            (*prefixTop)++;
            swprintf_s(prefixStack[*prefixTop], 256, L"%ls.%ls", prefixStack[*prefixTop - 1], fieldName);
        }
        const wchar_t* afterNestedOpen = subeq + 2;
        if (*afterNestedOpen) {
            ProcessCompoundToken(shell, array, arrIdx, prefixStack, prefixTop, afterNestedOpen);
        }
        return;
    }
    size_t tlen = wcslen(tok);
    int trailingParens = 0;
    while (tlen > 0 && tok[tlen - 1] == L')') {
        trailingParens++;
        tlen--;
    }
    wchar_t cleanTok[MAX_LINE_LEN];
    wcsncpy_s(cleanTok, MAX_LINE_LEN, tok, tlen);

    if (subeq) {
        const wchar_t* cleanEq = wcschr(cleanTok, L'=');
        if (cleanEq && cleanEq > cleanTok) {
            wchar_t compKey[512];
            swprintf_s(compKey, _countof(compKey), L"%ls.%.*ls", prefixStack[*prefixTop], (int)(cleanEq - cleanTok), cleanTok);
            shell->env->Set(shell->env, compKey, cleanEq + 1);
        }
    } else if (cleanTok[0]) {
        Env_ArraySetIndex(array, (*arrIdx)++, cleanTok);
    }

    while (trailingParens > 0 && *prefixTop > 0) {
        (*prefixTop)--;
        trailingParens--;
    }
}

static bool Shell_CopyCompound(IShell* shell, const wchar_t* dstName, const wchar_t* srcName) {
    if (!shell || !dstName || !srcName || !*dstName || !*srcName) return false;
    size_t srcLen = wcslen(srcName);
    bool foundAny = false;
    for (EnvEntry* e = shell->env->head; e; e = e->next) {
        if (_wcsnicmp(e->key, srcName, srcLen) == 0 && e->key[srcLen] == L'.') {
            wchar_t newKey[512];
            swprintf_s(newKey, _countof(newKey), L"%ls%ls", dstName, e->key + srcLen);
            shell->env->Set(shell->env, newKey, e->value);
            foundAny = true;
        }
    }
    for (ShellFunction* fn = shell->functions; fn; fn = fn->next) {
        if (_wcsnicmp(fn->name, srcName, srcLen) == 0 && fn->name[srcLen] == L'.') {
            wchar_t newFn[512];
            swprintf_s(newFn, _countof(newFn), L"%ls%ls", dstName, fn->name + srcLen);
            Shell_SetFunction(shell, newFn, fn->body ? fn->body : L"");
        }
    }
    return foundAny;
}

static bool TryExecuteAssignment(IShell* shell, CommandAST* cmd, int* out_code) {
    if (!cmd || cmd->argc == 0) return false;

    /* 1. Array & Compound initialization: NAME=(...) */
    const wchar_t* first = cmd->argv[0];
    const wchar_t* eq = wcschr(first, L'=');
    if (eq && *(eq + 1) == L'(') {
        size_t nameLen = (size_t)(eq - first);
        if (nameLen == 0 || nameLen >= 256) return false;
        wchar_t name[256];
        wcsncpy_s(name, 256, first, nameLen);

        wchar_t baseIdent[256];
        const wchar_t* brk = wcschr(name, L'[');
        if (brk) {
            wcsncpy_s(baseIdent, _countof(baseIdent), name, (size_t)(brk - name));
            if (!IsValidIdentifier(baseIdent)) return false;
        } else {
            if (!IsValidIdentifier(name)) return false;
        }

        ShellArray* array = Env_FindArray(shell->env, brk ? baseIdent : name);
        if (!array) array = Env_CreateArray(shell->env, brk ? baseIdent : name, false);
        if (!array) { *out_code = 1; return true; }
        if (!brk) Env_ArrayClear(array);

        wchar_t prefixStack[16][256];
        int prefixTop = 0;
        wcsncpy_s(prefixStack[0], 256, name, _TRUNCATE);

        size_t arrIdx = 0;
        const wchar_t* afterOpen = eq + 2;
        if (*afterOpen) {
            ProcessCompoundToken(shell, array, &arrIdx, prefixStack, &prefixTop, afterOpen);
        }

        for (int i = 1; i < cmd->argc; i++) {
            ProcessCompoundToken(shell, array, &arrIdx, prefixStack, &prefixTop, cmd->argv[i]);
            if (prefixTop == 0 && i > 1 && cmd->argv[i][wcslen(cmd->argv[i]) - 1] == L')') {
                // Done with compound
            }
        }
        *out_code = 0;
        return true;
    }

    /* 2. Check if all arguments are assignments (VAR=VAL or ARR[IDX]=VAL) */
    bool allAssignments = true;
    for (int i = 0; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        const wchar_t* e = wcschr(arg, L'=');
        if (!e || e == arg) {
            allAssignments = false;
            break;
        }
        const wchar_t* bracket = wcschr(arg, L'[');
        size_t identLen = (bracket && bracket < e) ? (size_t)(bracket - arg) : (size_t)(e - arg);
        if (identLen == 0 || identLen >= 256) {
            allAssignments = false;
            break;
        }
        wchar_t ident[256];
        wcsncpy_s(ident, 256, arg, identLen);
        if (!IsValidIdentifier(ident)) {
            allAssignments = false;
            break;
        }
    }

    if (!allAssignments) return false;

    for (int i = 0; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        const wchar_t* e = wcschr(arg, L'=');
        const wchar_t* bracket = wcschr(arg, L'[');
        if (bracket && bracket < e) {
            size_t nameLen = (size_t)(bracket - arg);
            wchar_t baseName[256];
            wcsncpy_s(baseName, 256, arg, nameLen);

            wchar_t resolvedKey[512] = {0};
            wcscpy_s(resolvedKey, _countof(resolvedKey), baseName);

            const wchar_t* p = bracket;
            wchar_t firstIdx[128] = {0};
            bool hasFirst = false;

            while (p < e) {
                if (*p == L'[') {
                    const wchar_t* cb = wcschr(p, L']');
                    if (!cb || cb > e) break;
                    size_t idxLen = (size_t)(cb - p - 1);
                    wchar_t idxStr[128];
                    if (idxLen >= sizeof(idxStr) / sizeof(idxStr[0])) idxLen = sizeof(idxStr) / sizeof(idxStr[0]) - 1;
                    wcsncpy_s(idxStr, _countof(idxStr), p + 1, idxLen);
                    const wchar_t* resIdx = idxStr;
                    if (idxStr[0] == L'$') {
                        const wchar_t* v = shell->env->Get(shell->env, idxStr + 1);
                        if (v) resIdx = v;
                    } else if (!iswdigit(idxStr[0])) {
                        const wchar_t* v = shell->env->Get(shell->env, idxStr);
                        if (v) resIdx = v;
                    }
                    if (!hasFirst) {
                        wcsncpy_s(firstIdx, _countof(firstIdx), resIdx, _TRUNCATE);
                        hasFirst = true;
                    }
                    wchar_t seg[136];
                    swprintf_s(seg, _countof(seg), L"[%ls]", resIdx);
                    wcscat_s(resolvedKey, _countof(resolvedKey), seg);
                    p = cb + 1;
                } else if (*p == L'.') {
                    const wchar_t* nextB = wcschr(p, L'[');
                    size_t memLen = (nextB && nextB < e) ? (size_t)(nextB - p) : (size_t)(e - p);
                    wchar_t memStr[128];
                    if (memLen >= sizeof(memStr) / sizeof(memStr[0])) memLen = sizeof(memStr) / sizeof(memStr[0]) - 1;
                    wcsncpy_s(memStr, _countof(memStr), p, memLen);
                    wcscat_s(resolvedKey, _countof(resolvedKey), memStr);
                    p += memLen;
                } else {
                    size_t curLen = wcslen(resolvedKey);
                    if (curLen + 1 < _countof(resolvedKey)) {
                        resolvedKey[curLen] = *p;
                        resolvedKey[curLen + 1] = L'\0';
                    }
                    p++;
                }
            }
            shell->env->Set(shell->env, resolvedKey, e + 1);
            ShellArray* array = Env_FindArray(shell->env, baseName);
            if (!array) array = Env_CreateArray(shell->env, baseName, false);
            if (array && hasFirst) {
                const wchar_t* subKey = resolvedKey + nameLen;
                if (subKey[0] == L'[' && subKey[wcslen(subKey) - 1] == L']' && !wcschr(subKey + 1, L'[')) {
                    Env_ArraySet(array, firstIdx, e + 1);
                } else {
                    Env_ArraySet(array, subKey, e + 1);
                }
            }
            continue;
        }

        size_t nameLen = (size_t)(e - arg);
        wchar_t name[256];
        wcsncpy_s(name, 256, arg, nameLen);
        const wchar_t* rval = e + 1;
        Shell_CopyCompound(shell, name, rval);
        shell->env->Set(shell->env, name, rval);
    }

    *out_code = 0;
    return true;
}

/* ========================================================================= */
/*                   Process & Pipeline Spawning Engine                      */
/* ========================================================================= */

static int ExecuteSingleCommandCore(IShell* shell, CommandAST* cmd, HANDLE hIn, HANDLE hOut) {
    if (!cmd) return 0;

    int assignment_code = 0;
    if (TryExecuteAssignment(shell, cmd, &assignment_code)) {
        return assignment_code;
    }

    if (cmd->argc == 0) {
        if (cmd->out_redirect) {
            const wchar_t* outPath = cmd->out_redirect;
            if (_wcsicmp(outPath, L"/dev/null") == 0 || _wcsicmp(outPath, L"nul") == 0) outPath = L"NUL";
            HANDLE h = CreateFileW(outPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                   cmd->append_out ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        }
        return 0;
    }

    if (_wcsicmp(cmd->argv[0], L"/usr/bin/env") == 0 && cmd->argc > 1) {
        CommandAST shifted = *cmd;
        shifted.argc--;
        for (int a = 0; a < shifted.argc; a++) shifted.argv[a] = cmd->argv[a + 1];
        return ExecuteSingleCommandCore(shell, &shifted, hIn, hOut);
    }
    if (_wcsicmp(cmd->argv[0], L"/bin/sh") == 0 || _wcsicmp(cmd->argv[0], L"/bin/ksh") == 0 ||
        _wcsicmp(cmd->argv[0], L"/bin/bash") == 0 || _wcsicmp(cmd->argv[0], L"/usr/bin/sh") == 0 ||
        _wcsicmp(cmd->argv[0], L"/usr/bin/ksh") == 0 || _wcsicmp(cmd->argv[0], L"/usr/bin/bash") == 0) {
        if (cmd->argc >= 3 && wcscmp(cmd->argv[1], L"-c") == 0) {
            return shell->ExecuteLine(shell, cmd->argv[2]);
        } else if (cmd->argc >= 2 && cmd->argv[1][0] != L'-') {
            return shell->RunScript(shell, cmd->argv[1]);
        }
    }

    ShellTypeDef* typeDef = Shell_FindType(shell, cmd->argv[0]);
    if (typeDef && cmd->argc > 1) {
        const wchar_t* instArg = cmd->argv[1];
        wchar_t instName[128];
        const wchar_t* eq = wcschr(instArg, L'=');
        if (eq) {
            size_t inLen = (size_t)(eq - instArg);
            wcsncpy_s(instName, _countof(instName), instArg, inLen);
        } else {
            wcsncpy_s(instName, _countof(instName), instArg, _TRUNCATE);
        }
        for (ShellTypeMember* m = typeDef->members; m; m = m->next) {
            if (m->isFunction) {
                wchar_t funcName[256];
                swprintf_s(funcName, _countof(funcName), L"%ls.%ls", instName, m->name);
                Shell_SetFunction(shell, funcName, m->functionBody ? m->functionBody : L"");
            } else {
                wchar_t varName[256];
                swprintf_s(varName, _countof(varName), L"%ls.%ls", instName, m->name);
                shell->env->Set(shell->env, varName, m->defaultValue);
            }
        }
        wchar_t typeProp[256];
        swprintf_s(typeProp, _countof(typeProp), L"%ls._type", instName);
        shell->env->Set(shell->env, typeProp, typeDef->name);
        for (int i = 1; i < cmd->argc; i++) {
            const wchar_t* a = cmd->argv[i];
            const wchar_t* subeq = wcschr(a, L'=');
            if (subeq) {
                const wchar_t* fieldStart = a;
                if (i == 1) {
                    const wchar_t* paren = wcschr(a, L'(');
                    if (paren) fieldStart = paren + 1;
                    else fieldStart = subeq + 1;
                    subeq = wcschr(fieldStart, L'=');
                }
                if (subeq && subeq > fieldStart) {
                    wchar_t fName[128];
                    wcsncpy_s(fName, _countof(fName), fieldStart, (size_t)(subeq - fieldStart));
                    wchar_t fVal[MAX_LINE_LEN];
                    wcsncpy_s(fVal, _countof(fVal), subeq + 1, _TRUNCATE);
                    size_t vlen = wcslen(fVal);
                    if (vlen > 0 && fVal[vlen - 1] == L')') fVal[vlen - 1] = L'\0';
                    wchar_t varName[256];
                    swprintf_s(varName, _countof(varName), L"%ls.%ls", instName, fName);
                    shell->env->Set(shell->env, varName, fVal);
                }
            }
        }
        return 0;
    }

    int builtin_code = 0;
    if (IsBuiltinCommand(cmd) || Shell_HasFunction(shell, cmd->argv[0]) ||
        IsBatchFileName(cmd->argv[0])) {
        return ExecuteBuiltinWithRedirections(shell, cmd, hIn, hOut,
            &builtin_code);
    }

    /* Redirection overrides */
    HANDLE std_in = hIn ? hIn : GetStdHandle(STD_INPUT_HANDLE);
    HANDLE std_out = hOut ? hOut : GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE std_err = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE file_in = NULL, file_out = NULL, file_err = NULL;

    if (cmd->in_redirect) {
        const wchar_t* inPath = cmd->in_redirect;
        if (_wcsicmp(inPath, L"/dev/null") == 0 || _wcsicmp(inPath, L"nul") == 0) {
            inPath = L"NUL";
        }
        file_in = CreateFileW(inPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file_in == INVALID_HANDLE_VALUE) {
            wprintf(L"ksh: cannot open %ls for input\n", cmd->in_redirect);
            return 1;
        }
        std_in = file_in;
    }

    if (cmd->err_to_out && cmd->err_to_out_before_out) {
        std_err = std_out;
    } else if (cmd->out_to_err && cmd->out_to_err_before_err) {
        std_out = std_err;
    }

    if (cmd->out_redirect) {
        const wchar_t* outPath = cmd->out_redirect;
        if (_wcsicmp(outPath, L"/dev/null") == 0 || _wcsicmp(outPath, L"nul") == 0) {
            outPath = L"NUL";
        }
        if (NoclobberBlocks(shell, cmd->out_redirect, cmd->append_out)) {
            if (file_in) CloseHandle(file_in);
            return 1;
        }
        file_out = CreateFileW(outPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               cmd->append_out ? OPEN_ALWAYS : CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
        if (file_out == INVALID_HANDLE_VALUE) {
            wprintf(L"ksh: cannot open %ls for output\n", cmd->out_redirect);
            if (file_in) CloseHandle(file_in);
            return 1;
        }
        if (cmd->append_out) {
            SetFilePointer(file_out, 0, NULL, FILE_END);
        }
        std_out = file_out;
    }

    if (cmd->err_redirect) {
        const wchar_t* errPath = cmd->err_redirect;
        if (_wcsicmp(errPath, L"/dev/null") == 0 || _wcsicmp(errPath, L"nul") == 0) {
            errPath = L"NUL";
        }
        if (NoclobberBlocks(shell, cmd->err_redirect, cmd->append_err)) {
            if (file_in) CloseHandle(file_in);
            if (file_out) CloseHandle(file_out);
            return 1;
        }
        file_err = CreateFileW(errPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               cmd->append_err ? OPEN_ALWAYS : CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
        if (file_err == INVALID_HANDLE_VALUE) {
            wprintf(L"ksh: cannot open %ls for error output\n", cmd->err_redirect);
            if (file_in) CloseHandle(file_in);
            if (file_out) CloseHandle(file_out);
            return 1;
        }
        if (cmd->append_err) {
            SetFilePointer(file_err, 0, NULL, FILE_END);
        }
        std_err = file_err;
    }

    if (cmd->err_to_out && !cmd->err_to_out_before_out) {
        std_err = std_out;
    } else if (cmd->out_to_err && !cmd->out_to_err_before_err) {
        std_out = std_err;
    }

    wchar_t* cmdline = BuildWin32CommandLine(cmd->argv, cmd->argc);

    if (!cmdline) {
        if (file_in) CloseHandle(file_in);
        if (file_out) CloseHandle(file_out);
        if (file_err) CloseHandle(file_err);
        return 1;
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = std_in;
    si.hStdOutput = std_out;
    si.hStdError = std_err;
    ZeroMemory(&pi, sizeof(pi));

    for (int fdIdx = 3; fdIdx < KSH_FD_COUNT; fdIdx++) {
        if (shell && shell->fdTable[fdIdx]) {
            SetHandleInformation(shell->fdTable[fdIdx], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
            wchar_t fdVar[32], fdVal[32];
            swprintf_s(fdVar, _countof(fdVar), L"__KSH_FD_%d", fdIdx);
            swprintf_s(fdVal, _countof(fdVal), L"%llx", (unsigned long long)(uintptr_t)shell->fdTable[fdIdx]);
            SetEnvironmentVariableW(fdVar, fdVal);
        }
    }

    DWORD creationFlags = (shell && shell->monitor) ? CREATE_NEW_PROCESS_GROUP : 0;
    DWORD exit_code = 127;
    BOOL procCreated = CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, creationFlags, NULL, NULL, &si, &pi);
    for (int fdIdx = 3; fdIdx < KSH_FD_COUNT; fdIdx++) {
        if (shell && shell->fdTable[fdIdx]) {
            wchar_t fdVar[32];
            swprintf_s(fdVar, _countof(fdVar), L"__KSH_FD_%d", fdIdx);
            SetEnvironmentVariableW(fdVar, NULL);
        }
    }
    if (procCreated) {
        g_foreground_process = pi.hProcess;
        while (WaitForSingleObject(pi.hProcess, 100) == WAIT_TIMEOUT) {
            if (g_interrupted) {
                if (WaitForSingleObject(pi.hProcess, 400) == WAIT_TIMEOUT) {
                    TerminateProcess(pi.hProcess, 130);
                }
                exit_code = 130;
                break;
            }
        }
        g_foreground_process = NULL;
        if (exit_code != 130) {
            GetExitCodeProcess(pi.hProcess, &exit_code);
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        wprintf(L"ksh: %ls: command not found or cannot execute\n", cmd->argv[0]);
    }

    free(cmdline);
    if (file_in) CloseHandle(file_in);
    if (file_out) CloseHandle(file_out);
    if (file_err) CloseHandle(file_err);
    return (int)exit_code;
}

static DWORD StdHandleId(int fd) {
    return fd == 0 ? STD_INPUT_HANDLE : (fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
}

static bool OpenRedirectTarget(IShell* shell, FdRedirectKind kind, const wchar_t* path, HANDLE* out) {
    if (kind == FD_REDIRECT_WRITE && NoclobberBlocks(shell, path, false)) return false;
    const wchar_t* target = path;
    if (_wcsicmp(path, L"/dev/null") == 0 || _wcsicmp(path, L"nul") == 0) target = L"NUL";
    DWORD access = kind == FD_REDIRECT_READ ? GENERIC_READ : GENERIC_WRITE;
    DWORD disposition = kind == FD_REDIRECT_READ ? OPEN_EXISTING :
        (kind == FD_REDIRECT_WRITE ? CREATE_ALWAYS : OPEN_ALWAYS);
    HANDLE h = CreateFileW(target, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
        disposition, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"ksh: cannot open %ls\n", path);
        return false;
    }
    if (kind == FD_REDIRECT_APPEND) SetFilePointer(h, 0, NULL, FILE_END);
    *out = h;
    return true;
}

/* Takes ownership of h. A NULL h closes descriptor n (only for n >= 3). */
static bool InstallFdHandle(IShell* shell, int n, HANDLE h, bool persistent) {
    if (n >= 3) {
        HANDLE old = shell->fdTable[n];
        shell->fdTable[n] = h;
        if (persistent && old) CloseHandle(old);
        return true;
    }
    int fd = _open_osfhandle((intptr_t)h, _O_BINARY);
    if (fd < 0) {
        CloseHandle(h);
        return false;
    }
    if (_dup2(fd, n) < 0) {
        _close(fd);
        fwprintf(stderr, L"ksh: cannot redirect descriptor %d\n", n);
        return false;
    }
    _close(fd);
    _setmode(n, _O_U8TEXT);
    SetStdHandle(StdHandleId(n), (HANDLE)_get_osfhandle(n));
    return true;
}

static HANDLE ResolveFdSource(IShell* shell, int n) {
    HANDLE h = n <= 2 ? GetStdHandle(StdHandleId(n)) : shell->fdTable[n];
    return (h && h != INVALID_HANDLE_VALUE) ? h : NULL;
}

static bool ApplyFdRedirect(IShell* shell, int n, FdRedirectKind kind,
    const wchar_t* path, int dupFrom, bool persistent) {
    HANDLE h = NULL;
    switch (kind) {
    case FD_REDIRECT_READ:
    case FD_REDIRECT_WRITE:
    case FD_REDIRECT_APPEND:
        if (!OpenRedirectTarget(shell, kind, path, &h)) return false;
        break;
    case FD_REDIRECT_CLOSE:
        if (n >= 3) return InstallFdHandle(shell, n, NULL, persistent);
        if (!OpenRedirectTarget(shell, n == 0 ? FD_REDIRECT_READ : FD_REDIRECT_WRITE, L"NUL", &h))
            return false;
        break;
    case FD_REDIRECT_DUP: {
        HANDLE src = NULL;
        if (dupFrom == -2) {
            if (shell && shell->coproc_active) {
                src = (kind == FD_REDIRECT_READ || n == 0) ? shell->coproc_out_read : shell->coproc_in_write;
            }
        } else {
            src = ResolveFdSource(shell, dupFrom);
        }
        if (!src) {
            fwprintf(stderr, L"ksh: %ls: bad file descriptor\n", dupFrom == -2 ? L"p" : L"");
            return false;
        }
        if (!DuplicateHandle(GetCurrentProcess(), src, GetCurrentProcess(), &h,
                0, FALSE, DUPLICATE_SAME_ACCESS)) {
            fwprintf(stderr, L"ksh: cannot duplicate descriptor %d\n", dupFrom);
            return false;
        }
        break;
    }
    default:
        return true;
    }
    return InstallFdHandle(shell, n, h, persistent);
}

/* persistent: exec with no command. Applies the legacy in/out/err fields too and closes
   replaced descriptors. Otherwise only fds[] applies, and the caller restores afterwards. */
bool ApplyFdRedirects(IShell* shell, CommandAST* cmd, bool persistent) {
    /* fds[] runs first so that "exec 3>&1 >file" saves the original stdout in fd 3. */
    for (int n = 0; n < KSH_FD_COUNT; n++) {
        const FdRedirect* r = &cmd->fds[n];
        if (r->kind == FD_REDIRECT_NONE) continue;
        if (!ApplyFdRedirect(shell, n, r->kind, r->path, r->dupFrom, persistent))
            return false;
    }
    if (persistent) {
        if (cmd->in_redirect && !ApplyFdRedirect(shell, 0, FD_REDIRECT_READ, cmd->in_redirect, -1, true))
            return false;
        if (cmd->out_redirect && !ApplyFdRedirect(shell, 1,
                cmd->append_out ? FD_REDIRECT_APPEND : FD_REDIRECT_WRITE, cmd->out_redirect, -1, true))
            return false;
        if (cmd->err_redirect && !ApplyFdRedirect(shell, 2,
                cmd->append_err ? FD_REDIRECT_APPEND : FD_REDIRECT_WRITE, cmd->err_redirect, -1, true))
            return false;
        if (cmd->err_to_out && !ApplyFdRedirect(shell, 2, FD_REDIRECT_DUP, NULL, 1, true))
            return false;
        if (cmd->out_to_err && !ApplyFdRedirect(shell, 1, FD_REDIRECT_DUP, NULL, 2, true))
            return false;
    }
    return true;
}

typedef struct FdSnapshot {
    int crt[3];
    HANDLE std[3];
    int mode[3];
    HANDLE table[KSH_FD_COUNT];
} FdSnapshot;

static void SnapshotFds(IShell* shell, FdSnapshot* s) {
    fflush(stdout);
    fflush(stderr);
    for (int i = 0; i < 3; i++) {
        s->crt[i] = _dup(i);
        s->std[i] = GetStdHandle(StdHandleId(i));
        s->mode[i] = _setmode(i, _O_U8TEXT);
        if (s->mode[i] >= 0) _setmode(i, s->mode[i]);
    }
    memcpy(s->table, shell->fdTable, sizeof(s->table));
}

static void RestoreFds(IShell* shell, FdSnapshot* s) {
    fflush(stdout);
    fflush(stderr);
    for (int i = 0; i < 3; i++) {
        if (s->crt[i] < 0) continue;
        _dup2(s->crt[i], i);
        _close(s->crt[i]);
        SetStdHandle(StdHandleId(i), s->std[i]);
        if (s->mode[i] >= 0) _setmode(i, s->mode[i]);
    }
    for (int n = 3; n < KSH_FD_COUNT; n++) {
        if (shell->fdTable[n] == s->table[n]) continue;
        if (shell->fdTable[n]) CloseHandle(shell->fdTable[n]);
        shell->fdTable[n] = s->table[n];
    }
}

static bool HasFdRedirects(const CommandAST* cmd) {
    for (int n = 0; n < KSH_FD_COUNT; n++) {
        if (cmd->fds[n].kind != FD_REDIRECT_NONE) return true;
    }
    return false;
}

int ExecuteSingleCommand(IShell* shell, CommandAST* cmd, HANDLE hIn, HANDLE hOut) {
    if (!cmd) return 0;
    if (cmd->argc > 0 && wcscmp(cmd->argv[0], L"exec") == 0 &&
        !Shell_HasFunction(shell, L"exec")) {
        int code = 0;
        Builtin_Exec(shell, cmd, &code);
        return code;
    }
    if (!HasFdRedirects(cmd)) return ExecuteSingleCommandCore(shell, cmd, hIn, hOut);

    FdSnapshot snapshot;
    SnapshotFds(shell, &snapshot);
    int code = 1;
    if (ApplyFdRedirects(shell, cmd, false)) {
        code = ExecuteSingleCommandCore(shell, cmd, hIn, hOut);
    }
    RestoreFds(shell, &snapshot);
    return code;
}

static int Executor_Execute(IExecutor* self, CommandAST* ast) {
    if (!ast) return 0;
    if (self->shell && self->shell->noexec && !self->shell->interactive) return 0;

    if (!ast->next_in_pipeline) {
        return ExecuteSingleCommand(self->shell, ast, NULL, NULL);
    }

    /* Pipeline Execution */
    CommandAST* curr = ast;
    HANDLE hPrevRead = NULL;
    int last_exit_code = 0;
    int rightmost_failed = 0;

    while (curr) {
        HANDLE hPipeRead = NULL;
        HANDLE hPipeWrite = NULL;

        if (curr->next_in_pipeline) {
            SECURITY_ATTRIBUTES sa;
            sa.nLength = sizeof(SECURITY_ATTRIBUTES);
            sa.bInheritHandle = TRUE;
            sa.lpSecurityDescriptor = NULL;

            if (!CreatePipe(&hPipeRead, &hPipeWrite, &sa, 1048576)) {
                wprintf(L"ksh: fatal pipe creation error\n");
                return 1;
            }
            SetHandleInformation(hPipeRead, HANDLE_FLAG_INHERIT, 0);
        }

        last_exit_code = ExecuteSingleCommand(self->shell, curr,
                                              hPrevRead,
                                              curr->next_in_pipeline ? hPipeWrite : NULL);
        if (last_exit_code != 0) rightmost_failed = last_exit_code;

        if (hPrevRead) CloseHandle(hPrevRead);
        if (hPipeWrite) CloseHandle(hPipeWrite);

        hPrevRead = hPipeRead;
        curr = curr->next_in_pipeline;
    }

    if (self->shell && self->shell->pipefail && rightmost_failed != 0) {
        return rightmost_failed;
    }
    return last_exit_code;
}

static void Executor_Destroy(IExecutor* self) {
    free(self);
}

IExecutor* CreateExecutor(IShell* shell) {
    IExecutor* exec = (IExecutor*)malloc(sizeof(IExecutor));
    if (!exec) return NULL;
    exec->shell = shell;
    exec->Execute = Executor_Execute;
    exec->Destroy = Executor_Destroy;
    return exec;
}

/* ========================================================================= */
/*                          OOP Interface: IShell                            */
/* ========================================================================= */

