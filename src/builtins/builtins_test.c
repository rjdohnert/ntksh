#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

typedef struct TestParser {
    CommandAST* command;
    int argc;
    int position;
    bool invalid;
} TestParser;

static bool TestIsUnaryOperator(const wchar_t* op) {
    static const wchar_t* const operators[] = {
        L"-b", L"-c", L"-d", L"-e", L"-f", L"-g", L"-h", L"-L",
        L"-n", L"-p", L"-r", L"-s", L"-S", L"-t", L"-u", L"-w",
        L"-x", L"-z"
    };
    for (size_t i = 0; i < sizeof(operators) / sizeof(operators[0]); ++i)
        if (wcscmp(op, operators[i]) == 0) return true;
    return false;
}

static bool TestIsBinaryOperator(const wchar_t* op) {
    static const wchar_t* const operators[] = {
        L"=", L"==", L"!=", L"<", L">", L"=~", L"-eq", L"-ne", L"-gt",
        L"-ge", L"-lt", L"-le"
    };
    for (size_t i = 0; i < sizeof(operators) / sizeof(operators[0]); ++i)
        if (wcscmp(op, operators[i]) == 0) return true;
    return false;
}

bool TestParseInteger(const wchar_t* text, long long* result) {
    wchar_t* end;
    errno = 0;
    long long value = wcstoll(text, &end, 10);
    if (text == end || *end || errno == ERANGE) return false;
    *result = value;
    return true;
}

static bool TestExecutablePath(const wchar_t* path) {
    static const wchar_t* const extensions[] = {
        L".exe", L".com", L".bat", L".cmd"
    };
    const wchar_t* extension = wcsrchr(path, L'.');
    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) || !extension)
        return false;
    for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i)
        if (_wcsicmp(extension, extensions[i]) == 0) return true;
    return false;
}

static DWORD TestWindowsFileType(const wchar_t* path) {
    HANDLE handle = CreateFileW(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (handle == INVALID_HANDLE_VALUE) return FILE_TYPE_UNKNOWN;
    DWORD type = GetFileType(handle);
    CloseHandle(handle);
    return type;
}

static bool TestUnary(const wchar_t* op, const wchar_t* value, bool* valid) {
    DWORD attributes = GetFileAttributesW(value);
    *valid = true;
    if (wcscmp(op, L"-n") == 0) return *value != L'\0';
    if (wcscmp(op, L"-z") == 0) return *value == L'\0';
    if (wcscmp(op, L"-t") == 0) {
        long descriptor = 1;
        if (*value) {
            wchar_t* end;
            errno = 0;
            descriptor = wcstol(value, &end, 10);
            if (errno == ERANGE || *end || end == value ||
                descriptor < 0 || descriptor > 2) {
                *valid = false;
                return false;
            }
        }
        DWORD mode;
        HANDLE handle = GetStdHandle(descriptor == 0 ? STD_INPUT_HANDLE :
            descriptor == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
        return handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode);
    }
    if (wcscmp(op, L"-e") == 0)
        return attributes != INVALID_FILE_ATTRIBUTES;
    if (wcscmp(op, L"-f") == 0)
        return attributes != INVALID_FILE_ATTRIBUTES &&
            !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    if (wcscmp(op, L"-d") == 0)
        return attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY);
    if (wcscmp(op, L"-h") == 0 || wcscmp(op, L"-L") == 0)
        return attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
    if (wcscmp(op, L"-r") == 0) return _waccess(value, 4) == 0;
    if (wcscmp(op, L"-w") == 0) return _waccess(value, 2) == 0;
    if (wcscmp(op, L"-x") == 0) return TestExecutablePath(value);
    if (wcscmp(op, L"-s") == 0) {
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (!GetFileAttributesExW(value, GetFileExInfoStandard, &data))
            return false;
        return data.nFileSizeHigh != 0 || data.nFileSizeLow != 0;
    }
    if (wcscmp(op, L"-c") == 0)
        return TestWindowsFileType(value) == FILE_TYPE_CHAR;
    if (wcscmp(op, L"-p") == 0)
        return TestWindowsFileType(value) == FILE_TYPE_PIPE;
    if (wcscmp(op, L"-b") == 0 || wcscmp(op, L"-g") == 0 ||
        wcscmp(op, L"-S") == 0 || wcscmp(op, L"-u") == 0)
        return false;
    *valid = false;
    return false;
}

