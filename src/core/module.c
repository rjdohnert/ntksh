/**
 * ntksh - Native Module and Extension System
 * Copyright (C) 2023-2026 PC/OpenSystems LLC. All rights reserved.
 * Licensed under the terms of the BSD-3 Clause License.
 *
 * Implements:
 * 1. Native C/C++ dynamic library plugin loading (.dll) via Win32 LoadLibraryW.
 * 2. Shell script package importing (.ksh / .kshm) from KSH_MODULE_PATH.
 * 3. Modern 'module load / unload / import / list' command interface.
 * 4. Classic AT&T ksh93 'builtin -f <file.dll> [cmd]' and 'builtin -d <cmd>' interface.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdbool.h>

#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/module.h>

#define MAX_DYNAMIC_BUILTINS 256
#define MAX_LOADED_MODULES   128

typedef struct DynamicBuiltinEntry {
    wchar_t name[64];
    KshBuiltinFn fn;
    wchar_t help[256];
    HMODULE ownerModule;
} DynamicBuiltinEntry;

typedef struct LoadedNativeModule {
    wchar_t name[128];
    wchar_t path[MAX_PATH];
    HMODULE hModule;
    KshModuleFiniFn finiFn;
} LoadedNativeModule;

typedef struct LoadedScriptModule {
    wchar_t name[128];
    wchar_t path[MAX_PATH];
} LoadedScriptModule;

static IShell* s_shell = NULL;
static DynamicBuiltinEntry s_dynamicBuiltins[MAX_DYNAMIC_BUILTINS];
static size_t s_dynamicBuiltinCount = 0;

static LoadedNativeModule s_nativeModules[MAX_LOADED_MODULES];
static size_t s_nativeModuleCount = 0;

static LoadedScriptModule s_scriptModules[MAX_LOADED_MODULES];
static size_t s_scriptModuleCount = 0;

static HMODULE s_currentlyLoadingDll = NULL;

/* ------------------------------------------------------------------------- */
/* KshPluginApi Implementation (vtable exposed to native DLL modules)        */
/* ------------------------------------------------------------------------- */

static bool Plugin_RegisterBuiltin(const wchar_t* name, KshBuiltinFn fn, const wchar_t* help_summary) {
    if (!name || !name[0] || !fn) return false;
    if (s_dynamicBuiltinCount >= MAX_DYNAMIC_BUILTINS) return false;

    /* Check if already registered */
    for (size_t i = 0; i < s_dynamicBuiltinCount; i++) {
        if (_wcsicmp(s_dynamicBuiltins[i].name, name) == 0) {
            /* Update existing */
            s_dynamicBuiltins[i].fn = fn;
            if (help_summary) {
                wcscpy_s(s_dynamicBuiltins[i].help, _countof(s_dynamicBuiltins[i].help), help_summary);
            }
            s_dynamicBuiltins[i].ownerModule = s_currentlyLoadingDll;
            return true;
        }
    }

    DynamicBuiltinEntry* entry = &s_dynamicBuiltins[s_dynamicBuiltinCount++];
    wcscpy_s(entry->name, _countof(entry->name), name);
    entry->fn = fn;
    if (help_summary) {
        wcscpy_s(entry->help, _countof(entry->help), help_summary);
    } else {
        entry->help[0] = L'\0';
    }
    entry->ownerModule = s_currentlyLoadingDll;
    return true;
}

static bool Plugin_UnregisterBuiltin(const wchar_t* name) {
    if (!name || !name[0]) return false;
    for (size_t i = 0; i < s_dynamicBuiltinCount; i++) {
        if (_wcsicmp(s_dynamicBuiltins[i].name, name) == 0) {
            for (size_t k = i; k + 1 < s_dynamicBuiltinCount; k++) {
                s_dynamicBuiltins[k] = s_dynamicBuiltins[k + 1];
            }
            s_dynamicBuiltinCount--;
            return true;
        }
    }
    return false;
}

static const wchar_t* Plugin_GetVariable(const wchar_t* name) {
    if (!name || !s_shell || !s_shell->env) return NULL;
    return s_shell->env->Get(s_shell->env, name);
}

static bool Plugin_SetVariable(const wchar_t* name, const wchar_t* value) {
    if (!name || !s_shell || !s_shell->env) return false;
    return s_shell->env->Set(s_shell->env, name, value ? value : L"");
}

