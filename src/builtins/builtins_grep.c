/* ========================================================================= */
/*              Category: grep / egrep / fgrep (BSD compatible)              */
/* ========================================================================= */

#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct GrepLongOpt {
    const wchar_t* name;
    wchar_t code;
    bool takes_arg;
} GrepLongOpt;

static const GrepLongOpt kGrepLongOpts[] = {
    { L"after-context", L'A', true },
    { L"before-context", L'B', true },
    { L"context", L'C', true },
    { L"basic-regexp", L'G', false },
    { L"extended-regexp", L'E', false },
    { L"fixed-strings", L'F', false },
    { L"fixed-regexp", L'F', false },
    { L"regexp", L'e', true },
    { L"file", L'f', true },
    { L"ignore-case", L'i', false },
    { L"invert-match", L'v', false },
    { L"word-regexp", L'w', false },
    { L"line-regexp", L'x', false },
    { L"count", L'c', false },
    { L"files-with-matches", L'l', false },
    { L"files-without-match", L'L', false },
    { L"max-count", L'm', true },
    { L"only-matching", L'o', false },
    { L"quiet", L'q', false },
    { L"silent", L'q', false },
    { L"no-messages", L's', false },
    { L"line-number", L'n', false },
    { L"with-filename", L'H', false },
    { L"no-filename", L'h', false },
    { L"null", L'Z', false },
    { L"null-data", L'z', false },
    { L"byte-offset", L'b', false },
    { L"initial-tab", L'T', false },
    { L"recursive", L'r', false },
    { L"dereference-recursive", L'R', false },
    { L"dereference", L'S', false },
    { L"no-dereference", L'p', false },
    { L"text", L'a', false },
    { L"binary", L'U', false },
    { L"binary-files", L'I', true },
    { L"directories", L'd', true },
    { L"devices", L'D', true },
    { L"perl-regexp", L'P', false },
    { L"line-buffered", L'u', false },
    { L"color", L'N', false },
    { L"colour", L'N', false },
    { L"version", L'V', false },
    { L"help", L'?', false },
};

typedef struct GrepState {
    RegexProg** progs;
    size_t nprogs;
    wchar_t** pats;
    size_t npats;
    size_t pats_cap;
    bool have_pattern;
    unsigned flags;
    bool ere_mode;
    bool fixed;
    bool icase;
    bool invert;
    bool word;
    bool line_match;
    bool only;
    bool count;
    bool list;
    bool list_no;
    bool number;
    bool quiet;
    bool nomsg;
    bool no_names;
    bool with_names;
    bool recursive;
    int symlinks; /* 0 = -p (default), 1 = -S (all), 2 = -O (cmdline) */
    bool ignore_binary;
    bool binary;
    bool null_sep;
    bool initial_tab;
    bool byte_off;
    bool context;
    long max_count;
    long before;
    long after;
    bool print_any;
    bool error;
    bool matched_any;
    bool quit;
    int depth;
} GrepState;

typedef struct GrepHist {
    wchar_t* text;
    size_t cap;
    size_t len;
    long no;
    size_t byte;
} GrepHist;

