#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

/* ========================================================================= */
/*              Category: grep, sed, awk, diff  (sed: BSD-compatible)        */
/* ========================================================================= */

typedef struct {
    wchar_t* p;
    size_t len;
    size_t cap;
} SedBuf;

typedef struct {
    FILE* fp;
    bool missing_nl;
} SedOut;

typedef struct SedFile {
    wchar_t* name;
    SedOut own;
    SedOut* out;
    FILE* rfp;
    bool rfp_tried;
    struct SedFile* next;
} SedFile;

typedef struct {
    RegexProg* prog;
    bool empty;
} SedRe;

enum { SED_ADDR_NONE, SED_ADDR_LINE, SED_ADDR_STEP, SED_ADDR_LAST, SED_ADDR_RE, SED_ADDR_PLUS, SED_ADDR_MULT };

typedef struct {
    int kind;
    size_t n;
    size_t step;
    SedRe re;
} SedAddr;

enum { SED_PIECE_LIT, SED_PIECE_GROUP, SED_PIECE_CASE };

typedef struct {
    int kind;
    wchar_t* text;
    size_t len;
    int value;
} SedPiece;

typedef struct {
    SedAddr a1, a2;
    bool has1, has2;
    bool negate;
    bool in_range;
    size_t range_end;
    wchar_t name;
    size_t jump;
    wchar_t* text;
    size_t text_len;
    SedFile* file;
    int int_arg;
    SedRe sre;
    SedPiece* pieces;
    size_t npieces;
    bool global, print;
    size_t nth;
    wchar_t* ysrc;
    wchar_t* ydst;
    size_t ylen;
} SedCmd;

typedef struct {
    int kind;
    wchar_t* text;
    size_t len;
} SedAppend;

typedef struct {
    SedCmd* cmds;
    size_t ncmds, capcmds;
    SedFile* files;
    bool quiet, ere, separate, unbuffered, in_place, delay_open;
    const wchar_t* suffix;
    int width;
    SedOut stdout_out, stderr_out;
    SedOut* out;
    SedBuf ps, hs, tmp, nb;
    bool nl;
    size_t line_no;
    bool replaced;
    RegexProg* last_re;
    SedAppend* app;
    size_t napp, capapp;
    bool quit, stop_input, fatal, missing, io_err;
    int quit_code;
    bool skip_read;
    const wchar_t* cur_name;
} SedState;

typedef struct {
    const wchar_t* const* names;
    int count;
    int next;
    FILE* fp;
    bool fp_std;
    SedBuf la;
    bool la_valid;
    bool la_nl;
} SedInput;

typedef struct {
    SedState* st;
    const wchar_t* s;
    size_t n;
    size_t pos;
    size_t* blocks;
    size_t nblocks, capblocks;
} SedParser;

enum { SED_END_PRINT, SED_END_DELETE, SED_END_RESTART, SED_END_SILENT };

static void SedOom(void) {
    fwprintf(stderr, L"sed: out of memory\n");
    exit(4);
}

static wchar_t* SedDupN(const wchar_t* s, size_t n) {
    wchar_t* r = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
    if (!r) SedOom();
    if (n) memcpy(r, s, n * sizeof(wchar_t));
    r[n] = L'\0';
    return r;
}

static void SedBufReserve(SedBuf* b, size_t extra) {
    if (extra > SIZE_MAX - b->len - 1) SedOom();
    if (b->len + extra <= b->cap) return;
    size_t cap = b->cap ? b->cap : 128;
    while (cap < b->len + extra) {
        if (cap > (SIZE_MAX / 2) / sizeof(wchar_t)) {
            cap = b->len + extra + 1;
            break;
        }
        cap *= 2;
    }
    wchar_t* np = (wchar_t*)realloc(b->p, cap * sizeof(wchar_t));
    if (!np) SedOom();
    b->p = np;
    b->cap = cap;
}

static void SedBufAppend(SedBuf* b, const wchar_t* s, size_t n) {
    if (n == 0) return;
    SedBufReserve(b, n);
    memcpy(b->p + b->len, s, n * sizeof(wchar_t));
    b->len += n;
}

static void SedBufPut(SedBuf* b, wchar_t c) {
    SedBufAppend(b, &c, 1);
}

static void SedBufSet(SedBuf* b, const wchar_t* s, size_t n) {
    b->len = 0;
    SedBufAppend(b, s, n);
}

static void SedBufFree(SedBuf* b) {
    free(b->p);
    b->p = NULL;
    b->len = 0;
    b->cap = 0;
}

static const wchar_t* SedBufData(const SedBuf* b) {
    return b->p ? b->p : L"";
}

static size_t SedFindNl(const SedBuf* b) {
    size_t k = 0;
    while (k < b->len && b->p[k] != L'\n') k++;
    return k;
}

static void SedPutChars(SedState* st, SedOut* o, const wchar_t* s, size_t n) {
    if (o->missing_nl) {
        if (putwc(L'\n', o->fp) == WEOF) st->io_err = true;
        o->missing_nl = false;
    }
    for (size_t i = 0; i < n; i++) {
        if (putwc(s[i], o->fp) == WEOF) {
            st->io_err = true;
            break;
        }
    }
}

/* A line without a trailing newline is written without one; the newline is added only if more output follows. */
static void SedPutLine(SedState* st, SedOut* o, const wchar_t* s, size_t n, bool nl) {
    SedPutChars(st, o, s, n);
    if (nl) {
        if (putwc(L'\n', o->fp) == WEOF) st->io_err = true;
    } else {
        o->missing_nl = true;
    }
}

static bool SedReadLine(FILE* fp, SedBuf* b, bool* nl) {
    wint_t c;
    bool got = false;
    b->len = 0;
    *nl = false;
    while ((c = fgetwc(fp)) != WEOF) {
        got = true;
        if (c == L'\n') {
            *nl = true;
            break;
        }
        SedBufPut(b, (wchar_t)c);
    }
    return got;
}

static FILE* SedOpenFile(const wchar_t* name) {
    FILE* fp = _wfsopen(name, L"rt, ccs=UTF-8", _SH_DENYNO);
    if (!fp) fp = _wfsopen(name, L"r", _SH_DENYNO);
    return fp;
}

static void SedInCloseFile(SedInput* in) {
    if (in->fp && !in->fp_std) fclose(in->fp);
    in->fp = NULL;
}

static void SedInitInput(SedInput* in, const wchar_t* const* names, int count) {
    memset(in, 0, sizeof(*in));
    in->names = names;
    in->count = count;
}

static void SedInDone(SedInput* in) {
    SedInCloseFile(in);
    SedBufFree(&in->la);
}

static bool SedInOpenNext(SedState* st, SedInput* in) {
    while (in->next < in->count) {
        const wchar_t* name = in->names[in->next++];
        if (wcscmp(name, L"-") == 0) {
            in->fp = stdin;
            in->fp_std = true;
        } else {
            in->fp = SedOpenFile(name);
            in->fp_std = false;
            if (!in->fp) {
                fwprintf(stderr, L"sed: %ls: No such file or directory\n", name);
                st->missing = true;
                continue;
            }
        }
        st->cur_name = name;
        return true;
    }
    return false;
}

