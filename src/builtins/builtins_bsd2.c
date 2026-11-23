#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>
#include <direct.h>
#include <sys/stat.h>
#include <time.h>

/* ========================================================================= */
/*                          BSD Scripting & Core Utilities                   */
/*             unlink, link, tsort, nice, nohup, patch, pax                  */
/* ========================================================================= */

/* ========================================================================= */
/* 1. UNLINK: POSIX / BSD System Call Primitive                              */
/* ========================================================================= */

bool Builtin_Unlink(CommandAST* cmd, int* code) {
    if (cmd->argc == 2 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: unlink file\n"
                L"Call the unlink function to remove the specified file.\n");
        *code = 0;
        return true;
    }

    if (cmd->argc < 2) {
        fwprintf(stderr, L"unlink: missing operand\n");
        *code = 1;
        return true;
    }
    if (cmd->argc > 2) {
        fwprintf(stderr, L"unlink: extra operand '%ls'\n", cmd->argv[2]);
        *code = 1;
        return true;
    }

    const wchar_t* path = cmd->argv[1];
    DWORD attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        fwprintf(stderr, L"unlink: %ls: No such file or directory\n", path);
        *code = 1;
        return true;
    }

    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        fwprintf(stderr, L"unlink: %ls: Is a directory\n", path);
        *code = 1;
        return true;
    }

    if (!DeleteFileW(path)) {
        /* Clear read-only if needed and retry */
        if (attrs & FILE_ATTRIBUTE_READONLY) {
            SetFileAttributesW(path, attrs & ~FILE_ATTRIBUTE_READONLY);
            if (DeleteFileW(path)) {
                *code = 0;
                return true;
            }
        }
        fwprintf(stderr, L"unlink: %ls: Cannot delete file (error %lu)\n", path, GetLastError());
        *code = 1;
        return true;
    }

    *code = 0;
    return true;
}

/* ========================================================================= */
/* 2. LINK: POSIX / BSD Hard Link Primitive                                  */
/* ========================================================================= */

bool Builtin_Link(CommandAST* cmd, int* code) {
    if (cmd->argc == 2 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: link file1 file2\n"
                L"Create a hard link file2 pointing to file1.\n");
        *code = 0;
        return true;
    }

    if (cmd->argc < 3) {
        fwprintf(stderr, L"link: missing operand\n");
        *code = 1;
        return true;
    }
    if (cmd->argc > 3) {
        fwprintf(stderr, L"link: extra operand '%ls'\n", cmd->argv[3]);
        *code = 1;
        return true;
    }

    const wchar_t* target = cmd->argv[1];
    const wchar_t* link_name = cmd->argv[2];

    if (!CreateHardLinkW(link_name, target, NULL)) {
        /* On filesystems where hard links are unsupported (such as ReFS or FAT),
           fallback gracefully to unprivileged symbolic link */
        DWORD flags = 0x2; /* SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE */
        if (!CreateSymbolicLinkW(link_name, target, flags)) {
            if (!CreateSymbolicLinkW(link_name, target, 0)) {
                fwprintf(stderr, L"link: %ls: %ls: Cannot create link (error %lu)\n",
                         target, link_name, GetLastError());
                *code = 1;
                return true;
            }
        }
    }

    *code = 0;
    return true;
}

/* ========================================================================= */
/* 3. TSORT: Topological Sort (POSIX / BSD)                                  */
/* ========================================================================= */

typedef struct {
    wchar_t name[256];
    int in_degree;
    int* successors;
    int succ_count;
    int succ_cap;
} TsortNode;

typedef struct {
    TsortNode* nodes;
    int count;
    int cap;
} TsortGraph;

static int TsortFindOrAdd(TsortGraph* g, const wchar_t* name) {
    for (int i = 0; i < g->count; i++) {
        if (wcscmp(g->nodes[i].name, name) == 0) return i;
    }
    if (g->count >= g->cap) {
        g->cap = (g->cap == 0) ? 64 : g->cap * 2;
        TsortNode* new_nodes = (TsortNode*)realloc(g->nodes, g->cap * sizeof(TsortNode));
        if (!new_nodes) return -1;
        g->nodes = new_nodes;
    }
    int idx = g->count++;
    wcsncpy_s(g->nodes[idx].name, _countof(g->nodes[idx].name), name, _TRUNCATE);
    g->nodes[idx].in_degree = 0;
    g->nodes[idx].successors = NULL;
    g->nodes[idx].succ_count = 0;
    g->nodes[idx].succ_cap = 0;
    return idx;
}

static void TsortAddEdge(TsortGraph* g, int from, int to) {
    if (from < 0 || to < 0 || from == to) return;
    TsortNode* fn = &g->nodes[from];
    for (int i = 0; i < fn->succ_count; i++) {
        if (fn->successors[i] == to) return;
    }
    if (fn->succ_count >= fn->succ_cap) {
        fn->succ_cap = (fn->succ_cap == 0) ? 8 : fn->succ_cap * 2;
        int* new_succ = (int*)realloc(fn->successors, fn->succ_cap * sizeof(int));
        if (!new_succ) return;
        fn->successors = new_succ;
    }
    fn->successors[fn->succ_count++] = to;
    g->nodes[to].in_degree++;
}

