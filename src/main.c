#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

static void Shell_SelfTestCheck(const wchar_t* name, int actual, int expected,
    int* failures) {
    if (actual == expected) {
        wprintf(L"PASS %ls\n", name);
    } else {
        wprintf(L"FAIL %ls (expected %d, got %d)\n", name, expected, actual);
        (*failures)++;
    }
}

int Shell_RunSelfTests(IShell* shell) {
    int failures = 0;
    Shell_SelfTestCheck(L"true status",
        shell->ExecuteLine(shell, L"true"), 0, &failures);
    Shell_SelfTestCheck(L"false status",
        shell->ExecuteLine(shell, L"false"), 1, &failures);
    Shell_SelfTestCheck(L"set errexit option toggles",
        shell->ExecuteLine(shell, L"set -o errexit") == 0 &&
            shell->errexit &&
            shell->ExecuteLine(shell, L"set +o errexit") == 0 &&
            !shell->errexit ? 0 : 1,
        0, &failures);
    Shell_SelfTestCheck(L"set nounset option toggles",
        shell->ExecuteLine(shell, L"set -u") == 0 &&
            shell->env->nounset &&
            shell->ExecuteLine(shell, L"set +u") == 0 &&
            !shell->env->nounset ? 0 : 1,
        0, &failures);
    Shell_SelfTestCheck(L"alias expansion and removal",
        shell->ExecuteLine(shell,
            L"alias ksh_self_test_alias=true; ksh_self_test_alias") == 0 &&
            shell->ExecuteLine(shell, L"unalias ksh_self_test_alias") == 0 ?
            0 : 1,
        0, &failures);
    Shell_SelfTestCheck(L"test integer comparison",
        shell->ExecuteLine(shell, L"test 8 -gt 3"), 0, &failures);
    Shell_SelfTestCheck(L"and-or short circuit",
        shell->ExecuteLine(shell, L"false && test 1 = 1 || true"), 0,
        &failures);
    Shell_SelfTestCheck(L"positional parameters and shift",
        shell->ExecuteLine(shell,
            L"set -- first second; shift; test \"$1\" = second"), 0,
        &failures);
    Shell_SelfTestCheck(L"integer variable attributes",
        shell->ExecuteLine(shell,
            L"typeset -i KSH_SELF_TEST_INTEGER=16#ff; test \"$KSH_SELF_TEST_INTEGER\" -eq 255"),
        0, &failures);
    Shell_SelfTestCheck(L"indexed arrays",
        shell->ExecuteLine(shell,
            L"typeset -a KSH_SELF_TEST_ARRAY; set -A KSH_SELF_TEST_ARRAY alpha beta; test \"${KSH_SELF_TEST_ARRAY[1]}\" = beta"),
        0, &failures);
    Shell_SelfTestCheck(L"associative arrays",
        shell->ExecuteLine(shell,
            L"typeset -A KSH_SELF_TEST_MAP; set -A KSH_SELF_TEST_MAP key=value; test \"${KSH_SELF_TEST_MAP[key]}\" = value"),
        0, &failures);
    Shell_SelfTestCheck(L"function return status",
        Shell_SetFunction(shell, L"ksh_self_test_function", L"return 7") ?
            shell->ExecuteLine(shell, L"ksh_self_test_function") : 1,
        7, &failures);
    Shell_SelfTestCheck(L"local function scope",
        shell->ExecuteLine(shell,
            L"typeset KSH_SELF_TEST_SCOPE=outer") == 0 &&
            Shell_SetFunction(shell, L"ksh_self_test_local",
                L"local KSH_SELF_TEST_SCOPE=inner\ntest \"$KSH_SELF_TEST_SCOPE\" = inner") &&
            shell->ExecuteLine(shell, L"ksh_self_test_local") == 0 ?
                shell->ExecuteLine(shell,
                    L"test \"$KSH_SELF_TEST_SCOPE\" = outer") : 1,
        0, &failures);
    wchar_t temporaryDirectory[MAX_PATH];
    wchar_t temporaryScript[MAX_PATH];
    FILE* batchFile = NULL;
    bool temporaryDirectoryReady = GetTempPathW(_countof(temporaryDirectory),
        temporaryDirectory) != 0 &&
        wcslen(temporaryDirectory) < _countof(temporaryDirectory);
    bool temporaryScriptCreated = temporaryDirectoryReady &&
        GetTempFileNameW(temporaryDirectory, L"ksh", 0,
            temporaryScript) != 0;
    bool batchFileReady = temporaryScriptCreated &&
        _wfopen_s(&batchFile, temporaryScript, L"w, ccs=UTF-8") == 0 &&
        batchFile != NULL;
    if (batchFileReady) {
        batchFileReady = fputws(L"@echo off\n"
                L"set KSH_SELF_TEST_BATCH=outer\n"
                L"setlocal\n"
                L"set KSH_SELF_TEST_BATCH=inner\n"
                L"endlocal\n"
                L"if \"%KSH_SELF_TEST_BATCH%\"==\"outer\" set KSH_SELF_TEST_BATCH=restored\n"
                L"for /L %%N in (1,1,3) do set KSH_SELF_TEST_BATCH=%%N\n"
                L"if \"%KSH_SELF_TEST_BATCH%\"==\"3\" call :nested\n"
                L"if \"%KSH_SELF_TEST_BATCH%\"==\"called\" set KSH_SELF_TEST_BATCH=passed\n"
                L"goto :eof\n"
                L":nested\n"
                L"set KSH_SELF_TEST_BATCH=called\n"
                L"exit /b\n",
                batchFile) >= 0 && !ferror(batchFile);
        if (fclose(batchFile) != 0) batchFileReady = false;
        batchFile = NULL;
    }
    int batchStatus = batchFileReady ?
        Shell_RunBatch(shell, temporaryScript, 0, NULL) : 1;
    if (temporaryScriptCreated) (void)DeleteFileW(temporaryScript);
    const wchar_t* batchResult = shell->env->Get(shell->env,
        L"KSH_SELF_TEST_BATCH");
    if (batchStatus != 0 || !batchResult ||
        wcscmp(batchResult, L"passed") != 0)
        batchStatus = 1;
    Shell_SelfTestCheck(L"native batch scope restoration, FOR /L, and nested CALL",
        batchStatus, 0, &failures);
    (void)shell->env->Unset(shell->env, L"KSH_SELF_TEST_BATCH");

    wchar_t redirectedOutput[MAX_PATH];
    bool redirectedPathReady = temporaryDirectoryReady &&
        GetTempFileNameW(temporaryDirectory, L"ksh", 0,
            redirectedOutput) != 0;
    if (redirectedPathReady) (void)DeleteFileW(redirectedOutput);
    wchar_t redirectCommand[MAX_LINE_LEN];
    int redirectLength = redirectedPathReady ?
        swprintf_s(redirectCommand, _countof(redirectCommand),
            L"echo self-test > \"%ls\"", redirectedOutput) : -1;
    int redirectStatus = redirectLength >= 0 ?
        shell->ExecuteLine(shell, redirectCommand) : 1;
    FILE* redirectedFile = NULL;
    wchar_t redirectedLine[MAX_LINE_LEN];
    if (redirectStatus == 0 &&
        _wfopen_s(&redirectedFile, redirectedOutput, L"r, ccs=UTF-8") == 0 &&
        redirectedFile) {
        if (!fgetws(redirectedLine, _countof(redirectedLine), redirectedFile) ||
            wcscmp(redirectedLine, L"self-test\n") != 0 ||
            fgetws(redirectedLine, _countof(redirectedLine), redirectedFile))
            redirectStatus = 1;
        if (fclose(redirectedFile) != 0) redirectStatus = 1;
    } else {
        redirectStatus = 1;
    }
    if (redirectedPathReady) (void)DeleteFileW(redirectedOutput);
    Shell_SelfTestCheck(L"output redirection creates the expected file",
        redirectStatus, 0, &failures);
    if (failures) {
        wprintf(L"Self-test: %d failure(s)\n", failures);
        return 1;
    }
    wprintf(L"Self-test: all passed\n");
    return 0;
}