static bool TestParseOr(TestParser* parser);

static bool TestParsePrimary(TestParser* parser) {
    if (parser->position >= parser->argc) {
        parser->invalid = true;
        return false;
    }
    const wchar_t* token = parser->command->argv[parser->position];
    if (wcscmp(token, L"(") == 0) {
        parser->position++;
        bool value = TestParseOr(parser);
        if (parser->position >= parser->argc ||
            wcscmp(parser->command->argv[parser->position], L")") != 0) {
            parser->invalid = true;
            return false;
        }
        parser->position++;
        return value;
    }

    if (wcscmp(token, L")") == 0) {
        parser->invalid = true;
        return false;
    }
    if (TestIsUnaryOperator(token)) {
        parser->position++;
        const wchar_t* value = L"";
        if (wcscmp(token, L"-t") == 0 &&
            (parser->position >= parser->argc ||
                TestIsBinaryOperator(
                    parser->command->argv[parser->position]) ||
                wcscmp(parser->command->argv[parser->position], L"-a") == 0 ||
                wcscmp(parser->command->argv[parser->position], L"-o") == 0 ||
                wcscmp(parser->command->argv[parser->position], L")") == 0)) {
            value = L"";
        } else if (parser->position < parser->argc) {
            value = parser->command->argv[parser->position++];
        } else {
            parser->invalid = true;
            return false;
        }
        bool valid;
        bool result = TestUnary(token, value, &valid);
        if (!valid) parser->invalid = true;
        return result;
    }

    const wchar_t* left = token;
    parser->position++;
    if (parser->position < parser->argc &&
        TestIsBinaryOperator(parser->command->argv[parser->position])) {
        const wchar_t* op = parser->command->argv[parser->position++];
        if (parser->position >= parser->argc ||
            wcscmp(parser->command->argv[parser->position], L")") == 0) {
            parser->invalid = true;
            return false;
        }
        const wchar_t* right = parser->command->argv[parser->position++];
        if (wcscmp(op, L"=~") == 0)
            return GrepRegexMatch(right, left, false, false, false, NULL, NULL);
        if (wcscmp(op, L"=") == 0 || wcscmp(op, L"==") == 0) {
            return CasePatternMatch(right, left);
        }
        if (wcscmp(op, L"!=") == 0) return !CasePatternMatch(right, left);
        if (wcscmp(op, L"<") == 0) return wcscmp(left, right) < 0;
        if (wcscmp(op, L">") == 0) return wcscmp(left, right) > 0;
        long long lhs, rhs;
        if (!TestParseInteger(left, &lhs) ||
            !TestParseInteger(right, &rhs)) {
            parser->invalid = true;
            return false;
        }
        if (wcscmp(op, L"-eq") == 0) return lhs == rhs;
        if (wcscmp(op, L"-ne") == 0) return lhs != rhs;
        if (wcscmp(op, L"-gt") == 0) return lhs > rhs;
        if (wcscmp(op, L"-ge") == 0) return lhs >= rhs;
        if (wcscmp(op, L"-lt") == 0) return lhs < rhs;
        return lhs <= rhs;
    }
    return *left != L'\0';
}

static bool TestParseNot(TestParser* parser) {
    if (parser->position < parser->argc &&
        wcscmp(parser->command->argv[parser->position], L"!") == 0) {
        parser->position++;
        return !TestParseNot(parser);
    }
    return TestParsePrimary(parser);
}

static bool TestParseAnd(TestParser* parser) {
    bool result = TestParseNot(parser);
    while (parser->position < parser->argc &&
        (wcscmp(parser->command->argv[parser->position], L"-a") == 0 ||
         wcscmp(parser->command->argv[parser->position], L"&&") == 0)) {
        parser->position++;
        bool right = TestParseNot(parser);
        result = result && right;
    }
    return result;
}