bool Builtin_Tsort(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: tsort [-q] [file]\n"
                L"Perform topological sort on ordered pairs of nodes (POSIX/BSD).\n\n"
                L"  -q          Quiet: suppress cycle warnings\n");
        *code = 0;
        return true;
    }

    bool quiet = false;
    const wchar_t* filename = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        if (wcscmp(cmd->argv[i], L"-q") == 0) {
            quiet = true;
        } else if (!filename) {
            filename = cmd->argv[i];
        }
    }

    FILE* fp = stdin;
    if (filename && wcscmp(filename, L"-") != 0) {
        fp = _wfsopen(filename, L"r", _SH_DENYNO);
        if (!fp) {
            fwprintf(stderr, L"tsort: %ls: Cannot open file\n", filename);
            *code = 1;
            return true;
        }
    }

    TsortGraph g;
    memset(&g, 0, sizeof(g));

    wchar_t token[256];
    wchar_t prev_token[256];
    bool have_prev = false;

    while (fgetws(token, sizeof(token) / sizeof(token[0]), fp)) {
        wchar_t* p = token;
        while (*p) {
            while (*p && iswspace(*p)) p++;
            if (!*p) break;
            wchar_t* start = p;
            while (*p && !iswspace(*p)) p++;
            wchar_t saved = *p;
            *p = L'\0';

            if (!have_prev) {
                wcsncpy(prev_token, start, 255);
                prev_token[255] = L'\0';
                have_prev = true;
            } else {
                int u = TsortFindOrAdd(&g, prev_token);
                int v = TsortFindOrAdd(&g, start);
                TsortAddEdge(&g, u, v);
                have_prev = false;
            }

            if (saved == L'\0') break;
            *p = saved;
        }
    }

    if (fp != stdin) fclose(fp);

    if (have_prev) {
        fwprintf(stderr, L"tsort: odd number of tokens\n");
        for (int i = 0; i < g.count; i++) free(g.nodes[i].successors);
        free(g.nodes);
        *code = 1;
        return true;
    }

    if (g.count == 0) {
        *code = 0;
        return true;
    }

    /* Kahn's algorithm */
    int* queue = (int*)malloc(g.count * sizeof(int));
    int q_head = 0, q_tail = 0;

    for (int i = 0; i < g.count; i++) {
        if (g.nodes[i].in_degree == 0) {
            queue[q_tail++] = i;
        }
    }

    int emitted = 0;
    while (q_head < q_tail) {
        int u = queue[q_head++];
        wprintf(L"%ls\n", g.nodes[u].name);
        emitted++;

        for (int i = 0; i < g.nodes[u].succ_count; i++) {
            int v = g.nodes[u].successors[i];
            if (--g.nodes[v].in_degree == 0) {
                queue[q_tail++] = v;
            }
        }
    }

    bool has_cycle = (emitted < g.count);
    if (has_cycle) {
        if (!quiet) {
            fwprintf(stderr, L"tsort: cycle in data\n");
        }
        /* Emit remaining nodes in cycle */
        for (int i = 0; i < g.count; i++) {
            if (g.nodes[i].in_degree > 0) {
                wprintf(L"%ls\n", g.nodes[i].name);
            }
        }
    }

    for (int i = 0; i < g.count; i++) free(g.nodes[i].successors);
    free(g.nodes);
    free(queue);

    *code = has_cycle ? 1 : 0;
    return true;
}

/* ========================================================================= */
/* 4. NICE: Process Priority Scheduling (POSIX / BSD)                        */
/* ========================================================================= */

bool Builtin_Nice(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: nice [-n increment] utility [argument ...]\n"
                L"Execute utility with an altered process scheduling priority.\n\n"
                L"  -n increment  Priority adjustment (-20 to +19, default: 10)\n");
        *code = 0;
        return true;
    }

    if (cmd->argc == 1) {
        /* Print current nice value: 0 for normal */
        DWORD prio = GetPriorityClass(GetCurrentProcess());
        int nice_val = 0;
        if (prio == IDLE_PRIORITY_CLASS) nice_val = 19;
        else if (prio == BELOW_NORMAL_PRIORITY_CLASS) nice_val = 10;
        else if (prio == ABOVE_NORMAL_PRIORITY_CLASS) nice_val = -5;
        else if (prio == HIGH_PRIORITY_CLASS) nice_val = -10;
        else if (prio == REALTIME_PRIORITY_CLASS) nice_val = -20;
        wprintf(L"%d\n", nice_val);
        *code = 0;
        return true;
    }

    int inc = 10;
    int arg_idx = 1;

    while (arg_idx < cmd->argc && cmd->argv[arg_idx][0] == L'-' && cmd->argv[arg_idx][1] != L'\0') {
        const wchar_t* opt = cmd->argv[arg_idx];
        if (wcscmp(opt, L"-n") == 0 && arg_idx + 1 < cmd->argc) {
            inc = (int)wcstol(cmd->argv[++arg_idx], NULL, 10);
        } else if (wcsncmp(opt, L"-n", 2) == 0 && iswdigit(opt[2])) {
            inc = (int)wcstol(opt + 2, NULL, 10);
        } else if (iswdigit(opt[1])) {
            inc = (int)wcstol(opt + 1, NULL, 10);
        } else {
            break;
        }
        arg_idx++;
    }

    if (arg_idx >= cmd->argc) {
        fwprintf(stderr, L"nice: missing utility\n");
        *code = 1;
        return true;
    }

    DWORD prio_class = NORMAL_PRIORITY_CLASS;
    if (inc >= 10) prio_class = IDLE_PRIORITY_CLASS;
    else if (inc > 0) prio_class = BELOW_NORMAL_PRIORITY_CLASS;
    else if (inc <= -10) prio_class = HIGH_PRIORITY_CLASS;
    else if (inc < 0) prio_class = ABOVE_NORMAL_PRIORITY_CLASS;

    DWORD old_prio = GetPriorityClass(GetCurrentProcess());
    SetPriorityClass(GetCurrentProcess(), prio_class);

    int exit_code = 0;
    if (shell) {
        wchar_t full_cmd[4096] = {0};
        for (int i = arg_idx; i < cmd->argc; i++) {
            if (!Ksh_AppendShellWord(full_cmd, sizeof(full_cmd) / sizeof(full_cmd[0]), cmd->argv[i])) {
                fwprintf(stderr, L"nice: command too long\n");
                exit_code = 126;
                break;
            }
        }
        if (exit_code == 0) {
            exit_code = shell->ExecuteLine(shell, full_cmd);
        }
    }

    SetPriorityClass(GetCurrentProcess(), old_prio);
    *code = exit_code;
    return true;
}

