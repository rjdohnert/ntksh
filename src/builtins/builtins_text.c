#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

/* ========================================================================= */
/*               Category 1: AT&T ksh93 libcmd & POSIX Text Built-ins         */
/* ========================================================================= */

typedef struct {
    int start;
    int end;
} RangeDef;

static int ParseRangeList(const wchar_t* str, RangeDef* ranges, int max_ranges) {
    int count = 0;
    const wchar_t* p = str;
    while (*p && count < max_ranges) {
        while (*p == L' ' || *p == L',') p++;
        if (!*p) break;
        int s = 1, e = INT_MAX;
        if (*p == L'-') {
            p++;
            e = (int)wcstol(p, (wchar_t**)&p, 10);
            if (e < 1) e = INT_MAX;
        } else {
            s = (int)wcstol(p, (wchar_t**)&p, 10);
            if (s < 1) s = 1;
            if (*p == L'-') {
                p++;
                if (iswdigit(*p)) {
                    e = (int)wcstol(p, (wchar_t**)&p, 10);
                } else {
                    e = INT_MAX;
                }
            } else {
                e = s;
            }
        }
        ranges[count].start = s;
        ranges[count].end = e;
        count++;
        while (*p && *p != L',') p++;
        if (*p == L',') p++;
    }
    return count;
}

static bool InRangeList(int idx, const RangeDef* ranges, int count, bool complement) {
    bool inside = false;
    for (int i = 0; i < count; i++) {
        if (idx >= ranges[i].start && idx <= ranges[i].end) {
            inside = true;
            break;
        }
    }
    return complement ? !inside : inside;
}

bool Builtin_Cut(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: cut -b LIST | -c LIST | -f LIST [-d DELIM] [-s] [--complement] [FILE...]\n"
                L"Prints selected parts of lines from each FILE to standard output.\n\n"
                L"  -b, -c LIST   Select only these bytes / characters\n"
                L"  -f LIST       Select only these fields\n"
                L"  -d DELIM      Use DELIM instead of TAB for field delimiter\n"
                L"  -s            Do not print lines not containing delimiters\n"
                L"  --complement  Complement the set of selected bytes, characters, or fields\n");
        *code = 0;
        return true;
    }

    enum { MODE_NONE, MODE_CHARS, MODE_FIELDS } mode = MODE_NONE;
    RangeDef ranges[64];
    int num_ranges = 0;
    wchar_t delim = L'\t';
    bool suppress_no_delim = false;
    bool complement = false;
    int file_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && wcscmp(arg, L"-") != 0) {
            if (wcscmp(arg, L"-s") == 0) {
                suppress_no_delim = true;
            } else if (wcscmp(arg, L"--complement") == 0) {
                complement = true;
            } else if (wcsncmp(arg, L"-d", 2) == 0) {
                if (arg[2] != L'\0') {
                    delim = arg[2];
                } else if (i + 1 < cmd->argc) {
                    delim = cmd->argv[++i][0];
                }
            } else if (wcsncmp(arg, L"-f", 2) == 0) {
                mode = MODE_FIELDS;
                const wchar_t* rstr = (arg[2] != L'\0') ? arg + 2 : ((i + 1 < cmd->argc) ? cmd->argv[++i] : L"");
                num_ranges = ParseRangeList(rstr, ranges, 64);
            } else if (wcsncmp(arg, L"-c", 2) == 0 || wcsncmp(arg, L"-b", 2) == 0) {
                mode = MODE_CHARS;
                const wchar_t* rstr = (arg[2] != L'\0') ? arg + 2 : ((i + 1 < cmd->argc) ? cmd->argv[++i] : L"");
                num_ranges = ParseRangeList(rstr, ranges, 64);
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

    if (mode == MODE_NONE) {
        fwprintf(stderr, L"cut: you must specify a list of bytes, characters, or fields\n");
        *code = 1;
        return true;
    }

    int files_count = cmd->argc - file_start;
    const wchar_t* dummy_stdin = L"-";
    const wchar_t* const* files = (files_count > 0) ? (const wchar_t* const*)(cmd->argv + file_start) : &dummy_stdin;
    if (files_count <= 0) files_count = 1;

    wchar_t line[4096];
    for (int fi = 0; fi < files_count; fi++) {
        FILE* fp = NULL;
        bool is_stdin = (wcscmp(files[fi], L"-") == 0);
        if (is_stdin) {
            fp = stdin;
        } else {
            fp = _wfsopen(files[fi], L"rt, ccs=UTF-8", _SH_DENYNO);
            if (!fp) fp = _wfsopen(files[fi], L"r", _SH_DENYNO);
            if (!fp) {
                fwprintf(stderr, L"cut: %ls: No such file or directory\n", files[fi]);
                continue;
            }
        }

        while (fgetws(line, sizeof(line) / sizeof(line[0]), fp)) {
            size_t len = wcslen(line);
            while (len > 0 && (line[len - 1] == L'\r' || line[len - 1] == L'\n')) {
                line[--len] = L'\0';
            }

            if (mode == MODE_CHARS) {
                for (int ci = 0; ci < (int)len; ci++) {
                    if (InRangeList(ci + 1, ranges, num_ranges, complement)) {
                        fputwc(line[ci], stdout);
                    }
                }
                fputwc(L'\n', stdout);
            } else {
                wchar_t* has_delim = wcschr(line, delim);
                if (!has_delim) {
                    if (!suppress_no_delim) {
                        wprintf(L"%ls\n", line);
                    }
                    continue;
                }

                int field_idx = 1;
                bool first_out = true;
                wchar_t* cur = line;
                while (cur) {
                    wchar_t* next = wcschr(cur, delim);
                    if (next) *next = L'\0';

                    if (InRangeList(field_idx, ranges, num_ranges, complement)) {
                        if (!first_out) fputwc(delim, stdout);
                        wprintf(L"%ls", cur);
                        first_out = false;
                    }

                    cur = next ? next + 1 : NULL;
                    field_idx++;
                }
                fputwc(L'\n', stdout);
            }
        }

        if (!is_stdin && fp) fclose(fp);
    }

    *code = 0;
    return true;
}

static wchar_t* ExpandTrSet(const wchar_t* src, int* out_len) {
    wchar_t* res = (wchar_t*)malloc(sizeof(wchar_t) * 65536);
    if (!res) return NULL;
    int len = 0;
    for (int i = 0; src[i] && len < 65535; i++) {
        wchar_t c = src[i];
        if (c == L'\\' && src[i + 1]) {
            i++;
            switch (src[i]) {
                case L'n': c = L'\n'; break;
                case L't': c = L'\t'; break;
                case L'r': c = L'\r'; break;
                case L'a': c = L'\a'; break;
                case L'b': c = L'\b'; break;
                case L'f': c = L'\f'; break;
                case L'v': c = L'\v'; break;
                case L'0': c = L'\0'; break;
                default: c = src[i]; break;
            }
            res[len++] = c;
        } else if (src[i + 1] == L'-' && src[i + 2]) {
            wchar_t start = src[i];
            wchar_t end = src[i + 2];
            i += 2;
            if (start <= end) {
                for (wchar_t ch = start; ch <= end && len < 65535; ch++) {
                    res[len++] = ch;
                }
            } else {
                res[len++] = start;
            }
        } else {
            res[len++] = c;
        }
    }
    res[len] = L'\0';
    if (out_len) *out_len = len;
    return res;
}

bool Builtin_Tr(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: tr [OPTION]... SET1 [SET2]\n"
                L"Translate, squeeze, and/or delete characters from standard input.\n\n"
                L"  -c, -C        Complement SET1\n"
                L"  -d            Delete characters in SET1, do not translate\n"
                L"  -s            Squeeze repeated characters in SET1 (or translated SET2)\n"
                L"  -t            Truncate SET1 to length of SET2\n");
        *code = 0;
        return true;
    }

    bool complement = false;
    bool del_mode = false;
    bool squeeze = false;
    bool truncate = false;
    int arg_idx = 1;

    while (arg_idx < cmd->argc && cmd->argv[arg_idx][0] == L'-' && cmd->argv[arg_idx][1] != L'\0') {
        const wchar_t* p = cmd->argv[arg_idx] + 1;
        while (*p) {
            if (*p == L'c' || *p == L'C') complement = true;
            else if (*p == L'd') del_mode = true;
            else if (*p == L's') squeeze = true;
            else if (*p == L't') truncate = true;
            p++;
        }
        arg_idx++;
    }

    if (arg_idx >= cmd->argc) {
        fwprintf(stderr, L"tr: missing operand\n");
        *code = 1;
        return true;
    }

    int set1_len = 0, set2_len = 0;
    wchar_t* set1 = ExpandTrSet(cmd->argv[arg_idx++], &set1_len);
    wchar_t* set2 = (arg_idx < cmd->argc) ? ExpandTrSet(cmd->argv[arg_idx++], &set2_len) : NULL;

    if (!set1) {
        *code = 1;
        return true;
    }

    if (truncate && set2 && set1_len > set2_len) {
        set1_len = set2_len;
        set1[set1_len] = L'\0';
    }

    wint_t wch;
    wint_t prev_out = WEOF;

    while ((wch = fgetwc(stdin)) != WEOF) {
        wchar_t ch = (wchar_t)wch;
        int found_idx = -1;
        for (int i = 0; i < set1_len; i++) {
            if (set1[i] == ch) {
                found_idx = i;
                break;
            }
        }

        bool in_set1 = (found_idx != -1);
        if (complement) in_set1 = !in_set1;

        if (del_mode) {
            if (in_set1) continue;
            if (squeeze && set2) {
                bool in_s2 = false;
                for (int i = 0; i < set2_len; i++) {
                    if (set2[i] == ch) { in_s2 = true; break; }
                }
                if (in_s2 && ch == prev_out) continue;
            }
            fputwc(ch, stdout);
            prev_out = ch;
        } else {
            wchar_t out_ch = ch;
            if (in_set1 && set2 && set2_len > 0) {
                int map_idx = (found_idx >= 0 && found_idx < set2_len) ? found_idx : (set2_len - 1);
                out_ch = set2[map_idx];
            }
            if (squeeze) {
                bool should_squeeze = false;
                if (set2 && set2_len > 0) {
                    for (int i = 0; i < set2_len; i++) {
                        if (set2[i] == out_ch) { should_squeeze = true; break; }
                    }
                } else if (in_set1) {
                    should_squeeze = true;
                }
                if (should_squeeze && out_ch == prev_out) continue;
            }
            fputwc(out_ch, stdout);
            prev_out = out_ch;
        }
    }

    if (set1) free(set1);
    if (set2) free(set2);
    *code = 0;
    return true;
}

