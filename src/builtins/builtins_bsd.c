#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>
#include <aclapi.h>
#include <time.h>
#include <math.h>

/* ========================================================================= */
/*                          BSD Core Utilities for Windows NT                */
/*      fmt, csplit, chmod, chown, jot, chflags, rs, lockf                   */
/* ========================================================================= */

/* Forward declaration of file recursion helper */
typedef void (*FileCallback)(const wchar_t* path, void* ctx);

static void RecurseDirectoryTree(const wchar_t* dirPath, FileCallback cb, void* ctx) {
    wchar_t searchPath[MAX_PATH * 2];
    swprintf(searchPath, sizeof(searchPath) / sizeof(searchPath[0]), L"%ls\\*", dirPath);

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        wchar_t fullPath[MAX_PATH * 2];
        swprintf(fullPath, sizeof(fullPath) / sizeof(fullPath[0]), L"%ls\\%ls", dirPath, fd.cFileName);

        cb(fullPath, ctx);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            RecurseDirectoryTree(fullPath, cb, ctx);
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

/* ========================================================================= */
/* 1. FMT: BSD Paragraph Formatter                                           */
/* ========================================================================= */

typedef struct {
    int max_width;
    int goal_width;
    bool crown;
    bool split_only;
    bool mail_headers;
} FmtConfig;

static bool IsHeaderLine(const wchar_t* line) {
    if (wcsncmp(line, L"From ", 5) == 0 ||
        wcsncmp(line, L">", 1) == 0 ||
        wcsncmp(line, L"#", 1) == 0)
        return true;
    const wchar_t* colon = wcschr(line, L':');
    if (colon && colon > line && (colon - line) < 30) {
        bool all_ident = true;
        for (const wchar_t* p = line; p < colon; p++) {
            if (!iswalnum(*p) && *p != L'-' && *p != L'_') {
                all_ident = false;
                break;
            }
        }
        if (all_ident) return true;
    }
    return false;
}

static size_t CountIndent(const wchar_t* s) {
    size_t count = 0;
    while (*s == L' ' || *s == L'\t') {
        count += (*s == L'\t') ? 8 : 1;
        s++;
    }
    return count;
}

static void FormatParagraph(wchar_t** lines, size_t line_count, const FmtConfig* cfg) {
    if (line_count == 0) return;

    if (cfg->split_only) {
        for (size_t i = 0; i < line_count; i++) {
            const wchar_t* p = lines[i];
            size_t ind = CountIndent(p);
            while (*p == L' ' || *p == L'\t') p++;

            while (*p) {
                size_t rem = wcslen(p);
                if ((int)(rem + ind) <= cfg->max_width) {
                    for (size_t k = 0; k < ind; k++) putwchar(L' ');
                    wprintf(L"%ls\n", p);
                    break;
                }
                int break_pos = cfg->max_width - (int)ind;
                if (break_pos <= 0) break_pos = 1;
                int space_pos = -1;
                for (int j = 0; j <= break_pos && p[j]; j++) {
                    if (iswspace(p[j])) space_pos = j;
                }
                if (space_pos > 0) {
                    for (size_t k = 0; k < ind; k++) putwchar(L' ');
                    wprintf(L"%.*ls\n", space_pos, p);
                    p += space_pos + 1;
                    while (*p == L' ') p++;
                } else {
                    for (size_t k = 0; k < ind; k++) putwchar(L' ');
                    wprintf(L"%.*ls\n", break_pos, p);
                    p += break_pos;
                }
            }
        }
        return;
    }

    size_t indent1 = CountIndent(lines[0]);
    size_t indent2 = (line_count > 1 && cfg->crown) ? CountIndent(lines[1]) : indent1;

    /* Collect words */
    size_t cap = 256, wcount = 0;
    wchar_t** words = (wchar_t**)malloc(cap * sizeof(wchar_t*));
    if (!words) return;

    for (size_t i = 0; i < line_count; i++) {
        wchar_t* p = lines[i];
        while (*p == L' ' || *p == L'\t') p++;
        while (*p) {
            while (*p && iswspace(*p)) p++;
            if (!*p) break;
            wchar_t* start = p;
            while (*p && !iswspace(*p)) p++;
            size_t wlen = p - start;
            wchar_t* word = (wchar_t*)malloc((wlen + 1) * sizeof(wchar_t));
            if (word) {
                wcsncpy(word, start, wlen);
                word[wlen] = L'\0';
                if (wcount >= cap) {
                    cap *= 2;
                    wchar_t** new_words = (wchar_t**)realloc(words, cap * sizeof(wchar_t*));
                    if (!new_words) {
                        free(word);
                        break;
                    }
                    words = new_words;
                }
                words[wcount++] = word;
            }
        }
    }

    if (wcount == 0) {
        free(words);
        return;
    }

    /* Output wrapped text */
    size_t cur_line = 0;
    size_t cur_col = (cur_line == 0) ? indent1 : indent2;
    for (size_t k = 0; k < cur_col; k++) putwchar(L' ');

    for (size_t i = 0; i < wcount; i++) {
        size_t wlen = wcslen(words[i]);
        if (cur_col + wlen + (cur_col > ((cur_line == 0) ? indent1 : indent2) ? 1 : 0) > (size_t)cfg->max_width) {
            putwchar(L'\n');
            cur_line++;
            cur_col = (cur_line == 0) ? indent1 : indent2;
            for (size_t k = 0; k < cur_col; k++) putwchar(L' ');
        } else if (cur_col > ((cur_line == 0) ? indent1 : indent2)) {
            putwchar(L' ');
            cur_col++;
        }
        wprintf(L"%ls", words[i]);
        cur_col += wlen;
        free(words[i]);
    }
    putwchar(L'\n');
    free(words);
}

static void ProcessFmtStream(FILE* fp, const FmtConfig* cfg) {
    wchar_t line[8192];
    size_t cap = 128, count = 0;
    wchar_t** para = (wchar_t**)malloc(cap * sizeof(wchar_t*));
    if (!para) return;

    while (fgetws(line, sizeof(line) / sizeof(line[0]), fp)) {
        size_t len = wcslen(line);
        while (len > 0 && (line[len - 1] == L'\r' || line[len - 1] == L'\n'))
            line[--len] = L'\0';

        /* Blank line breaks paragraph */
        if (len == 0) {
            if (count > 0) {
                FormatParagraph(para, count, cfg);
                for (size_t i = 0; i < count; i++) free(para[i]);
                count = 0;
            }
            putwchar(L'\n');
            continue;
        }

        /* Mail headers or standalone quotes */
        if (cfg->mail_headers && IsHeaderLine(line)) {
            if (count > 0) {
                FormatParagraph(para, count, cfg);
                for (size_t i = 0; i < count; i++) free(para[i]);
                count = 0;
            }
            wprintf(L"%ls\n", line);
            continue;
        }

        if (count >= cap) {
            cap *= 2;
            wchar_t** new_para = (wchar_t**)realloc(para, cap * sizeof(wchar_t*));
            if (!new_para) {
                break;
            }
            para = new_para;
        }
        wchar_t* dup = _wcsdup(line);
        if (dup) {
            para[count++] = dup;
        }
    }

    if (count > 0) {
        FormatParagraph(para, count, cfg);
        for (size_t i = 0; i < count; i++) free(para[i]);
    }
    free(para);
}

bool Builtin_Fmt(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: fmt [-c] [-m] [-s] [-w width] [file ...]\n"
                L"Simple optimal text and paragraph formatter.\n\n"
                L"  -c          Crown margin (preserve indentation of first two lines)\n"
                L"  -m          Allow paragraph header lines without rejoining\n"
                L"  -s          Split lines only (do not join short lines)\n"
                L"  -w width    Set maximum display width (default: 65)\n"
                L"  -width      Numeric abbreviation for -w width\n");
        *code = 0;
        return true;
    }

    FmtConfig cfg;
    cfg.max_width = 65;
    cfg.goal_width = 55;
    cfg.crown = false;
    cfg.split_only = false;
    cfg.mail_headers = false;

    int file_start = 1;
    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && wcscmp(arg, L"-") != 0) {
            if (iswdigit(arg[1])) {
                cfg.max_width = (int)wcstol(arg + 1, NULL, 10);
                if (cfg.max_width < 10) cfg.max_width = 65;
            } else if (wcscmp(arg, L"-c") == 0) {
                cfg.crown = true;
            } else if (wcscmp(arg, L"-m") == 0) {
                cfg.mail_headers = true;
            } else if (wcscmp(arg, L"-s") == 0) {
                cfg.split_only = true;
            } else if (wcscmp(arg, L"-w") == 0) {
                if (++i < cmd->argc) {
                    cfg.max_width = (int)wcstol(cmd->argv[i], NULL, 10);
                    if (cfg.max_width < 10) cfg.max_width = 65;
                }
            } else if (wcsncmp(arg, L"-w", 2) == 0) {
                cfg.max_width = (int)wcstol(arg + 2, NULL, 10);
                if (cfg.max_width < 10) cfg.max_width = 65;
            } else {
                file_start = i;
                break;
            }
            file_start = i + 1;
        } else {
            file_start = i;
            break;
        }
    }

    if (file_start >= cmd->argc) {
        ProcessFmtStream(stdin, &cfg);
    } else {
        for (int i = file_start; i < cmd->argc; i++) {
            if (wcscmp(cmd->argv[i], L"-") == 0) {
                ProcessFmtStream(stdin, &cfg);
            } else {
                FILE* fp = _wfsopen(cmd->argv[i], L"r", _SH_DENYNO);
                if (!fp) {
                    fwprintf(stderr, L"fmt: %ls: No such file or directory\n", cmd->argv[i]);
                    *code = 1;
                    continue;
                }
                ProcessFmtStream(fp, &cfg);
                fclose(fp);
            }
        }
    }

    *code = 0;
    return true;
}