/* ========================================================================= */
/*                               Main Entry                                  */
/* ========================================================================= */

/* Keep CRT *_s overflow checks from terminating the whole shell; the call fails instead. */
static void Ksh_InvalidParameter(const wchar_t* expr, const wchar_t* func, const wchar_t* file,
    unsigned int line, uintptr_t reserved) {
    (void)expr; (void)func; (void)file; (void)line; (void)reserved;
}

int wmain(int argc, wchar_t* argv[]) {
    _set_invalid_parameter_handler(Ksh_InvalidParameter);
    /* CreateProcessW must not resolve bare command names from the current directory. */
    SetEnvironmentVariableW(L"NoDefaultCurrentDirectoryInExePath", L"1");
    Ksh_SweepStaleTempFiles();
    (void)_setmode(_fileno(stdin), _O_U8TEXT);
    (void)_setmode(_fileno(stdout), _O_U8TEXT);
    IShell* shell = CreateShell();
    if (!shell) {
        fwprintf(stderr, L"ksh: failed to initialize shell engine.\n");
        return 1;
    }
    Shell_InstallSignalHandler();
    Shell_LoadKshIni(shell);

    if (argc == 2 && wcscmp(argv[1], L"--self-test") == 0) {
        int selfTestStatus = Shell_RunSelfTests(shell);
        shell->Destroy(shell);
        return selfTestStatus;
    }

    /* Internal: child shell for batch pipe stages and FOR /F commands. */
    if (argc >= 4 && wcscmp(argv[1], L"--batch-stage") == 0) {
        int stageStatus = Shell_RunBatchStage(shell, argv[2], argv[3],
            argc - 4, &argv[4]);
        fflush(stdout);
        shell->Destroy(shell);
        return stageStatus;
    }

    if (argc >= 2 && (wcscmp(argv[1], L"--version") == 0 || wcscmp(argv[1], L"-V") == 0 || wcscmp(argv[1], L"-v") == 0)) {
        wprintf(L"%ls\n", NTKSH_VERSION);
        shell->Destroy(shell);
        return 0;
    }

    int exit_status = 0;
    const wchar_t* positionalZero = argc > 0 ? argv[0] : L"ksh";
    int positionalCount = 0;
    wchar_t** positionalArguments = NULL;

    if (argc > 1 && wcscmp(argv[1], L"-c") == 0) {
        if (argc > 3) {
            positionalZero = argv[3];
            positionalCount = argc - 4;
            positionalArguments = &argv[4];
        }
    } else if (argc > 1 && wcscmp(argv[1], L"--help") != 0 &&
        wcscmp(argv[1], L"-h") != 0 &&
        wcscmp(argv[1], L"--version") != 0 &&
        wcscmp(argv[1], L"-V") != 0 &&
        wcscmp(argv[1], L"-v") != 0) {
        positionalZero = argv[1];
        positionalCount = argc - 2;
        positionalArguments = &argv[2];
    }
    if (!Env_SetPositional(shell->env, positionalZero, positionalCount,
            positionalArguments)) {
        fwprintf(stderr, L"ksh: failed to initialize positional parameters\n");
        shell->Destroy(shell);
        return 1;
    }

    if (argc > 1) {
        if (wcscmp(argv[1], L"-c") == 0 && argc > 2) {
            /* Run single command string */
            shell->env->Set(shell->env, L"LINENO", L"1");
            exit_status = shell->ExecuteLine(shell, argv[2]);
        } else if (wcscmp(argv[1], L"--version") == 0 || wcscmp(argv[1], L"-V") == 0 || wcscmp(argv[1], L"-v") == 0) {
            wprintf(L"%ls\n", NTKSH_VERSION);
            exit_status = 0;
        } else if (wcscmp(argv[1], L"--help") == 0 || wcscmp(argv[1], L"-h") == 0) {
            DisplayComprehensiveHelp(NULL);
            exit_status = 0;
        } else {
            /* Execute script file */
            exit_status = shell->RunScript(shell, argv[1]);
        }
    } else {
        /* Interactive REPL */
        shell->RunREPL(shell);
        exit_status = shell->last_status;
    }

    shell->Destroy(shell);
    return exit_status;
}