/* ========================================================================= */
/* 5. NOHUP: Terminal Hangup Immunity (POSIX / BSD)                          */
/* ========================================================================= */

bool Builtin_Nohup(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: nohup utility [argument ...]\n"
                L"Invoke a utility immune to hangups, with output to nohup.out.\n");
        *code = 0;
        return true;
    }

    if (cmd->argc < 2) {
        fwprintf(stderr, L"nohup: missing utility\n");
        *code = 127;
        return true;
    }

    /* Check if stdout is interactive terminal */
    bool stdout_is_tty = (_isatty(_fileno(stdout)) != 0);
    FILE* nohup_file = NULL;

    if (stdout_is_tty) {
        nohup_file = _wfsopen(L"nohup.out", L"a+", _SH_DENYNO);
        if (!nohup_file) {
            const wchar_t* home = _wgetenv(L"USERPROFILE");
            if (home) {
                wchar_t home_path[MAX_PATH];
                swprintf(home_path, sizeof(home_path) / sizeof(home_path[0]), L"%ls\\nohup.out", home);
                nohup_file = _wfsopen(home_path, L"a+", _SH_DENYNO);
                if (nohup_file) {
                    fwprintf(stderr, L"nohup: appending output to %ls\n", home_path);
                }
            }
        } else {
            fwprintf(stderr, L"nohup: appending output to nohup.out\n");
        }
    }

    /* Temporarily ignore ctrl handler */
    SetConsoleCtrlHandler(NULL, TRUE);

    int exit_code = 0;
    if (shell) {
        wchar_t full_cmd[4096] = {0};
        for (int i = 1; i < cmd->argc; i++) {
            if (!Ksh_AppendShellWord(full_cmd, sizeof(full_cmd) / sizeof(full_cmd[0]), cmd->argv[i])) {
                fwprintf(stderr, L"nohup: command line too long\n");
                exit_code = 126;
                break;
            }
        }

        if (exit_code == 0) {
            int old_stdout_fd = -1;
            if (nohup_file) {
                fflush(stdout);
                old_stdout_fd = _dup(_fileno(stdout));
                _dup2(_fileno(nohup_file), _fileno(stdout));
            }

            exit_code = shell->ExecuteLine(shell, full_cmd);

            if (nohup_file) {
                fflush(stdout);
                if (old_stdout_fd >= 0) {
                    _dup2(old_stdout_fd, _fileno(stdout));
                    _close(old_stdout_fd);
                }
                fclose(nohup_file);
            }
        }
    }

    SetConsoleCtrlHandler(NULL, FALSE);
    *code = exit_code;
    return true;
}

/* ========================================================================= */
/* 6. PATCH: Unified Diff Applicator (BSD patch)                             */
/* ========================================================================= */

static const wchar_t* StripPath(const wchar_t* path, int p_strip) {
    if (p_strip <= 0) return path;
    const wchar_t* p = path;
    for (int i = 0; i < p_strip && *p; i++) {
        while (*p && *p != L'/' && *p != L'\\') p++;
        if (*p) p++;
    }
    return (*p) ? p : path;
}

static bool PatchAppend(wchar_t*** arr, size_t* count, size_t* cap, wchar_t* line) {
    if (!*arr || !line) { free(line); return false; }
    if (*count >= *cap) {
        size_t ncap = *cap * 2;
        wchar_t** narr = (wchar_t**)realloc(*arr, ncap * sizeof(wchar_t*));
        if (!narr) { free(line); return false; }
        *arr = narr;
        *cap = ncap;
    }
    (*arr)[(*count)++] = line;
    return true;
}