/* ========================================================================= */
/* 2. CSPLIT: BSD Context Split                                              */
/* ========================================================================= */

typedef struct {
    const wchar_t* prefix;
    int digits;
    int file_num;
    wchar_t created_files[256][MAX_PATH];
    int created_count;
    bool silent;
    wchar_t** lines;
    size_t total_lines;
} CsplitWriter;

static bool CsplitWriteSection(CsplitWriter* cw, size_t from, size_t to) {
    wchar_t out_name[MAX_PATH];
    if (swprintf_s(out_name, _countof(out_name), L"%ls%0*d", cw->prefix, cw->digits, cw->file_num++) < 0) {
        return false;
    }
    FILE* out = _wfsopen(out_name, L"w", _SH_DENYWR);
    if (!out) return false;

    size_t bytes = 0;
    for (size_t l = from; l < to && l < cw->total_lines; l++) {
        fputws(cw->lines[l], out);
        bytes += wcslen(cw->lines[l]);
    }
    fclose(out);

    if (cw->created_count < 256) {
        wcscpy_s(cw->created_files[cw->created_count++], MAX_PATH, out_name);
    }
    if (!cw->silent) {
        wprintf(L"%zu\n", bytes);
    }
    return true;
}

bool Builtin_Csplit(CommandAST* cmd, int* code) {
    if (cmd->argc < 3 || (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: csplit [-k] [-s] [-f prefix] [-n digits] file arg1 [arg2 ...]\n"
                L"Split a file into sections determined by context lines.\n\n"
                L"  -f prefix   Use prefix instead of 'xx'\n"
                L"  -n digits   Use digits decimal places (default: 2)\n"
                L"  -k          Keep created files upon error\n"
                L"  -s, -q      Do not print sizes of output files\n"
                L"  Args can be: LINE_NUM, /REGEXP/[offset], %%REGEXP%%[offset], {REPEAT}\n");
        *code = (cmd->argc < 3) ? 1 : 0;
        return true;
    }

    const wchar_t* prefix = L"xx";
    int digits = 2;
    bool keep_files = false;
    bool silent = false;
    int arg_idx = 1;

    while (arg_idx < cmd->argc && cmd->argv[arg_idx][0] == L'-' && cmd->argv[arg_idx][1] != L'\0' && wcscmp(cmd->argv[arg_idx], L"-") != 0) {
        const wchar_t* opt = cmd->argv[arg_idx];
        if (wcscmp(opt, L"-k") == 0) {
            keep_files = true;
        } else if (wcscmp(opt, L"-s") == 0 || wcscmp(opt, L"-q") == 0) {
            silent = true;
        } else if (wcscmp(opt, L"-f") == 0 && arg_idx + 1 < cmd->argc) {
            prefix = cmd->argv[++arg_idx];
        } else if (wcsncmp(opt, L"-f", 2) == 0) {
            prefix = opt + 2;
        } else if (wcscmp(opt, L"-n") == 0 && arg_idx + 1 < cmd->argc) {
            digits = (int)wcstol(cmd->argv[++arg_idx], NULL, 10);
            if (digits < 1) digits = 2;
            if (digits > 16) digits = 16;
        } else if (wcsncmp(opt, L"-n", 2) == 0) {
            digits = (int)wcstol(opt + 2, NULL, 10);
            if (digits < 1) digits = 2;
            if (digits > 16) digits = 16;
        } else {
            break;
        }
        arg_idx++;
    }

    if (arg_idx >= cmd->argc) {
        fwprintf(stderr, L"csplit: missing file argument\n");
        *code = 1;
        return true;
    }

    const wchar_t* filename = cmd->argv[arg_idx++];
    FILE* fp = (wcscmp(filename, L"-") == 0) ? stdin : _wfsopen(filename, L"r", _SH_DENYNO);
    if (!fp) {
        fwprintf(stderr, L"csplit: %ls: Cannot open file\n", filename);
        *code = 1;
        return true;
    }

    /* Read all lines */
    size_t cap = 512, total_lines = 0;
    wchar_t** lines = (wchar_t**)malloc(cap * sizeof(wchar_t*));
    if (!lines) {
        if (fp != stdin) fclose(fp);
        *code = 1;
        return true;
    }
    wchar_t buf[8192];
    while (fgetws(buf, sizeof(buf) / sizeof(buf[0]), fp)) {
        if (total_lines >= cap) {
            cap *= 2;
            wchar_t** new_lines = (wchar_t**)realloc(lines, cap * sizeof(wchar_t*));
            if (!new_lines) {
                break;
            }
            lines = new_lines;
        }
        wchar_t* dup = _wcsdup(buf);
        if (dup) {
            lines[total_lines++] = dup;
        }
    }
    if (fp != stdin) fclose(fp);

    CsplitWriter cw;
    cw.prefix = prefix;
    cw.digits = digits;
    cw.file_num = 0;
    cw.created_count = 0;
    cw.silent = silent;
    cw.lines = lines;
    cw.total_lines = total_lines;

    size_t cur_line = 0;
    bool error_occurred = false;
    for (int a = arg_idx; a < cmd->argc; a++) {
        const wchar_t* pat = cmd->argv[a];
        int repeat_count = 1;
        bool repeat_inf = false;

        /* Check for repeat suffix {N} or {*} */
        if (a + 1 < cmd->argc && cmd->argv[a + 1][0] == L'{') {
            const wchar_t* rep_str = cmd->argv[a + 1];
            if (wcscmp(rep_str, L"{*}") == 0) {
                repeat_inf = true;
            } else {
                repeat_count = (int)wcstol(rep_str + 1, NULL, 10) + 1;
            }
            a++;
        }

        for (int r = 0; r < repeat_count || repeat_inf; r++) {
            if (cur_line >= total_lines) break;

            if (iswdigit(pat[0])) {
                size_t target_line = (size_t)wcstoul(pat, NULL, 10);
                if (target_line < 1) target_line = 1;
                target_line--; /* 1-indexed to 0-indexed */
                if (target_line <= cur_line || target_line > total_lines) {
                    error_occurred = true;
                    break;
                }
                if (!CsplitWriteSection(&cw, cur_line, target_line)) {
                    error_occurred = true;
                    break;
                }
                cur_line = target_line;
            } else if (pat[0] == L'/' || pat[0] == L'%') {
                bool skip_output = (pat[0] == L'%');
                wchar_t delim = pat[0];
                const wchar_t* end_delim = wcschr(pat + 1, delim);
                if (!end_delim) { error_occurred = true; break; }

                size_t re_len = end_delim - (pat + 1);
                wchar_t* re_str = (wchar_t*)malloc((re_len + 1) * sizeof(wchar_t));
                if (!re_str) { error_occurred = true; break; }
                wcsncpy(re_str, pat + 1, re_len);
                re_str[re_len] = L'\0';

                int offset = 0;
                if (*(end_delim + 1) != L'\0') {
                    offset = (int)wcstol(end_delim + 1, NULL, 10);
                }

                size_t match_line = total_lines;
                for (size_t l = cur_line; l < total_lines; l++) {
                    int off = 0, len = 0;
                    if (GrepRegexMatch(re_str, lines[l], false, false, false, &off, &len)) {
                        match_line = l;
                        break;
                    }
                }
                free(re_str);

                if (match_line == total_lines) {
                    if (repeat_inf) break;
                    error_occurred = true;
                    break;
                }

                size_t split_point = (size_t)((int)match_line + offset);
                if (split_point > total_lines) split_point = total_lines;

                if (!skip_output) {
                    if (!CsplitWriteSection(&cw, cur_line, split_point)) {
                        error_occurred = true;
                        break;
                    }
                }
                cur_line = split_point;
            }
        }

        if (error_occurred) break;
    }

    if (!error_occurred && cur_line < total_lines) {
        CsplitWriteSection(&cw, cur_line, total_lines);
    }

    /* Cleanup on error if -k was not specified */
    if (error_occurred && !keep_files) {
        for (int i = 0; i < cw.created_count; i++) {
            _wremove(cw.created_files[i]);
        }
    }

    for (size_t l = 0; l < total_lines; l++) free(lines[l]);
    free(lines);

    *code = error_occurred ? 1 : 0;
    return true;
}