static void GrepUsage(void) {
    wprintf(L"Usage: grep [OPTION]... PATTERNS [FILE]...\n"
            L"Search for PATTERNS in each FILE or standard input.\n\n"
            L"  -E, egrep     PATTERNS are extended regular expressions\n"
            L"  -F, fgrep     PATTERNS are strings\n"
            L"  -G            PATTERNS are basic regular expressions (default)\n"
            L"  -e PATTERNS   Use PATTERNS for matching (newline separated)\n"
            L"  -f FILE       Take PATTERNS from FILE, one per line\n"
            L"  -i, -y        Ignore case distinctions\n"
            L"  -v            Invert the sense of matching\n"
            L"  -w            Select only lines containing whole words\n"
            L"  -x            Select only lines that exactly match the whole line\n"
            L"  -c            Print only a count of selected lines per FILE\n"
            L"  -l            Print only names of FILEs with selected lines\n"
            L"  -L            Print only names of FILEs with no selected lines\n"
            L"  -o            Print only the matched (non-empty) parts of lines\n"
            L"  -n            Prefix each line with its 1-based line number\n"
            L"  -b            Prefix each line with its byte offset\n"
            L"  -T            Align output with a tab after the prefix\n"
            L"  -Z            Print a NUL byte after file names\n"
            L"  -H, -h        Print or suppress file name prefixes\n"
            L"  -m NUM        Stop after NUM selected lines\n"
            L"  -A NUM        Print NUM lines of trailing context\n"
            L"  -B NUM        Print NUM lines of leading context\n"
            L"  -C NUM        Print NUM lines of context around matches\n"
            L"  -q            Quiet; exit 0 on first match\n"
            L"  -s            Suppress error messages about files\n"
            L"  -r, -R        Search directories recursively\n"
            L"  -p            If -R is specified, no symbolic links are followed (BSD default)\n"
            L"  -S            If -R is specified, follow all symbolic links\n"
            L"  -O            If -R is specified, follow command-line symbolic links only\n"
            L"  -I            Ignore binary files\n"
            L"  -U            Treat files as binary\n"
            L"  -P            Interpret patterns as modern/Perl-compatible extended regexes\n"
            L"  -a, -N        Accepted for compatibility (text output, color ignored)\n"
            L"  -V            Print version information\n"
            L"  --help, /?    Show this help\n");
}

static void GrepError(const GrepState* st, const wchar_t* fmt, const wchar_t* arg) {
    if (!st->nomsg) fwprintf(stderr, fmt, arg);
}

static bool GrepAddPattern(GrepState* st, const wchar_t* s, size_t n) {
    if (st->npats == st->pats_cap) {
        size_t cap = st->pats_cap ? st->pats_cap * 2 : 8;
        wchar_t** grown = realloc(st->pats, cap * sizeof(*grown));
        if (!grown) return false;
        st->pats = grown;
        st->pats_cap = cap;
    }
    wchar_t* copy = malloc((n + 1) * sizeof(wchar_t));
    if (!copy) return false;
    memcpy(copy, s, n * sizeof(wchar_t));
    copy[n] = L'\0';
    st->pats[st->npats++] = copy;
    st->have_pattern = true;
    return true;
}

static bool GrepAddText(GrepState* st, const wchar_t* text) {
    const wchar_t* p = text;
    for (;;) {
        const wchar_t* nl = wcschr(p, L'\n');
        size_t n = nl ? (size_t)(nl - p) : wcslen(p);
        if (!GrepAddPattern(st, p, n)) return false;
        if (!nl) return true;
        p = nl + 1;
    }
}

/* Reads one line (without its terminator). Grows *buf as needed. */
static bool GrepReadLine(FILE* fp, wchar_t** buf, size_t* cap, size_t* len) {
    if (!*buf) {
        *cap = 256;
        *buf = malloc(*cap * sizeof(wchar_t));
        if (!*buf) return false;
    }
    size_t n = 0;
    for (;;) {
        if (*cap - n < 2) {
            size_t ncap = *cap * 2;
            wchar_t* grown = realloc(*buf, ncap * sizeof(wchar_t));
            if (!grown) return false;
            *buf = grown;
            *cap = ncap;
        }
        if (!fgetws(*buf + n, (int)(*cap - n), fp)) break;
        n += wcslen(*buf + n);
        if (n > 0 && (*buf)[n - 1] == L'\n') break;
    }
    if (n == 0) return false;
    while (n > 0 && ((*buf)[n - 1] == L'\n' || (*buf)[n - 1] == L'\r')) n--;
    (*buf)[n] = L'\0';
    *len = n;
    return true;
}

static bool GrepAddFile(GrepState* st, const wchar_t* fname) {
    bool is_stdin = wcscmp(fname, L"-") == 0;
    FILE* fp = is_stdin ? stdin : _wfsopen(fname, L"rt, ccs=UTF-8", _SH_DENYNO);
    if (!fp && !is_stdin) fp = _wfsopen(fname, L"r", _SH_DENYNO);
    if (!fp) {
        fwprintf(stderr, L"grep: %ls: No such file or directory\n", fname);
        return false;
    }
    wchar_t* buf = NULL;
    size_t cap = 0, len = 0;
    bool ok = true;
    while (GrepReadLine(fp, &buf, &cap, &len)) {
        if (!GrepAddPattern(st, buf, len)) { ok = false; break; }
    }
    free(buf);
    st->have_pattern = true;
    if (!is_stdin) fclose(fp);
    return ok;
}