typedef struct {
    bool reverse;
    bool numeric;
    bool ignore_case;
    int key_field;
    wchar_t delim;
} SortContext;

static const wchar_t* GetSortKey(const wchar_t* line, const SortContext* ctx) {
    if (ctx->key_field <= 1) return line;
    int field = 1;
    const wchar_t* p = line;
    while (*p && field < ctx->key_field) {
        if (ctx->delim) {
            if (*p == ctx->delim) field++;
            p++;
        } else {
            while (*p && !iswspace(*p)) p++;
            while (*p && iswspace(*p)) p++;
            field++;
        }
    }
    return p;
}

static int CompareLines(void* context, const void* a, const void* b) {
    SortContext* ctx = (SortContext*)context;
    const wchar_t* sa = *(const wchar_t* const*)a;
    const wchar_t* sb = *(const wchar_t* const*)b;
    const wchar_t* ka = GetSortKey(sa, ctx);
    const wchar_t* kb = GetSortKey(sb, ctx);

    const wchar_t* lc = _wgetenv(L"LC_ALL");
    if (!lc || !*lc) lc = _wgetenv(L"LC_COLLATE");
    if (!lc || !*lc) lc = _wgetenv(L"LANG");
    bool isStandardC = (!lc || _wcsicmp(lc, L"C") == 0 || _wcsicmp(lc, L"POSIX") == 0);

    int cmp = 0;
    if (ctx->numeric) {
        double da = wcstod(ka, NULL);
        double db = wcstod(kb, NULL);
        if (da < db) cmp = -1;
        else if (da > db) cmp = 1;
        else cmp = 0;
    } else if (ctx->ignore_case) {
        if (!isStandardC) {
            int res = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, ka, -1, kb, -1);
            cmp = (res != 0) ? (res - 2) : _wcsicmp(ka, kb);
        } else {
            cmp = _wcsicmp(ka, kb);
        }
    } else {
        if (!isStandardC) {
            int res = CompareStringW(LOCALE_USER_DEFAULT, 0, ka, -1, kb, -1);
            cmp = (res != 0) ? (res - 2) : wcscmp(ka, kb);
        } else {
            cmp = wcscmp(ka, kb);
        }
    }

    if (cmp == 0) {
        cmp = isStandardC ? wcscmp(sa, sb) : (CompareStringW(LOCALE_USER_DEFAULT, 0, sa, -1, sb, -1) - 2);
    }
    return ctx->reverse ? -cmp : cmp;
}

bool Builtin_Sort(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: sort [OPTION]... [FILE]...\n"
                L"Write sorted concatenation of all FILE(s) to standard output.\n\n"
                L"  -r            Reverse the result of comparisons\n"
                L"  -n            Compare according to string numerical value\n"
                L"  -f, -i        Fold lower case to upper case characters (case-insensitive)\n"
                L"  -u            Output only the first of an equal run\n"
                L"  -k POS        Start a key at field POS (1-based)\n"
                L"  -t CHAR       Use CHAR as field delimiter\n"
                L"  -o FILE       Write result to FILE instead of standard output\n");
        *code = 0;
        return true;
    }

    SortContext ctx = { false, false, false, 0, 0 };
    bool unique = false;
    const wchar_t* out_file = NULL;
    int file_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && wcscmp(arg, L"-") != 0) {
            if (wcscmp(arg, L"-r") == 0) ctx.reverse = true;
            else if (wcscmp(arg, L"-n") == 0) ctx.numeric = true;
            else if (wcscmp(arg, L"-f") == 0 || wcscmp(arg, L"-i") == 0) ctx.ignore_case = true;
            else if (wcscmp(arg, L"-u") == 0) unique = true;
            else if (wcsncmp(arg, L"-k", 2) == 0) {
                const wchar_t* kval = (arg[2] != L'\0') ? arg + 2 : ((i + 1 < cmd->argc) ? cmd->argv[++i] : L"1");
                ctx.key_field = (int)wcstol(kval, NULL, 10);
            } else if (wcsncmp(arg, L"-t", 2) == 0) {
                ctx.delim = (arg[2] != L'\0') ? arg[2] : ((i + 1 < cmd->argc) ? cmd->argv[++i][0] : L'\t');
            } else if (wcscmp(arg, L"-o") == 0 && i + 1 < cmd->argc) {
                out_file = cmd->argv[++i];
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

    int files_count = cmd->argc - file_start;
    const wchar_t* dummy_stdin = L"-";
    const wchar_t* const* files = (files_count > 0) ? (const wchar_t* const*)(cmd->argv + file_start) : &dummy_stdin;
    if (files_count <= 0) files_count = 1;

    size_t cap = 256;
    size_t count = 0;
    wchar_t** lines = (wchar_t**)malloc(cap * sizeof(wchar_t*));
    if (!lines) { *code = 1; return true; }

    wchar_t buf[4096];
    for (int fi = 0; fi < files_count; fi++) {
        FILE* fp = NULL;
        bool is_stdin = (wcscmp(files[fi], L"-") == 0);
        if (is_stdin) {
            fp = stdin;
        } else {
            fp = _wfsopen(files[fi], L"rt, ccs=UTF-8", _SH_DENYNO);
            if (!fp) fp = _wfsopen(files[fi], L"r", _SH_DENYNO);
            if (!fp) {
                fwprintf(stderr, L"sort: %ls: No such file or directory\n", files[fi]);
                continue;
            }
        }

        while (fgetws(buf, sizeof(buf) / sizeof(buf[0]), fp)) {
            size_t blen = wcslen(buf);
            while (blen > 0 && (buf[blen - 1] == L'\r' || buf[blen - 1] == L'\n')) {
                buf[--blen] = L'\0';
            }
            if (count >= cap) {
                size_t new_cap = cap * 2;
                wchar_t** new_lines = (wchar_t**)realloc(lines, new_cap * sizeof(wchar_t*));
                if (!new_lines) break;
                lines = new_lines;
                cap = new_cap;
            }
            lines[count++] = _wcsdup(buf);
        }

        if (!is_stdin && fp) fclose(fp);
    }

    qsort_s(lines, count, sizeof(wchar_t*), CompareLines, &ctx);

    FILE* out_fp = stdout;
    if (out_file) {
        out_fp = _wfsopen(out_file, L"wt, ccs=UTF-8", _SH_DENYNO);
        if (!out_fp) out_fp = stdout;
    }

    for (size_t i = 0; i < count; i++) {
        if (unique && i > 0 && CompareLines(&lines[i - 1], &lines[i], &ctx) == 0) {
            continue;
        }
        fwprintf(out_fp, L"%ls\n", lines[i]);
    }

    if (out_fp && out_fp != stdout) fclose(out_fp);
    for (size_t i = 0; i < count; i++) free(lines[i]);
    free(lines);

    *code = 0;
    return true;
}

static const wchar_t* SkipFieldsAndChars(const wchar_t* line, int fields, int chars) {
    const wchar_t* p = line;
    for (int f = 0; f < fields && *p; f++) {
        while (*p && iswspace(*p)) p++;
        while (*p && !iswspace(*p)) p++;
    }
    for (int c = 0; c < chars && *p; c++) {
        p++;
    }
    return p;
}

bool Builtin_Uniq(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: uniq [OPTION]... [INPUT [OUTPUT]]\n"
                L"Filter adjacent matching lines from INPUT to OUTPUT.\n\n"
                L"  -c            Prefix lines by the number of occurrences\n"
                L"  -d            Only print duplicate lines, one for each group\n"
                L"  -u            Only print unique lines\n"
                L"  -i            Ignore differences in case when comparing\n"
                L"  -f N          Avoid comparing the first N fields\n"
                L"  -s N          Avoid comparing the first N characters\n");
        *code = 0;
        return true;
    }

    bool show_count = false;
    bool duplicates_only = false;
    bool unique_only = false;
    bool ignore_case = false;
    int skip_fields = 0;
    int skip_chars = 0;
    const wchar_t* input_file = NULL;
    const wchar_t* output_file = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && wcscmp(arg, L"-") != 0) {
            if (wcscmp(arg, L"-c") == 0) show_count = true;
            else if (wcscmp(arg, L"-d") == 0) duplicates_only = true;
            else if (wcscmp(arg, L"-u") == 0) unique_only = true;
            else if (wcscmp(arg, L"-i") == 0) ignore_case = true;
            else if (wcsncmp(arg, L"-f", 2) == 0) {
                const wchar_t* fstr = (arg[2] != L'\0') ? arg + 2 : ((i + 1 < cmd->argc) ? cmd->argv[++i] : L"0");
                skip_fields = (int)wcstol(fstr, NULL, 10);
            } else if (wcsncmp(arg, L"-s", 2) == 0) {
                const wchar_t* sstr = (arg[2] != L'\0') ? arg + 2 : ((i + 1 < cmd->argc) ? cmd->argv[++i] : L"0");
                skip_chars = (int)wcstol(sstr, NULL, 10);
            }
        } else {
            if (!input_file) input_file = arg;
            else if (!output_file) output_file = arg;
        }
    }

    FILE* in_fp = stdin;
    if (input_file && wcscmp(input_file, L"-") != 0) {
        in_fp = _wfsopen(input_file, L"rt, ccs=UTF-8", _SH_DENYNO);
        if (!in_fp) in_fp = _wfsopen(input_file, L"r", _SH_DENYNO);
        if (!in_fp) {
            fwprintf(stderr, L"uniq: %ls: No such file or directory\n", input_file);
            *code = 1;
            return true;
        }
    }

    FILE* out_fp = stdout;
    if (output_file && wcscmp(output_file, L"-") != 0) {
        out_fp = _wfsopen(output_file, L"wt, ccs=UTF-8", _SH_DENYNO);
        if (!out_fp) out_fp = stdout;
    }

    wchar_t cur_line[4096];
    wchar_t prev_line[4096];
    prev_line[0] = L'\0';
    int line_count = 0;
    bool has_prev = false;

    while (fgetws(cur_line, sizeof(cur_line) / sizeof(cur_line[0]), in_fp)) {
        size_t len = wcslen(cur_line);
        while (len > 0 && (cur_line[len - 1] == L'\r' || cur_line[len - 1] == L'\n')) {
            cur_line[--len] = L'\0';
        }

        if (!has_prev) {
            wcscpy_s(prev_line, sizeof(prev_line) / sizeof(prev_line[0]), cur_line);
            line_count = 1;
            has_prev = true;
            continue;
        }

        const wchar_t* p1 = SkipFieldsAndChars(prev_line, skip_fields, skip_chars);
        const wchar_t* p2 = SkipFieldsAndChars(cur_line, skip_fields, skip_chars);
        int cmp = ignore_case ? _wcsicmp(p1, p2) : wcscmp(p1, p2);

        if (cmp == 0) {
            line_count++;
        } else {
            bool print_it = true;
            if (duplicates_only && line_count <= 1) print_it = false;
            if (unique_only && line_count > 1) print_it = false;

            if (print_it) {
                if (show_count) fwprintf(out_fp, L"%7d %ls\n", line_count, prev_line);
                else fwprintf(out_fp, L"%ls\n", prev_line);
            }

            wcscpy_s(prev_line, sizeof(prev_line) / sizeof(prev_line[0]), cur_line);
            line_count = 1;
        }
    }

    if (has_prev) {
        bool print_it = true;
        if (duplicates_only && line_count <= 1) print_it = false;
        if (unique_only && line_count > 1) print_it = false;
        if (print_it) {
            if (show_count) fwprintf(out_fp, L"%7d %ls\n", line_count, prev_line);
            else fwprintf(out_fp, L"%ls\n", prev_line);
        }
    }

    if (in_fp && in_fp != stdin) fclose(in_fp);
    if (out_fp && out_fp != stdout) fclose(out_fp);

    *code = 0;
    return true;
}