static bool SedInReadRaw(SedState* st, SedInput* in, SedBuf* b, bool* nl) {
    for (;;) {
        if (!in->fp && !SedInOpenNext(st, in)) return false;
        if (SedReadLine(in->fp, b, nl)) return true;
        SedInCloseFile(in);
    }
}

static bool SedInNext(SedState* st, SedInput* in, SedBuf* b, bool* nl) {
    if (in->la_valid) {
        in->la_valid = false;
        SedBufSet(b, SedBufData(&in->la), in->la.len);
        *nl = in->la_nl;
        return true;
    }
    return SedInReadRaw(st, in, b, nl);
}

static bool SedInIsLast(SedState* st, SedInput* in) {
    if (in->la_valid) return false;
    if (SedInReadRaw(st, in, &in->la, &in->la_nl)) {
        in->la_valid = true;
        return false;
    }
    return true;
}

static int SedUtf8Bytes(unsigned int c, unsigned char* out) {
    if (c < 0x80) {
        out[0] = (unsigned char)c;
        return 1;
    }
    if (c < 0x800) {
        out[0] = (unsigned char)(0xC0 | (c >> 6));
        out[1] = (unsigned char)(0x80 | (c & 0x3F));
        return 2;
    }
    out[0] = (unsigned char)(0xE0 | (c >> 12));
    out[1] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
    out[2] = (unsigned char)(0x80 | (c & 0x3F));
    return 3;
}

/* Writes the `l` representation of c into esc and returns its length. */
static size_t SedListEscape(wchar_t c, wchar_t* esc) {
    switch (c) {
        case L'\\': esc[0] = L'\\'; esc[1] = L'\\'; return 2;
        case 7: esc[0] = L'\\'; esc[1] = L'a'; return 2;
        case 8: esc[0] = L'\\'; esc[1] = L'b'; return 2;
        case 12: esc[0] = L'\\'; esc[1] = L'f'; return 2;
        case 10: esc[0] = L'\\'; esc[1] = L'n'; return 2;
        case 13: esc[0] = L'\\'; esc[1] = L'r'; return 2;
        case 9: esc[0] = L'\\'; esc[1] = L't'; return 2;
        case 11: esc[0] = L'\\'; esc[1] = L'v'; return 2;
        default: break;
    }
    if (c >= 32 && c < 127) {
        esc[0] = c;
        return 1;
    }
    unsigned char bytes[3];
    int nb = SedUtf8Bytes((unsigned int)c, bytes);
    size_t k = 0;
    for (int i = 0; i < nb; i++) {
        esc[k++] = L'\\';
        esc[k++] = L'0' + ((bytes[i] >> 6) & 7);
        esc[k++] = L'0' + ((bytes[i] >> 3) & 7);
        esc[k++] = L'0' + (bytes[i] & 7);
    }
    return k;
}

static void SedList(SedState* st, int width) {
    wchar_t esc[16];
    size_t col = 0;
    for (size_t i = 0; i < st->ps.len; i++) {
        size_t k = SedListEscape(st->ps.p[i], esc);
        if (width > 1 && col + k > (size_t)(width - 1)) {
            SedPutChars(st, st->out, L"\\\n", 2);
            col = 0;
        }
        SedPutChars(st, st->out, esc, k);
        col += k;
    }
    SedPutChars(st, st->out, L"$\n", 2);
}

static void SedCopyFile(SedState* st, const wchar_t* name) {
    FILE* fp = SedOpenFile(name);
    if (!fp) return;
    wint_t c;
    while ((c = fgetwc(fp)) != WEOF) {
        wchar_t w = (wchar_t)c;
        SedPutChars(st, st->out, &w, 1);
    }
    fclose(fp);
}

static void SedQueue(SedState* st, int kind, const wchar_t* s, size_t n) {
    if (st->napp == st->capapp) {
        size_t cap = st->capapp ? st->capapp * 2 : 8;
        SedAppend* na = (SedAppend*)realloc(st->app, cap * sizeof(SedAppend));
        if (!na) SedOom();
        st->app = na;
        st->capapp = cap;
    }
    SedAppend* a = &st->app[st->napp++];
    a->kind = kind;
    a->text = SedDupN(s, n);
    a->len = n;
}

static void SedFlushAppend(SedState* st) {
    for (size_t i = 0; i < st->napp; i++) {
        SedAppend* a = &st->app[i];
        if (a->kind == 0) SedPutChars(st, st->out, a->text, a->len);
        else SedCopyFile(st, a->text);
        free(a->text);
    }
    st->napp = 0;
}

static SedFile* SedFindFile(SedState* st, const wchar_t* name) {
    for (SedFile* f = st->files; f; f = f->next) {
        if (wcscmp(f->name, name) == 0) return f;
    }
    SedFile* f = (SedFile*)calloc(1, sizeof(SedFile));
    if (!f) SedOom();
    f->name = SedDupN(name, wcslen(name));
    if (wcscmp(name, L"/dev/stdout") == 0) f->out = &st->stdout_out;
    else if (wcscmp(name, L"/dev/stderr") == 0) f->out = &st->stderr_out;
    else f->out = &f->own;
    f->next = st->files;
    st->files = f;
    return f;
}

static bool SedOpenWriteDirect(SedState* st, SedFile* f) {
    if (f->out != &f->own || f->own.fp) return true;
    f->own.fp = _wfsopen(f->name, L"w, ccs=UTF-8", _SH_DENYNO);
    if (!f->own.fp) f->own.fp = _wfsopen(f->name, L"w", _SH_DENYNO);
    if (!f->own.fp) {
        fwprintf(stderr, L"sed: couldn't open file %ls\n", f->name);
        st->io_err = true;
        return false;
    }
    return true;
}

static bool SedOpenWrite(SedState* st, SedFile* f) {
    if (st->delay_open) return true;
    return SedOpenWriteDirect(st, f);
}

static bool SedRegexTestPrepared(SedState* st, const SedRe* re, const wchar_t* s, size_t n) {
    RegexProg* prog = re->prog;
    if (re->empty) {
        prog = st->last_re;
        if (!prog) {
            fwprintf(stderr, L"sed: no previous regular expression\n");
            st->fatal = true;
            st->quit = true;
            return false;
        }
    } else {
        st->last_re = prog;
    }
    RegexMatch m;
    return RegexSearch(prog, s, n, 0, 0, &m);
}

static bool SedAddrMatch(SedState* st, SedInput* in, const SedAddr* a) {
    switch (a->kind) {
        case SED_ADDR_LINE:
            return st->line_no == a->n;
        case SED_ADDR_STEP:
            if (a->step == 0) return st->line_no == a->n;
            return st->line_no >= a->n && (st->line_no - a->n) % a->step == 0;
        case SED_ADDR_LAST:
            return SedInIsLast(st, in);
        case SED_ADDR_RE:
            return SedRegexTestPrepared(st, &a->re, SedBufData(&st->ps), st->ps.len);
        default:
            return false;
    }
}