static wchar_t* GrepEscapeBasic(const wchar_t* s, size_t n) {
    wchar_t* out = malloc((2 * n + 1) * sizeof(wchar_t));
    if (!out) return NULL;
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] != L'\0' && wcschr(L"\\.[]*^$", s[i])) out[k++] = L'\\';
        out[k++] = s[i];
    }
    out[k] = L'\0';
    return out;
}

static bool GrepCompileAll(GrepState* st) {
    for (size_t i = 0; i < st->npats; i++) {
        const wchar_t* src = st->pats[i];
        wchar_t* escaped = NULL;
        if (st->fixed) {
            escaped = GrepEscapeBasic(src, wcslen(src));
            if (!escaped) return false;
            src = escaped;
        }
        RegexProg* prog = NULL;
        const wchar_t* err = NULL;
        bool ok = RegexCompile(src, st->ere_mode, st->icase, &prog, &err);
        free(escaped);
        if (!ok) {
            fwprintf(stderr, L"grep: %ls\n", err ? err : L"invalid regular expression");
            return false;
        }
        RegexProg** grown = realloc(st->progs, (st->nprogs + 1) * sizeof(*grown));
        if (!grown) { RegexFree(prog); return false; }
        st->progs = grown;
        st->progs[st->nprogs++] = prog;
    }
    return true;
}

static void GrepFreeState(GrepState* st) {
    for (size_t i = 0; i < st->npats; i++) free(st->pats[i]);
    free(st->pats);
    for (size_t i = 0; i < st->nprogs; i++) RegexFree(st->progs[i]);
    free(st->progs);
}

static size_t GrepUtf8Len(const wchar_t* s, size_t n) {
    if (n == 0) return 0;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, s, (int)n, NULL, 0, NULL, NULL);
    return bytes > 0 ? (size_t)bytes : n;
}

/* Leftmost (then longest) match among all patterns, starting at from. */
static bool GrepFindFrom(const GrepState* st, const wchar_t* text, size_t len, size_t from, size_t* ms, size_t* me) {
    bool found = false;
    for (size_t i = 0; i < st->nprogs; i++) {
        RegexMatch m;
        if (!RegexSearch(st->progs[i], text, len, from, st->flags, &m)) continue;
        if (!found || m.s[0] < *ms || (m.s[0] == *ms && m.e[0] > *me)) {
            *ms = m.s[0];
            *me = m.e[0];
            found = true;
        }
    }
    return found;
}

static void GrepPrefix(const GrepState* st, const wchar_t* name, bool show_name, long no, size_t byte, wchar_t sep) {
    bool any = false;
    if (show_name) {
        wprintf(L"%ls", name);
        if (st->null_sep) fputwc(L'\0', stdout);
        else fputwc(sep, stdout);
        any = true;
    }
    if (st->number) {
        wprintf(L"%ld%lc", no, (wint_t)sep);
        any = true;
    }
    if (st->byte_off) {
        wprintf(L"%llu%lc", (unsigned long long)byte, (wint_t)sep);
        any = true;
    }
    if (st->initial_tab && any) fputwc(L'\t', stdout);
}

static void GrepPrintRecord(GrepState* st, const wchar_t* name, bool show_name, long no, size_t byte,
                            const wchar_t* text, size_t len, wchar_t sep, long* last_printed) {
    if (st->context && st->print_any && (*last_printed == 0 || no != *last_printed + 1)) {
        wprintf(L"--\n");
    }
    GrepPrefix(st, name, show_name, no, byte, sep);
    wprintf(L"%.*ls\n", (int)len, text);
    st->print_any = true;
    *last_printed = no;
}