bool Builtin_Touch(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: touch [OPTION]... FILE...\n"
                L"Update the access and modification times of each FILE to the current time.\n\n"
                L"  -a            Change only the access time\n"
                L"  -m            Change only the modification time\n"
                L"  -c, -h        Do not create any files\n"
                L"  -r FILE       Use this file's times instead of current time\n"
                L"  -t STAMP      Use [[CC]YY]MMDDhhmm[.ss] instead of current time\n");
        *code = 0;
        return true;
    }

    bool set_access = true;
    bool set_mod = true;
    bool no_create = false;
    const wchar_t* ref_file = NULL;
    const wchar_t* stamp_str = NULL;
    int file_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0') {
            if (wcscmp(arg, L"-a") == 0) { set_access = true; set_mod = false; }
            else if (wcscmp(arg, L"-m") == 0) { set_mod = true; set_access = false; }
            else if (wcscmp(arg, L"-c") == 0 || wcscmp(arg, L"-h") == 0) { no_create = true; }
            else if (wcscmp(arg, L"-r") == 0 && i + 1 < cmd->argc) { ref_file = cmd->argv[++i]; }
            else if (wcscmp(arg, L"-t") == 0 && i + 1 < cmd->argc) { stamp_str = cmd->argv[++i]; }
            else { file_start = i; break; }
            file_start = i + 1;
        } else {
            file_start = i;
            break;
        }
    }

    if (file_start >= cmd->argc) {
        fwprintf(stderr, L"touch: missing file operand\n");
        *code = 1;
        return true;
    }

    FILETIME target_ft;
    GetSystemTimeAsFileTime(&target_ft);

    if (ref_file) {
        HANDLE hRef = CreateFileW(ref_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hRef != INVALID_HANDLE_VALUE) {
            FILETIME ftMod;
            if (GetFileTime(hRef, NULL, NULL, &ftMod)) {
                target_ft = ftMod;
            }
            CloseHandle(hRef);
        }
    } else if (stamp_str) {
        SYSTEMTIME st;
        GetSystemTime(&st);
        int slen = (int)wcslen(stamp_str);
        if (slen >= 8) {
            swscanf_s(stamp_str, L"%2hd%2hd%2hd%2hd", &st.wMonth, &st.wDay, &st.wHour, &st.wMinute);
            SystemTimeToFileTime(&st, &target_ft);
        }
    }

    int ret = 0;
    for (int i = file_start; i < cmd->argc; i++) {
        const wchar_t* path = cmd->argv[i];
        DWORD disp = no_create ? OPEN_EXISTING : OPEN_ALWAYS;
        HANDLE hFile = CreateFileW(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, disp, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            if (!no_create) ret = 1;
            continue;
        }

        SetFileTime(hFile, NULL, set_access ? &target_ft : NULL, set_mod ? &target_ft : NULL);
        CloseHandle(hFile);
    }

    *code = ret;
    return true;
}

bool Builtin_Yes(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: yes [STRING]...\n"
                L"Repeatedly output a line with all specified STRING(s), or 'y'.\n");
        *code = 0;
        return true;
    }

    const wchar_t* msg = L"y";
    wchar_t* dyn_msg = NULL;
    if (cmd->argc > 1) {
        size_t total_len = 0;
        for (int i = 1; i < cmd->argc; i++) total_len += wcslen(cmd->argv[i]) + 1;
        dyn_msg = (wchar_t*)malloc((total_len + 1) * sizeof(wchar_t));
        if (!dyn_msg) {
            *code = 1;
            return true;
        }
        dyn_msg[0] = L'\0';
        size_t cur = 0;
        for (int i = 1; i < cmd->argc; i++) {
            if (i > 1) dyn_msg[cur++] = L' ';
            size_t l = wcslen(cmd->argv[i]);
            wmemcpy(dyn_msg + cur, cmd->argv[i], l);
            cur += l;
        }
        dyn_msg[cur] = L'\0';
        msg = dyn_msg;
    }

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD fileType = GetFileType(hOut);
    bool is_pipe = (fileType == FILE_TYPE_PIPE);

    int count = 0;
    while (1) {
        if (wprintf(L"%ls\n", msg) < 0 || fflush(stdout) != 0) break;
        count++;
        if (is_pipe && count >= 20) break;
    }

    free(dyn_msg);
    *code = 0;
    return true;
}