static bool TestParseOr(TestParser* parser) {
    bool result = TestParseAnd(parser);
    while (parser->position < parser->argc &&
        (wcscmp(parser->command->argv[parser->position], L"-o") == 0 ||
         wcscmp(parser->command->argv[parser->position], L"||") == 0)) {
        parser->position++;
        bool right = TestParseAnd(parser);
        result = result || right;
    }
    return result;
}

/* POSIX test / [ / [[ evaluates shell expressions */
bool Builtin_Test(CommandAST* cmd, int* code) {
    TestParser parser = { cmd, cmd->argc, 1, false };
    if (wcscmp(cmd->argv[0], L"[") == 0) {
        if (parser.argc < 2 ||
            wcscmp(cmd->argv[parser.argc - 1], L"]") != 0) {
            fwprintf(stderr, L"ksh: [: missing ']'\n");
            *code = 2;
            return true;
        }
        parser.argc--;
    } else if (wcscmp(cmd->argv[0], L"[[") == 0) {
        if (parser.argc < 2 ||
            wcscmp(cmd->argv[parser.argc - 1], L"]]") != 0) {
            fwprintf(stderr, L"ksh: [[: missing ']]'\n");
            *code = 2;
            return true;
        }
        parser.argc--;
    }
    if (parser.argc == 1) {
        *code = 1;
        return true;
    }
    bool result = TestParseOr(&parser);
    if (parser.position != parser.argc) parser.invalid = true;
    if (parser.invalid) {
        fwprintf(stderr, L"ksh: %ls: invalid expression\n", cmd->argv[0]);
        *code = 2;
    } else {
        *code = result ? 0 : 1;
    }
    return true;
}

bool Builtin_Unset(IShell* shell, CommandAST* cmd, int* code) {
    enum UnsetMode { UNSET_DEFAULT, UNSET_VARIABLES, UNSET_FUNCTIONS };
    enum UnsetMode mode = UNSET_DEFAULT;
    int first = 1;
    while (first < cmd->argc && cmd->argv[first][0] == L'-' && cmd->argv[first][1]) {
        const wchar_t* opt = cmd->argv[first];
        if (wcscmp(opt, L"--") == 0) {
            first++;
            break;
        }
        enum UnsetMode requested;
        if (wcscmp(opt, L"-v") == 0) {
            requested = UNSET_VARIABLES;
        } else if (wcscmp(opt, L"-f") == 0) {
            requested = UNSET_FUNCTIONS;
        } else {
            fwprintf(stderr, L"ksh: unset: %ls: invalid option\n", opt);
            *code = 2;
            return true;
        }
        if (mode != UNSET_DEFAULT && mode != requested) {
            fwprintf(stderr, L"ksh: unset: -f and -v are mutually exclusive\n");
            *code = 2;
            return true;
        }
        mode = requested;
        first++;
    }
    if (first >= cmd->argc) {
        fwprintf(stderr, L"ksh: unset: usage: unset [-f | -v] name ...\n");
        *code = 2;
        return true;
    }
    for (int i = first; i < cmd->argc; ++i) {
        const wchar_t* name = cmd->argv[i];
        if (mode == UNSET_FUNCTIONS) {
            Shell_UnsetFunction(shell, name);
            continue;
        }
        if (!IsValidIdentifier(name)) {
            fwprintf(stderr, L"ksh: unset: '%ls': not a valid identifier\n", name);
            *code = 2;
            return true;
        }
        if (mode == UNSET_DEFAULT && !shell->env->Get(shell->env, name)) {
            Shell_UnsetFunction(shell, name);
            continue;
        }
        if (!shell->env->Unset(shell->env, name)) {
            fwprintf(stderr, L"ksh: unset: cannot unset '%ls'\n", name);
            *code = 1;
            return true;
        }
    }
    *code = 0;
    return true;
}

bool Builtin_Exit(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc > 2) {
        fwprintf(stderr, L"ksh: exit: too many arguments\n");
        *code = 1;
        return true;
    }
    Shell_TerminateActiveJobs();
    if (cmd->argc == 1) {
        *code = shell->last_status & 0xff;
        shell->running = false;
        return true;
    }
    long long value;
    if (!TestParseInteger(cmd->argv[1], &value)) {
        fwprintf(stderr, L"ksh: exit: '%ls': numeric argument required\n",
            cmd->argv[1]);
        *code = 2;
        shell->running = false;
        return true;
    }
    *code = (int)((unsigned long long)value & 0xffu);
    shell->running = false;
    return true;
}