static bool SedSelect(SedState* st, SedInput* in, SedCmd* c) {
    if (!c->has1) return true;
    if (!c->has2) return SedAddrMatch(st, in, &c->a1);

    if (c->in_range) {
        switch (c->a2.kind) {
            case SED_ADDR_LINE:
            case SED_ADDR_PLUS:
                if (st->line_no >= c->range_end) {
                    c->in_range = false;
                    return st->line_no <= c->range_end;
                }
                return true;
            case SED_ADDR_MULT:
                if (st->line_no % c->a2.n == 0) c->in_range = false;
                return true;
            case SED_ADDR_LAST:
                if (SedInIsLast(st, in)) c->in_range = false;
                return true;
            case SED_ADDR_RE:
                if (SedRegexTestPrepared(st, &c->a2.re, SedBufData(&st->ps), st->ps.len)) c->in_range = false;
                return true;
            default:
                c->in_range = false;
                return true;
        }
    }

    if (!SedAddrMatch(st, in, &c->a1)) return false;
    switch (c->a2.kind) {
        case SED_ADDR_LINE:
            if (c->a2.n > st->line_no) {
                c->in_range = true;
                c->range_end = c->a2.n;
            }
            break;
        case SED_ADDR_PLUS:
            if (c->a2.n > 0) {
                c->in_range = true;
                c->range_end = st->line_no + c->a2.n;
            }
            break;
        case SED_ADDR_MULT:
            if (c->a2.n > 0 && st->line_no % c->a2.n != 0) c->in_range = true;
            break;
        case SED_ADDR_LAST:
            if (!SedInIsLast(st, in)) c->in_range = true;
            break;
        case SED_ADDR_RE:
            c->in_range = true;
            break;
        default:
            break;
    }
    return true;
}

static void SedCaseAppend(SedBuf* b, const wchar_t* s, size_t n, int mode, int* one) {
    if (mode == 0 && *one == 0) {
        SedBufAppend(b, s, n);
        return;
    }
    for (size_t i = 0; i < n; i++) {
        wchar_t ch = s[i];
        if (*one == 'u') ch = (wchar_t)towupper(ch);
        else if (*one == 'l') ch = (wchar_t)towlower(ch);
        else if (mode == 'U') ch = (wchar_t)towupper(ch);
        else if (mode == 'L') ch = (wchar_t)towlower(ch);
        *one = 0;
        SedBufPut(b, ch);
    }
}

static void SedAppendRepl(SedState* st, const SedCmd* c, const wchar_t* src, const RegexMatch* m) {
    int mode = 0, one = 0;
    for (size_t i = 0; i < c->npieces; i++) {
        const SedPiece* p = &c->pieces[i];
        if (p->kind == SED_PIECE_CASE) {
            switch (p->value) {
                case 'U': case 'L': mode = p->value; break;
                case 'E': mode = 0; one = 0; break;
                case 'u': case 'l': one = p->value; break;
                default: break;
            }
        } else if (p->kind == SED_PIECE_LIT) {
            SedCaseAppend(&st->nb, p->text, p->len, mode, &one);
        } else {
            int g = p->value;
            if (m->s[g] != REGEX_UNSET_POS && m->e[g] != REGEX_UNSET_POS && m->e[g] >= m->s[g]) {
                SedCaseAppend(&st->nb, src + m->s[g], m->e[g] - m->s[g], mode, &one);
            }
        }
    }
}

static void SedSubst(SedState* st, SedCmd* c) {
    RegexProg* prog = NULL;
    if (c->sre.empty) {
        prog = st->last_re;
        if (!prog) {
            fwprintf(stderr, L"sed: no previous regular expression\n");
            st->fatal = true;
            st->quit = true;
            return;
        }
    } else {
        prog = c->sre.prog;
        st->last_re = prog;
    }

    const wchar_t* src = SedBufData(&st->ps);
    size_t slen = st->ps.len;
    st->nb.len = 0;
    size_t pos = 0, copied = 0, count = 0, prev_end = 0;
    bool have_prev = false, did = false;
    RegexMatch m;
    while (pos <= slen && RegexSearch(prog, src, slen, pos, REGEX_LONGEST, &m)) {
        size_t ms = m.s[0], me = m.e[0];
        if (ms == me && have_prev && ms == prev_end) {
            pos = ms + 1;
            continue;
        }
        count++;
        if (count >= c->nth) {
            SedBufAppend(&st->nb, src + copied, ms - copied);
            SedAppendRepl(st, c, src, &m);
            copied = me;
            did = true;
            if (!c->global) break;
        }
        have_prev = true;
        prev_end = me;
        pos = (me == ms) ? me + 1 : me;
    }
    if (!did) return;

    SedBufAppend(&st->nb, src + copied, slen - copied);
    SedBuf t = st->ps;
    st->ps = st->nb;
    st->nb = t;
    st->replaced = true;
    if (c->print) SedPutLine(st, st->out, SedBufData(&st->ps), st->ps.len, st->nl);
    if (c->file) {
        if (!c->file->own.fp && !SedOpenWriteDirect(st, c->file)) return;
        SedPutLine(st, c->file->out, SedBufData(&st->ps), st->ps.len, st->nl);
    }
}

static void SedTranslate(SedState* st, const SedCmd* c) {
    for (size_t i = 0; i < st->ps.len; i++) {
        for (size_t j = 0; j < c->ylen; j++) {
            if (st->ps.p[i] == c->ysrc[j]) {
                st->ps.p[i] = c->ydst[j];
                break;
            }
        }
    }
}