bool Builtin_Paste(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: paste [OPTION]... [FILE]...\n"
                L"Write lines consisting of the sequentially corresponding lines from each FILE,\n"
                L"separated by TABs, to standard output.\n\n"
                L"  -d LIST       Reuse characters from LIST instead of TABs\n"
                L"  -s            Paste one file at a time instead of in parallel\n");
        *code = 0;
        return true;
    }

    bool serial = false;
    wchar_t delims[64] = L"\t";
    int file_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && wcscmp(arg, L"-") != 0) {
            if (wcscmp(arg, L"-s") == 0) serial = true;
            else if (wcsncmp(arg, L"-d", 2) == 0) {
                const wchar_t* dstr = (arg[2] != L'\0') ? arg + 2 : ((i + 1 < cmd->argc) ? cmd->argv[++i] : L"\t");
                wcsncpy_s(delims, _countof(delims), dstr, _TRUNCATE);
            } else { file_start = i; break; }
            file_start = i + 1;
        } else { file_start = i; break; }
    }

    int files_count = cmd->argc - file_start;
    const wchar_t* dummy_stdin = L"-";
    const wchar_t* const* files = (files_count > 0) ? (const wchar_t* const*)(cmd->argv + file_start) : &dummy_stdin;
    if (files_count <= 0) files_count = 1;

    size_t dlen = wcslen(delims);
    if (dlen == 0) { delims[0] = L'\t'; delims[1] = L'\0'; dlen = 1; }

    if (serial) {
        wchar_t line[4096];
        for (int fi = 0; fi < files_count; fi++) {
            FILE* fp = wcscmp(files[fi], L"-") == 0 ? stdin : _wfsopen(files[fi], L"rt, ccs=UTF-8", _SH_DENYNO);
            if (!fp && wcscmp(files[fi], L"-") != 0) fp = _wfsopen(files[fi], L"r", _SH_DENYNO);
            if (!fp) continue;
            bool first = true;
            size_t didx = 0;
            while (fgetws(line, sizeof(line) / sizeof(line[0]), fp)) {
                size_t len = wcslen(line);
                while (len > 0 && (line[len - 1] == L'\r' || line[len - 1] == L'\n')) line[--len] = L'\0';
                const wchar_t* p = (line[0] == 0xFEFF) ? line + 1 : line;
                if (!first) fputwc(delims[didx % dlen], stdout);
                wprintf(L"%ls", p);
                first = false;
                didx++;
            }
            fputwc(L'\n', stdout);
            if (fp != stdin) fclose(fp);
        }
    } else {
        FILE** fps = (FILE**)malloc(files_count * sizeof(FILE*));
        wchar_t (*row)[4096] = (wchar_t (*)[4096])malloc((size_t)files_count * sizeof(*row));
        bool* has_line = (bool*)malloc((size_t)files_count * sizeof(*has_line));
        if (!fps || !row || !has_line) {
            free(fps);
            free(row);
            free(has_line);
            *code = 1;
            return true;
        }
        for (int fi = 0; fi < files_count; fi++) {
            fps[fi] = wcscmp(files[fi], L"-") == 0 ? stdin : _wfsopen(files[fi], L"rt, ccs=UTF-8", _SH_DENYNO);
            if (!fps[fi] && wcscmp(files[fi], L"-") != 0) fps[fi] = _wfsopen(files[fi], L"r", _SH_DENYNO);
        }

        wchar_t line[4096];

        while (1) {
            bool any_read = false;
            for (int fi = 0; fi < files_count; fi++) {
                row[fi][0] = L'\0';
                has_line[fi] = false;
                if (fps[fi] && fgetws(line, sizeof(line) / sizeof(line[0]), fps[fi])) {
                    any_read = true;
                    has_line[fi] = true;
                    size_t len = wcslen(line);
                    while (len > 0 && (line[len - 1] == L'\r' || line[len - 1] == L'\n')) line[--len] = L'\0';
                    const wchar_t* p = (line[0] == 0xFEFF) ? line + 1 : line;
                    wcscpy_s(row[fi], 4096, p);
                }
            }
            if (!any_read) break;

            for (int fi = 0; fi < files_count; fi++) {
                if (fi > 0) fputwc(delims[(fi - 1) % dlen], stdout);
                if (has_line[fi]) wprintf(L"%ls", row[fi]);
            }
            fputwc(L'\n', stdout);
        }

        for (int fi = 0; fi < files_count; fi++) {
            if (fps[fi] && fps[fi] != stdin) fclose(fps[fi]);
        }
        free(fps);
        free(row);
        free(has_line);
    }

    *code = 0;
    return true;
}

bool Builtin_Fold(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: fold [OPTION]... [FILE]...\n"
                L"Wrap input lines in each FILE (standard input by default), writing to standard output.\n\n"
                L"  -w WIDTH      Use WIDTH columns instead of 80\n"
                L"  -s            Break at spaces\n"
                L"  -b            Count bytes rather than columns\n");
        *code = 0;
        return true;
    }

    int width = 80;
    bool break_at_spaces = false;
    int file_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && wcscmp(arg, L"-") != 0) {
            if (wcscmp(arg, L"-s") == 0) break_at_spaces = true;
            else if (wcscmp(arg, L"-b") == 0) { /* wide chars treated as cols */ }
            else if (wcsncmp(arg, L"-w", 2) == 0) {
                const wchar_t* wstr = (arg[2] != L'\0') ? arg + 2 : ((i + 1 < cmd->argc) ? cmd->argv[++i] : L"80");
                width = (int)wcstol(wstr, NULL, 10);
                if (width < 1) width = 80;
            } else { file_start = i; break; }
            file_start = i + 1;
        } else { file_start = i; break; }
    }

    int files_count = cmd->argc - file_start;
    const wchar_t* dummy_stdin = L"-";
    const wchar_t* const* files = (files_count > 0) ? (const wchar_t* const*)(cmd->argv + file_start) : &dummy_stdin;
    if (files_count <= 0) files_count = 1;

    wchar_t line[8192];
    for (int fi = 0; fi < files_count; fi++) {
        FILE* fp = wcscmp(files[fi], L"-") == 0 ? stdin : _wfsopen(files[fi], L"r", _SH_DENYNO);
        if (!fp) continue;

        while (fgetws(line, sizeof(line) / sizeof(line[0]), fp)) {
            size_t len = wcslen(line);
            while (len > 0 && (line[len - 1] == L'\r' || line[len - 1] == L'\n')) line[--len] = L'\0';

            const wchar_t* p = line;
            while (*p) {
                size_t rem = wcslen(p);
                if ((int)rem <= width) {
                    wprintf(L"%ls\n", p);
                    break;
                }
                int chunk = width;
                if (break_at_spaces) {
                    int last_space = -1;
                    for (int j = 0; j < width; j++) {
                        if (iswspace(p[j])) last_space = j;
                    }
                    if (last_space > 0) chunk = last_space + 1;
                }
                wprintf(L"%.*ls\n", chunk, p);
                p += chunk;
            }
        }
        if (fp != stdin) fclose(fp);
    }

    *code = 0;
    return true;
}

bool Builtin_Expand(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: expand [OPTION]... [FILE]...\n"
                L"Convert tabs in each FILE to spaces, writing to standard output.\n\n"
                L"  -i            Do not convert tabs after non blanks\n"
                L"  -t N          Have tabs N characters apart, not 8\n");
        *code = 0;
        return true;
    }

    int tabstop = 8;
    bool initial_only = false;
    int file_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && wcscmp(arg, L"-") != 0) {
            if (wcscmp(arg, L"-i") == 0) initial_only = true;
            else if (wcsncmp(arg, L"-t", 2) == 0) {
                const wchar_t* tstr = (arg[2] != L'\0') ? arg + 2 : ((i + 1 < cmd->argc) ? cmd->argv[++i] : L"8");
                tabstop = (int)wcstol(tstr, NULL, 10);
                if (tabstop < 1) tabstop = 8;
            } else { file_start = i; break; }
            file_start = i + 1;
        } else { file_start = i; break; }
    }

    int files_count = cmd->argc - file_start;
    const wchar_t* dummy_stdin = L"-";
    const wchar_t* const* files = (files_count > 0) ? (const wchar_t* const*)(cmd->argv + file_start) : &dummy_stdin;
    if (files_count <= 0) files_count = 1;

    for (int fi = 0; fi < files_count; fi++) {
        FILE* fp = wcscmp(files[fi], L"-") == 0 ? stdin : _wfsopen(files[fi], L"r", _SH_DENYNO);
        if (!fp) continue;

        wint_t wch;
        int col = 0;
        bool non_blank_seen = false;
        while ((wch = fgetwc(fp)) != WEOF) {
            if (wch == L'\n' || wch == L'\r') {
                fputwc((wchar_t)wch, stdout);
                col = 0;
                non_blank_seen = false;
            } else if (wch == L'\t') {
                if (!initial_only || !non_blank_seen) {
                    int spaces = tabstop - (col % tabstop);
                    for (int s = 0; s < spaces; s++) fputwc(L' ', stdout);
                    col += spaces;
                } else {
                    fputwc(L'\t', stdout);
                    col += tabstop - (col % tabstop);
                }
            } else {
                fputwc((wchar_t)wch, stdout);
                col++;
                if (!iswspace((wchar_t)wch)) non_blank_seen = true;
            }
        }
        if (fp != stdin) fclose(fp);
    }

    *code = 0;
    return true;
}

bool Builtin_Unexpand(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: unexpand [OPTION]... [FILE]...\n"
                L"Convert spaces in each FILE to tabs, writing to standard output.\n\n"
                L"  -a            Convert all blanks, instead of just initial blanks\n"
                L"  -t N          Have tabs N characters apart, not 8\n");
        *code = 0;
        return true;
    }

    int tabstop = 8;
    bool all = false;
    int file_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && wcscmp(arg, L"-") != 0) {
            if (wcscmp(arg, L"-a") == 0) all = true;
            else if (wcsncmp(arg, L"-t", 2) == 0) {
                const wchar_t* tstr = (arg[2] != L'\0') ? arg + 2 : ((i + 1 < cmd->argc) ? cmd->argv[++i] : L"8");
                tabstop = (int)wcstol(tstr, NULL, 10);
                if (tabstop < 1) tabstop = 8;
            } else { file_start = i; break; }
            file_start = i + 1;
        } else { file_start = i; break; }
    }

    int files_count = cmd->argc - file_start;
    const wchar_t* dummy_stdin = L"-";
    const wchar_t* const* files = (files_count > 0) ? (const wchar_t* const*)(cmd->argv + file_start) : &dummy_stdin;
    if (files_count <= 0) files_count = 1;

    wchar_t line[4096];
    for (int fi = 0; fi < files_count; fi++) {
        FILE* fp = wcscmp(files[fi], L"-") == 0 ? stdin : _wfsopen(files[fi], L"r", _SH_DENYNO);
        if (!fp) continue;

        while (fgetws(line, sizeof(line) / sizeof(line[0]), fp)) {
            size_t len = wcslen(line);
            size_t p = 0;
            int col = 0;
            while (p < len) {
                if (line[p] == L' ') {
                    size_t start_p = p;
                    int start_col = col;
                    while (p < len && line[p] == L' ') {
                        p++;
                        col++;
                    }
                    if (all || start_col == 0) {
                        int num_tabs = (col / tabstop) - (start_col / tabstop);
                        if (num_tabs > 0) {
                            for (int t = 0; t < num_tabs; t++) fputwc(L'\t', stdout);
                            int rem_spaces = col % tabstop;
                            for (int s = 0; s < rem_spaces; s++) fputwc(L' ', stdout);
                        } else {
                            for (size_t s = start_p; s < p; s++) fputwc(line[s], stdout);
                        }
                    } else {
                        for (size_t s = start_p; s < p; s++) fputwc(line[s], stdout);
                    }
                } else if (line[p] == L'\t') {
                    fputwc(L'\t', stdout);
                    col += tabstop - (col % tabstop);
                    p++;
                } else {
                    fputwc(line[p], stdout);
                    col++;
                    p++;
                }
            }
        }
        if (fp != stdin) fclose(fp);
    }

    *code = 0;
    return true;
}