/* ========================================================================= */
/* 3. CHMOD: BSD File Mode / ReadOnly Attribute Control                      */
/* ========================================================================= */

typedef struct {
    bool recursive;
    bool verbose;
    bool force;
    int octal_mode;
    wchar_t symbolic_mode[64];
    bool is_octal;
} ChmodContext;

static void ApplyChmod(const wchar_t* path, void* ctx) {
    ChmodContext* c = (ChmodContext*)ctx;
    DWORD attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (!c->force) fwprintf(stderr, L"chmod: %ls: No such file or directory\n", path);
        return;
    }

    DWORD new_attrs = attrs;
    if (c->is_octal) {
        /* If write bits (0222) are absent, set READONLY */
        if ((c->octal_mode & 0222) == 0) {
            new_attrs |= FILE_ATTRIBUTE_READONLY;
        } else {
            new_attrs &= ~FILE_ATTRIBUTE_READONLY;
        }
    } else {
        const wchar_t* m = c->symbolic_mode;
        while (*m) {
            wchar_t op = L'\0';
            while (*m && *m != L'+' && *m != L'-' && *m != L'=') m++;
            if (!*m) break;
            op = *m++;
            bool has_w = false;
            while (*m && *m != L',') {
                if (*m == L'w') has_w = true;
                m++;
            }
            if (op == L'+' && has_w) new_attrs &= ~FILE_ATTRIBUTE_READONLY;
            else if (op == L'-' && has_w) new_attrs |= FILE_ATTRIBUTE_READONLY;
            else if (op == L'=' && !has_w) new_attrs |= FILE_ATTRIBUTE_READONLY;
            else if (op == L'=' && has_w) new_attrs &= ~FILE_ATTRIBUTE_READONLY;
            if (*m == L',') m++;
        }
    }

    if (new_attrs != attrs) {
        if (!SetFileAttributesW(path, new_attrs)) {
            if (!c->force) fwprintf(stderr, L"chmod: %ls: Failed to change mode\n", path);
            return;
        }
        if (c->verbose) {
            wprintf(L"mode of '%ls' changed to %04o\n", path, c->is_octal ? c->octal_mode : 0644);
        }
    } else if (c->verbose) {
        wprintf(L"mode of '%ls' retained\n", path);
    }
}