bool Builtin_Patch(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: patch [-R] [-s] [-b] [-p strip] [-i patchfile] [-o outfile] [-d dir] [origfile]\n"
                L"Apply unified diff patches to files (BSD patch).\n\n"
                L"  -p[num]     Strip num leading path components\n"
                L"  -i file     Read patch from file instead of stdin\n"
                L"  -o file     Output patched file to file\n"
                L"  -R          Reverse the patch\n"
                L"  -s, -q      Silent / quiet mode\n"
                L"  -b          Create backup of original file (.orig)\n"
                L"  -d dir      Change working directory before patching\n");
        *code = 0;
        return true;
    }

    int p_strip = 0;
    const wchar_t* patch_file = NULL;
    const wchar_t* out_file = NULL;
    const wchar_t* orig_file = NULL;
    const wchar_t* work_dir = NULL;
    bool reverse = false;
    bool quiet = false;
    bool backup = false;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (wcscmp(arg, L"-R") == 0) reverse = true;
        else if (wcscmp(arg, L"-s") == 0 || wcscmp(arg, L"-q") == 0) quiet = true;
        else if (wcscmp(arg, L"-b") == 0) backup = true;
        else if (wcscmp(arg, L"-i") == 0 && i + 1 < cmd->argc) patch_file = cmd->argv[++i];
        else if (wcscmp(arg, L"-o") == 0 && i + 1 < cmd->argc) out_file = cmd->argv[++i];
        else if (wcscmp(arg, L"-d") == 0 && i + 1 < cmd->argc) work_dir = cmd->argv[++i];
        else if (wcscmp(arg, L"-p") == 0 && i + 1 < cmd->argc) p_strip = (int)wcstol(cmd->argv[++i], NULL, 10);
        else if (wcsncmp(arg, L"-p", 2) == 0 && iswdigit(arg[2])) p_strip = (int)wcstol(arg + 2, NULL, 10);
        else if (arg[0] != L'-' && !orig_file) orig_file = arg;
    }

    if (work_dir) {
        _wchdir(work_dir);
    }

    FILE* pfp = stdin;
    if (patch_file) {
        pfp = _wfsopen(patch_file, L"r", _SH_DENYNO);
        if (!pfp) {
            fwprintf(stderr, L"patch: %ls: Cannot open patch file\n", patch_file);
            *code = 1;
            return true;
        }
    }

    /* Read all patch lines */
    size_t pcap = 512, pcount = 0;
    wchar_t** plines = (wchar_t**)malloc(pcap * sizeof(wchar_t*));
    if (!plines) {
        if (pfp != stdin) fclose(pfp);
        *code = 1;
        return true;
    }
    wchar_t lbuf[8192];
    while (fgetws(lbuf, sizeof(lbuf) / sizeof(lbuf[0]), pfp)) {
        PatchAppend(&plines, &pcount, &pcap, _wcsdup(lbuf));
    }
    if (pfp != stdin) fclose(pfp);

    /* Find target filename if not specified */
    wchar_t detected_file[MAX_PATH] = {0};
    for (size_t i = 0; i < pcount && !orig_file; i++) {
        if (wcsncmp(plines[i], L"+++ ", 4) == 0) {
            wchar_t* tab = wcschr(plines[i] + 4, L'\t');
            if (tab) *tab = L'\0';
            wchar_t* nl = wcschr(plines[i] + 4, L'\n');
            if (nl) *nl = L'\0';
            orig_file = StripPath(plines[i] + 4, p_strip);
            wcsncpy_s(detected_file, _countof(detected_file), orig_file, _TRUNCATE);
            orig_file = detected_file;
            break;
        }
    }

    if (!orig_file) {
        fwprintf(stderr, L"patch: cannot determine target file\n");
        for (size_t i = 0; i < pcount; i++) free(plines[i]);
        free(plines);
        *code = 1;
        return true;
    }

    /* Read target file lines */
    FILE* tfp = _wfsopen(orig_file, L"r", _SH_DENYNO);
    if (!tfp) {
        fwprintf(stderr, L"patch: %ls: No such file or directory\n", orig_file);
        for (size_t i = 0; i < pcount; i++) free(plines[i]);
        free(plines);
        *code = 1;
        return true;
    }

    size_t fcap = 512, fcount = 0;
    wchar_t** flines = (wchar_t**)malloc(fcap * sizeof(wchar_t*));
    if (!flines) {
        fclose(tfp);
        for (size_t i = 0; i < pcount; i++) free(plines[i]);
        free(plines);
        *code = 1;
        return true;
    }
    while (fgetws(lbuf, sizeof(lbuf) / sizeof(lbuf[0]), tfp)) {
        PatchAppend(&flines, &fcount, &fcap, _wcsdup(lbuf));
    }
    fclose(tfp);

    if (!quiet) {
        wprintf(L"patching file %ls\n", orig_file);
    }

    /* Result lines buffer */
    size_t rcap = fcount + 512, rcount = 0;
    wchar_t** rlines = (wchar_t**)malloc(rcap * sizeof(wchar_t*));
    if (!rlines) {
        for (size_t i = 0; i < pcount; i++) free(plines[i]);
        free(plines);
        for (size_t i = 0; i < fcount; i++) free(flines[i]);
        free(flines);
        *code = 1;
        return true;
    }
    size_t cur_src = 0;

    for (size_t i = 0; i < pcount; i++) {
        if (wcsncmp(plines[i], L"@@ -", 4) == 0) {
            int o_start = 1, o_count = 1, n_start = 1, n_count = 1;
            swscanf(plines[i], L"@@ -%d,%d +%d,%d @@", &o_start, &o_count, &n_start, &n_count);
            if (o_count == 1 && wcschr(plines[i], L',') == NULL) {
                swscanf(plines[i], L"@@ -%d +%d @@", &o_start, &n_start);
            }

            int target_line = (reverse ? n_start : o_start) - 1;
            if (target_line < 0) target_line = 0;

            /* Copy unchanged source lines up to target_line */
            while (cur_src < (size_t)target_line && cur_src < fcount) {
                PatchAppend(&rlines, &rcount, &rcap, _wcsdup(flines[cur_src++]));
            }

            /* Process hunk lines */
            i++;
            while (i < pcount && (plines[i][0] == L' ' || plines[i][0] == L'+' || plines[i][0] == L'-')) {
                wchar_t type = plines[i][0];
                if (reverse) {
                    if (type == L'+') type = L'-';
                    else if (type == L'-') type = L'+';
                }

                if (type == L' ') {
                    if (cur_src < fcount) {
                        PatchAppend(&rlines, &rcount, &rcap, _wcsdup(flines[cur_src++]));
                    }
                } else if (type == L'-') {
                    if (cur_src < fcount) cur_src++;
                } else if (type == L'+') {
                    PatchAppend(&rlines, &rcount, &rcap, _wcsdup(plines[i] + 1));
                }
                i++;
            }
            i--; /* step back for outer loop */
        }
    }

    /* Copy any remaining trailing lines */
    while (cur_src < fcount) {
        PatchAppend(&rlines, &rcount, &rcap, _wcsdup(flines[cur_src++]));
    }

    /* Optional backup */
    if (backup) {
        wchar_t bpath[MAX_PATH];
        swprintf(bpath, sizeof(bpath) / sizeof(bpath[0]), L"%ls.orig", orig_file);
        CopyFileW(orig_file, bpath, FALSE);
    }

    /* Write target file */
    const wchar_t* dest_file = out_file ? out_file : orig_file;
    FILE* wfp = _wfsopen(dest_file, L"w", _SH_DENYWR);
    if (!wfp) {
        fwprintf(stderr, L"patch: %ls: Cannot write file\n", dest_file);
        *code = 1;
    } else {
        for (size_t j = 0; j < rcount; j++) {
            fputws(rlines[j], wfp);
        }
        fclose(wfp);
        *code = 0;
    }

    for (size_t j = 0; j < pcount; j++) free(plines[j]);
    free(plines);
    for (size_t j = 0; j < fcount; j++) free(flines[j]);
    free(flines);
    for (size_t j = 0; j < rcount; j++) free(rlines[j]);
    free(rlines);

    return true;
}