bool Builtin_Nl(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: nl [OPTION]... [FILE]...\n"
                L"Write each FILE to standard output, with line numbers added.\n\n"
                L"  -b STYLE      Body numbering style (a: all lines, t: non-empty lines, n: none)\n"
                L"  -n FORMAT     Line numbering format (ln: left, rn: right, rz: right zero-filled)\n"
                L"  -w WIDTH      Line number field width (default 6)\n"
                L"  -s SEP        Separator string between line number and text (default TAB)\n"
                L"  -v START      Starting line number (default 1)\n"
                L"  -i INCR       Line number increment (default 1)\n");
        *code = 0;
        return true;
    }

    wchar_t body_style = L't';
    wchar_t format[8] = L"rn";
    int width = 6;
    wchar_t sep[64] = L"\t";
    long long line_num = 1;
    long long incr = 1;
    int file_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && wcscmp(arg, L"-") != 0) {
            if (wcsncmp(arg, L"-b", 2) == 0) {
                body_style = (arg[2] != L'\0') ? arg[2] : ((i + 1 < cmd->argc) ? cmd->argv[++i][0] : L't');
            } else if (wcsncmp(arg, L"-n", 2) == 0) {
                const wchar_t* fstr = (arg[2] != L'\0') ? arg + 2 : ((i + 1 < cmd->argc) ? cmd->argv[++i] : L"rn");
                wcsncpy_s(format, _countof(format), fstr, _TRUNCATE);
            } else if (wcsncmp(arg, L"-w", 2) == 0) {
                const wchar_t* wstr = (arg[2] != L'\0') ? arg + 2 : ((i + 1 < cmd->argc) ? cmd->argv[++i] : L"6");
                width = (int)wcstol(wstr, NULL, 10);
            } else if (wcsncmp(arg, L"-s", 2) == 0) {
                const wchar_t* sstr = (arg[2] != L'\0') ? arg + 2 : ((i + 1 < cmd->argc) ? cmd->argv[++i] : L"\t");
                wcsncpy_s(sep, _countof(sep), sstr, _TRUNCATE);
            } else if (wcsncmp(arg, L"-v", 2) == 0) {
                const wchar_t* vstr = (arg[2] != L'\0') ? arg + 2 : ((i + 1 < cmd->argc) ? cmd->argv[++i] : L"1");
                line_num = _wcstoi64(vstr, NULL, 10);
            } else if (wcsncmp(arg, L"-i", 2) == 0) {
                const wchar_t* istr = (arg[2] != L'\0') ? arg + 2 : ((i + 1 < cmd->argc) ? cmd->argv[++i] : L"1");
                incr = _wcstoi64(istr, NULL, 10);
            } else { file_start = i; break; }
            file_start = i + 1;
        } else { file_start = i; break; }
    }

    int files_count = cmd->argc - file_start;
    const wchar_t* dummy_stdin = L"-";
    const wchar_t* const* files = (files_count > 0) ? (const wchar_t* const*)(cmd->argv + file_start) : &dummy_stdin;
    if (files_count <= 0) files_count = 1;

    wchar_t line[4096];
    for (int fi = 0; fi < files_count; fi++) {
        FILE* fp = wcscmp(files[fi], L"-") == 0 ? stdin : _wfsopen(files[fi], L"r", _SH_DENYNO);
        if (!fp) continue;

        while (fgetws(line, sizeof(line) / sizeof(line[0]), fp)) {
            size_t len = wcslen(line);
            while (len > 0 && (line[len - 1] == L'\r' || line[len - 1] == L'\n')) line[--len] = L'\0';

            bool number_it = (body_style == L'a') || (body_style == L't' && len > 0);
            if (number_it) {
                if (wcscmp(format, L"ln") == 0) {
                    wprintf(L"%-*lld%ls%ls\n", width, line_num, sep, line);
                } else if (wcscmp(format, L"rz") == 0) {
                    wprintf(L"%0*lld%ls%ls\n", width, line_num, sep, line);
                } else {
                    wprintf(L"%*lld%ls%ls\n", width, line_num, sep, line);
                }
                line_num += incr;
            } else {
                wprintf(L"%*ls%ls%ls\n", width, L"", sep, line);
            }
        }
        if (fp != stdin) fclose(fp);
    }

    *code = 0;
    return true;
}

bool Builtin_Readlink(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: readlink [OPTION]... FILE...\n"
                L"Print value of a symbolic link or canonical file name.\n\n"
                L"  -f            Canonicalize by following every symlink; all components must exist\n"
                L"  -e            Canonicalize; all components must exist\n"
                L"  -m            Canonicalize; non-existing components allowed\n"
                L"  -n            Do not output the trailing delimiter\n"
                L"  -q, -s        Quiet; suppress error messages\n");
        *code = 0;
        return true;
    }

    bool canonical = false;
    bool no_newline = false;
    bool quiet = false;
    int file_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0') {
            if (wcscmp(arg, L"-f") == 0 || wcscmp(arg, L"-e") == 0 || wcscmp(arg, L"-m") == 0) canonical = true;
            else if (wcscmp(arg, L"-n") == 0) no_newline = true;
            else if (wcscmp(arg, L"-q") == 0 || wcscmp(arg, L"-s") == 0) quiet = true;
            else { file_start = i; break; }
            file_start = i + 1;
        } else { file_start = i; break; }
    }

    if (file_start >= cmd->argc) {
        if (!quiet) fwprintf(stderr, L"readlink: missing operand\n");
        *code = 1;
        return true;
    }

    int ret = 0;
    for (int i = file_start; i < cmd->argc; i++) {
        const wchar_t* path = cmd->argv[i];
        HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            wchar_t final_path[MAX_PATH * 2];
            DWORD len = GetFinalPathNameByHandleW(h, final_path, MAX_PATH * 2, FILE_NAME_NORMALIZED);
            CloseHandle(h);
            if (len > 0 && len < MAX_PATH * 2) {
                wchar_t* p = final_path;
                if (wcsncmp(p, L"\\\\?\\", 4) == 0) p += 4;
                wprintf(no_newline ? L"%ls" : L"%ls\n", p);
            } else {
                ret = 1;
            }
        } else {
            if (canonical) {
                wchar_t full[MAX_PATH];
                if (GetFullPathNameW(path, MAX_PATH, full, NULL)) {
                    wprintf(no_newline ? L"%ls" : L"%ls\n", full);
                } else {
                    ret = 1;
                }
            } else {
                ret = 1;
            }
        }
    }

    *code = ret;
    return true;
}

bool Builtin_Realpath(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: realpath [OPTION]... FILE...\n"
                L"Print the resolved absolute file name.\n\n"
                L"  -s            Strip symbolic link expansion\n"
                L"  -q            Quiet; suppress error messages\n"
                L"  -z            End each output line with NUL, not newline\n");
        *code = 0;
        return true;
    }

    bool strip = false;
    bool quiet = false;
    bool null_delim = false;
    int file_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0') {
            if (wcscmp(arg, L"-s") == 0) strip = true;
            else if (wcscmp(arg, L"-q") == 0) quiet = true;
            else if (wcscmp(arg, L"-z") == 0) null_delim = true;
            else { file_start = i; break; }
            file_start = i + 1;
        } else { file_start = i; break; }
    }

    if (file_start >= cmd->argc) {
        if (!quiet) fwprintf(stderr, L"realpath: missing operand\n");
        *code = 1;
        return true;
    }

    int ret = 0;
    for (int i = file_start; i < cmd->argc; i++) {
        const wchar_t* path = cmd->argv[i];
        wchar_t full[MAX_PATH];
        if (!GetFullPathNameW(path, MAX_PATH, full, NULL)) {
            ret = 1;
            continue;
        }
        if (!strip) {
            HANDLE h = CreateFileW(full, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                wchar_t final_path[MAX_PATH * 2];
                DWORD len = GetFinalPathNameByHandleW(h, final_path, MAX_PATH * 2, FILE_NAME_NORMALIZED);
                CloseHandle(h);
                if (len > 0 && len < MAX_PATH * 2) {
                    wchar_t* p = final_path;
                    if (wcsncmp(p, L"\\\\?\\", 4) == 0) p += 4;
                    wprintf(null_delim ? L"%ls%lc" : L"%ls\n", p, L'\0');
                    continue;
                }
            }
        }
        wprintf(null_delim ? L"%ls%lc" : L"%ls\n", full, L'\0');
    }

    *code = ret;
    return true;
}

bool Builtin_Logname(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: logname\nPrint the current user's login name.\n");
        *code = 0;
        return true;
    }
    wchar_t user[256];
    DWORD len = sizeof(user) / sizeof(user[0]);
    if (GetUserNameW(user, &len)) {
        wprintf(L"%ls\n", user);
        *code = 0;
    } else {
        wchar_t env_user[256];
        DWORD env_len = GetEnvironmentVariableW(L"USERNAME", env_user, 256);
        if (env_len > 0 && env_len < 256) {
            wprintf(L"%ls\n", env_user);
            *code = 0;
        } else {
            *code = 1;
        }
    }
    return true;
}