static int SedExec(SedState* st, SedInput* in) {
    size_t pc = 0;
    while (pc < st->ncmds) {
        SedCmd* c = &st->cmds[pc];
        bool m = SedSelect(st, in, c);
        if (c->negate) m = !m;
        if (st->quit) return SED_END_SILENT;
        pc++;
        if (!m) {
            if (c->name == L'{') pc = c->jump;
            continue;
        }

        switch (c->name) {
            case L'{':
            case L'}':
            case L':':
                break;
            case L'=': {
                wchar_t buf[32];
                swprintf_s(buf, _countof(buf), L"%lu\n", (unsigned long)st->line_no);
                SedPutChars(st, st->out, buf, wcslen(buf));
                break;
            }
            case L'a':
                SedQueue(st, 0, c->text, c->text_len);
                break;
            case L'i':
                SedPutChars(st, st->out, c->text, c->text_len);
                break;
            case L'c':
                if (!c->has2 || !c->in_range) SedPutChars(st, st->out, c->text, c->text_len);
                return SED_END_DELETE;
            case L'b':
                pc = c->jump;
                break;
            case L't':
                if (st->replaced) {
                    st->replaced = false;
                    pc = c->jump;
                }
                break;
            case L'T':
                if (!st->replaced) pc = c->jump;
                else st->replaced = false;
                break;
            case L'd':
                return SED_END_DELETE;
            case L'D': {
                size_t k = SedFindNl(&st->ps);
                if (k == st->ps.len) return SED_END_DELETE;
                memmove(st->ps.p, st->ps.p + k + 1, (st->ps.len - k - 1) * sizeof(wchar_t));
                st->ps.len -= k + 1;
                return SED_END_RESTART;
            }
            case L'F': {
                const wchar_t* nm = st->cur_name ? st->cur_name : L"-";
                SedPutChars(st, st->out, nm, wcslen(nm));
                SedPutChars(st, st->out, L"\n", 1);
                break;
            }
            case L'g':
                SedBufSet(&st->ps, SedBufData(&st->hs), st->hs.len);
                break;
            case L'G':
                SedBufPut(&st->ps, L'\n');
                SedBufAppend(&st->ps, SedBufData(&st->hs), st->hs.len);
                break;
            case L'h':
                SedBufSet(&st->hs, SedBufData(&st->ps), st->ps.len);
                break;
            case L'H':
                SedBufPut(&st->hs, L'\n');
                SedBufAppend(&st->hs, SedBufData(&st->ps), st->ps.len);
                break;
            case L'x': {
                SedBuf t = st->ps;
                st->ps = st->hs;
                st->hs = t;
                break;
            }
            case L'l':
                SedList(st, c->int_arg >= 0 ? c->int_arg : st->width);
                break;
            case L'n': {
                if (SedInIsLast(st, in)) {
                    st->stop_input = true;
                    return SED_END_PRINT;
                }
                if (!st->quiet) SedPutLine(st, st->out, SedBufData(&st->ps), st->ps.len, st->nl);
                SedFlushAppend(st);
                SedInNext(st, in, &st->ps, &st->nl);
                st->line_no++;
                st->replaced = false;
                break;
            }
            case L'N': {
                if (SedInIsLast(st, in)) {
                    st->stop_input = true;
                    return SED_END_PRINT;
                }
                SedFlushAppend(st);
                bool tnl = false;
                SedInNext(st, in, &st->tmp, &tnl);
                st->line_no++;
                st->replaced = false;
                SedBufPut(&st->ps, L'\n');
                SedBufAppend(&st->ps, SedBufData(&st->tmp), st->tmp.len);
                st->nl = tnl;
                break;
            }
            case L'p':
                SedPutLine(st, st->out, SedBufData(&st->ps), st->ps.len, st->nl);
                break;
            case L'P': {
                size_t k = SedFindNl(&st->ps);
                if (k < st->ps.len) SedPutLine(st, st->out, SedBufData(&st->ps), k, true);
                else SedPutLine(st, st->out, SedBufData(&st->ps), st->ps.len, st->nl);
                break;
            }
            case L'q':
                st->quit = true;
                st->quit_code = c->int_arg > 0 ? c->int_arg : 0;
                return SED_END_PRINT;
            case L'Q':
                st->quit = true;
                st->quit_code = c->int_arg > 0 ? c->int_arg : 0;
                return SED_END_SILENT;
            case L'r':
                SedQueue(st, 1, c->text, c->text_len);
                break;
            case L'R': {
                SedFile* f = c->file;
                if (!f->rfp_tried) {
                    f->rfp_tried = true;
                    f->rfp = SedOpenFile(f->name);
                }
                if (f->rfp) {
                    bool rnl = false;
                    if (SedReadLine(f->rfp, &st->tmp, &rnl)) {
                        SedBufPut(&st->tmp, L'\n');
                        SedQueue(st, 0, SedBufData(&st->tmp), st->tmp.len);
                    }
                }
                break;
            }
            case L's':
                SedSubst(st, c);
                if (st->quit) return SED_END_SILENT;
                break;
            case L'w':
                if (c->file) {
                    if (!c->file->own.fp && !SedOpenWriteDirect(st, c->file)) break;
                    SedPutLine(st, c->file->out, SedBufData(&st->ps), st->ps.len, st->nl);
                }
                break;
            case L'W': {
                if (c->file) {
                    if (!c->file->own.fp && !SedOpenWriteDirect(st, c->file)) break;
                    size_t k = SedFindNl(&st->ps);
                    SedPutLine(st, c->file->out, SedBufData(&st->ps), k, true);
                }
                break;
            }
            case L'y':
                SedTranslate(st, c);
                break;
            case L'z':
                st->ps.len = 0;
                break;
            default:
                break;
        }
    }
    return SED_END_PRINT;
}

static void SedRunInput(SedState* st, SedInput* in) {
    st->stop_input = false;
    st->skip_read = false;
    for (;;) {
        if (!st->skip_read) {
            if (!SedInNext(st, in, &st->ps, &st->nl)) break;
            st->line_no++;
            st->replaced = false;
        }
        st->skip_read = false;
        int how = SedExec(st, in);
        if (how == SED_END_PRINT && !st->quiet) SedPutLine(st, st->out, SedBufData(&st->ps), st->ps.len, st->nl);
        if (how != SED_END_SILENT) SedFlushAppend(st);
        if (st->unbuffered) fflush(st->out->fp);
        if (how == SED_END_RESTART) st->skip_read = true;
        if (st->quit || st->stop_input) break;
    }
    SedFlushAppend(st);
}

/* ------------------------------------------------------------------------- */
/*                                  Parser                                   */
/* ------------------------------------------------------------------------- */

static bool SedError(SedParser* p, const wchar_t* msg) {
    size_t end = p->pos < p->n ? p->pos + 1 : p->n;
    fwprintf(stderr, L"sed: 1: \"%.*ls\" %ls\n", (int)end, p->s, msg);
    return false;
}

static wchar_t SedPeek(const SedParser* p) {
    return p->pos < p->n ? p->s[p->pos] : L'\0';
}

static void SedSkipBlanks(SedParser* p) {
    while (p->pos < p->n && (p->s[p->pos] == L' ' || p->s[p->pos] == L'\t')) p->pos++;
}

static bool SedAtEndCmd(SedParser* p) {
    SedSkipBlanks(p);
    wchar_t c = SedPeek(p);
    return c == L'\0' || c == L';' || c == L'\n' || c == L'}' || c == L'#';
}

static bool SedExpectEnd(SedParser* p) {
    if (SedAtEndCmd(p)) return true;
    return SedError(p, L"extra characters after command");
}

static bool SedIsDigit(wchar_t c) {
    return c >= L'0' && c <= L'9';
}

static size_t SedReadNum(SedParser* p) {
    size_t v = 0;
    while (p->pos < p->n && SedIsDigit(p->s[p->pos])) {
        v = v * 10 + (size_t)(p->s[p->pos] - L'0');
        p->pos++;
    }
    return v;
}

static SedCmd* SedNewCmd(SedState* st) {
    if (st->ncmds == st->capcmds) {
        size_t cap = st->capcmds ? st->capcmds * 2 : 16;
        SedCmd* nc = (SedCmd*)realloc(st->cmds, cap * sizeof(SedCmd));
        if (!nc) SedOom();
        st->cmds = nc;
        st->capcmds = cap;
    }
    SedCmd* c = &st->cmds[st->ncmds++];
    memset(c, 0, sizeof(*c));
    c->int_arg = -1;
    return c;
}

static void SedAddPiece(SedCmd* c, int kind, wchar_t* text, size_t len, int value) {
    SedPiece* np = (SedPiece*)realloc(c->pieces, (c->npieces + 1) * sizeof(SedPiece));
    if (!np) SedOom();
    c->pieces = np;
    SedPiece* p = &np[c->npieces++];
    p->kind = kind;
    p->text = text;
    p->len = len;
    p->value = value;
}

static void SedFlushLit(SedCmd* c, SedBuf* lit) {
    if (lit->len == 0) return;
    SedAddPiece(c, SED_PIECE_LIT, SedDupN(lit->p, lit->len), lit->len, 0);
    lit->len = 0;
}

