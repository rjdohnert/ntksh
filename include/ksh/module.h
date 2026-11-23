/**
 * ntksh - Public Module / Plugin Extension API
 * Copyright (C) 2023-2026 PC/OpenSystems LLC. All rights reserved.
 * Licensed under the terms of the BSD-3 Clause License.
 *
 * Developers can build native DLL modules in C, C++, Rust, Zig, or Go
 * and load them into ntksh via 'module load <file.dll>' or 'builtin -f <file.dll>'.
 */

#ifndef KSH_MODULE_H
#define KSH_MODULE_H

#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KSH_MODULE_API_VERSION 1

#define KSH_MODULE_SUCCESS         0
#define KSH_MODULE_ERR_VERSION     1
#define KSH_MODULE_ERR_INIT        2
#define KSH_MODULE_ERR_ALREADY     3
#define KSH_MODULE_ERR_NOTFOUND    4

/* Command AST representation passed to built-ins */
#ifndef KSH_AST_H
typedef struct CommandAST CommandAST;
#endif

typedef bool (*KshBuiltinFn)(CommandAST* cmd, int* exit_code);

typedef struct KshPluginApi {
    uint32_t api_version;

    /* Built-in command management */
    bool (*RegisterBuiltin)(const wchar_t* name, KshBuiltinFn fn, const wchar_t* help_summary);
    bool (*UnregisterBuiltin)(const wchar_t* name);

    /* Shell environment & variable access */
    const wchar_t* (*GetVariable)(const wchar_t* name);
    bool (*SetVariable)(const wchar_t* name, const wchar_t* value);

    /* Shell command execution */
    int (*Eval)(const wchar_t* cmdline);

    /* Output formatting */
    void (*Print)(const wchar_t* fmt, ...);
    void (*PrintError)(const wchar_t* fmt, ...);
} KshPluginApi;

/* Required entry point function for native dynamic link libraries */
typedef int (*KshModuleInitFn)(const KshPluginApi* api);

/* Optional cleanup function when a module is unloaded */
typedef void (*KshModuleFiniFn)(void);

#ifdef __cplusplus
}
#endif

#endif /* KSH_MODULE_H */