bool Builtin_Id(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: id [OPTION]... [USER]\n"
                L"Print user and group information for the current user.\n\n"
                L"  -u            Print only the effective user ID\n"
                L"  -g            Print only the effective group ID\n"
                L"  -G            Print all group IDs\n"
                L"  -n            Print a name instead of a number/SID\n"
                L"  -r            Print the real ID instead of the effective ID\n");
        *code = 0;
        return true;
    }

    bool only_user = false;
    bool only_group = false;
    bool all_groups = false;
    bool print_name = false;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0') {
            for (int j = 1; arg[j]; j++) {
                if (arg[j] == L'u') only_user = true;
                else if (arg[j] == L'g') only_group = true;
                else if (arg[j] == L'G') all_groups = true;
                else if (arg[j] == L'n') print_name = true;
                else if (arg[j] == L'r') { /* real == effective on Win32 */ }
            }
        }
    }

    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        *code = 1;
        return true;
    }

    BYTE buffer[1024];
    DWORD retLen = 0;
    TOKEN_USER* pTokenUser = NULL;
    if (GetTokenInformation(hToken, TokenUser, buffer, sizeof(buffer), &retLen)) {
        pTokenUser = (TOKEN_USER*)buffer;
    }

    wchar_t user_name[256] = L"", domain_name[256] = L"";
    DWORD ulen = 256, dlen = 256;
    SID_NAME_USE sid_type;
    DWORD uid_num = 1000;
    if (pTokenUser && pTokenUser->User.Sid) {
        LookupAccountSidW(NULL, pTokenUser->User.Sid, user_name, &ulen, domain_name, &dlen, &sid_type);
        DWORD subCount = *GetSidSubAuthorityCount(pTokenUser->User.Sid);
        if (subCount > 0) uid_num = *GetSidSubAuthority(pTokenUser->User.Sid, subCount - 1);
    }

    TOKEN_PRIMARY_GROUP* pGroup = NULL;
    BYTE gbuf[512];
    DWORD gid_num = 1000;
    wchar_t group_name[256] = L"";
    if (GetTokenInformation(hToken, TokenPrimaryGroup, gbuf, sizeof(gbuf), &retLen)) {
        pGroup = (TOKEN_PRIMARY_GROUP*)gbuf;
        ulen = 256; dlen = 256;
        LookupAccountSidW(NULL, pGroup->PrimaryGroup, group_name, &ulen, domain_name, &dlen, &sid_type);
        DWORD subCount = *GetSidSubAuthorityCount(pGroup->PrimaryGroup);
        if (subCount > 0) gid_num = *GetSidSubAuthority(pGroup->PrimaryGroup, subCount - 1);
    }

    if (only_user) {
        if (print_name) wprintf(L"%ls\n", user_name);
        else wprintf(L"%u\n", uid_num);
    } else if (only_group) {
        if (print_name) wprintf(L"%ls\n", group_name);
        else wprintf(L"%u\n", gid_num);
    } else if (all_groups) {
        if (print_name) wprintf(L"%ls\n", group_name);
        else wprintf(L"%u\n", gid_num);
    } else {
        wprintf(L"uid=%u(%ls) gid=%u(%ls) groups=%u(%ls)\n", uid_num, user_name, gid_num, group_name, gid_num, group_name);
    }

    CloseHandle(hToken);
    *code = 0;
    return true;
}



/* ========================================================================= */
/*                   Category B: Text & Stream Utilities                     */
/* ========================================================================= */

bool Builtin_Comm(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: comm [OPTION]... FILE1 FILE2\n"
                L"Compare sorted files FILE1 and FILE2 line by line.\n\n"
                L"  -1            suppress column 1 (lines unique to FILE1)\n"
                L"  -2            suppress column 2 (lines unique to FILE2)\n"
                L"  -3            suppress column 3 (lines that appear in both files)\n"
                L"  -i            case-insensitive comparison\n");
        *code = 0;
        return true;
    }

    bool sup1 = false, sup2 = false, sup3 = false, ignore_case = false;
    const wchar_t* file1 = NULL;
    const wchar_t* file2 = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* a = cmd->argv[i];
        if (a[0] == L'-' && a[1] && a[1] != L'-') {
            for (int j = 1; a[j]; j++) {
                if (a[j] == L'1') sup1 = true;
                else if (a[j] == L'2') sup2 = true;
                else if (a[j] == L'3') sup3 = true;
                else if (a[j] == L'i') ignore_case = true;
            }
        } else if (!file1) file1 = a;
        else if (!file2) file2 = a;
    }

    if (!file1 || !file2) {
        fwprintf(stderr, L"comm: missing operand\n");
        *code = 1;
        return true;
    }

    FILE* fp1 = (wcscmp(file1, L"-") == 0) ? stdin : _wfsopen(file1, L"r, ccs=UTF-8", _SH_DENYNO);
    FILE* fp2 = (wcscmp(file2, L"-") == 0) ? stdin : _wfsopen(file2, L"r, ccs=UTF-8", _SH_DENYNO);
    if (!fp1 || !fp2) {
        if (!fp1) fwprintf(stderr, L"comm: %ls: No such file or directory\n", file1);
        if (!fp2) fwprintf(stderr, L"comm: %ls: No such file or directory\n", file2);
        if (fp1 && fp1 != stdin) fclose(fp1);
        if (fp2 && fp2 != stdin) fclose(fp2);
        *code = 1;
        return true;
    }

    wchar_t line1[4096] = {0}, line2[4096] = {0};
    bool has1 = fgetws(line1, _countof(line1), fp1) != NULL;
    bool has2 = fgetws(line2, _countof(line2), fp2) != NULL;

    while (has1 || has2) {
        /* Strip trailing newlines */
        size_t l1 = wcslen(line1); while (l1 > 0 && (line1[l1-1] == L'\r' || line1[l1-1] == L'\n')) line1[--l1] = L'\0';
        size_t l2 = wcslen(line2); while (l2 > 0 && (line2[l2-1] == L'\r' || line2[l2-1] == L'\n')) line2[--l2] = L'\0';

        int cmp = 0;
        if (has1 && has2) {
            cmp = ignore_case ? _wcsicmp(line1, line2) : wcscmp(line1, line2);
        } else if (has1) {
            cmp = -1;
        } else {
            cmp = 1;
        }

        if (cmp < 0) {
            if (!sup1) {
                wprintf(L"%ls\n", line1);
            }
            has1 = fgetws(line1, _countof(line1), fp1) != NULL;
        } else if (cmp > 0) {
            if (!sup2) {
                if (!sup1) putwchar(L'\t');
                wprintf(L"%ls\n", line2);
            }
            has2 = fgetws(line2, _countof(line2), fp2) != NULL;
        } else {
            if (!sup3) {
                if (!sup1) putwchar(L'\t');
                if (!sup2) putwchar(L'\t');
                wprintf(L"%ls\n", line1);
            }
            has1 = fgetws(line1, _countof(line1), fp1) != NULL;
            has2 = fgetws(line2, _countof(line2), fp2) != NULL;
        }
    }

    if (fp1 != stdin) fclose(fp1);
    if (fp2 != stdin) fclose(fp2);
    *code = 0;
    return true;
}

static int JoinSplitFields(const wchar_t* line, wchar_t delim, wchar_t*** out) {
    int cap = 8, n = 0;
    wchar_t** f = (wchar_t**)malloc(cap * sizeof(wchar_t*));
    const wchar_t* p = line;
    bool blank = (delim == L' ');
    for (;;) {
        if (blank) {
            while (*p == L' ' || *p == L'\t') p++;
            if (*p == L'\0') break;
        }
        const wchar_t* start = p;
        while (*p != L'\0' && (blank ? (*p != L' ' && *p != L'\t') : (*p != delim))) p++;
        if (n == cap) {
            cap *= 2;
            f = (wchar_t**)realloc(f, cap * sizeof(wchar_t*));
        }
        size_t len = (size_t)(p - start);
        wchar_t* field = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
        if (field) {
            memcpy(field, start, len * sizeof(wchar_t));
            field[len] = L'\0';
        }
        f[n++] = field;
        if (*p == L'\0') break;
        p++;
    }
    *out = f;
    return n;
}

static void JoinFreeFields(wchar_t** f, int n) {
    for (int i = 0; i < n; i++) free(f[i]);
    free(f);
}

/* Writes the key, then the non-key fields of each row (BSD join layout). */
static void JoinWriteRow(wchar_t sep, const wchar_t* key, wchar_t** fa, int na, int ka,
                         wchar_t** fb, int nb, int kb) {
    wprintf(L"%ls", key);
    for (int i = 0; i < na; i++) if (i != ka) wprintf(L"%lc%ls", sep, fa[i]);
    for (int i = 0; i < nb; i++) if (i != kb) wprintf(L"%lc%ls", sep, fb[i]);
    wprintf(L"\n");
}