static void SedBuildPieces(SedCmd* c, const wchar_t* raw, size_t n) {
    SedBuf lit = {0};
    for (size_t i = 0; i < n; i++) {
        wchar_t ch = raw[i];
        if (ch == L'&') {
            SedFlushLit(c, &lit);
            SedAddPiece(c, SED_PIECE_GROUP, NULL, 0, 0);
            continue;
        }
        if (ch != L'\\' || i + 1 >= n) {
            SedBufPut(&lit, ch);
            continue;
        }
        wchar_t d = raw[++i];
        if (SedIsDigit(d)) {
            SedFlushLit(c, &lit);
            SedAddPiece(c, SED_PIECE_GROUP, NULL, 0, d - L'0');
        } else if (d == L'L' || d == L'U' || d == L'E' || d == L'l' || d == L'u') {
            SedFlushLit(c, &lit);
            SedAddPiece(c, SED_PIECE_CASE, NULL, 0, d);
        } else if (d == L'n') {
            SedBufPut(&lit, L'\n');
        } else if (d == L't') {
            SedBufPut(&lit, L'\t');
        } else if (d == L'r') {
            SedBufPut(&lit, L'\r');
        } else {
            SedBufPut(&lit, d);
        }
    }
    SedFlushLit(c, &lit);
    SedBufFree(&lit);
}

/* Reads up to an unescaped delimiter. Regex text gets \n and \t converted; other escapes are kept for later stages. */
static bool SedReadDelim(SedParser* p, wchar_t delim, bool regex, SedBuf* out) {
    while (p->pos < p->n) {
        wchar_t c = p->s[p->pos++];
        if (c == delim) return true;
        if (c == L'\n' && regex) return false;
        if (c == L'\\' && p->pos < p->n) {
            wchar_t d = p->s[p->pos++];
            if (d == delim) SedBufPut(out, d);
            else if (regex && d == L'n') SedBufPut(out, L'\n');
            else if (regex && d == L't') SedBufPut(out, L'\t');
            else {
                SedBufPut(out, L'\\');
                SedBufPut(out, d);
            }
            continue;
        }
        SedBufPut(out, c);
    }
    return false;
}

static bool SedCompileRe(SedParser* p, const SedBuf* text, bool icase, SedRe* re) {
    re->prog = NULL;
    re->empty = text->len == 0;
    if (re->empty) return true;
    wchar_t* pat = SedDupN(text->p, text->len);
    const wchar_t* err = NULL;
    bool ok = RegexCompile(pat, p->st->ere, icase, &re->prog, &err);
    free(pat);
    if (!ok) return SedError(p, err ? err : L"invalid regular expression");
    return true;
}

static bool SedParseAddr(SedParser* p, SedAddr* a, bool second) {
    a->kind = SED_ADDR_NONE;
    wchar_t c = SedPeek(p);
    if (SedIsDigit(c)) {
        size_t n = SedReadNum(p);
        if (!second && SedPeek(p) == L'~') {
            p->pos++;
            a->kind = SED_ADDR_STEP;
            a->n = n;
            a->step = SedReadNum(p);
            return true;
        }
        a->kind = SED_ADDR_LINE;
        a->n = n;
        return true;
    }
    if (c == L'$') {
        p->pos++;
        a->kind = SED_ADDR_LAST;
        return true;
    }
    if (c == L'/' || c == L'\\') {
        p->pos++;
        wchar_t delim = L'/';
        if (c == L'\\') {
            delim = SedPeek(p);
            if (!delim || delim == L'\n' || delim == L'\\') return SedError(p, L"unexpected `\\'");
            p->pos++;
        }
        SedBuf raw = {0};
        if (!SedReadDelim(p, delim, true, &raw)) {
            SedBufFree(&raw);
            return SedError(p, L"unterminated address regex");
        }
        bool icase = false;
        for (;;) {
            wchar_t f = SedPeek(p);
            if (f == L'I') {
                icase = true;
                p->pos++;
            } else if (f == L'M') {
                SedBufFree(&raw);
                return SedError(p, L"multiline address flag M is not supported");
            } else {
                break;
            }
        }
        a->kind = SED_ADDR_RE;
        bool ok = SedCompileRe(p, &raw, icase, &a->re);
        SedBufFree(&raw);
        return ok;
    }
    if (second && (c == L'+' || c == L'~')) {
        p->pos++;
        if (!SedIsDigit(SedPeek(p))) return SedError(p, L"expected number after + or ~");
        a->n = SedReadNum(p);
        a->kind = (c == L'+') ? SED_ADDR_PLUS : SED_ADDR_MULT;
        return true;
    }
    return true;
}

static wchar_t* SedReadLabel(SedParser* p, bool branch) {
    SedSkipBlanks(p);
    size_t start = p->pos;
    while (p->pos < p->n) {
        wchar_t ch = p->s[p->pos];
        if (ch == L'\n' || ch == L';') break;
        if (branch && (ch == L' ' || ch == L'\t' || ch == L'}')) break;
        p->pos++;
    }
    size_t len = p->pos - start;
    if (!branch) {
        while (len > 0 && (p->s[start + len - 1] == L' ' || p->s[start + len - 1] == L'\t')) len--;
    }
    return SedDupN(p->s + start, len);
}

static wchar_t* SedReadFileName(SedParser* p) {
    SedSkipBlanks(p);
    size_t start = p->pos;
    while (p->pos < p->n && p->s[p->pos] != L'\n') p->pos++;
    size_t len = p->pos - start;
    if (len == 0) return NULL;
    return SedDupN(p->s + start, len);
}

static bool SedParseText(SedParser* p, SedCmd* c) {
    SedSkipBlanks(p);
    wchar_t first = SedPeek(p);
    if (first == L'\\') {
        p->pos++;
        if (SedPeek(p) == L'\n') p->pos++;
    } else if (first == L'\0' || first == L'\n') {
        return SedError(p, L"expected \\ after `a', `c' or `i'");
    }
    SedBuf b = {0};
    while (p->pos < p->n) {
        wchar_t ch = p->s[p->pos++];
        if (ch == L'\n') break;
        if (ch == L'\\') {
            if (p->pos >= p->n) break;
            ch = p->s[p->pos++];
        }
        SedBufPut(&b, ch);
    }
    SedBufPut(&b, L'\n');
    c->text = SedDupN(SedBufData(&b), b.len);
    c->text_len = b.len;
    SedBufFree(&b);
    return true;
}

static wchar_t* SedYDecode(const SedBuf* in, size_t* out_len) {
    SedBuf out = {0};
    for (size_t i = 0; i < in->len; i++) {
        wchar_t ch = in->p[i];
        if (ch == L'\\' && i + 1 < in->len) {
            wchar_t d = in->p[++i];
            ch = d == L'n' ? L'\n' : (d == L't' ? L'\t' : d);
        }
        SedBufPut(&out, ch);
    }
    *out_len = out.len;
    return out.p;
}