/* ========================================================================= */
/* 7. PAX: Portable Archive Interchange (POSIX / BSD)                        */
/* ========================================================================= */

#pragma pack(push, 1)
typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} TarHeader;
#pragma pack(pop)

static unsigned int TarChecksum(const TarHeader* h) {
    const unsigned char* p = (const unsigned char*)h;
    unsigned int sum = 0;
    for (size_t i = 0; i < sizeof(TarHeader); i++) {
        if (i >= offsetof(TarHeader, chksum) && i < offsetof(TarHeader, chksum) + sizeof(h->chksum)) {
            sum += ' ';
        } else {
            sum += p[i];
        }
    }
    return sum;
}

static void FormatOctal(char* dst, size_t size, unsigned __int64 val) {
    snprintf(dst, size, "%0*llo", (int)(size - 1), val);
}

static bool MatchPattern(const wchar_t* pat, const wchar_t* str) {
    if (!pat || !*pat || wcscmp(pat, L"*") == 0) return true;
    return wcsstr(str, pat) != NULL;
}

static bool TarIsPathSafe(const wchar_t* path) {
    if (!path || !*path) return false;
    if (!path || !*path) return false;
    /* Reject absolute paths: leading slash or backslash */
    if (path[0] == L'/' || path[0] == L'\\') return false;
    /* Reject drive letters or NTFS alternate data streams */
    if (wcschr(path, L':') != NULL) return false;
    /* Reject directory traversal with .. */
    const wchar_t* p = path;
    while (*p) {
        if (p[0] == L'.' && p[1] == L'.') {
            if (p == path || p[-1] == L'/' || p[-1] == L'\\') {
                if (p[2] == L'\0' || p[2] == L'/' || p[2] == L'\\') {
                    return false;
                }
            }
        }
        p++;
    }
    return true;
}

static void TarEnsureParentDirs(const wchar_t* path) {
    wchar_t buf[MAX_PATH];
    wcsncpy_s(buf, _countof(buf), path, _TRUNCATE);
    wchar_t* p = buf;
    while (*p) {
        if (*p == L'/' || *p == L'\\') {
            *p = L'\0';
            if (buf[0] != L'\0' && wcscmp(buf, L".") != 0 && wcscmp(buf, L"..") != 0 && !(buf[1] == L':' && buf[2] == L'\0')) {
                CreateDirectoryW(buf, NULL);
            }
            *p = L'\\';
        }
        p++;
    }
}

static void PaxCopyTree(const wchar_t* src, const wchar_t* dst, bool verbose, bool newer_only) {
    DWORD attr = GetFileAttributesW(src);
    if (attr == INVALID_FILE_ATTRIBUTES) return;

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        CreateDirectoryW(dst, NULL);
        wchar_t search[MAX_PATH * 2];
        swprintf(search, sizeof(search) / sizeof(search[0]), L"%ls\\*", src);
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(search, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                wchar_t ssub[MAX_PATH * 2], dsub[MAX_PATH * 2];
                swprintf(ssub, sizeof(ssub) / sizeof(ssub[0]), L"%ls\\%ls", src, fd.cFileName);
                swprintf(dsub, sizeof(dsub) / sizeof(dsub[0]), L"%ls\\%ls", dst, fd.cFileName);
                PaxCopyTree(ssub, dsub, verbose, newer_only);
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    } else {
        if (newer_only) {
            WIN32_FIND_DATAW fd_src, fd_dst;
            HANDLE hs = FindFirstFileW(src, &fd_src);
            HANDLE hd = FindFirstFileW(dst, &fd_dst);
            if (hs != INVALID_HANDLE_VALUE && hd != INVALID_HANDLE_VALUE) {
                if (CompareFileTime(&fd_src.ftLastWriteTime, &fd_dst.ftLastWriteTime) <= 0) {
                    FindClose(hs); FindClose(hd);
                    return;
                }
            }
            if (hs != INVALID_HANDLE_VALUE) FindClose(hs);
            if (hd != INVALID_HANDLE_VALUE) FindClose(hd);
        }
        CopyFileW(src, dst, FALSE);
        if (verbose) {
            wprintf(L"%ls\n", src);
        }
    }
}