bool Builtin_Pause(CommandAST* cmd, int* code) {
    (void)cmd;
    if (_isatty(_fileno(stdin))) {
        fputws(L"Press any key to continue . . . ", stdout);
        fflush(stdout);
        (void)_getwch();
        putwchar(L'\n');
    } else {
        wint_t ch = fgetwc(stdin);
        if (ch == 0xFEFF) ch = fgetwc(stdin);
        if (ch == WEOF) {
            fwprintf(stderr, L"ksh: pause: input is not available\n");
            *code = 1;
            return true;
        }
    }
    *code = 0;
    return true;
}

typedef struct SuspendContext {
    HANDLE shellThread;
    HANDLE consoleInput;
} SuspendContext;

static DWORD WINAPI ResumeSuspendedShell(LPVOID parameter) {
    SuspendContext* context = (SuspendContext*)parameter;
    if (SuspendThread(context->shellThread) == (DWORD)-1) return 1;
    for (;;) {
        DWORD wait = WaitForSingleObject(context->consoleInput, INFINITE);
        if (wait != WAIT_OBJECT_0) {
            (void)ResumeThread(context->shellThread);
            return 1;
        }
        INPUT_RECORD records[16];
        DWORD count = 0;
        if (!ReadConsoleInputW(context->consoleInput, records,
                (DWORD)(sizeof(records) / sizeof(records[0])), &count)) {
            (void)ResumeThread(context->shellThread);
            return 1;
        }
        for (DWORD i = 0; i < count; ++i) {
            if (records[i].EventType == KEY_EVENT &&
                records[i].Event.KeyEvent.bKeyDown) {
                if (ResumeThread(context->shellThread) == (DWORD)-1)
                    return 1;
                return 0;
            }
        }
    }
}

bool Builtin_Suspend(CommandAST* cmd, int* code) {
    bool force = false;
    for (int i = 1; i < cmd->argc; ++i) {
        if (wcscmp(cmd->argv[i], L"-f") == 0 && !force) {
            force = true;
        } else {
            fwprintf(stderr, L"ksh: suspend: usage: suspend [-f]\n");
            *code = 2;
            return true;
        }
    }
    if (!force) {
        fwprintf(stderr,
            L"ksh: suspend: job control is unavailable; use suspend -f to pause this console shell\n");
        *code = 1;
        return true;
    }

    HANDLE consoleInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD consoleMode;
    if (!consoleInput || consoleInput == INVALID_HANDLE_VALUE ||
        !GetConsoleMode(consoleInput, &consoleMode)) {
        fwprintf(stderr, L"ksh: suspend: requires an interactive console\n");
        *code = 1;
        return true;
    }
    HANDLE shellThread = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
            GetCurrentProcess(), &shellThread, THREAD_SUSPEND_RESUME, FALSE,
            0)) {
        fwprintf(stderr, L"ksh: suspend: cannot access shell thread\n");
        *code = 1;
        return true;
    }
    SuspendContext context = {shellThread, consoleInput};
    HANDLE resumeThread = CreateThread(NULL, 0, ResumeSuspendedShell,
        &context, 0, NULL);
    if (!resumeThread) {
        CloseHandle(shellThread);
        fwprintf(stderr, L"ksh: suspend: cannot create console resume worker\n");
        *code = 1;
        return true;
    }

    fputws(L"Shell suspended. Press any key to resume.\n", stdout);
    fflush(stdout);
    WaitForSingleObject(resumeThread, INFINITE);
    DWORD workerStatus = 1;
    GetExitCodeThread(resumeThread, &workerStatus);
    if (workerStatus != 0)
        fwprintf(stderr, L"ksh: suspend: resume worker failed\n");
    *code = workerStatus == 0 ? 0 : 1;
    CloseHandle(resumeThread);
    CloseHandle(shellThread);
    return true;
}

/* ========================================================================= */
/*               Native Batch & FreeBSD Compatibility Builtins               */
/* ========================================================================= */