bool Builtin_Chmod(CommandAST* cmd, int* code) {
    if (cmd->argc < 3 || (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: chmod [-R] [-v] [-f] mode file ...\n"
                L"Change file access permissions / attributes on Windows NT.\n\n"
                L"  -R          Recursive directory processing\n"
                L"  -v          Verbose diagnostic output for each file\n"
                L"  -f          Force / suppress error diagnostics\n"
                L"  mode        Octal (e.g. 0755, 644, 444) or symbolic (+w, -w, +x, u=rw)\n");
        *code = (cmd->argc < 3) ? 1 : 0;
        return true;
    }

    ChmodContext ctx;
    ctx.recursive = false;
    ctx.verbose = false;
    ctx.force = false;
    ctx.is_octal = false;
    ctx.octal_mode = 0644;
    ctx.symbolic_mode[0] = L'\0';

    int arg_idx = 1;
    while (arg_idx < cmd->argc && cmd->argv[arg_idx][0] == L'-' &&
           !iswdigit(cmd->argv[arg_idx][1]) && cmd->argv[arg_idx][1] != L'\0') {
        const wchar_t* arg = cmd->argv[arg_idx];
        if (wcscmp(arg, L"-R") == 0) ctx.recursive = true;
        else if (wcscmp(arg, L"-v") == 0) ctx.verbose = true;
        else if (wcscmp(arg, L"-f") == 0) ctx.force = true;
        else break;
        arg_idx++;
    }

    if (arg_idx >= cmd->argc) {
        fwprintf(stderr, L"chmod: missing operand\n");
        *code = 1;
        return true;
    }

    const wchar_t* mode_str = cmd->argv[arg_idx++];
    if (iswdigit(mode_str[0])) {
        ctx.is_octal = true;
        ctx.octal_mode = (int)wcstol(mode_str, NULL, 8);
    } else {
        ctx.is_octal = false;
        wcsncpy_s(ctx.symbolic_mode, _countof(ctx.symbolic_mode), mode_str, _TRUNCATE);
    }

    for (int i = arg_idx; i < cmd->argc; i++) {
        ApplyChmod(cmd->argv[i], &ctx);
        if (ctx.recursive) {
            DWORD attr = GetFileAttributesW(cmd->argv[i]);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                RecurseDirectoryTree(cmd->argv[i], ApplyChmod, &ctx);
            }
        }
    }

    *code = 0;
    return true;
}

/* ========================================================================= */
/* 4. CHOWN: BSD Change File Owner / Group via Windows Security Descriptors  */
/* ========================================================================= */

typedef struct {
    bool recursive;
    bool verbose;
    bool force;
    PSID owner_sid;
    PSID group_sid;
    wchar_t owner_name[128];
    wchar_t group_name[128];
} ChownContext;

static void EnablePrivilege(LPCWSTR privName) {
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp;
        LUID luid;
        if (LookupPrivilegeValueW(NULL, privName, &luid)) {
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Luid = luid;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
        }
        CloseHandle(hToken);
    }
}

static void ApplyChown(const wchar_t* path, void* ctx) {
    ChownContext* c = (ChownContext*)ctx;

    /* Check if already matching current owner */
    PSECURITY_DESCRIPTOR pSD = NULL;
    PSID pCurrentOwner = NULL;
    if (GetNamedSecurityInfoW((LPWSTR)path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &pCurrentOwner, NULL, NULL, NULL, &pSD) == ERROR_SUCCESS) {
        if (c->owner_sid && pCurrentOwner && EqualSid(c->owner_sid, pCurrentOwner)) {
            LocalFree(pSD);
            if (c->verbose) {
                wprintf(L"ownership of '%ls' retained as %ls\n", path, c->owner_name);
            }
            return;
        }
        LocalFree(pSD);
    }

    EnablePrivilege(SE_TAKE_OWNERSHIP_NAME);
    EnablePrivilege(SE_RESTORE_NAME);

    SECURITY_INFORMATION sec_info = 0;
    if (c->owner_sid) sec_info |= OWNER_SECURITY_INFORMATION;
    if (c->group_sid) sec_info |= GROUP_SECURITY_INFORMATION;

    DWORD res = SetNamedSecurityInfoW(
        (LPWSTR)path,
        SE_FILE_OBJECT,
        sec_info,
        c->owner_sid,
        c->group_sid,
        NULL,
        NULL
    );

    if (res != ERROR_SUCCESS) {
        if (!c->force) {
            fwprintf(stderr, L"chown: %ls: Failed to change ownership (error %lu)\n", path, res);
        }
    } else if (c->verbose) {
        wprintf(L"changed ownership of '%ls' to %ls\n", path, c->owner_name);
    }
}