static int Plugin_Eval(const wchar_t* cmdline) {
    if (!cmdline || !s_shell || !s_shell->ExecuteLine) return -1;
    return s_shell->ExecuteLine(s_shell, cmdline);
}

static void Plugin_Print(const wchar_t* fmt, ...) {
    if (!fmt) return;
    va_list ap;
    va_start(ap, fmt);
    vwprintf(fmt, ap);
    va_end(ap);
}

static void Plugin_PrintError(const wchar_t* fmt, ...) {
    if (!fmt) return;
    va_list ap;
    va_start(ap, fmt);
    vfwprintf(stderr, fmt, ap);
    va_end(ap);
}

static const KshPluginApi s_pluginApi = {
    KSH_MODULE_API_VERSION,
    Plugin_RegisterBuiltin,
    Plugin_UnregisterBuiltin,
    Plugin_GetVariable,
    Plugin_SetVariable,
    Plugin_Eval,
    Plugin_Print,
    Plugin_PrintError
};

/* ------------------------------------------------------------------------- */
/* Module Lifecycle Functions                                                */
/* ------------------------------------------------------------------------- */

bool Module_Init(IShell* shell) {
    s_shell = shell;
    return true;
}

void Module_Cleanup(void) {
    /* Invoke cleanup on all loaded native modules and free libraries */
    for (size_t i = 0; i < s_nativeModuleCount; i++) {
        if (s_nativeModules[i].finiFn) {
            s_nativeModules[i].finiFn();
        }
        if (s_nativeModules[i].hModule) {
            FreeLibrary(s_nativeModules[i].hModule);
        }
    }
    s_nativeModuleCount = 0;
    s_dynamicBuiltinCount = 0;
    s_scriptModuleCount = 0;
}

