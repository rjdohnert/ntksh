/**
 * sample_module.c - Example native C extension module for ntksh
 * Demonstrates the ntksh Plugin C ABI (KshPluginApi).
 *
 * Compile:
 *   clang -shared -O2 -Iinclude examples/sample_module/sample_module.c -o sample_module.dll
 *
 * Load in ntksh:
 *   module load ./sample_module.dll
 *   sample_hello World
 *   sample_add 10 25
 *   module unload sample_module
 *
 * Or using ksh93 builtin syntax:
 *   builtin -f ./sample_module.dll sample_hello
 *   builtin -d sample_hello
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ksh/module.h>
#include <ksh/ast.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static const KshPluginApi* g_api = NULL;

/* Command 1: sample_hello [name] */
static bool Builtin_SampleHello(CommandAST* cmd, int* exit_code) {
    const wchar_t* name = (cmd->argc > 1) ? cmd->argv[1] : L"World";
    g_api->Print(L"Hello from sample_module extension, %ls!\n", name);

    /* Also demonstrate querying and setting shell variables */
    const wchar_t* ver = g_api->GetVariable(L"KSH_VERSION");
    if (ver) {
        g_api->Print(L"Running under ntksh version: %ls\n", ver);
    }
    g_api->SetVariable(L"SAMPLE_MODULE_STATUS", L"active");

    *exit_code = 0;
    return true;
}

/* Command 2: sample_add <a> <b> */
static bool Builtin_SampleAdd(CommandAST* cmd, int* exit_code) {
    if (cmd->argc < 3) {
        g_api->PrintError(L"sample_add: usage: sample_add <number1> <number2>\n");
        *exit_code = 1;
        return true;
    }

    long long a = _wcstoi64(cmd->argv[1], NULL, 10);
    long long b = _wcstoi64(cmd->argv[2], NULL, 10);
    long long sum = a + b;

    g_api->Print(L"%lld\n", sum);

    wchar_t sumStr[32];
    swprintf_s(sumStr, _countof(sumStr), L"%lld", sum);
    g_api->SetVariable(L"SAMPLE_SUM", sumStr);

    *exit_code = 0;
    return true;
}

/* Classic standalone ksh93 entry points */
__declspec(dllexport) bool b_sample_classic(CommandAST* cmd, int* exit_code) {
    (void)cmd;
    wprintf(L"sample_classic: executed via classic ksh93 DLL entry point!\n");
    *exit_code = 0;
    return true;
}

__declspec(dllexport) bool ksh_builtin_sample_classic(CommandAST* cmd, int* exit_code) {
    return b_sample_classic(cmd, exit_code);
}

/* Module initialization entry point */
__declspec(dllexport) int ksh_module_init(const KshPluginApi* api) {
    if (!api || api->api_version != KSH_MODULE_API_VERSION) {
        return KSH_MODULE_ERR_VERSION;
    }
    g_api = api;

    /* Register commands */
    g_api->RegisterBuiltin(L"sample_hello", Builtin_SampleHello, L"sample_hello [name] - Sample module greeting");
    g_api->RegisterBuiltin(L"sample_add", Builtin_SampleAdd, L"sample_add <a> <b> - Add two integers");

    return KSH_MODULE_SUCCESS;
}

/* Module cleanup entry point */
__declspec(dllexport) void ksh_module_fini(void) {
    /* Optional cleanup when module is unloaded */
    g_api = NULL;
}