bool Builtin_Pax(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: pax [-r] [-w] [-v] [-k] [-u] [-f archive] [pattern... | file... dir]\n"
                L"Portable Archive Interchange (POSIX / BSD standard archiver).\n\n"
                L"  -r          Read (extract from archive or copy)\n"
                L"  -w          Write (create archive or copy)\n"
                L"  -f archive  Archive file name (default: stdin/stdout)\n"
                L"  -v          Verbose: list file details\n"
                L"  -k          Do not overwrite existing files\n"
                L"  -u          Copy or extract only if file is newer\n");
        *code = 0;
        return true;
    }

    bool mode_r = false;
    bool mode_w = false;
    bool verbose = false;
    bool keep_existing = false;
    bool newer_only = false;
    const wchar_t* archive_file = NULL;
    int arg_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && wcscmp(arg, L"-") != 0) {
            for (int j = 1; arg[j]; j++) {
                if (arg[j] == L'r') mode_r = true;
                else if (arg[j] == L'w') mode_w = true;
                else if (arg[j] == L'v') verbose = true;
                else if (arg[j] == L'k') keep_existing = true;
                else if (arg[j] == L'u') newer_only = true;
                else if (arg[j] == L'f') {
                    if (i + 1 < cmd->argc) archive_file = cmd->argv[++i];
                    break;
                }
            }
            arg_start = i + 1;
        } else {
            arg_start = i;
            break;
        }
    }

    /* COPY MODE: pax -r -w [files...] directory */
    if (mode_r && mode_w) {
        int target_idx = cmd->argc - 1;
        if (target_idx < arg_start) {
            fwprintf(stderr, L"pax: missing destination directory\n");
            *code = 1;
            return true;
        }
        const wchar_t* dest_dir = cmd->argv[target_idx];
        DWORD dattr = GetFileAttributesW(dest_dir);
        if (dattr == INVALID_FILE_ATTRIBUTES || !(dattr & FILE_ATTRIBUTE_DIRECTORY)) {
            CreateDirectoryW(dest_dir, NULL);
        }

        for (int i = arg_start; i < target_idx; i++) {
            const wchar_t* src = cmd->argv[i];
            const wchar_t* bname = wcsrchr(src, L'\\');
            if (!bname) bname = wcsrchr(src, L'/');
            bname = bname ? bname + 1 : src;
            wchar_t target_path[MAX_PATH * 2];
            swprintf(target_path, sizeof(target_path) / sizeof(target_path[0]), L"%ls\\%ls", dest_dir, bname);
            PaxCopyTree(src, target_path, verbose, newer_only);
        }
        *code = 0;
        return true;
    }

    /* WRITE / ARCHIVE CREATION MODE: pax -w -f archive [files...] */
    if (mode_w) {
        FILE* afp = stdout;
        if (archive_file) {
            afp = _wfsopen(archive_file, L"wb", _SH_DENYWR);
            if (!afp) {
                fwprintf(stderr, L"pax: %ls: Cannot create archive\n", archive_file);
                *code = 1;
                return true;
            }
        }

        for (int i = arg_start; i < cmd->argc; i++) {
            const wchar_t* path = cmd->argv[i];
            FILE* ifp = _wfsopen(path, L"rb", _SH_DENYNO);
            if (!ifp) continue;

            fseek(ifp, 0, SEEK_END);
            unsigned __int64 fsize = _ftelli64(ifp);
            fseek(ifp, 0, SEEK_SET);

            TarHeader hdr;
            memset(&hdr, 0, sizeof(hdr));

            char mbs_path[100];
            wcstombs(mbs_path, path, sizeof(mbs_path) - 1);
            strncpy(hdr.name, mbs_path, sizeof(hdr.name) - 1);
            FormatOctal(hdr.mode, sizeof(hdr.mode), 0644);
            FormatOctal(hdr.uid, sizeof(hdr.uid), 1000);
            FormatOctal(hdr.gid, sizeof(hdr.gid), 1000);
            FormatOctal(hdr.size, sizeof(hdr.size), fsize);
            FormatOctal(hdr.mtime, sizeof(hdr.mtime), (unsigned __int64)time(NULL));
            hdr.typeflag = '0';
            memcpy(hdr.magic, "ustar", 6);
            memcpy(hdr.version, "00", 2);

            unsigned int sum = TarChecksum(&hdr);
            snprintf(hdr.chksum, sizeof(hdr.chksum), "%06o", sum);

            fwrite(&hdr, 1, sizeof(hdr), afp);

            char buf[512];
            size_t bytes;
            while ((bytes = fread(buf, 1, sizeof(buf), ifp)) > 0) {
                if (bytes < 512) memset(buf + bytes, 0, 512 - bytes);
                fwrite(buf, 1, 512, afp);
            }
            fclose(ifp);

            if (verbose) {
                wprintf(L"%ls\n", path);
            }
        }

        /* End of archive: two empty blocks */
        char zeros[1024];
        memset(zeros, 0, sizeof(zeros));
        fwrite(zeros, 1, sizeof(zeros), afp);
        if (afp != stdout) fclose(afp);

        *code = 0;
        return true;
    }

    /* READ / LIST MODE: pax [-r] [-f archive] [patterns...] */
    FILE* rfp = stdin;
    if (archive_file) {
        rfp = _wfsopen(archive_file, L"rb", _SH_DENYNO);
        if (!rfp) {
            fwprintf(stderr, L"pax: %ls: Cannot open archive\n", archive_file);
            *code = 1;
            return true;
        }
    }

    TarHeader hdr;
    while (fread(&hdr, 1, sizeof(hdr), rfp) == sizeof(hdr)) {
        if (hdr.name[0] == '\0') break;

        char name_buf[101] = {0};
        memcpy(name_buf, hdr.name, sizeof(hdr.name));
        name_buf[sizeof(hdr.name)] = '\0';

        wchar_t wname[MAX_PATH] = {0};
        size_t conv = mbstowcs(wname, name_buf, _countof(wname) - 1);
        if (conv == (size_t)-1) conv = 0;
        wname[conv < _countof(wname) ? conv : _countof(wname) - 1] = L'\0';

        char size_buf[13] = {0};
        memcpy(size_buf, hdr.size, sizeof(hdr.size));
        size_buf[sizeof(hdr.size)] = '\0';
        unsigned __int64 fsize = 0;
        sscanf_s(size_buf, "%llo", &fsize);

        bool matched = true;
        if (arg_start < cmd->argc) {
            matched = false;
            for (int k = arg_start; k < cmd->argc; k++) {
                if (MatchPattern(cmd->argv[k], wname)) { matched = true; break; }
            }
        }

        if (mode_r && matched) {
            /* Extraction mode */
            if (!TarIsPathSafe(wname)) {
                fwprintf(stderr, L"pax: skipping unsafe path: %ls\n", wname);
                unsigned __int64 blocks = (fsize + 511) / 512;
                _fseeki64(rfp, blocks * 512, SEEK_CUR);
            } else if (keep_existing && GetFileAttributesW(wname) != INVALID_FILE_ATTRIBUTES) {
                unsigned __int64 blocks = (fsize + 511) / 512;
                _fseeki64(rfp, blocks * 512, SEEK_CUR);
            } else {
                TarEnsureParentDirs(wname);
                FILE* out = _wfsopen(wname, L"wb", _SH_DENYWR);
                unsigned __int64 rem = fsize;
                char buf[512];
                while (rem > 0) {
                    size_t to_read = (rem > 512) ? 512 : (size_t)rem;
                    fread(buf, 1, 512, rfp);
                    if (out) fwrite(buf, 1, to_read, out);
                    rem -= to_read;
                }
                if (out) fclose(out);
            }
        } else {
            /* Skip data blocks */
            unsigned __int64 blocks = (fsize + 511) / 512;
            _fseeki64(rfp, blocks * 512, SEEK_CUR);
        }

        if (verbose || !mode_r) {
            if (matched) wprintf(L"%ls\n", wname);
        }
    }

    if (rfp != stdin) fclose(rfp);
    *code = 0;
    return true;
}