bool Builtin_Chown(CommandAST* cmd, int* code) {
    if (cmd->argc < 3 || (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: chown [-R] [-v] [-f] [-h] [owner][:group] file ...\n"
                L"Change file ownership via Windows NT Security Descriptors.\n\n"
                L"  -R          Recursive directory traversal\n"
                L"  -v          Verbose diagnostic output for each file\n"
                L"  -f          Force / suppress error diagnostics\n"
                L"  -h          Operate on symbolic links rather than targets\n");
        *code = (cmd->argc < 3) ? 1 : 0;
        return true;
    }

    ChownContext ctx;
    ctx.recursive = false;
    ctx.verbose = false;
    ctx.force = false;
    ctx.owner_sid = NULL;
    ctx.group_sid = NULL;
    ctx.owner_name[0] = L'\0';
    ctx.group_name[0] = L'\0';

    int arg_idx = 1;
    while (arg_idx < cmd->argc && cmd->argv[arg_idx][0] == L'-' && cmd->argv[arg_idx][1] != L'\0') {
        const wchar_t* arg = cmd->argv[arg_idx];
        if (wcscmp(arg, L"-R") == 0) ctx.recursive = true;
        else if (wcscmp(arg, L"-v") == 0) ctx.verbose = true;
        else if (wcscmp(arg, L"-f") == 0) ctx.force = true;
        else if (wcscmp(arg, L"-h") == 0) { /* symlink no-op */ }
        else break;
        arg_idx++;
    }

    if (arg_idx >= cmd->argc) {
        fwprintf(stderr, L"chown: missing operand\n");
        *code = 1;
        return true;
    }

    const wchar_t* user_spec = cmd->argv[arg_idx++];
    wchar_t user_buf[128];
    wcsncpy(user_buf, user_spec, 127);
    user_buf[127] = L'\0';

    wchar_t* colon = wcschr(user_buf, L':');
    if (!colon) colon = wcschr(user_buf, L'.');

    if (colon) {
        *colon = L'\0';
        wcsncpy_s(ctx.group_name, _countof(ctx.group_name), colon + 1, _TRUNCATE);
    }
    wcsncpy_s(ctx.owner_name, _countof(ctx.owner_name), user_buf, _TRUNCATE);

    BYTE sidBufferOwner[SECURITY_MAX_SID_SIZE];
    BYTE sidBufferGroup[SECURITY_MAX_SID_SIZE];
    DWORD sidSizeOwner = sizeof(sidBufferOwner);
    DWORD sidSizeGroup = sizeof(sidBufferGroup);
    wchar_t domainOwner[256], domainGroup[256];
    DWORD domainSizeOwner = 256, domainSizeGroup = 256;
    SID_NAME_USE sidType;

    if (ctx.owner_name[0] != L'\0') {
        if (LookupAccountNameW(NULL, ctx.owner_name, (PSID)sidBufferOwner, &sidSizeOwner, domainOwner, &domainSizeOwner, &sidType)) {
            ctx.owner_sid = (PSID)sidBufferOwner;
        } else if (!ctx.force) {
            fwprintf(stderr, L"chown: invalid user '%ls'\n", ctx.owner_name);
        }
    }

    if (ctx.group_name[0] != L'\0') {
        if (LookupAccountNameW(NULL, ctx.group_name, (PSID)sidBufferGroup, &sidSizeGroup, domainGroup, &domainSizeGroup, &sidType)) {
            ctx.group_sid = (PSID)sidBufferGroup;
        }
    }

    for (int i = arg_idx; i < cmd->argc; i++) {
        ApplyChown(cmd->argv[i], &ctx);
        if (ctx.recursive) {
            DWORD attr = GetFileAttributesW(cmd->argv[i]);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                RecurseDirectoryTree(cmd->argv[i], ApplyChown, &ctx);
            }
        }
    }

    *code = 0;
    return true;
}

/* ========================================================================= */
/* 5. JOT: BSD Sequence, Random, and Character Generator                     */
/* ========================================================================= */

bool Builtin_Jot(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: jot [options] [reps [begin [end [s]]]]\n"
                L"Generate sequential, random, or character data (BSD jot).\n\n"
                L"  -r          Generate random data within range\n"
                L"  -b word     Print word repeatedly instead of numbers\n"
                L"  -w format   Custom printf format string (e.g. %%d, 0x%%02x)\n"
                L"  -c          Character mode (abbreviation for -w %%c)\n"
                L"  -s string   Separator string between items (default: newline)\n"
                L"  -n          Do not print trailing separator / newline\n"
                L"  -p prec     Set precision for floating-point output\n");
        *code = 0;
        return true;
    }

    bool random_mode = false;
    const wchar_t* word = NULL;
    const wchar_t* format = NULL;
    bool char_mode = false;
    const wchar_t* sep = L"\n";
    bool no_final_newline = false;
    int precision = -1;

    int arg_idx = 1;
    while (arg_idx < cmd->argc && cmd->argv[arg_idx][0] == L'-' &&
           !iswdigit(cmd->argv[arg_idx][1]) && cmd->argv[arg_idx][1] != L'\0') {
        const wchar_t* opt = cmd->argv[arg_idx];
        if (wcscmp(opt, L"-r") == 0) random_mode = true;
        else if (wcscmp(opt, L"-c") == 0) char_mode = true;
        else if (wcscmp(opt, L"-n") == 0) no_final_newline = true;
        else if (wcscmp(opt, L"-b") == 0 && arg_idx + 1 < cmd->argc) word = cmd->argv[++arg_idx];
        else if (wcscmp(opt, L"-w") == 0 && arg_idx + 1 < cmd->argc) format = cmd->argv[++arg_idx];
        else if (wcscmp(opt, L"-s") == 0 && arg_idx + 1 < cmd->argc) sep = cmd->argv[++arg_idx];
        else if (wcscmp(opt, L"-p") == 0 && arg_idx + 1 < cmd->argc) precision = (int)wcstol(cmd->argv[++arg_idx], NULL, 10);
        else break;
        arg_idx++;
    }

    int pos_args = cmd->argc - arg_idx;
    long reps = (pos_args > 0) ? wcstol(cmd->argv[arg_idx], NULL, 10) : 100;
    double begin = (pos_args > 1) ? wcstod(cmd->argv[arg_idx + 1], NULL) : 1.0;
    double end = (pos_args > 2) ? wcstod(cmd->argv[arg_idx + 2], NULL) : (pos_args > 1 ? begin + reps - 1 : 100.0);
    double step = (pos_args > 3) ? wcstod(cmd->argv[arg_idx + 3], NULL) : 0.0;

    if (pos_args == 1 && reps > 0) {
        begin = 1.0;
        end = (double)reps;
    }

    if (reps == 0) {
        if (step != 0.0) {
            reps = (long)(fabs(end - begin) / fabs(step)) + 1;
        } else {
            reps = 100;
        }
    }

    if (step == 0.0 && reps > 1) {
        step = (end - begin) / (double)(reps - 1);
    }

    if (random_mode) {
        static bool seeded = false;
        if (!seeded) {
            srand((unsigned int)time(NULL) ^ GetCurrentProcessId());
            seeded = true;
        }
    }

    bool is_float = (precision > 0 || (pos_args > 1 && (wcschr(cmd->argv[arg_idx + 1], L'.') != NULL)) ||
                     (pos_args > 2 && (wcschr(cmd->argv[arg_idx + 2], L'.') != NULL)));

    double cur = begin;
    for (long i = 0; i < reps; i++) {
        if (word) {
            wprintf(L"%ls", word);
        } else if (char_mode) {
            wchar_t ch = random_mode ? (wchar_t)((rand() % (int)(end - begin + 1)) + (int)begin) : (wchar_t)(int)cur;
            putwchar(ch);
        } else if (format) {
            if (wcsstr(format, L"%f") || wcsstr(format, L"%g") || wcsstr(format, L"%e")) {
                double val = random_mode ? (begin + ((double)rand() / (double)RAND_MAX) * (end - begin)) : cur;
                wprintf(format, val);
            } else {
                long val = random_mode ? (long)((rand() % (long)(end - begin + 1)) + (long)begin) : (long)cur;
                wprintf(format, val);
            }
        } else if (is_float) {
            double val = random_mode ? (begin + ((double)rand() / (double)RAND_MAX) * (end - begin)) : cur;
            if (precision >= 0) wprintf(L"%.*f", precision, val);
            else wprintf(L"%g", val);
        } else {
            long val = random_mode ? (long)((rand() % (long)(end - begin + 1)) + (long)begin) : (long)cur;
            wprintf(L"%ld", val);
        }

        if (i + 1 < reps) {
            wprintf(L"%ls", sep);
        }
        cur += step;
    }

    if (!no_final_newline) {
        putwchar(L'\n');
    }

    *code = 0;
    return true;
}