static void GrepPrintMatches(GrepState* st, const wchar_t* name, bool show_name, long no, size_t line_byte,
                             const wchar_t* text, size_t len) {
    size_t from = 0;
    while (from <= len) {
        size_t ms = 0, me = 0;
        if (!GrepFindFrom(st, text, len, from, &ms, &me)) break;
        if (me == ms) {
            from = me + 1;
            continue;
        }
        GrepPrefix(st, name, show_name, no, line_byte + GrepUtf8Len(text, ms), L':');
        wprintf(L"%.*ls\n", (int)(me - ms), text + ms);
        st->print_any = true;
        from = me;
    }
}

/* Searches one open stream and returns the number of selected lines. */
static long GrepSearchStream(GrepState* st, FILE* fp, const wchar_t* name, bool show_name) {
    wchar_t* buf = NULL;
    size_t cap = 0, len = 0;
    GrepHist* hist = NULL;
    size_t hhead = 0, hcount = 0;
    long no = 0, selected = 0, last_printed = 0, after_left = 0;
    size_t byte_pos = 0;
    bool max_hit = (st->max_count == 0);
    bool printing = !st->count && !st->list && !st->list_no && !st->quiet;

    if (st->before > 0) hist = calloc((size_t)st->before, sizeof(*hist));

    while (!max_hit && GrepReadLine(fp, &buf, &cap, &len)) {
        no++;
        size_t line_byte = byte_pos;
        if (st->byte_off) byte_pos += GrepUtf8Len(buf, len) + 1;
        size_t ms = 0, me = 0;
        bool hit = GrepFindFrom(st, buf, len, 0, &ms, &me);
        if (st->invert) hit = !hit;

        if (hit) {
            selected++;
            if (st->quiet) { st->quit = true; break; }
            if (st->list || st->list_no) break;
            if (printing && !st->only) {
                for (size_t k = 0; hist && k < hcount; k++) {
                    size_t idx = (hhead + (size_t)st->before - hcount + k) % (size_t)st->before;
                    GrepHist* h = &hist[idx];
                    if (h->no > last_printed) {
                        GrepPrintRecord(st, name, show_name, h->no, h->byte, h->text, h->len, L'-', &last_printed);
                    }
                }
                hcount = 0;
            }
            if (printing) {
                if (st->only) {
                    if (!st->invert) GrepPrintMatches(st, name, show_name, no, line_byte, buf, len);
                } else {
                    GrepPrintRecord(st, name, show_name, no, line_byte, buf, len, L':', &last_printed);
                }
            }
            after_left = st->after;
            if (st->max_count > 0 && selected >= st->max_count) max_hit = true;
        } else if (max_hit && after_left <= 0) {
            break;
        } else if (printing && !st->only && after_left > 0) {
            GrepPrintRecord(st, name, show_name, no, line_byte, buf, len, L'-', &last_printed);
            after_left--;
        } else if (hist && st->before > 0) {
            GrepHist* h = &hist[hhead];
            if (h->cap < len + 1) {
                wchar_t* grown = realloc(h->text, (len + 1) * sizeof(wchar_t));
                if (grown) {
                    h->text = grown;
                    h->cap = len + 1;
                }
            }
            if (h->cap >= len + 1) {
                memcpy(h->text, buf, len * sizeof(wchar_t));
                h->text[len] = L'\0';
                h->len = len;
                h->no = no;
                h->byte = line_byte;
            }
            hhead = (hhead + 1) % (size_t)st->before;
            if (hcount < (size_t)st->before) hcount++;
        }
        if (max_hit && after_left <= 0) break;
    }

    if (hist) {
        for (long k = 0; k < st->before; k++) free(hist[k].text);
        free(hist);
    }
    free(buf);
    return selected;
}

static void GrepReportFile(GrepState* st, const wchar_t* display, bool show_name, long selected) {
    if (st->count) {
        if (show_name) {
            wprintf(L"%ls", display);
            if (st->null_sep) fputwc(L'\0', stdout);
            else fputwc(L':', stdout);
        }
        wprintf(L"%ld\n", selected);
    }
    if (st->list && selected > 0) {
        wprintf(L"%ls", display);
        fputwc(st->null_sep ? L'\0' : L'\n', stdout);
    }
    if (st->list_no && selected == 0) {
        wprintf(L"%ls", display);
        fputwc(st->null_sep ? L'\0' : L'\n', stdout);
    }
}