static bool SedParseY(SedParser* p, SedCmd* c) {
    wchar_t delim = SedPeek(p);
    if (!delim || delim == L'\n' || delim == L'\\') return SedError(p, L"unterminated `y' command");
    p->pos++;
    SedBuf a = {0}, b = {0};
    if (!SedReadDelim(p, delim, false, &a) || !SedReadDelim(p, delim, false, &b)) {
        SedBufFree(&a);
        SedBufFree(&b);
        return SedError(p, L"unterminated `y' command");
    }
    size_t na = 0, nb = 0;
    wchar_t* src = SedYDecode(&a, &na);
    wchar_t* dst = SedYDecode(&b, &nb);
    SedBufFree(&a);
    SedBufFree(&b);
    if (na != nb) {
        free(src);
        free(dst);
        return SedError(p, L"strings for `y' command are different lengths");
    }
    c->ysrc = src;
    c->ydst = dst;
    c->ylen = na;
    return SedExpectEnd(p);
}

static bool SedParseSubst(SedParser* p, SedCmd* c) {
    SedState* st = p->st;
    wchar_t delim = SedPeek(p);
    if (!delim || delim == L'\n' || delim == L'\\') return SedError(p, L"unterminated `s' command");
    p->pos++;
    SedBuf re_raw = {0}, rep_raw = {0};
    if (!SedReadDelim(p, delim, true, &re_raw) || !SedReadDelim(p, delim, false, &rep_raw)) {
        SedBufFree(&re_raw);
        SedBufFree(&rep_raw);
        return SedError(p, L"unterminated `s' command");
    }

    bool icase = false, wflag = false;
    for (;;) {
        wchar_t f = SedPeek(p);
        if (f == L'g') {
            c->global = true;
        } else if (f == L'p') {
            c->print = true;
        } else if (f == L'i' || f == L'I') {
            icase = true;
        } else if (SedIsDigit(f)) {
            c->nth = SedReadNum(p);
            if (c->nth == 0) {
                SedBufFree(&re_raw);
                SedBufFree(&rep_raw);
                return SedError(p, L"number option to `s' command may not be zero");
            }
            continue;
        } else if (f == L'm' || f == L'M') {
            SedBufFree(&re_raw);
            SedBufFree(&rep_raw);
            return SedError(p, L"multiline flag M is not supported");
        } else if (f == L'e') {
            SedBufFree(&re_raw);
            SedBufFree(&rep_raw);
            return SedError(p, L"e flag is not supported");
        } else if (f == L'w') {
            p->pos++;
            wchar_t* name = SedReadFileName(p);
            if (!name) {
                SedBufFree(&re_raw);
                SedBufFree(&rep_raw);
                return SedError(p, L"missing filename in r/R/w/W commands");
            }
            c->file = SedFindFile(st, name);
            free(name);
            wflag = true;
            if (!SedOpenWrite(st, c->file)) {
                SedBufFree(&re_raw);
                SedBufFree(&rep_raw);
                return false;
            }
            break;
        } else {
            break;
        }
        p->pos++;
    }

    if (c->nth == 0) c->nth = 1;
    bool ok = SedCompileRe(p, &re_raw, icase, &c->sre);
    if (ok) SedBuildPieces(c, SedBufData(&rep_raw), rep_raw.len);
    SedBufFree(&re_raw);
    SedBufFree(&rep_raw);
    if (!ok) return false;
    return wflag ? true : SedExpectEnd(p);
}

static bool SedParseOne(SedParser* p) {
    SedState* st = p->st;
    SedCmd* c = SedNewCmd(st);
    size_t idx = st->ncmds - 1;

    if (!SedParseAddr(p, &c->a1, false)) return false;
    c->has1 = c->a1.kind != SED_ADDR_NONE;
    if (c->has1) {
        SedSkipBlanks(p);
        if (SedPeek(p) == L',') {
            p->pos++;
            SedSkipBlanks(p);
            if (!SedParseAddr(p, &c->a2, true)) return false;
            if (c->a2.kind == SED_ADDR_NONE) return SedError(p, L"unexpected `,'");
            c->has2 = true;
        }
    }
    if (c->has1 && c->a1.kind == SED_ADDR_LINE && c->a1.n == 0) {
        if (!c->has2 || c->a2.kind != SED_ADDR_RE) return SedError(p, L"invalid usage of line address 0");
        c->in_range = true;
    }

    SedSkipBlanks(p);
    while (SedPeek(p) == L'!') {
        if (c->negate) return SedError(p, L"multiple `!'s");
        c->negate = true;
        p->pos++;
        SedSkipBlanks(p);
    }

    wchar_t cmd = SedPeek(p);
    if (!cmd || cmd == L'\n' || cmd == L';') return SedError(p, L"missing command");
    p->pos++;
    c->name = cmd;

    if ((cmd == L':' || cmd == L'}') && c->has1) {
        return SedError(p, cmd == L':' ? L": doesn't want any addresses" : L"} doesn't want any addresses");
    }

    switch (cmd) {
        case L'{':
            if (p->nblocks == p->capblocks) {
                size_t cap = p->capblocks ? p->capblocks * 2 : 8;
                size_t* nb = (size_t*)realloc(p->blocks, cap * sizeof(size_t));
                if (!nb) SedOom();
                p->blocks = nb;
                p->capblocks = cap;
            }
            p->blocks[p->nblocks++] = idx;
            return true;
        case L'}': {
            if (p->nblocks == 0) return SedError(p, L"unexpected `}'");
            size_t open = p->blocks[--p->nblocks];
            st->cmds[open].jump = idx;
            return SedExpectEnd(p);
        }
        case L'=': case L'd': case L'D': case L'g': case L'G': case L'h': case L'H':
        case L'x': case L'n': case L'N': case L'p': case L'P': case L'z': case L'F':
            return SedExpectEnd(p);
        case L':': {
            c->text = SedReadLabel(p, false);
            if (c->text[0] == L'\0') return SedError(p, L"\":\" lacks a label");
            c->text_len = wcslen(c->text);
            return true;
        }
        case L'b': case L't': case L'T':
            c->text = SedReadLabel(p, true);
            c->text_len = wcslen(c->text);
            return SedExpectEnd(p);
        case L'a': case L'i': case L'c':
            return SedParseText(p, c);
        case L'r': case L'R': case L'w': case L'W': {
            SedSkipBlanks(p);
            wchar_t* name = SedReadFileName(p);
            if (!name) return SedError(p, L"missing filename in r/R/w/W commands");
            if (cmd == L'r') {
                c->text = name;
                c->text_len = wcslen(name);
            } else {
                c->file = SedFindFile(st, name);
                free(name);
                if ((cmd == L'w' || cmd == L'W') && !SedOpenWrite(st, c->file)) return false;
            }
            return true;
        }
        case L'q': case L'Q':
            if (c->has2) return SedError(p, L"command only uses one address");
            SedSkipBlanks(p);
            c->int_arg = SedIsDigit(SedPeek(p)) ? (int)SedReadNum(p) : 0;
            return SedExpectEnd(p);
        case L'l':
            SedSkipBlanks(p);
            if (SedIsDigit(SedPeek(p))) c->int_arg = (int)SedReadNum(p);
            return SedExpectEnd(p);
        case L's':
            return SedParseSubst(p, c);
        case L'y':
            return SedParseY(p, c);
        case L'e':
            return SedError(p, L"e command is not supported");
        default: {
            wchar_t msg[64];
            swprintf_s(msg, _countof(msg), L"unknown command: '%lc'", cmd);
            return SedError(p, msg);
        }
    }
}