/* ========================================================================= */
/* 6. CHFLAGS: BSD File Flags mapped to Windows NT Attributes                */
/* ========================================================================= */

typedef struct {
    bool recursive;
    bool verbose;
    bool force;
    DWORD set_mask;
    DWORD clear_mask;
} ChflagsContext;

static void ApplyChflags(const wchar_t* path, void* ctx) {
    ChflagsContext* c = (ChflagsContext*)ctx;
    DWORD attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (!c->force) fwprintf(stderr, L"chflags: %ls: No such file or directory\n", path);
        return;
    }

    DWORD new_attrs = (attrs | c->set_mask) & ~c->clear_mask;
    if (new_attrs != attrs) {
        if (!SetFileAttributesW(path, new_attrs)) {
            if (!c->force) fwprintf(stderr, L"chflags: %ls: Failed to set attributes\n", path);
            return;
        }
        if (c->verbose) {
            wprintf(L"flags of '%ls' changed\n", path);
        }
    } else if (c->verbose) {
        wprintf(L"flags of '%ls' retained\n", path);
    }
}

bool Builtin_Chflags(CommandAST* cmd, int* code) {
    if (cmd->argc < 3 || (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: chflags [-R] [-v] [-f] [-h] flags file ...\n"
                L"Change file flags mapped to Windows NT file attributes.\n\n"
                L"  -R          Recursive directory processing\n"
                L"  -v          Verbose diagnostic output for each file\n"
                L"  -f          Force / suppress error diagnostics\n"
                L"  -h          Operate on symbolic links themselves\n\n"
                L"Supported Flags:\n"
                L"  hidden / nohidden       Windows FILE_ATTRIBUTE_HIDDEN\n"
                L"  arch / noarch           Windows FILE_ATTRIBUTE_ARCHIVE\n"
                L"  uchg / nouchg / readonly Windows FILE_ATTRIBUTE_READONLY\n"
                L"  system / nosystem       Windows FILE_ATTRIBUTE_SYSTEM\n"
                L"  temp / notemp           Windows FILE_ATTRIBUTE_TEMPORARY\n");
        *code = (cmd->argc < 3) ? 1 : 0;
        return true;
    }

    ChflagsContext ctx;
    ctx.recursive = false;
    ctx.verbose = false;
    ctx.force = false;
    ctx.set_mask = 0;
    ctx.clear_mask = 0;

    int arg_idx = 1;
    while (arg_idx < cmd->argc && cmd->argv[arg_idx][0] == L'-' && cmd->argv[arg_idx][1] != L'\0') {
        const wchar_t* arg = cmd->argv[arg_idx];
        if (wcscmp(arg, L"-R") == 0) ctx.recursive = true;
        else if (wcscmp(arg, L"-v") == 0) ctx.verbose = true;
        else if (wcscmp(arg, L"-f") == 0) ctx.force = true;
        else if (wcscmp(arg, L"-h") == 0) { /* symlink */ }
        else break;
        arg_idx++;
    }

    if (arg_idx >= cmd->argc) {
        fwprintf(stderr, L"chflags: missing flags operand\n");
        *code = 1;
        return true;
    }

    const wchar_t* flags_str = cmd->argv[arg_idx++];
    wchar_t fbuf[256];
    wcsncpy(fbuf, flags_str, 255);
    fbuf[255] = L'\0';

    wchar_t* tok_ctx = NULL;
    wchar_t* token = wcstok(fbuf, L", ", &tok_ctx);
    while (token) {
        if (_wcsicmp(token, L"hidden") == 0) ctx.set_mask |= FILE_ATTRIBUTE_HIDDEN;
        else if (_wcsicmp(token, L"nohidden") == 0) ctx.clear_mask |= FILE_ATTRIBUTE_HIDDEN;
        else if (_wcsicmp(token, L"arch") == 0 || _wcsicmp(token, L"archive") == 0) ctx.set_mask |= FILE_ATTRIBUTE_ARCHIVE;
        else if (_wcsicmp(token, L"noarch") == 0 || _wcsicmp(token, L"noarchive") == 0) ctx.clear_mask |= FILE_ATTRIBUTE_ARCHIVE;
        else if (_wcsicmp(token, L"uchg") == 0 || _wcsicmp(token, L"readonly") == 0 || _wcsicmp(token, L"rdonly") == 0) ctx.set_mask |= FILE_ATTRIBUTE_READONLY;
        else if (_wcsicmp(token, L"nouchg") == 0 || _wcsicmp(token, L"noreadonly") == 0 || _wcsicmp(token, L"nordonly") == 0) ctx.clear_mask |= FILE_ATTRIBUTE_READONLY;
        else if (_wcsicmp(token, L"system") == 0) ctx.set_mask |= FILE_ATTRIBUTE_SYSTEM;
        else if (_wcsicmp(token, L"nosystem") == 0) ctx.clear_mask |= FILE_ATTRIBUTE_SYSTEM;
        else if (_wcsicmp(token, L"temp") == 0 || _wcsicmp(token, L"temporary") == 0) ctx.set_mask |= FILE_ATTRIBUTE_TEMPORARY;
        else if (_wcsicmp(token, L"notemp") == 0 || _wcsicmp(token, L"notemporary") == 0) ctx.clear_mask |= FILE_ATTRIBUTE_TEMPORARY;
        token = wcstok(NULL, L", ", &tok_ctx);
    }

    for (int i = arg_idx; i < cmd->argc; i++) {
        ApplyChflags(cmd->argv[i], &ctx);
        if (ctx.recursive) {
            DWORD attr = GetFileAttributesW(cmd->argv[i]);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                RecurseDirectoryTree(cmd->argv[i], ApplyChflags, &ctx);
            }
        }
    }

    *code = 0;
    return true;
}