static void TarWriteFile(const wchar_t* full_path, const wchar_t* arc_name, FILE* afp, bool verbose) {
    FILE* ifp = _wfsopen(full_path, L"rb", _SH_DENYNO);
    if (!ifp) return;

    _fseeki64(ifp, 0, SEEK_END);
    unsigned __int64 fsize = _ftelli64(ifp);
    _fseeki64(ifp, 0, SEEK_SET);

    TarHeader hdr;
    memset(&hdr, 0, sizeof(hdr));

    char mbs_path[100];
    wcstombs(mbs_path, arc_name, sizeof(mbs_path) - 1);
    for (size_t k = 0; k < sizeof(mbs_path) && mbs_path[k]; k++) {
        if (mbs_path[k] == '\\') mbs_path[k] = '/';
    }
    strncpy(hdr.name, mbs_path, sizeof(hdr.name) - 1);
    FormatOctal(hdr.mode, sizeof(hdr.mode), 0644);
    FormatOctal(hdr.uid, sizeof(hdr.uid), 1000);
    FormatOctal(hdr.gid, sizeof(hdr.gid), 1000);
    FormatOctal(hdr.size, sizeof(hdr.size), fsize);
    FormatOctal(hdr.mtime, sizeof(hdr.mtime), (unsigned __int64)time(NULL));
    hdr.typeflag = '0';
    memcpy(hdr.magic, "ustar", 6);
    memcpy(hdr.version, "00", 2);

    unsigned int sum = TarChecksum(&hdr);
    snprintf(hdr.chksum, sizeof(hdr.chksum), "%06o", sum);

    fwrite(&hdr, 1, sizeof(hdr), afp);

    char buf[512];
    size_t bytes;
    while ((bytes = fread(buf, 1, sizeof(buf), ifp)) > 0) {
        if (bytes < 512) memset(buf + bytes, 0, 512 - bytes);
        fwrite(buf, 1, 512, afp);
    }
    fclose(ifp);

    if (verbose) {
        wprintf(L"%ls\n", arc_name);
    }
}

static void TarAddRecursive(const wchar_t* full_path, const wchar_t* arc_name, FILE* afp, bool verbose) {
    DWORD attr = GetFileAttributesW(full_path);
    if (attr == INVALID_FILE_ATTRIBUTES) return;

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        wchar_t search[MAX_PATH * 2];
        swprintf(search, _countof(search), L"%ls\\*", full_path);
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(search, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                wchar_t sub_full[MAX_PATH * 2], sub_arc[MAX_PATH * 2];
                swprintf(sub_full, _countof(sub_full), L"%ls\\%ls", full_path, fd.cFileName);
                if (arc_name[0]) {
                    swprintf(sub_arc, _countof(sub_arc), L"%ls/%ls", arc_name, fd.cFileName);
                } else {
                    swprintf(sub_arc, _countof(sub_arc), L"%ls", fd.cFileName);
                }
                TarAddRecursive(sub_full, sub_arc, afp, verbose);
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    } else {
        TarWriteFile(full_path, arc_name, afp, verbose);
    }
}