static bool SedParseScript(SedState* st, const wchar_t* script, size_t n) {
    SedParser p;
    memset(&p, 0, sizeof(p));
    p.st = st;
    p.s = script;
    p.n = n;

    if (n >= 2 && script[0] == L'#' && script[1] == L'n' && (n == 2 || script[2] == L'\n')) st->quiet = true;

    bool ok = true;
    for (;;) {
        while (p.pos < n && (script[p.pos] == L' ' || script[p.pos] == L'\t' || script[p.pos] == L'\n' || script[p.pos] == L';')) p.pos++;
        if (p.pos >= n) break;
        if (script[p.pos] == L'#') {
            while (p.pos < n && script[p.pos] != L'\n') p.pos++;
            continue;
        }
        if (!SedParseOne(&p)) {
            ok = false;
            break;
        }
    }
    if (ok && p.nblocks > 0) {
        p.pos = n;
        ok = SedError(&p, L"unexpected end of script, `{' not closed");
    }
    if (ok) {
        for (size_t i = 0; i < st->ncmds; i++) {
            SedCmd* c = &st->cmds[i];
            if (c->name != L'b' && c->name != L't' && c->name != L'T') continue;
            if (c->text_len == 0) {
                c->jump = st->ncmds;
                continue;
            }
            c->jump = st->ncmds;
            for (size_t j = 0; j < st->ncmds; j++) {
                if (st->cmds[j].name == L':' && wcscmp(st->cmds[j].text, c->text) == 0) {
                    c->jump = j;
                    break;
                }
            }
            if (c->jump == st->ncmds) {
                fwprintf(stderr, L"sed: can't find label for jump to `%ls'\n", c->text);
                ok = false;
                break;
            }
        }
    }
    free(p.blocks);
    return ok;
}

/* ------------------------------------------------------------------------- */
/*                         Options, script input, usage                       */
/* ------------------------------------------------------------------------- */

static void SedUsage(FILE* fp) {
    fwprintf(fp, L"Usage: sed [-Ealnrsu] [-i[SUFFIX]] [-l N] [-e script] [-f script_file] [script] [file ...]\n");
}

static void SedHelp(void) {
    wprintf(L"Usage: sed [-Ealnrsu] [-i[SUFFIX]] [-l N] [-e script] [-f script_file] [script] [file ...]\n"
            L"Stream editor (BSD-compatible).\n\n"
            L"  -n, --quiet, --silent  Suppress automatic printing of pattern space\n"
            L"  -e SCRIPT              Add the script to the commands to be executed\n"
            L"  -f SCRIPT_FILE         Add the contents of the script file ('-' is stdin)\n"
            L"  -E, -r                 Use extended regular expressions\n"
            L"  -i[SUFFIX]             Edit files in place (optional backup SUFFIX; '*' = file name)\n"
            L"  -s, --separate         Treat each file separately for $ and line numbers\n"
            L"  -u, --unbuffered       Flush output after each line\n"
            L"  -l N                   Line wrap length for the l command (default 70)\n");
}

static void SedScriptAdd(SedBuf* script, const wchar_t* s, size_t n, bool* any) {
    if (*any) SedBufPut(script, L'\n');
    SedBufAppend(script, s, n);
    *any = true;
}

static bool SedScriptFile(const wchar_t* name, SedBuf* script, bool* any) {
    FILE* fp = (wcscmp(name, L"-") == 0) ? stdin : SedOpenFile(name);
    if (!fp) {
        fwprintf(stderr, L"sed: couldn't open file %ls: No such file or directory\n", name);
        return false;
    }
    SedBuf line = {0};
    bool nl = false;
    while (SedReadLine(fp, &line, &nl)) SedScriptAdd(script, SedBufData(&line), line.len, any);
    if (fp != stdin) fclose(fp);
    SedBufFree(&line);
    return true;
}

static bool SedParseArgs(SedState* st, CommandAST* cmd, SedBuf* script, bool* have_script, int* argi) {
    int argc = cmd->argc;
    int i = 1;
    for (; i < argc; i++) {
        const wchar_t* a = cmd->argv[i];
        if (a[0] != L'-' || a[1] == L'\0') break;
        if (wcscmp(a, L"--") == 0) {
            i++;
            break;
        }
        if (wcscmp(a, L"-V") == 0 || wcscmp(a, L"--version") == 0) {
            wprintf(L"sed (BSD-compatible) 1.0 (ntksh)\n");
            st->quit = true;
            return true;
        }
        if (a[1] == L'-') {
            if (wcscmp(a, L"--quiet") == 0 || wcscmp(a, L"--silent") == 0) st->quiet = true;
            else if (wcscmp(a, L"--regexp-extended") == 0) st->ere = true;
            else if (wcscmp(a, L"--separate") == 0) st->separate = true;
            else if (wcscmp(a, L"--unbuffered") == 0) st->unbuffered = true;
            else if (wcscmp(a, L"--in-place") == 0) {
                st->in_place = true;
                st->separate = true;
            } else {
                fwprintf(stderr, L"sed: unrecognized option '%ls'\n", a);
                SedUsage(stderr);
                return false;
            }
            continue;
        }
        for (size_t j = 1; a[j] != L'\0'; j++) {
            wchar_t o = a[j];
            const wchar_t* rest = a + j + 1;
            if (o == L'n') {
                st->quiet = true;
            } else if (o == L'E' || o == L'r') {
                st->ere = true;
            } else if (o == L'a') {
                st->delay_open = true;
            } else if (o == L's') {
                st->separate = true;
            } else if (o == L'u') {
                st->unbuffered = true;
            } else if (o == L'i' || o == L'I') {
                st->in_place = true;
                st->separate = true;
                if (*rest != L'\0') {
                    st->suffix = rest;
                } else if (i + 1 < argc && (cmd->argv[i + 1][0] == L'\0' || cmd->argv[i + 1][0] == L'.' ||
                           (!*have_script && i + 2 < argc && !wcschr(cmd->argv[i + 1], L'/') && !wcschr(cmd->argv[i + 1], L';')))) {
                    st->suffix = cmd->argv[++i];
                } else {
                    st->suffix = L"";
                }
                break;
            } else if (o == L'l') {
                if (*rest != L'\0' && iswdigit(*rest)) {
                    st->width = _wtoi(rest);
                    break;
                } else if (i + 1 < argc && iswdigit(cmd->argv[i + 1][0])) {
                    st->width = _wtoi(cmd->argv[++i]);
                    break;
                } else {
                    st->unbuffered = true; /* BSD line buffered */
                }
            } else if (o == L'e' || o == L'f') {
                const wchar_t* val;
                if (*rest) {
                    val = rest;
                } else if (i + 1 < argc) {
                    val = cmd->argv[++i];
                } else {
                    fwprintf(stderr, L"sed: option requires an argument -- %lc\n", o);
                    SedUsage(stderr);
                    return false;
                }
                if (o == L'e') {
                    SedScriptAdd(script, val, wcslen(val), have_script);
                } else if (o == L'f') {
                    if (!SedScriptFile(val, script, have_script)) return false;
                }
                break;
            } else {
                fwprintf(stderr, L"sed: illegal option -- %lc\n", o);
                SedUsage(stderr);
                return false;
            }
        }
    }
    *argi = i;
    return true;
}