/* ========================================================================= */
/* 7. RS: BSD Reshape Data Array / Transpose Utility                         */
/* ========================================================================= */

bool Builtin_Rs(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: rs [options] [rows [cols]]\n"
                L"Reshape a data array / transpose rows and columns (BSD rs).\n\n"
                L"  -t, -T      Transpose rows and columns\n"
                L"  -c[x]       Input column delimiter character (default: space/tab)\n"
                L"  -C[x]       Output column delimiter character (default: space)\n"
                L"  -w width    Set maximum line width (default: 80)\n"
                L"  -g gutter   Gutter space between output columns (default: 2)\n"
                L"  -j          Right-justify column values\n"
                L"  -z          Squeeze empty delimiter runs\n"
                L"  -e          Consider each input line as a complete row\n");
        *code = 0;
        return true;
    }

    bool transpose = false;
    wchar_t in_delim = L'\0';
    wchar_t out_delim = L' ';
    int width = 80;
    int gutter = 2;
    bool right_justify = false;
    bool line_as_row = false;

    int arg_idx = 1;
    while (arg_idx < cmd->argc && cmd->argv[arg_idx][0] == L'-' &&
           !iswdigit(cmd->argv[arg_idx][1]) && cmd->argv[arg_idx][1] != L'\0') {
        const wchar_t* opt = cmd->argv[arg_idx];
        if (wcscmp(opt, L"-t") == 0 || wcscmp(opt, L"-T") == 0) transpose = true;
        else if (wcscmp(opt, L"-j") == 0) right_justify = true;
        else if (wcscmp(opt, L"-e") == 0) line_as_row = true;
        else if (wcsncmp(opt, L"-c", 2) == 0) in_delim = (opt[2] != L'\0') ? opt[2] : L'\0';
        else if (wcsncmp(opt, L"-C", 2) == 0) out_delim = (opt[2] != L'\0') ? opt[2] : L' ';
        else if (wcscmp(opt, L"-w") == 0 && arg_idx + 1 < cmd->argc) width = (int)wcstol(cmd->argv[++arg_idx], NULL, 10);
        else if (wcsncmp(opt, L"-w", 2) == 0) width = (int)wcstol(opt + 2, NULL, 10);
        else if (wcscmp(opt, L"-g") == 0 && arg_idx + 1 < cmd->argc) gutter = (int)wcstol(cmd->argv[++arg_idx], NULL, 10);
        else if (wcsncmp(opt, L"-g", 2) == 0) gutter = (int)wcstol(opt + 2, NULL, 10);
        else break;
        arg_idx++;
    }

    int req_rows = 0, req_cols = 0;
    if (arg_idx < cmd->argc) req_rows = (int)wcstol(cmd->argv[arg_idx++], NULL, 10);
    if (arg_idx < cmd->argc) req_cols = (int)wcstol(cmd->argv[arg_idx++], NULL, 10);

    /* Read all items from stdin */
    size_t cap = 256, count = 0;
    wchar_t** items = (wchar_t**)malloc(cap * sizeof(wchar_t*));
    if (!items) { *code = 1; return true; }

    wchar_t line[8192];
    while (fgetws(line, sizeof(line) / sizeof(line[0]), stdin)) {
        size_t len = wcslen(line);
        while (len > 0 && (line[len - 1] == L'\r' || line[len - 1] == L'\n')) line[--len] = L'\0';
        if (len == 0 && !line_as_row) continue;

        if (line_as_row) {
            if (count >= cap) { cap *= 2; items = (wchar_t**)realloc(items, cap * sizeof(wchar_t*)); }
            items[count++] = _wcsdup(line);
        } else if (in_delim != L'\0') {
            wchar_t* p = line;
            while (*p) {
                wchar_t* next = wcschr(p, in_delim);
                if (next) *next = L'\0';
                if (count >= cap) { cap *= 2; items = (wchar_t**)realloc(items, cap * sizeof(wchar_t*)); }
                items[count++] = _wcsdup(p);
                if (!next) break;
                p = next + 1;
            }
        } else {
            wchar_t* p = line;
            while (*p) {
                while (*p && iswspace(*p)) p++;
                if (!*p) break;
                wchar_t* start = p;
                while (*p && !iswspace(*p)) p++;
                size_t wlen = p - start;
                wchar_t* item = (wchar_t*)malloc((wlen + 1) * sizeof(wchar_t));
                if (item) {
                    wcsncpy(item, start, wlen);
                    item[wlen] = L'\0';
                    if (count >= cap) {
                        cap *= 2;
                        wchar_t** new_items = (wchar_t**)realloc(items, cap * sizeof(wchar_t*));
                        if (!new_items) {
                            free(item);
                            break;
                        }
                        items = new_items;
                    }
                    items[count++] = item;
                }
            }
        }
    }

    if (count == 0) {
        free(items);
        *code = 0;
        return true;
    }

    int rows = req_rows;
    int cols = req_cols;
    if (rows == 0 && cols == 0) {
        int max_len = 0;
        for (size_t i = 0; i < count; i++) {
            int l = (int)wcslen(items[i]);
            if (l > max_len) max_len = l;
        }
        int est_cols = (width + gutter) / (max_len + gutter > 0 ? max_len + gutter : 1);
        if (est_cols < 1) est_cols = 1;
        cols = est_cols;
        rows = (int)((count + cols - 1) / cols);
    } else if (rows > 0 && cols == 0) {
        cols = (int)((count + rows - 1) / rows);
    } else if (cols > 0 && rows == 0) {
        rows = (int)((count + cols - 1) / cols);
    }

    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    if (transpose) {
        int tmp = rows;
        rows = cols;
        cols = tmp;
    }

    /* Compute column widths */
    int* col_widths = (int*)calloc(cols, sizeof(int));
    if (!col_widths) {
        for (size_t i = 0; i < count; i++) free(items[i]);
        free(items);
        *code = 1;
        return true;
    }
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            size_t idx = transpose ? (size_t)(c * rows + r) : (size_t)(r * cols + c);
            if (idx < count) {
                int item_len = (int)wcslen(items[idx]);
                if (item_len > col_widths[c]) col_widths[c] = item_len;
            }
        }
    }

    /* Print matrix */
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            size_t idx = transpose ? (size_t)(c * rows + r) : (size_t)(r * cols + c);
            const wchar_t* str = (idx < count) ? items[idx] : L"";
            int pad = col_widths[c] - (int)wcslen(str);

            if (c > 0) {
                for (int g = 0; g < gutter; g++) putwchar(out_delim);
            }

            if (right_justify) {
                for (int k = 0; k < pad; k++) putwchar(L' ');
                wprintf(L"%ls", str);
            } else {
                wprintf(L"%ls", str);
                if (c + 1 < cols) {
                    for (int k = 0; k < pad; k++) putwchar(L' ');
                }
            }
        }
        putwchar(L'\n');
    }

    for (size_t i = 0; i < count; i++) free(items[i]);
    free(items);
    free(col_widths);

    *code = 0;
    return true;
}

