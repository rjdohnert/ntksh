#ifndef KSH_COMMON_H
#define KSH_COMMON_H

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <wchar.h>
#include <wctype.h>
#include <errno.h>
#include <io.h>
#include <fcntl.h>
#include <conio.h>
#include <stdint.h>
#include <limits.h>
#include <sys/stat.h>
#include <share.h>
#include <sddl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <wincrypt.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "psapi.lib")

#define NTKSH_VERSION L"version      ntksh (PC/OpenSystems LLC) 93u+m/4.0.16 2026-07-01"
#define MAX_LINE_LEN 4096
#define MAX_ARGS 256
#define KSH_PATH_MAX 4096
#define SHELL_LIMIT_COUNT 16
#define MAX_HISTORY_ENTRIES 1024

enum {
    EDIT_MODE_EMACS = 0,
    EDIT_MODE_VI = 1
};

enum {
    BATCH_FLOW_NONE,
    BATCH_FLOW_GOTO,
    BATCH_FLOW_RETURN
};

enum {
    BATCH_EXPAND_PERCENT,
    BATCH_EXPAND_FINAL,
    BATCH_EXPAND_LOOP
};

struct IShell;
typedef struct IShell IShell;

wchar_t* SafeStrDup(const wchar_t* src);
wchar_t* GetEnvironmentValue(const wchar_t* name);
wchar_t* GetCurrentDirectoryAlloc(void);
bool StrContainsW(const wchar_t* haystack, const wchar_t* needle, bool ignoreCase);
void FormatBytesHuman(unsigned __int64 bytes, wchar_t* out, size_t out_sz);
bool Ksh_EvaluateArithmetic(IShell* shell, const wchar_t* expr, long long* out_int, double* out_float, bool* is_float);

/* ========================================================================= */
/* KshArena: Region / Arena Memory Management                                */
/* ========================================================================= */
typedef struct KshArenaBlock {
    struct KshArenaBlock* next;
    size_t capacity;
    size_t used;
} KshArenaBlock;

typedef struct KshArena {
    KshArenaBlock* blocks;
    size_t default_block_size;
} KshArena;

KshArena* KshArena_Create(size_t default_block_size);
void* KshArena_Alloc(KshArena* arena, size_t size);
void* KshArena_Calloc(KshArena* arena, size_t count, size_t elem_size);
wchar_t* KshArena_StrDup(KshArena* arena, const wchar_t* src);
wchar_t* KshArena_StrNDup(KshArena* arena, const wchar_t* src, size_t len);
void KshArena_Reset(KshArena* arena);
void KshArena_Destroy(KshArena* arena);

/* ========================================================================= */
/* Long Path (Extended NT Path > MAX_PATH) Support                           */
/* ========================================================================= */
bool Ksh_IsExtendedPath(const wchar_t* path);
bool Ksh_IsDevicePath(const wchar_t* path);
wchar_t* Ksh_ToExtendedPath(const wchar_t* path, wchar_t* out_buf, size_t out_cap);
HANDLE Ksh_CreateFile(const wchar_t* path, DWORD access, DWORD share,
                      SECURITY_ATTRIBUTES* sa, DWORD creation, DWORD flags, HANDLE templateFile);
DWORD Ksh_GetFileAttributes(const wchar_t* path);
BOOL Ksh_SetFileAttributes(const wchar_t* path, DWORD attrs);
BOOL Ksh_CreateDirectory(const wchar_t* path, SECURITY_ATTRIBUTES* sa);
BOOL Ksh_RemoveDirectory(const wchar_t* path);
BOOL Ksh_DeleteFile(const wchar_t* path);
FILE* Ksh_OpenFile(const wchar_t* path, const wchar_t* mode, int shflag);

#endif /* KSH_COMMON_H */