/* ------------------------------------------------------------------------- */
/*                              File processing                              */
/* ------------------------------------------------------------------------- */

static void SedBackupName(const wchar_t* name, const wchar_t* suffix, wchar_t* out, size_t cap) {
    size_t dir_len = 0, base_start = 0;
    for (size_t k = 0; name[k]; k++) {
        if (name[k] == L'\\' || name[k] == L'/' || name[k] == L':') {
            dir_len = k + 1;
            base_start = k + 1;
        }
    }
    size_t base_len = wcslen(name + base_start);
    size_t o = 0;
    if (!wcschr(suffix, L'*')) {
        o = (swprintf_s(out, cap, L"%ls%ls", name, suffix) < 0) ? 0 : wcslen(out);
        return;
    }
    if (dir_len >= cap) dir_len = 0;
    if (dir_len) memcpy(out, name, dir_len * sizeof(wchar_t));
    o = dir_len;
    for (const wchar_t* s = suffix; *s && o + base_len + 1 < cap; s++) {
        if (*s == L'*') {
            memcpy(out + o, name + base_start, base_len * sizeof(wchar_t));
            o += base_len;
        } else {
            out[o++] = *s;
        }
    }
    out[o] = L'\0';
}

static void SedInPlaceFile(SedState* st, const wchar_t* name) {
    FILE* probe = SedOpenFile(name);
    if (!probe) {
        fwprintf(stderr, L"sed: %ls: No such file or directory\n", name);
        st->missing = true;
        return;
    }
    fclose(probe);

    wchar_t dir[KSH_PATH_MAX];
    size_t dir_len = 0;
    for (size_t k = 0; name[k] && k + 1 < KSH_PATH_MAX; k++) {
        if (name[k] == L'\\' || name[k] == L'/') dir_len = k + 1;
    }
    if (dir_len == 0) wcscpy_s(dir, KSH_PATH_MAX, L".");
    else {
        memcpy(dir, name, dir_len * sizeof(wchar_t));
        dir[dir_len] = L'\0';
    }

    wchar_t tmp[KSH_PATH_MAX];
    if (!GetTempFileNameW(dir, L"sed", 0, tmp)) {
        fwprintf(stderr, L"sed: %ls: cannot create temporary file\n", name);
        st->io_err = true;
        return;
    }
    FILE* ofp = _wfsopen(tmp, L"w, ccs=UTF-8", _SH_DENYNO);
    if (!ofp) {
        fwprintf(stderr, L"sed: %ls: cannot create temporary file\n", name);
        DeleteFileW(tmp);
        st->io_err = true;
        return;
    }

    SedOut out = {ofp, false};
    SedOut* saved = st->out;
    st->out = &out;
    st->line_no = 0;
    const wchar_t* names[1] = {name};
    SedInput in;
    SedInitInput(&in, names, 1);
    SedRunInput(st, &in);
    SedInDone(&in);
    st->out = saved;

    if (fclose(ofp) != 0) st->io_err = true;
    if (st->io_err) {
        DeleteFileW(tmp);
        return;
    }
    if (st->suffix && *st->suffix) {
        wchar_t backup[KSH_PATH_MAX];
        SedBackupName(name, st->suffix, backup, KSH_PATH_MAX);
        if (!MoveFileExW(name, backup, MOVEFILE_REPLACE_EXISTING)) {
            fwprintf(stderr, L"sed: cannot rename %ls\n", name);
            st->io_err = true;
            DeleteFileW(tmp);
            return;
        }
    }
    if (!MoveFileExW(tmp, name, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        fwprintf(stderr, L"sed: cannot replace %ls\n", name);
        st->io_err = true;
        DeleteFileW(tmp);
    }
}

static void SedRunFiles(SedState* st, const wchar_t* const* names, int count) {
    if (st->in_place || st->separate) {
        for (int i = 0; i < count && !st->quit; i++) {
            if (st->in_place && wcscmp(names[i], L"-") != 0) {
                SedInPlaceFile(st, names[i]);
            } else {
                const wchar_t* one[1] = {names[i]};
                SedInput in;
                SedInitInput(&in, one, 1);
                st->line_no = 0;
                SedRunInput(st, &in);
                SedInDone(&in);
            }
        }
        return;
    }
    SedInput in;
    SedInitInput(&in, names, count);
    st->line_no = 0;
    SedRunInput(st, &in);
    SedInDone(&in);
}

static void SedFreeState(SedState* st) {
    for (size_t i = 0; i < st->ncmds; i++) {
        SedCmd* c = &st->cmds[i];
        free(c->text);
        for (size_t k = 0; k < c->npieces; k++) free(c->pieces[k].text);
        free(c->pieces);
        free(c->ysrc);
        free(c->ydst);
        if (c->sre.prog) RegexFree(c->sre.prog);
        if (c->a1.kind == SED_ADDR_RE && c->a1.re.prog) RegexFree(c->a1.re.prog);
        if (c->a2.kind == SED_ADDR_RE && c->a2.re.prog) RegexFree(c->a2.re.prog);
    }
    free(st->cmds);
    for (SedFile* f = st->files; f;) {
        SedFile* next = f->next;
        if (f->own.fp) fclose(f->own.fp);
        if (f->rfp) fclose(f->rfp);
        free(f->name);
        free(f);
        f = next;
    }
    for (size_t i = 0; i < st->napp; i++) free(st->app[i].text);
    free(st->app);
    SedBufFree(&st->ps);
    SedBufFree(&st->hs);
    SedBufFree(&st->tmp);
    SedBufFree(&st->nb);
}

bool Builtin_Sed(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        SedHelp();
        *code = 0;
        return true;
    }

    SedState st;
    memset(&st, 0, sizeof(st));
    st.width = 70;
    st.suffix = L"";
    st.stdout_out.fp = stdout;
    st.stderr_out.fp = stderr;
    st.out = &st.stdout_out;

    SedBuf script = {0};
    bool have_script = false;
    int argi = 1;
    if (!SedParseArgs(&st, cmd, &script, &have_script, &argi)) {
        *code = 1;
        SedBufFree(&script);
        SedFreeState(&st);
        return true;
    }
    if (st.quit) {
        *code = 0;
        SedBufFree(&script);
        SedFreeState(&st);
        return true;
    }
    if (!have_script) {
        if (argi >= cmd->argc) {
            SedUsage(stderr);
            *code = 1;
            SedBufFree(&script);
            SedFreeState(&st);
            return true;
        }
        const wchar_t* arg = cmd->argv[argi];
        argi++;
        SedScriptAdd(&script, arg, wcslen(arg), &have_script);
    }

    bool ok = SedParseScript(&st, SedBufData(&script), script.len);
    SedBufFree(&script);
    if (ok) {
        const wchar_t* default_names[1] = {L"-"};
        if (argi < cmd->argc) {
            SedRunFiles(&st, (const wchar_t* const*)&cmd->argv[argi], cmd->argc - argi);
        } else {
            SedRunFiles(&st, default_names, 1);
        }
    }
    fflush(stdout);

    if (st.io_err || !ok) {
        *code = st.io_err ? 4 : 1;
    } else if (st.fatal) {
        *code = 1;
    } else if (st.missing) {
        *code = 2;
    } else {
        *code = st.quit_code;
    }
    SedFreeState(&st);
    return true;
}