bool Module_IsDynamicBuiltin(const wchar_t* name) {
    if (!name) return false;
    for (size_t i = 0; i < s_dynamicBuiltinCount; i++) {
        if (_wcsicmp(s_dynamicBuiltins[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

bool Module_ExecDynamicBuiltin(CommandAST* cmd, int* code) {
    if (!cmd || cmd->argc < 1) return false;
    for (size_t i = 0; i < s_dynamicBuiltinCount; i++) {
        if (_wcsicmp(s_dynamicBuiltins[i].name, cmd->argv[0]) == 0) {
            return s_dynamicBuiltins[i].fn(cmd, code);
        }
    }
    return false;
}

size_t Module_GetDynamicBuiltinNames(const wchar_t* names[], size_t max_count) {
    size_t count = (s_dynamicBuiltinCount < max_count) ? s_dynamicBuiltinCount : max_count;
    for (size_t i = 0; i < count; i++) {
        names[i] = s_dynamicBuiltins[i].name;
    }
    return count;
}

/* ------------------------------------------------------------------------- */
/* Native Module Loading & Unloading                                         */
/* ------------------------------------------------------------------------- */

bool Module_LoadNative(const wchar_t* path, const wchar_t* alias_cmd, wchar_t* err_buf, size_t err_cap) {
    if (!path || !path[0]) {
        if (err_buf && err_cap) swprintf_s(err_buf, err_cap, L"missing module path");
        return false;
    }

    /* Check if module is already loaded */
    for (size_t i = 0; i < s_nativeModuleCount; i++) {
        if (_wcsicmp(s_nativeModules[i].path, path) == 0 || _wcsicmp(s_nativeModules[i].name, path) == 0) {
            if (err_buf && err_cap) swprintf_s(err_buf, err_cap, L"module already loaded: %ls", path);
            return true; /* idempotent success */
        }
    }

    if (s_nativeModuleCount >= MAX_LOADED_MODULES) {
        if (err_buf && err_cap) swprintf_s(err_buf, err_cap, L"maximum loaded module limit reached");
        return false;
    }

    wchar_t resolvedPath[MAX_PATH];
    DWORD pathLen = GetFullPathNameW(path, _countof(resolvedPath), resolvedPath, NULL);
    if (pathLen == 0 || pathLen >= _countof(resolvedPath)) {
        wcscpy_s(resolvedPath, _countof(resolvedPath), path);
    }

    HMODULE hMod = LoadLibraryW(resolvedPath);
    if (!hMod) {
        /* Try relative to module search path */
        hMod = LoadLibraryW(path);
    }
    if (!hMod) {
        DWORD err = GetLastError();
        if (err_buf && err_cap) {
            swprintf_s(err_buf, err_cap, L"LoadLibrary failed (error %lu): %ls", err, resolvedPath);
        }
        return false;
    }

    s_currentlyLoadingDll = hMod;

    /* Check for ksh_module_init entry point */
    KshModuleInitFn initFn = (KshModuleInitFn)GetProcAddress(hMod, "ksh_module_init");
    KshModuleFiniFn finiFn = (KshModuleFiniFn)GetProcAddress(hMod, "ksh_module_fini");

    bool loadedSuccessfully = false;

    if (initFn) {
        int initRes = initFn(&s_pluginApi);
        if (initRes == KSH_MODULE_SUCCESS) {
            loadedSuccessfully = true;
        } else {
            if (err_buf && err_cap) {
                swprintf_s(err_buf, err_cap, L"module initialization returned error code %d", initRes);
            }
        }
    }

    if (alias_cmd && alias_cmd[0] && !Module_IsDynamicBuiltin(alias_cmd)) {
        /* Classic ksh93 builtin -f fallback: look for b_<cmd>, ksh_builtin_<cmd>, Builtin_<cmd>, or <cmd> */
        char symName[128];
        sprintf_s(symName, sizeof(symName), "b_%ls", alias_cmd);
        KshBuiltinFn bfn = (KshBuiltinFn)GetProcAddress(hMod, symName);
        if (!bfn) {
            sprintf_s(symName, sizeof(symName), "ksh_builtin_%ls", alias_cmd);
            bfn = (KshBuiltinFn)GetProcAddress(hMod, symName);
        }
        if (!bfn) {
            sprintf_s(symName, sizeof(symName), "Builtin_%ls", alias_cmd);
            bfn = (KshBuiltinFn)GetProcAddress(hMod, symName);
        }
        if (!bfn) {
            sprintf_s(symName, sizeof(symName), "%ls", alias_cmd);
            bfn = (KshBuiltinFn)GetProcAddress(hMod, symName);
        }

        if (bfn) {
            Plugin_RegisterBuiltin(alias_cmd, bfn, L"Native ksh93 extension command");
            loadedSuccessfully = true;
        } else if (!initFn) {
            if (err_buf && err_cap) {
                swprintf_s(err_buf, err_cap, L"entry point not found: b_%ls or ksh_builtin_%ls", alias_cmd, alias_cmd);
            }
        }
    } else if (!initFn && !loadedSuccessfully) {
        if (err_buf && err_cap) {
            swprintf_s(err_buf, err_cap, L"module %ls does not export ksh_module_init", path);
        }
    }

    s_currentlyLoadingDll = NULL;

    if (!loadedSuccessfully) {
        FreeLibrary(hMod);
        return false;
    }

    /* Extract module base name */
    const wchar_t* pName = wcsrchr(resolvedPath, L'\\');
    if (!pName) pName = wcsrchr(resolvedPath, L'/');
    pName = pName ? pName + 1 : resolvedPath;

    LoadedNativeModule* entry = &s_nativeModules[s_nativeModuleCount++];
    wcscpy_s(entry->name, _countof(entry->name), pName);
    wcscpy_s(entry->path, _countof(entry->path), resolvedPath);
    entry->hModule = hMod;
    entry->finiFn = finiFn;

    return true;
}

bool Module_UnloadNative(const wchar_t* name_or_path) {
    if (!name_or_path || !name_or_path[0]) return false;

    /* Extract bare filename without path for comparison */
    const wchar_t* base = wcsrchr(name_or_path, L'\\');
    const wchar_t* slash2 = wcsrchr(name_or_path, L'/');
    if (slash2 && (!base || slash2 > base)) base = slash2;
    base = base ? (base + 1) : name_or_path;

    wchar_t withDll[MAX_PATH];
    swprintf_s(withDll, _countof(withDll), L"%ls.dll", base);

    for (size_t i = 0; i < s_nativeModuleCount; i++) {
        if (_wcsicmp(s_nativeModules[i].name, name_or_path) == 0 ||
            _wcsicmp(s_nativeModules[i].name, base) == 0 ||
            _wcsicmp(s_nativeModules[i].name, withDll) == 0 ||
            _wcsicmp(s_nativeModules[i].path, name_or_path) == 0) {

            HMODULE hMod = s_nativeModules[i].hModule;

            /* Call module cleanup */
            if (s_nativeModules[i].finiFn) {
                s_nativeModules[i].finiFn();
            }

            /* Unregister all built-ins owned by this module */
            for (size_t b = 0; b < s_dynamicBuiltinCount; ) {
                if (s_dynamicBuiltins[b].ownerModule == hMod) {
                    for (size_t k = b; k + 1 < s_dynamicBuiltinCount; k++) {
                        s_dynamicBuiltins[k] = s_dynamicBuiltins[k + 1];
                    }
                    s_dynamicBuiltinCount--;
                } else {
                    b++;
                }
            }

            FreeLibrary(hMod);

            for (size_t k = i; k + 1 < s_nativeModuleCount; k++) {
                s_nativeModules[k] = s_nativeModules[k + 1];
            }
            s_nativeModuleCount--;
            return true;
        }
    }

    /* Also check if name_or_path is the name of a dynamic builtin directly */
    return Plugin_UnregisterBuiltin(name_or_path);
}

/* ------------------------------------------------------------------------- */
/* Script Module Importing (KSH_MODULE_PATH)                                 */
/* ------------------------------------------------------------------------- */

static bool FindScriptModule(const wchar_t* modName, wchar_t* outPath, size_t outCap) {
    /* 1. Try directly if path exists */
    if (GetFileAttributesW(modName) != INVALID_FILE_ATTRIBUTES) {
        wcscpy_s(outPath, outCap, modName);
        return true;
    }

    wchar_t withExt[MAX_PATH];
    swprintf_s(withExt, _countof(withExt), L"%ls.ksh", modName);
    if (GetFileAttributesW(withExt) != INVALID_FILE_ATTRIBUTES) {
        wcscpy_s(outPath, outCap, withExt);
        return true;
    }

    swprintf_s(withExt, _countof(withExt), L"%ls.kshm", modName);
    if (GetFileAttributesW(withExt) != INVALID_FILE_ATTRIBUTES) {
        wcscpy_s(outPath, outCap, withExt);
        return true;
    }

    /* 2. Check KSH_MODULE_PATH */
    wchar_t envKshModPath[MAX_PATH * 4] = L"";
    const wchar_t* searchPath = s_shell && s_shell->env ? s_shell->env->Get(s_shell->env, L"KSH_MODULE_PATH") : NULL;
    if (!searchPath) {
        if (GetEnvironmentVariableW(L"KSH_MODULE_PATH", envKshModPath, _countof(envKshModPath)) > 0) {
            searchPath = envKshModPath;
        }
    }

    wchar_t defaultPath[MAX_PATH * 4] = L"";
    if (!searchPath) {
        wchar_t appdata[MAX_PATH] = L"";
        GetEnvironmentVariableW(L"APPDATA", appdata, _countof(appdata));
        swprintf_s(defaultPath, _countof(defaultPath), L".;%ls\\ksh\\modules;C:\\ProgramData\\ksh\\modules", appdata);
        searchPath = defaultPath;
    }

    wchar_t pathCopy[MAX_PATH * 4];
    wcscpy_s(pathCopy, _countof(pathCopy), searchPath);

    wchar_t* context = NULL;
    wchar_t* token = wcstok_s(pathCopy, L";", &context);
    while (token) {
        /* try dir/modName.ksh */
        swprintf_s(outPath, outCap, L"%ls\\%ls.ksh", token, modName);
        if (GetFileAttributesW(outPath) != INVALID_FILE_ATTRIBUTES) return true;

        /* try dir/modName.kshm */
        swprintf_s(outPath, outCap, L"%ls\\%ls.kshm", token, modName);
        if (GetFileAttributesW(outPath) != INVALID_FILE_ATTRIBUTES) return true;

        /* try dir/modName/modName.ksh */
        swprintf_s(outPath, outCap, L"%ls\\%ls\\%ls.ksh", token, modName, modName);
        if (GetFileAttributesW(outPath) != INVALID_FILE_ATTRIBUTES) return true;

        token = wcstok_s(NULL, L";", &context);
    }

    return false;
}

bool Module_ImportScript(IShell* shell, const wchar_t* module_name, wchar_t* err_buf, size_t err_cap) {
    if (!module_name || !module_name[0] || !shell) return false;

    /* Check if already imported */
    for (size_t i = 0; i < s_scriptModuleCount; i++) {
        if (_wcsicmp(s_scriptModules[i].name, module_name) == 0) {
            return true; /* idempotent */
        }
    }

    wchar_t resolvedPath[MAX_PATH];
    if (!FindScriptModule(module_name, resolvedPath, _countof(resolvedPath))) {
        if (err_buf && err_cap) {
            swprintf_s(err_buf, err_cap, L"module not found in KSH_MODULE_PATH: %ls", module_name);
        }
        return false;
    }

    /* Execute the script in current shell context (source equivalent) */
    int rc = shell->RunScript(shell, resolvedPath);
    if (rc != 0) {
        if (err_buf && err_cap) {
            swprintf_s(err_buf, err_cap, L"module script %ls returned status %d", resolvedPath, rc);
        }
        return false;
    }

    if (s_scriptModuleCount < MAX_LOADED_MODULES) {
        LoadedScriptModule* sm = &s_scriptModules[s_scriptModuleCount++];
        wcscpy_s(sm->name, _countof(sm->name), module_name);
        wcscpy_s(sm->path, _countof(sm->path), resolvedPath);
    }

    return true;
}

/* ------------------------------------------------------------------------- */
/* Module List Query Function                                                */
/* ------------------------------------------------------------------------- */

void Module_List(void) {
    wprintf(L"LOADED NATIVE MODULES (%zu):\n", s_nativeModuleCount);
    if (s_nativeModuleCount == 0) {
        wprintf(L"  (none)\n");
    } else {
        for (size_t i = 0; i < s_nativeModuleCount; i++) {
            wprintf(L"  %-20ls %ls\n", s_nativeModules[i].name, s_nativeModules[i].path);
        }
    }

    wprintf(L"\nLOADED SCRIPT MODULES (%zu):\n", s_scriptModuleCount);
    if (s_scriptModuleCount == 0) {
        wprintf(L"  (none)\n");
    } else {
        for (size_t i = 0; i < s_scriptModuleCount; i++) {
            wprintf(L"  %-20ls %ls\n", s_scriptModules[i].name, s_scriptModules[i].path);
        }
    }

    wprintf(L"\nDYNAMICALLY REGISTERED BUILT-INS (%zu):\n", s_dynamicBuiltinCount);
    if (s_dynamicBuiltinCount == 0) {
        wprintf(L"  (none)\n");
    } else {
        for (size_t i = 0; i < s_dynamicBuiltinCount; i++) {
            wprintf(L"  %-16ls %ls\n", s_dynamicBuiltins[i].name,
                    s_dynamicBuiltins[i].help[0] ? s_dynamicBuiltins[i].help : L"(no description)");
        }
    }
}

/* ------------------------------------------------------------------------- */
/* 1. BUILTIN Command: ksh93 Dynamic Builtin Loader & Manager                */
/* ------------------------------------------------------------------------- */

bool Builtin_Builtin(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc < 2 || _wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0) {
        wprintf(L"Usage: builtin [-f lib.dll] [name]\n"
                L"       builtin -d name\n"
                L"       builtin -s\n"
                L"Add, delete, or list shell built-in commands (AT&T ksh93 builtin).\n\n"
                L"  -f lib.dll    Load dynamic built-in from Windows DLL module\n"
                L"  -d name       Delete / unload a dynamically loaded built-in command\n"
                L"  -s            Print all registered built-in command names\n");
        *code = 0;
        return true;
    }

    if (wcscmp(cmd->argv[1], L"-s") == 0 || cmd->argc == 1) {
        Module_List();
        *code = 0;
        return true;
    }

    if (wcscmp(cmd->argv[1], L"-f") == 0) {
        if (cmd->argc < 3) {
            fwprintf(stderr, L"builtin: -f requires dynamic library path\n");
            *code = 1;
            return true;
        }
        const wchar_t* dllPath = cmd->argv[2];
        const wchar_t* cmdName = (cmd->argc > 3) ? cmd->argv[3] : NULL;

        wchar_t errBuf[256] = L"";
        if (!Module_LoadNative(dllPath, cmdName, errBuf, _countof(errBuf))) {
            fwprintf(stderr, L"builtin: cannot load %ls: %ls\n", dllPath, errBuf);
            *code = 1;
            return true;
        }
        *code = 0;
        return true;
    }

    if (wcscmp(cmd->argv[1], L"-d") == 0) {
        if (cmd->argc < 3) {
            fwprintf(stderr, L"builtin: -d requires command name\n");
            *code = 1;
            return true;
        }
        if (!Module_UnloadNative(cmd->argv[2])) {
            fwprintf(stderr, L"builtin: %ls: not a dynamic built-in\n", cmd->argv[2]);
            *code = 1;
            return true;
        }
        *code = 0;
        return true;
    }

    /* Execute the builtin command directly, bypassing aliases and functions */
    CommandAST subCmd;
    memset(&subCmd, 0, sizeof(subCmd));
    subCmd.argc = cmd->argc - 1;
    for (int i = 0; i < subCmd.argc && i < 255; i++) {
        subCmd.argv[i] = cmd->argv[i + 1];
    }
    subCmd.argv[subCmd.argc] = NULL;
    return TryExecBuiltin(shell, &subCmd, code);
}

/* ------------------------------------------------------------------------- */
/* 2. MODULE Command: Modern Shell Extension Manager                         */
/* ------------------------------------------------------------------------- */

bool Builtin_Module(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc < 2 || _wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0 || wcscmp(cmd->argv[1], L"help") == 0) {
        wprintf(L"Usage: module <command> [arguments]\n"
                L"Manage dynamic native DLL extensions and shell script packages.\n\n"
                L"Commands:\n"
                L"  load <file.dll> [cmd]   Load a native Windows dynamic library extension\n"
                L"  unload <name>           Unload a dynamic module or built-in\n"
                L"  import <name>           Import a shell package (.ksh) from KSH_MODULE_PATH\n"
                L"  list                    List all loaded modules and dynamic built-ins\n");
        *code = (cmd->argc < 2) ? 0 : 0;
        return true;
    }

    const wchar_t* sub = cmd->argv[1];

    if (_wcsicmp(sub, L"list") == 0) {
        Module_List();
        *code = 0;
        return true;
    }

    if (_wcsicmp(sub, L"load") == 0) {
        if (cmd->argc < 3) {
            fwprintf(stderr, L"module load: missing path to DLL module\n");
            *code = 1;
            return true;
        }
        const wchar_t* dllPath = cmd->argv[2];
        const wchar_t* cmdName = (cmd->argc > 3) ? cmd->argv[3] : NULL;

        wchar_t errBuf[256] = L"";
        if (!Module_LoadNative(dllPath, cmdName, errBuf, _countof(errBuf))) {
            fwprintf(stderr, L"module: failed to load %ls: %ls\n", dllPath, errBuf);
            *code = 1;
            return true;
        }
        wprintf(L"Module loaded successfully: %ls\n", dllPath);
        *code = 0;
        return true;
    }

    if (_wcsicmp(sub, L"unload") == 0) {
        if (cmd->argc < 3) {
            fwprintf(stderr, L"module unload: missing module or built-in name\n");
            *code = 1;
            return true;
        }
        if (!Module_UnloadNative(cmd->argv[2])) {
            fwprintf(stderr, L"module: %ls: not loaded\n", cmd->argv[2]);
            *code = 1;
            return true;
        }
        wprintf(L"Module unloaded: %ls\n", cmd->argv[2]);
        *code = 0;
        return true;
    }

    if (_wcsicmp(sub, L"import") == 0) {
        if (cmd->argc < 3) {
            fwprintf(stderr, L"module import: missing module name\n");
            *code = 1;
            return true;
        }
        wchar_t errBuf[256] = L"";
        if (!Module_ImportScript(shell, cmd->argv[2], errBuf, _countof(errBuf))) {
            fwprintf(stderr, L"module: failed to import %ls: %ls\n", cmd->argv[2], errBuf);
            *code = 1;
            return true;
        }
        *code = 0;
        return true;
    }

    fwprintf(stderr, L"module: unknown command '%ls'. Use 'module help' for usage.\n", sub);
    *code = 1;
    return true;
}