bool Builtin_Join(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: join [OPTION]... FILE1 FILE2\n"
                L"For each pair of input lines with identical join fields, write a line to\n"
                L"standard output. The default join field is the first, delimited by blanks.\n\n"
                L"  -1 FIELD      join on this FIELD of file 1\n"
                L"  -2 FIELD      join on this FIELD of file 2\n"
                L"  -j FIELD      equivalent to '-1 FIELD -2 FIELD'\n"
                L"  -t CHAR       use CHAR as input and output field separator\n"
                L"  -i            ignore differences in case when comparing fields\n");
        *code = 0;
        return true;
    }

    int field1 = 1, field2 = 1;
    wchar_t delim = L' ';
    bool ignore_case = false;
    bool show_paired = true;
    bool print_un1 = false, print_un2 = false;
    const wchar_t* file1 = NULL;
    const wchar_t* file2 = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* a = cmd->argv[i];
        if (wcscmp(a, L"-") == 0 || a[0] != L'-') {
            if (!file1) file1 = a;
            else if (!file2) file2 = a;
            continue;
        }
        if (wcscmp(a, L"--") == 0) continue;
        wchar_t opt = a[1];
        if (opt == L'i' && a[2] == L'\0') { ignore_case = true; continue; }
        if (!wcschr(L"12jtav", opt)) {
            fwprintf(stderr, L"join: illegal option -- %lc\n", opt);
            *code = 1;
            return true;
        }
        const wchar_t* val = (a[2] != L'\0') ? a + 2 : (i + 1 < cmd->argc ? cmd->argv[++i] : NULL);
        if (!val) {
            fwprintf(stderr, L"join: option requires an argument -- %lc\n", opt);
            *code = 1;
            return true;
        }
        switch (opt) {
        case L'1': field1 = (int)wcstol(val, NULL, 10); break;
        case L'2': field2 = (int)wcstol(val, NULL, 10); break;
        case L'j': field1 = field2 = (int)wcstol(val, NULL, 10); break;
        case L't': if (val[0]) delim = val[0]; break;
        case L'a':
        case L'v': {
            int fnum = (int)wcstol(val, NULL, 10);
            if (fnum == 1) print_un1 = true;
            else if (fnum == 2) print_un2 = true;
            if (opt == L'v') show_paired = false;
            break;
        }
        }
    }

    if (!file1 || !file2) {
        fwprintf(stderr, L"join: missing operand\n");
        *code = 1;
        return true;
    }

    FILE* fp1 = (wcscmp(file1, L"-") == 0) ? stdin : _wfsopen(file1, L"r, ccs=UTF-8", _SH_DENYNO);
    FILE* fp2 = (wcscmp(file2, L"-") == 0) ? stdin : _wfsopen(file2, L"r, ccs=UTF-8", _SH_DENYNO);
    if (!fp1 || !fp2) {
        if (!fp1) fwprintf(stderr, L"join: %ls: No such file or directory\n", file1);
        if (!fp2) fwprintf(stderr, L"join: %ls: No such file or directory\n", file2);
        if (fp1 && fp1 != stdin) fclose(fp1);
        if (fp2 && fp2 != stdin) fclose(fp2);
        *code = 1;
        return true;
    }

    /* Slurp file 2 lines */
    wchar_t** f2_lines = NULL;
    int f2_count = 0, f2_cap = 64;
    f2_lines = (wchar_t**)malloc(f2_cap * sizeof(wchar_t*));
    wchar_t buf[4096];
    while (fgetws(buf, _countof(buf), fp2)) {
        size_t l = wcslen(buf); while (l > 0 && (buf[l-1] == L'\r' || buf[l-1] == L'\n')) buf[--l] = L'\0';
        if (f2_count >= f2_cap) {
            f2_cap *= 2;
            f2_lines = (wchar_t**)realloc(f2_lines, f2_cap * sizeof(wchar_t*));
        }
        f2_lines[f2_count++] = _wcsdup(buf);
    }
    if (fp2 != stdin) fclose(fp2);

    /* Stream file 1 */
    bool* f2_matched = (bool*)calloc(f2_count > 0 ? f2_count : 1, sizeof(bool));
    int ka = field1 - 1, kb = field2 - 1;
    while (fgetws(buf, _countof(buf), fp1)) {
        size_t l = wcslen(buf); while (l > 0 && (buf[l-1] == L'\r' || buf[l-1] == L'\n')) buf[--l] = L'\0';
        wchar_t** fa = NULL;
        int na = JoinSplitFields(buf, delim, &fa);
        const wchar_t* key1 = (ka >= 0 && ka < na) ? fa[ka] : L"";
        bool matched = false;

        for (int j = 0; j < f2_count; j++) {
            wchar_t** fb = NULL;
            int nb = JoinSplitFields(f2_lines[j], delim, &fb);
            const wchar_t* key2 = (kb >= 0 && kb < nb) ? fb[kb] : L"";
            int cmp = ignore_case ? _wcsicmp(key1, key2) : wcscmp(key1, key2);
            if (cmp == 0) {
                matched = true;
                f2_matched[j] = true;
                if (show_paired) JoinWriteRow(delim, key1, fa, na, ka, fb, nb, kb);
            }
            JoinFreeFields(fb, nb);
        }
        if (!matched && print_un1) JoinWriteRow(delim, key1, fa, na, ka, NULL, 0, -1);
        JoinFreeFields(fa, na);
    }

    if (print_un2) {
        for (int j = 0; j < f2_count; j++) {
            if (f2_matched[j]) continue;
            wchar_t** fb = NULL;
            int nb = JoinSplitFields(f2_lines[j], delim, &fb);
            const wchar_t* key2 = (kb >= 0 && kb < nb) ? fb[kb] : L"";
            JoinWriteRow(delim, key2, NULL, 0, -1, fb, nb, kb);
            JoinFreeFields(fb, nb);
        }
    }

    free(f2_matched);
    for (int j = 0; j < f2_count; j++) free(f2_lines[j]);
    free(f2_lines);
    if (fp1 != stdin) fclose(fp1);
    *code = 0;
    return true;
}

static FILE* SplitOpenPart(const wchar_t* prefix, int part_idx, int suffix_len, bool numeric) {
    wchar_t out_path[MAX_PATH];
    if (numeric) {
        wchar_t fmt[32]; swprintf_s(fmt, _countof(fmt), L"%%ls%%0%dd", suffix_len);
        swprintf_s(out_path, _countof(out_path), fmt, prefix, part_idx);
    } else {
        wchar_t sfx[16] = {0};
        int val = part_idx;
        for (int s = suffix_len - 1; s >= 0; s--) {
            sfx[s] = (wchar_t)(L'a' + (val % 26));
            val /= 26;
        }
        swprintf_s(out_path, _countof(out_path), L"%ls%ls", prefix, sfx);
    }
    return _wfsopen(out_path, L"wb", _SH_DENYNO);
}

bool Builtin_Split(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: split [OPTION]... [FILE [PREFIX]]\n"
                L"Output pieces of FILE to PREFIXaa, PREFIXab, ...; default size is 1000 lines,\n"
                L"and default PREFIX is 'x'.\n\n"
                L"  -l, --lines=NUMBER   put NUMBER lines/records per output file\n"
                L"  -b, --bytes=SIZE     put SIZE bytes per output file (suffixes K, M, G)\n"
                L"  -d                   use numeric suffixes starting at 0\n"
                L"  -a, --suffix-length=N generate suffixes of length N (default 2)\n");
        *code = 0;
        return true;
    }

    unsigned __int64 lines_per = 1000;
    unsigned __int64 bytes_per = 0;
    bool numeric = false;
    int suffix_len = 2;
    const wchar_t* filename = NULL;
    const wchar_t* prefix = L"x";

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* a = cmd->argv[i];
        if (wcscmp(a, L"-d") == 0) { numeric = true; continue; }
        const wchar_t* val = NULL;
        wchar_t kind = 0;
        if (wcsncmp(a, L"--lines=", 8) == 0) { kind = L'l'; val = a + 8; }
        else if (wcsncmp(a, L"--bytes=", 8) == 0) { kind = L'b'; val = a + 8; }
        else if (wcsncmp(a, L"--suffix-length=", 16) == 0) { kind = L'a'; val = a + 16; }
        else if (wcscmp(a, L"--lines") == 0 || wcscmp(a, L"-l") == 0) kind = L'l';
        else if (wcscmp(a, L"--bytes") == 0 || wcscmp(a, L"-b") == 0) kind = L'b';
        else if (wcscmp(a, L"--suffix-length") == 0 || wcscmp(a, L"-a") == 0) kind = L'a';
        else if (a[0] == L'-' && (a[1] == L'l' || a[1] == L'b' || a[1] == L'a') && a[2] != L'\0') { kind = a[1]; val = a + 2; }

        if (kind) {
            if (!val) {
                if (i + 1 >= cmd->argc) {
                    fwprintf(stderr, L"split: option requires an argument -- %lc\n", a[1] ? a[1] : L'-');
                    *code = 1;
                    return true;
                }
                val = cmd->argv[++i];
            }
            if (kind == L'l') {
                lines_per = wcstoull(val, NULL, 10);
                bytes_per = 0;
            } else if (kind == L'b') {
                wchar_t* endp = NULL;
                bytes_per = wcstoull(val, &endp, 10);
                if (endp && *endp) {
                    if (*endp == L'k' || *endp == L'K') bytes_per *= 1024;
                    else if (*endp == L'm' || *endp == L'M') bytes_per *= 1024 * 1024;
                    else if (*endp == L'g' || *endp == L'G') bytes_per *= 1024 * 1024 * 1024;
                }
                lines_per = 0;
            } else {
                suffix_len = (int)wcstol(val, NULL, 10);
                if (suffix_len <= 0) suffix_len = 2;
            }
        } else if (!filename) filename = a;
        else prefix = a;
    }

    FILE* in = (!filename || wcscmp(filename, L"-") == 0) ? stdin : _wfsopen(filename, L"rb", _SH_DENYNO);
    if (!in) {
        fwprintf(stderr, L"split: cannot open '%ls' for reading: No such file or directory\n", filename);
        *code = 1;
        return true;
    }

    int part = 0;
    FILE* out = NULL;

    if (bytes_per > 0) {
        char buf[8192];
        unsigned __int64 cur_bytes = 0;
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
            size_t written = 0;
            while (written < n) {
                if (!out) out = SplitOpenPart(prefix, part++, suffix_len, numeric);
                unsigned __int64 can_write = bytes_per - cur_bytes;
                size_t chunk = (size_t)((n - written < can_write) ? (n - written) : can_write);
                fwrite(buf + written, 1, chunk, out);
                written += chunk;
                cur_bytes += chunk;
                if (cur_bytes >= bytes_per) {
                    fclose(out);
                    out = NULL;
                    cur_bytes = 0;
                }
            }
        }
        if (out) fclose(out);
    } else {
        int ch;
        unsigned __int64 cur_lines = 0;
        while ((ch = fgetc(in)) != EOF) {
            if (!out) out = SplitOpenPart(prefix, part++, suffix_len, numeric);
            fputc(ch, out);
            if (ch == '\n') {
                cur_lines++;
                if (cur_lines >= lines_per) {
                    fclose(out);
                    out = NULL;
                    cur_lines = 0;
                }
            }
        }
        if (out) fclose(out);
    }

    if (in != stdin) fclose(in);
    *code = 0;
    return true;
}