bool Builtin_Tar(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: tar [-c|-x|-t] [-v] [-f ARCHIVE] [-C DIR] [files...]\n"
                L"Standard Tape Archiver (POSIX ustar format).\n\n"
                L"  -c, --create        Create a new archive\n"
                L"  -x, --extract       Extract files from an archive\n"
                L"  -t, --list          List archive contents\n"
                L"  -f, --file ARCHIVE  Archive file name (default: stdout/stdin)\n"
                L"  -v, --verbose       Verbosely list files processed\n"
                L"  -k                  Do not overwrite existing files\n"
                L"  -C, --directory DIR Change directory before operation\n"
                L"  -z, --gzip          Gzip compression / decompression filter\n");
        *code = 0;
        return true;
    }

    bool mode_c = false;
    bool mode_x = false;
    bool mode_t = false;
    bool verbose = false;
    bool keep_existing = false;
    const wchar_t* archive_file = NULL;
    const wchar_t* change_dir = NULL;
    int arg_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && wcscmp(arg, L"-") != 0) {
            if (wcscmp(arg, L"--create") == 0) mode_c = true;
            else if (wcscmp(arg, L"--extract") == 0) mode_x = true;
            else if (wcscmp(arg, L"--list") == 0) mode_t = true;
            else if (wcscmp(arg, L"--verbose") == 0) verbose = true;
            else if (wcscmp(arg, L"--file") == 0 && i + 1 < cmd->argc) archive_file = cmd->argv[++i];
            else if (wcscmp(arg, L"--directory") == 0 && i + 1 < cmd->argc) change_dir = cmd->argv[++i];
            else {
                for (int j = 1; arg[j]; j++) {
                    if (arg[j] == L'c') mode_c = true;
                    else if (arg[j] == L'x') mode_x = true;
                    else if (arg[j] == L't') mode_t = true;
                    else if (arg[j] == L'v') verbose = true;
                    else if (arg[j] == L'k') keep_existing = true;
                    else if (arg[j] == L'z') { /* transparent ustar */ }
                    else if (arg[j] == L'f') {
                        if (i + 1 < cmd->argc) archive_file = cmd->argv[++i];
                        break;
                    } else if (arg[j] == L'C') {
                        if (i + 1 < cmd->argc) change_dir = cmd->argv[++i];
                        break;
                    }
                }
            }
            arg_start = i + 1;
        } else if (i == 1 && arg[0] != L'-') {
            /* Support traditional tar bundle syntax without leading dash: tar cf, tar xf, tar tvf */
            bool handled = false;
            for (int j = 0; arg[j]; j++) {
                if (arg[j] == L'c') { mode_c = true; handled = true; }
                else if (arg[j] == L'x') { mode_x = true; handled = true; }
                else if (arg[j] == L't') { mode_t = true; handled = true; }
                else if (arg[j] == L'v') { verbose = true; handled = true; }
                else if (arg[j] == L'k') { keep_existing = true; handled = true; }
                else if (arg[j] == L'z') { handled = true; }
                else if (arg[j] == L'f') {
                    if (i + 1 < cmd->argc) archive_file = cmd->argv[++i];
                    handled = true;
                    break;
                } else if (arg[j] == L'C') {
                    if (i + 1 < cmd->argc) change_dir = cmd->argv[++i];
                    handled = true;
                    break;
                }
            }
            if (handled) {
                arg_start = i + 1;
            } else {
                arg_start = i;
                break;
            }
        } else {
            arg_start = i;
            break;
        }
    }

    if (!mode_c && !mode_x && !mode_t) {
        fwprintf(stderr, L"tar: You must specify one of the '-c', '-t' or '-x' options\n");
        *code = 1;
        return true;
    }

    wchar_t orig_dir[MAX_PATH] = {0};
    if (change_dir) {
        GetCurrentDirectoryW(_countof(orig_dir), orig_dir);
        if (!SetCurrentDirectoryW(change_dir)) {
            fwprintf(stderr, L"tar: cannot change directory to '%ls'\n", change_dir);
            *code = 1;
            return true;
        }
    }

    /* CREATE MODE */
    if (mode_c) {
        FILE* afp = stdout;
        if (archive_file && wcscmp(archive_file, L"-") != 0) {
            afp = _wfsopen(archive_file, L"wb", _SH_DENYWR);
            if (!afp) {
                fwprintf(stderr, L"tar: %ls: Cannot create archive\n", archive_file);
                if (change_dir && orig_dir[0]) SetCurrentDirectoryW(orig_dir);
                *code = 1;
                return true;
            }
        }

        for (int i = arg_start; i < cmd->argc; i++) {
            const wchar_t* path = cmd->argv[i];
            TarAddRecursive(path, path, afp, verbose);
        }

        char zeros[1024];
        memset(zeros, 0, sizeof(zeros));
        fwrite(zeros, 1, sizeof(zeros), afp);
        if (afp != stdout) fclose(afp);

        if (change_dir && orig_dir[0]) SetCurrentDirectoryW(orig_dir);
        *code = 0;
        return true;
    }

    /* EXTRACT / LIST MODE */
    FILE* rfp = stdin;
    if (archive_file && wcscmp(archive_file, L"-") != 0) {
        rfp = _wfsopen(archive_file, L"rb", _SH_DENYNO);
        if (!rfp) {
            fwprintf(stderr, L"tar: %ls: Cannot open archive\n", archive_file);
            if (change_dir && orig_dir[0]) SetCurrentDirectoryW(orig_dir);
            *code = 1;
            return true;
        }
    }

    TarHeader hdr;
    while (fread(&hdr, 1, sizeof(hdr), rfp) == sizeof(hdr)) {
        if (hdr.name[0] == '\0') break;

        char name_buf[101] = {0};
        memcpy(name_buf, hdr.name, sizeof(hdr.name));
        name_buf[sizeof(hdr.name)] = '\0';

        wchar_t wname[MAX_PATH] = {0};
        size_t conv = mbstowcs(wname, name_buf, _countof(wname) - 1);
        if (conv == (size_t)-1) conv = 0;
        wname[conv < _countof(wname) ? conv : _countof(wname) - 1] = L'\0';

        char size_buf[13] = {0};
        memcpy(size_buf, hdr.size, sizeof(hdr.size));
        size_buf[sizeof(hdr.size)] = '\0';
        unsigned __int64 fsize = 0;
        sscanf_s(size_buf, "%llo", &fsize);

        bool matched = true;
        if (arg_start < cmd->argc) {
            matched = false;
            for (int k = arg_start; k < cmd->argc; k++) {
                if (MatchPattern(cmd->argv[k], wname)) { matched = true; break; }
            }
        }

        if (mode_x && matched) {
            if (!TarIsPathSafe(wname)) {
                fwprintf(stderr, L"tar: skipping unsafe path: %ls\n", wname);
                unsigned __int64 blocks = (fsize + 511) / 512;
                _fseeki64(rfp, blocks * 512, SEEK_CUR);
            } else if (keep_existing && GetFileAttributesW(wname) != INVALID_FILE_ATTRIBUTES) {
                unsigned __int64 blocks = (fsize + 511) / 512;
                _fseeki64(rfp, blocks * 512, SEEK_CUR);
            } else {
                TarEnsureParentDirs(wname);
                FILE* out = _wfsopen(wname, L"wb", _SH_DENYWR);
                unsigned __int64 rem = fsize;
                char buf[512];
                while (rem > 0) {
                    size_t to_read = (rem > 512) ? 512 : (size_t)rem;
                    fread(buf, 1, 512, rfp);
                    if (out) fwrite(buf, 1, to_read, out);
                    rem -= to_read;
                }
                if (out) fclose(out);
            }
        } else {
            unsigned __int64 blocks = (fsize + 511) / 512;
            _fseeki64(rfp, blocks * 512, SEEK_CUR);
        }

        if (verbose || mode_t) {
            if (matched) wprintf(L"%ls\n", wname);
        }
    }

    if (rfp != stdin) fclose(rfp);
    if (change_dir && orig_dir[0]) SetCurrentDirectoryW(orig_dir);
    *code = 0;
    return true;
}