static void GrepSearchOpen(GrepState* st, FILE* fp, const wchar_t* display, bool show_name) {
    long selected = GrepSearchStream(st, fp, display, show_name);
    if (selected > 0) st->matched_any = true;
    GrepReportFile(st, display, show_name, selected);
}

static void GrepSearchPath(GrepState* st, const wchar_t* path, const wchar_t* display, bool show_name) {
    if (st->quit) return;
    if (st->depth > 64) {
        GrepError(st, L"grep: %ls: Maximum recursion depth exceeded\n", path);
        st->error = true;
        return;
    }
    DWORD attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        GrepError(st, L"grep: %ls: No such file or directory\n", path);
        st->error = true;
        return;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        if (!st->recursive) {
            GrepError(st, L"grep: %ls: Is a directory\n", path);
            st->error = true;
            return;
        }
        size_t plen = wcslen(path);
        wchar_t* pattern = malloc((plen + 3) * sizeof(wchar_t));
        if (!pattern) { st->error = true; return; }
        swprintf_s(pattern, plen + 3, L"%ls\\*", path);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pattern, &fd);
        free(pattern);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            size_t child_len = plen + wcslen(fd.cFileName) + 2;
            wchar_t* child = malloc(child_len * sizeof(wchar_t));
            wchar_t* child_display = NULL;
            if (!child) { st->error = true; break; }
            swprintf_s(child, child_len, L"%ls\\%ls", path, fd.cFileName);
            if (display[0] == L'\0') {
                child_display = _wcsdup(fd.cFileName);
            } else {
                size_t dlen = wcslen(display) + wcslen(fd.cFileName) + 2;
                child_display = malloc(dlen * sizeof(wchar_t));
                if (child_display) swprintf_s(child_display, dlen, L"%ls\\%ls", display, fd.cFileName);
            }
            if (child_display) {
                st->depth++;
                GrepSearchPath(st, child, child_display, show_name);
                st->depth--;
            }
            free(child_display);
            free(child);
        } while (FindNextFileW(h, &fd) && !st->quit);
        FindClose(h);
        return;
    }
    FILE* fp = _wfsopen(path, L"rt, ccs=UTF-8", _SH_DENYNO);
    if (!fp) fp = _wfsopen(path, L"r", _SH_DENYNO);
    if (!fp) {
        GrepError(st, L"grep: %ls: Permission denied\n", path);
        st->error = true;
        return;
    }
    GrepSearchOpen(st, fp, display, show_name);
    fclose(fp);
}

static bool GrepNum(const wchar_t* s, long* out) {
    wchar_t* end = NULL;
    long v = wcstol(s, &end, 10);
    if (end == s || *end != L'\0' || v < 0) return false;
    *out = v;
    return true;
}

static bool GrepApply(GrepState* st, wchar_t c, const wchar_t* val) {
    long n = 0;
    switch (c) {
    case L'E': st->fixed = false; st->ere_mode = true; break;
    case L'F': st->fixed = true; st->ere_mode = false; break;
    case L'G': st->fixed = false; st->ere_mode = false; break;
    case L'P': st->fixed = false; st->ere_mode = true; break; /* modern / perl ERE */
    case L'e': return val && GrepAddText(st, val);
    case L'f': return val && GrepAddFile(st, val);
    case L'i': case L'y': st->icase = true; break;
    case L'v': st->invert = true; break;
    case L'w': st->word = true; break;
    case L'x': st->line_match = true; break;
    case L'c': st->count = true; break;
    case L'l': st->list = true; break;
    case L'L': st->list_no = true; break;
    case L'o': st->only = true; break;
    case L'q': st->quiet = true; break;
    case L's': st->nomsg = true; break;
    case L'n': st->number = true; break;
    case L'H': st->with_names = true; st->no_names = false; break;
    case L'h': st->no_names = true; st->with_names = false; break;
    case L'Z': st->null_sep = true; break;
    case L'b': st->byte_off = true; break;
    case L'T': st->initial_tab = true; break;
    case L'r': case L'R': st->recursive = true; break;
    case L'p': st->symlinks = 0; break; /* BSD grep -p: no symlinks */
    case L'S': st->symlinks = 1; break; /* BSD grep -S: follow all symlinks */
    case L'O': st->symlinks = 2; break; /* BSD grep -O: follow cmdline symlinks */
    case L'a': case L'N': break;
    case L'U': st->binary = true; break;
    case L'u': break; /* line-buffered / ignored */
    case L'I': st->ignore_binary = true; break;
    case L'D': break; /* devices: read/skip */
    case L'd':
        if (val && (wcscmp(val, L"recurse") == 0 || wcscmp(val, L"r") == 0)) st->recursive = true;
        break;
    case L'm': return val && GrepNum(val, &st->max_count);
    case L'A': return val && GrepNum(val, &st->after);
    case L'B': return val && GrepNum(val, &st->before);
    case L'C':
        if (!val) {
            st->after = 2;
            st->before = 2;
            break;
        }
        if (!GrepNum(val, &n)) return false;
        st->after = n;
        st->before = n;
        break;
    default: return false;
    }
    return true;
}