bool Builtin_Rev(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: rev [file...]\n"
                L"Reverse lines characterwise.\n");
        *code = 0;
        return true;
    }

    int file_count = cmd->argc - 1;
    const wchar_t* dummy_stdin = L"-";
    const wchar_t* const* files = (file_count > 0) ? (const wchar_t* const*)(cmd->argv + 1) : &dummy_stdin;
    if (file_count <= 0) file_count = 1;

    for (int fi = 0; fi < file_count; fi++) {
        const wchar_t* fname = files[fi];
        bool is_stdin = (wcscmp(fname, L"-") == 0);
        FILE* fp = is_stdin ? stdin : _wfsopen(fname, L"r, ccs=UTF-8", _SH_DENYNO);
        if (!fp) {
            fwprintf(stderr, L"rev: cannot open %ls: No such file or directory\n", fname);
            continue;
        }

        wchar_t line[4096];
        while (fgetws(line, _countof(line), fp)) {
            size_t l = wcslen(line);
            while (l > 0 && (line[l-1] == L'\r' || line[l-1] == L'\n')) line[--l] = L'\0';
            for (size_t i = 0; i < l / 2; i++) {
                wchar_t tmp = line[i];
                line[i] = line[l - 1 - i];
                line[l - 1 - i] = tmp;
            }
            wprintf(L"%ls\n", line);
        }
        if (!is_stdin && fp) fclose(fp);
    }
    *code = 0;
    return true;
}

bool Builtin_Tac(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: tac [FILE]...\n"
                L"Write each FILE to standard output, last line first.\n");
        *code = 0;
        return true;
    }

    int file_count = cmd->argc - 1;
    const wchar_t* dummy_stdin = L"-";
    const wchar_t* const* files = (file_count > 0) ? (const wchar_t* const*)(cmd->argv + 1) : &dummy_stdin;
    if (file_count <= 0) file_count = 1;

    for (int fi = 0; fi < file_count; fi++) {
        const wchar_t* fname = files[fi];
        bool is_stdin = (wcscmp(fname, L"-") == 0);
        FILE* fp = is_stdin ? stdin : _wfsopen(fname, L"r, ccs=UTF-8", _SH_DENYNO);
        if (!fp) {
            fwprintf(stderr, L"tac: failed to open '%ls': No such file or directory\n", fname);
            continue;
        }

        wchar_t** lines = NULL;
        int count = 0, cap = 128;
        lines = (wchar_t**)malloc(cap * sizeof(wchar_t*));
        wchar_t buf[4096];
        while (fgetws(buf, _countof(buf), fp)) {
            size_t l = wcslen(buf);
            while (l > 0 && (buf[l-1] == L'\r' || buf[l-1] == L'\n')) buf[--l] = L'\0';
            if (count >= cap) {
                cap *= 2;
                lines = (wchar_t**)realloc(lines, cap * sizeof(wchar_t*));
            }
            lines[count++] = _wcsdup(buf);
        }
        if (!is_stdin && fp) fclose(fp);

        for (int i = count - 1; i >= 0; i--) {
            wprintf(L"%ls\n", lines[i]);
            free(lines[i]);
        }
        free(lines);
    }
    *code = 0;
    return true;
}

bool Builtin_Shuf(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: shuf [OPTION]... [FILE]\n"
                L"  or:  shuf -e [OPTION]... [ARG]...\n"
                L"  or:  shuf -i LO-HI [OPTION]...\n"
                L"Write a random permutation of the input lines to standard output.\n\n"
                L"  -e, --echo                treat each ARG as an input line\n"
                L"  -i, --input-range=LO-HI   treat each number LO through HI as an input line\n"
                L"  -n, --head-count=COUNT    output at most COUNT lines\n"
                L"  -o, --output=FILE         write result to FILE instead of standard output\n");
        *code = 0;
        return true;
    }

    bool echo_mode = false;
    long long lo = 0, hi = -1;
    int head_count = -1;
    const wchar_t* out_file = NULL;
    const wchar_t* in_file = NULL;
    int arg_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* a = cmd->argv[i];
        if (wcscmp(a, L"-e") == 0 || wcscmp(a, L"--echo") == 0) {
            echo_mode = true;
            arg_start = i + 1;
            break;
        } else if ((wcscmp(a, L"-n") == 0 || wcscmp(a, L"--head-count") == 0) && i + 1 < cmd->argc) {
            head_count = (int)wcstol(cmd->argv[++i], NULL, 10);
        } else if ((wcscmp(a, L"-o") == 0 || wcscmp(a, L"--output") == 0) && i + 1 < cmd->argc) {
            out_file = cmd->argv[++i];
        } else if ((wcscmp(a, L"-i") == 0 || wcscmp(a, L"--input-range") == 0) && i + 1 < cmd->argc) {
            const wchar_t* r = cmd->argv[++i];
            wchar_t* dash = (wchar_t*)wcschr(r, L'-');
            if (dash) {
                lo = _wtoi64(r);
                hi = _wtoi64(dash + 1);
            }
        } else if (a[0] != L'-' && !in_file) {
            in_file = a;
        }
    }

    wchar_t** lines = NULL;
    int count = 0, cap = 64;
    lines = (wchar_t**)malloc(cap * sizeof(wchar_t*));
    if (!lines) {
        *code = 1;
        return true;
    }

    if (echo_mode) {
        for (int i = arg_start; i < cmd->argc; i++) {
            if (count >= cap) {
                if (cap > 1000000) { fwprintf(stderr, L"shuf: too many arguments\n"); break; }
                cap *= 2;
                wchar_t** nl = (wchar_t**)realloc(lines, cap * sizeof(wchar_t*));
                if (!nl) break;
                lines = nl;
            }
            wchar_t* d = _wcsdup(cmd->argv[i]);
            if (d) lines[count++] = d;
        }
    } else if (hi >= lo) {
        if (hi - lo > 10000000) {
            fwprintf(stderr, L"shuf: range too large (max 10,000,000)\n");
            free(lines);
            *code = 1;
            return true;
        }
        for (long long v = lo; v <= hi; v++) {
            if (count >= cap) {
                cap *= 2;
                wchar_t** nl = (wchar_t**)realloc(lines, cap * sizeof(wchar_t*));
                if (!nl) break;
                lines = nl;
            }
            wchar_t nbuf[32]; swprintf_s(nbuf, _countof(nbuf), L"%lld", v);
            wchar_t* d = _wcsdup(nbuf);
            if (d) lines[count++] = d;
        }
    } else {
        FILE* fp = (!in_file || wcscmp(in_file, L"-") == 0) ? stdin : _wfsopen(in_file, L"r, ccs=UTF-8", _SH_DENYNO);
        if (!fp) {
            fwprintf(stderr, L"shuf: cannot open '%ls': No such file or directory\n", in_file);
            free(lines);
            *code = 1;
            return true;
        }
        wchar_t buf[4096];
        while (fgetws(buf, _countof(buf), fp)) {
            size_t l = wcslen(buf); while (l > 0 && (buf[l-1] == L'\r' || buf[l-1] == L'\n')) buf[--l] = L'\0';
            if (count >= cap) {
                if (cap > 10000000) { fwprintf(stderr, L"shuf: input too large\n"); break; }
                cap *= 2;
                wchar_t** nl = (wchar_t**)realloc(lines, cap * sizeof(wchar_t*));
                if (!nl) break;
                lines = nl;
            }
            wchar_t* d = _wcsdup(buf);
            if (d) lines[count++] = d;
        }
        if (fp != stdin) fclose(fp);
    }

    /* Seed and Fisher-Yates shuffle */
    srand((unsigned int)GetTickCount64());
    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        wchar_t* tmp = lines[i];
        lines[i] = lines[j];
        lines[j] = tmp;
    }

    FILE* out = stdout;
    if (out_file) {
        out = _wfsopen(out_file, L"w, ccs=UTF-8", _SH_DENYNO);
        if (!out) out = stdout;
    }

    int limit = (head_count >= 0 && head_count < count) ? head_count : count;
    for (int i = 0; i < limit; i++) {
        if (out == stdout) wprintf(L"%ls\n", lines[i]);
        else fwprintf(out, L"%ls\n", lines[i]);
        free(lines[i]);
    }
    for (int i = limit; i < count; i++) free(lines[i]);
    free(lines);
    if (out != stdout) fclose(out);
    *code = 0;
    return true;
}