/* ========================================================================= */
/* 8. LOCKF: BSD Command Execution with Advisory/Exclusive File Locking      */
/* ========================================================================= */

bool Builtin_Lockf(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    if (cmd->argc < 3 || (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: lockf [-kns] [-t seconds] file command [args...]\n"
                L"Execute a command while holding an exclusive/shared lock on a file (BSD lockf).\n\n"
                L"  -k          Keep lock open across command termination\n"
                L"  -s          Acquire shared lock (default: exclusive)\n"
                L"  -n          Non-blocking: fail immediately if locked\n"
                L"  -t seconds  Timeout in seconds waiting to acquire lock\n");
        *code = (cmd->argc < 3) ? 1 : 0;
        return true;
    }

    bool keep_lock = false;
    bool shared = false;
    bool nonblocking = false;
    int timeout_sec = -1;

    int arg_idx = 1;
    while (arg_idx < cmd->argc && cmd->argv[arg_idx][0] == L'-' && cmd->argv[arg_idx][1] != L'\0') {
        const wchar_t* opt = cmd->argv[arg_idx];
        if (wcscmp(opt, L"-k") == 0) keep_lock = true;
        else if (wcscmp(opt, L"-s") == 0) shared = true;
        else if (wcscmp(opt, L"-n") == 0) nonblocking = true;
        else if (wcscmp(opt, L"-t") == 0 && arg_idx + 1 < cmd->argc) {
            timeout_sec = (int)wcstol(cmd->argv[++arg_idx], NULL, 10);
        } else if (wcsncmp(opt, L"-t", 2) == 0) {
            timeout_sec = (int)wcstol(opt + 2, NULL, 10);
        } else {
            break;
        }
        arg_idx++;
    }

    if (arg_idx + 1 >= cmd->argc) {
        fwprintf(stderr, L"lockf: missing file or command\n");
        *code = 1;
        return true;
    }

    const wchar_t* lock_file = cmd->argv[arg_idx++];
    HANDLE hFile = CreateFileW(
        lock_file,
        GENERIC_READ | (shared ? 0 : GENERIC_WRITE),
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        hFile = CreateFileW(
            lock_file,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
    }

    if (hFile == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"lockf: %ls: Cannot open lock file\n", lock_file);
        *code = 1;
        return true;
    }

    DWORD lock_flags = (shared ? 0 : LOCKFILE_EXCLUSIVE_LOCK) | (nonblocking ? LOCKFILE_FAIL_IMMEDIATELY : 0);
    OVERLAPPED ov;
    memset(&ov, 0, sizeof(ov));

    bool locked = false;
    DWORD start_time = GetTickCount();
    while (true) {
        if (LockFileEx(hFile, lock_flags | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &ov)) {
            locked = true;
            break;
        }

        if (nonblocking) break;

        if (timeout_sec >= 0) {
            DWORD elapsed = (GetTickCount() - start_time) / 1000;
            if ((int)elapsed >= timeout_sec) break;
        }

        Sleep(50);
    }

    if (!locked) {
        fwprintf(stderr, L"lockf: %ls: already locked\n", lock_file);
        CloseHandle(hFile);
        *code = 1;
        return true;
    }

    int child_exit = 0;
    if (shell) {
        wchar_t full_cmd[4096] = {0};
        for (int i = arg_idx; i < cmd->argc; i++) {
            if (!Ksh_AppendShellWord(full_cmd, sizeof(full_cmd) / sizeof(full_cmd[0]), cmd->argv[i])) {
                fwprintf(stderr, L"lockf: command too long\n");
                child_exit = 1;
                break;
            }
        }
        if (child_exit == 0) {
            child_exit = shell->ExecuteLine(shell, full_cmd);
        }
    } else {
        /* Fallback to CreateProcess with proper Win32 escaping */
        wchar_t* cmdline = BuildWin32CommandLine(cmd->argv + arg_idx, cmd->argc - arg_idx);
        if (!cmdline) {
            if (!keep_lock) {
                UnlockFileEx(hFile, 0, MAXDWORD, MAXDWORD, &ov);
                CloseHandle(hFile);
            }
            *code = 1;
            return true;
        }

        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        memset(&pi, 0, sizeof(pi));

        DWORD proc_exit = 1;
        if (CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            GetExitCodeProcess(pi.hProcess, &proc_exit);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            child_exit = (int)proc_exit;
        } else {
            fwprintf(stderr, L"lockf: %ls: execution failed\n", cmd->argv[arg_idx]);
            child_exit = 1;
        }
        free(cmdline);
    }

    if (!keep_lock) {
        UnlockFileEx(hFile, 0, MAXDWORD, MAXDWORD, &ov);
        CloseHandle(hFile);
    }

    *code = (int)child_exit;
    return true;
}