static const GrepLongOpt* GrepFindLong(const wchar_t* name, size_t n) {
    for (size_t i = 0; i < sizeof(kGrepLongOpts) / sizeof(kGrepLongOpts[0]); i++) {
        if (wcslen(kGrepLongOpts[i].name) == n && wcsncmp(kGrepLongOpts[i].name, name, n) == 0) {
            return &kGrepLongOpts[i];
        }
    }
    return NULL;
}

bool GrepRegexMatch(const wchar_t* re, const wchar_t* text, bool ignore_case, bool word, bool exact, int* out_offset, int* out_len) {
    RegexProg* prog = NULL;
    const wchar_t* err = NULL;
    if (!RegexCompile(re, true, ignore_case, &prog, &err)) return false;
    unsigned flags = (word ? REGEX_WORD : 0u) | (exact ? REGEX_LINE : 0u) | (out_offset ? REGEX_LONGEST : 0u);
    RegexMatch m;
    bool ok = RegexSearch(prog, text, wcslen(text), 0, flags, &m);
    if (ok) {
        if (out_offset) *out_offset = (int)m.s[0];
        if (out_len) *out_len = (int)(m.e[0] - m.s[0]);
    }
    RegexFree(prog);
    return ok;
}

bool Builtin_Grep(CommandAST* cmd, int* code) {
    const wchar_t* invoker = cmd->argv[0];

    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        GrepUsage();
        *code = 0;
        return true;
    }

    GrepState st;
    memset(&st, 0, sizeof(st));
    st.max_count = -1;
    st.ere_mode = _wcsicmp(invoker, L"egrep") == 0;
    st.fixed = _wcsicmp(invoker, L"fgrep") == 0;
    st.flags = 0;

    int* operands = malloc((size_t)cmd->argc * sizeof(int));
    size_t nops = 0;
    if (!operands) { *code = 2; return true; }

    int rc = -1;
    bool opts_done = false;
    for (int i = 1; i < cmd->argc && rc < 0; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (opts_done || arg[0] != L'-' || arg[1] == L'\0') {
            operands[nops++] = i;
            continue;
        }
        if (wcscmp(arg, L"--") == 0) {
            opts_done = true;
            continue;
        }
        if (arg[1] == L'V' && arg[2] == L'\0') {
            wprintf(L"grep (ntksh) 1.0\n");
            rc = 0;
            break;
        }
        /* BSD -NUM numeric context option (e.g. -3) */
        if (iswdigit(arg[1])) {
            wchar_t* endp = NULL;
            long ctx = wcstol(arg + 1, &endp, 10);
            if (endp && *endp == L'\0' && ctx >= 0) {
                st.after = ctx;
                st.before = ctx;
                continue;
            }
        }
        if (arg[1] == L'-') {
            const wchar_t* name = arg + 2;
            const wchar_t* eq = wcschr(name, L'=');
            size_t nlen = eq ? (size_t)(eq - name) : wcslen(name);
            const GrepLongOpt* lo = GrepFindLong(name, nlen);
            if (!lo) {
                fwprintf(stderr, L"grep: unrecognized option '%ls'\n", arg);
                rc = 2;
                break;
            }
            const wchar_t* val = eq ? eq + 1 : NULL;
            if (lo->takes_arg && !val) {
                if (i + 1 >= cmd->argc) {
                    fwprintf(stderr, L"grep: option '--%ls' requires an argument\n", lo->name);
                    rc = 2;
                    break;
                }
                val = cmd->argv[++i];
            }
            if (!GrepApply(&st, lo->code, val)) {
                fwprintf(stderr, L"grep: invalid argument for '--%ls'\n", lo->name);
                rc = 2;
            }
            continue;
        }
        for (int j = 1; arg[j] != L'\0' && rc < 0; j++) {
            wchar_t c = arg[j];
            if (c == L'C') {
                if (arg[j + 1] != L'\0' && iswdigit(arg[j + 1])) {
                    if (!GrepApply(&st, L'C', arg + j + 1)) {
                        fwprintf(stderr, L"grep: invalid argument for -C\n");
                        rc = 2;
                    }
                    break;
                } else if (i + 1 < cmd->argc && iswdigit(cmd->argv[i + 1][0])) {
                    const wchar_t* val = cmd->argv[++i];
                    if (!GrepApply(&st, L'C', val)) {
                        fwprintf(stderr, L"grep: invalid argument for -C\n");
                        rc = 2;
                    }
                    break;
                } else {
                    GrepApply(&st, L'C', NULL);
                    continue;
                }
            }
            if (wcschr(L"efmABDd", c)) {
                const wchar_t* val = arg[j + 1] ? arg + j + 1 : (i + 1 < cmd->argc ? cmd->argv[++i] : NULL);
                if (!val) {
                    fwprintf(stderr, L"grep: option requires an argument -- '%lc'\n", (wint_t)c);
                    rc = 2;
                    break;
                }
                if (!GrepApply(&st, c, val)) {
                    fwprintf(stderr, L"grep: invalid argument for -%lc\n", (wint_t)c);
                    rc = 2;
                }
                break;
            }
            if (!GrepApply(&st, c, NULL)) {
                fwprintf(stderr, L"grep: invalid option -- '%lc'\n", (wint_t)c);
                rc = 2;
            }
        }
    }

    if (rc >= 0) {
        free(operands);
        GrepFreeState(&st);
        *code = rc;
        return true;
    }

    size_t first_file = 0;
    if (!st.have_pattern) {
        if (nops == 0) {
            GrepUsage();
            free(operands);
            *code = 2;
            return true;
        }
        const wchar_t* pat = cmd->argv[operands[0]];
        if (!GrepAddText(&st, pat)) {
            free(operands);
            GrepFreeState(&st);
            *code = 2;
            return true;
        }
        first_file = 1;
    }

    if (st.word) st.flags |= REGEX_WORD;
    if (st.line_match) st.flags |= REGEX_LINE;
    if (st.only) st.flags |= REGEX_LONGEST;
    if (st.only) {
        st.before = 0;
        st.after = 0;
    }
    st.context = (st.before > 0 || st.after > 0);

    if (!GrepCompileAll(&st)) {
        free(operands);
        GrepFreeState(&st);
        *code = 2;
        return true;
    }

    size_t nfiles = nops - first_file;
    bool show_name = st.with_names || (!st.no_names && (nfiles > 1 || st.recursive));

    if (nfiles == 0) {
        if (st.recursive) {
            GrepSearchPath(&st, L".", L"", show_name);
        } else {
            GrepSearchOpen(&st, stdin, L"(standard input)", show_name);
        }
    } else {
        for (size_t fi = 0; fi < nfiles && !st.quit; fi++) {
            const wchar_t* fname = cmd->argv[operands[first_file + fi]];
            if (wcscmp(fname, L"-") == 0) {
                GrepSearchOpen(&st, stdin, L"(standard input)", show_name);
            } else {
                GrepSearchPath(&st, fname, fname, show_name);
            }
        }
    }

    free(operands);
    if (st.quit) *code = 0;
    else if (st.error && !(st.quiet && st.matched_any)) *code = 2;
    else *code = st.matched_any ? 0 : 1;
    GrepFreeState(&st);
    return true;
}
