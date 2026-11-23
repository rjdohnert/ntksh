#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>
#include <wctype.h>
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <math.h>
#include <limits.h>
#include <time.h>

/* ========================================================================= */
/*        Category: awk (POSIX awk with BSD-compatible options)              */
/* ========================================================================= */

#define AWK_RX_CACHE 64
#define AWK_MAX_DEPTH 1000
#define AWK_IS_DIGIT(ch) ((ch) >= L'0' && (ch) <= L'9')

enum {
    AWKF_NUM = 1,
    AWKF_STR = 2
};

enum {
    AWKT_EOF, AWKT_NEWLINE, AWKT_LBRACE, AWKT_RBRACE, AWKT_LPAREN, AWKT_RPAREN,
    AWKT_LBRACKET, AWKT_RBRACKET, AWKT_SEMI, AWKT_COMMA, AWKT_PLUS, AWKT_MINUS,
    AWKT_STAR, AWKT_SLASH, AWKT_PERCENT, AWKT_CARET, AWKT_NOT, AWKT_GT, AWKT_LT,
    AWKT_PIPE, AWKT_QUESTION, AWKT_COLON, AWKT_TILDE, AWKT_NOMATCH, AWKT_DOLLAR,
    AWKT_ASSIGN, AWKT_ADD_ASSIGN, AWKT_SUB_ASSIGN, AWKT_MUL_ASSIGN, AWKT_DIV_ASSIGN,
    AWKT_MOD_ASSIGN, AWKT_POW_ASSIGN, AWKT_EQ, AWKT_NE, AWKT_LE, AWKT_GE,
    AWKT_APPEND, AWKT_AND, AWKT_OR, AWKT_INCR, AWKT_DECR,
    AWKT_NUMBER, AWKT_STRING, AWKT_REGEX, AWKT_NAME, AWKT_FUNC_NAME, AWKT_BUILTIN,
    AWKT_BEGIN, AWKT_END, AWKT_FUNCTION, AWKT_IF, AWKT_ELSE, AWKT_WHILE, AWKT_FOR,
    AWKT_DO, AWKT_BREAK, AWKT_CONTINUE, AWKT_NEXT, AWKT_NEXTFILE, AWKT_EXIT,
    AWKT_RETURN, AWKT_DELETE, AWKT_GETLINE, AWKT_PRINT, AWKT_PRINTF, AWKT_IN
};

enum {
    AWKB_LENGTH, AWKB_SUBSTR, AWKB_INDEX, AWKB_SPLIT, AWKB_SUB, AWKB_GSUB, AWKB_MATCH,
    AWKB_SPRINTF, AWKB_SIN, AWKB_COS, AWKB_ATAN2, AWKB_EXP, AWKB_LOG, AWKB_SQRT, AWKB_INT,
    AWKB_RAND, AWKB_SRAND, AWKB_TOLOWER, AWKB_TOUPPER, AWKB_FFLUSH, AWKB_SYSTEM, AWKB_CLOSE
};

enum {
    AWKN_NUM, AWKN_STR, AWKN_REGEX, AWKN_VAR, AWKN_ELEM, AWKN_FIELD, AWKN_ASSIGN,
    AWKN_COND, AWKN_AND, AWKN_OR, AWKN_NOT, AWKN_NEG, AWKN_PLUS, AWKN_ARITH, AWKN_CMP,
    AWKN_CONCAT, AWKN_MATCH, AWKN_IN, AWKN_INCDEC, AWKN_CALL, AWKN_BUILTIN, AWKN_GETLINE,
    AWKN_GROUP,
    AWKN_EXPR, AWKN_PRINT, AWKN_PRINTF, AWKN_IF, AWKN_WHILE, AWKN_DO, AWKN_FOR, AWKN_FORIN,
    AWKN_BLOCK, AWKN_NEXT, AWKN_NEXTFILE, AWKN_BREAK, AWKN_CONTINUE, AWKN_EXIT, AWKN_RETURN,
    AWKN_DELETE, AWKN_NOP
};

enum { AWKC_LT, AWKC_LE, AWKC_GT, AWKC_GE, AWKC_EQ, AWKC_NE };
enum { AWKG_PLAIN, AWKG_FILE, AWKG_CMD };
enum { AWKR_NONE, AWKR_FILE, AWKR_APPEND, AWKR_PIPE };
enum { AWKL_VAR, AWKL_ELEM, AWKL_FIELD };
enum { AWKX_NORMAL, AWKX_BREAK, AWKX_CONTINUE, AWKX_NEXT, AWKX_NEXTFILE, AWKX_EXIT, AWKX_RETURN };
enum { AWKI_BEGIN, AWKI_END, AWKI_MAIN };
enum { AWKP_RAW, AWKP_NODE };
enum { AWK_ST_OUTFILE, AWK_ST_OUTPIPE, AWK_ST_INFILE, AWK_ST_INPIPE };

enum {
    AWKV_NR, AWKV_NF, AWKV_FNR, AWKV_FS, AWKV_OFS, AWKV_ORS, AWKV_RS, SUBSEP_IDX,
    AWKV_RSTART, AWKV_RLENGTH, AWKV_FILENAME, AWKV_ENVIRON, AWKV_ARGC, AWKV_ARGV,
    AWKV_CONVFMT, AWKV_OFMT, AWKV_COUNT
};

static const wchar_t* const AwkSpecialNames[AWKV_COUNT] = {
    L"NR", L"NF", L"FNR", L"FS", L"OFS", L"ORS", L"RS", L"SUBSEP",
    L"RSTART", L"RLENGTH", L"FILENAME", L"ENVIRON", L"ARGC", L"ARGV",
    L"CONVFMT", L"OFMT"
};

typedef struct AwkValue {
    unsigned flags;
    double num;
    wchar_t* str;
} AwkValue;

typedef struct AwkElem {
    wchar_t* key;
    AwkValue v;
    struct AwkElem* next;
} AwkElem;

typedef struct AwkArray {
    AwkElem** buckets;
    size_t nbuckets;
    size_t count;
} AwkArray;

typedef struct AwkVar {
    AwkValue v;
    AwkArray* arr;
    bool borrowed;
} AwkVar;

typedef struct AwkBuf {
    wchar_t* p;
    size_t len;
    size_t cap;
} AwkBuf;

typedef struct AwkRxEntry {
    wchar_t* pat;
    RegexProg* prog;
} AwkRxEntry;

typedef struct AwkStream {
    wchar_t* name;
    int kind;
    FILE* fp;
    struct AwkStream* next;
} AwkStream;

typedef struct AwkTokn {
    int type;
    int line;
    double num;
    wchar_t* text;
    int builtin;
} AwkTokn;

typedef struct AwkNode {
    int op;
    int line;
    int idx;
    int sub;
    bool local;
    bool flag;
    double num;
    wchar_t* str;
    struct AwkNode* a;
    struct AwkNode* b;
    struct AwkNode* c;
    struct AwkNode* d;
    struct AwkNode** list;
    size_t nlist;
    size_t cap;
} AwkNode;

typedef struct AwkFunc {
    wchar_t* name;
    wchar_t** params;
    size_t nparams;
    AwkNode* body;
    bool defined;
} AwkFunc;

typedef struct AwkItem {
    int kind;
    AwkNode* pat1;
    AwkNode* pat2;
    AwkNode* action;
    bool in_range;
} AwkItem;

typedef struct AwkRef {
    int kind;
    AwkVar* var;
    AwkArray* arr;
    wchar_t* key;
    size_t field;
} AwkRef;

typedef struct AwkPoolEnt {
    void* p;
    int kind;
} AwkPoolEnt;

typedef struct AwkFmtSpec {
    bool left;
    bool zero;
    bool plus;
    bool space;
    bool alt;
    int width;
    int prec;
} AwkFmtSpec;

typedef struct AwkCtx {
    jmp_buf jb;
    AwkPoolEnt* pool;
    size_t npool;
    size_t cappool;

    const wchar_t* src;
    size_t srclen;
    size_t pos;
    int line;
    AwkTokn cur;
    int no_gt;
    int cur_func;

    wchar_t** gnames;
    int ngnames;
    int cgnames;
    AwkFunc* funcs;
    int nfuncs;
    int cfuncs;
    AwkItem* items;
    size_t nitems;
    size_t citems;

    AwkVar* globals;
    AwkVar* frame;
    int depth;
    AwkValue retval;
    wchar_t* rec;
    size_t reclen;
    bool rec_valid;
    bool split_valid;
    AwkValue* fields;
    size_t nf;
    size_t capf;
    int exit_code;
    unsigned int rng;
    bool safe_mode;
    AwkStream* streams;
    AwkRxEntry rx[AWK_RX_CACHE];
    int nrx;
} AwkCtx;

static void AwkFatal(AwkCtx* c, const wchar_t* fmt, ...) {
    va_list ap;
    fwprintf(stderr, L"awk: ");
    va_start(ap, fmt);
    vfwprintf(stderr, fmt, ap);
    va_end(ap);
    fputwc(L'\n', stderr);
    longjmp(c->jb, 1);
}

static void* AwkAllocRaw(AwkCtx* c, size_t n) {
    void* p = calloc(1, n ? n : 1);
    if (!p) AwkFatal(c, L"out of memory");
    return p;
}

static void AwkPoolAdd(AwkCtx* c, void* p, int kind) {
    if (c->npool == c->cappool) {
        size_t nc = c->cappool ? c->cappool * 2 : 256;
        AwkPoolEnt* np = (AwkPoolEnt*)realloc(c->pool, nc * sizeof(AwkPoolEnt));
        if (!np) AwkFatal(c, L"out of memory");
        c->pool = np;
        c->cappool = nc;
    }
    c->pool[c->npool].p = p;
    c->pool[c->npool].kind = kind;
    c->npool++;
}

static wchar_t* AwkDupN(AwkCtx* c, const wchar_t* s, size_t n) {
    wchar_t* p = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
    if (!p) AwkFatal(c, L"out of memory");
    if (n) memcpy(p, s, n * sizeof(wchar_t));
    p[n] = L'\0';
    return p;
}

static wchar_t* AwkPoolDupN(AwkCtx* c, const wchar_t* s, size_t n) {
    wchar_t* p = AwkDupN(c, s, n);
    AwkPoolAdd(c, p, AWKP_RAW);
    return p;
}

static void AwkBufReserve(AwkCtx* c, AwkBuf* b, size_t extra) {
    if (extra > SIZE_MAX - b->len - 2) AwkFatal(c, L"out of memory");
    if (b->len + extra + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 64;
        while (nc < b->len + extra + 1) {
            if (nc > (SIZE_MAX / 2) / sizeof(wchar_t)) {
                nc = b->len + extra + 1;
                break;
            }
            nc *= 2;
        }
        wchar_t* np = (wchar_t*)realloc(b->p, nc * sizeof(wchar_t));
        if (!np) AwkFatal(c, L"out of memory");
        b->p = np;
        b->cap = nc;
    }
}

static void AwkBufAdd(AwkCtx* c, AwkBuf* b, const wchar_t* s, size_t n) {
    AwkBufReserve(c, b, n);
    if (n) memcpy(b->p + b->len, s, n * sizeof(wchar_t));
    b->len += n;
    b->p[b->len] = L'\0';
}

static void AwkBufAddStr(AwkCtx* c, AwkBuf* b, const wchar_t* s) {
    AwkBufAdd(c, b, s, wcslen(s));
}

static wchar_t* AwkBufTake(AwkCtx* c, AwkBuf* b) {
    AwkBufReserve(c, b, 0);
    b->p[b->len] = L'\0';
    wchar_t* r = b->p;
    b->p = NULL;
    b->len = 0;
    b->cap = 0;
    return r;
}

static void AwkBufFree(AwkBuf* b) {
    free(b->p);
    b->p = NULL;
    b->len = 0;
    b->cap = 0;
}

static bool AwkIsBlank(wchar_t ch) {
    return ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r' || ch == L'\f' || ch == L'\v';
}

static AwkValue AwkNumV(double d) {
    AwkValue v;
    v.flags = AWKF_NUM;
    v.num = d;
    v.str = NULL;
    return v;
}

static AwkValue AwkStrOwnV(wchar_t* s) {
    AwkValue v;
    v.flags = AWKF_STR;
    v.num = 0;
    v.str = s;
    return v;
}

static AwkValue AwkUninitV(AwkCtx* c) {
    AwkValue v;
    v.flags = AWKF_NUM | AWKF_STR;
    v.num = 0;
    v.str = AwkDupN(c, L"", 0);
    return v;
}

static void AwkValFree(AwkValue* v) {
    if (v->flags & AWKF_STR) free(v->str);
    v->flags = 0;
    v->str = NULL;
    v->num = 0;
}

static AwkValue AwkValDup(AwkCtx* c, const AwkValue* v) {
    AwkValue r = *v;
    if (v->flags & AWKF_STR) r.str = AwkDupN(c, v->str, wcslen(v->str));
    return r;
}

static bool AwkScanNumber(const wchar_t* s, size_t len, size_t* start, size_t* end) {
    size_t i = 0;
    size_t digits = 0;
    while (i < len && AwkIsBlank(s[i])) i++;
    *start = i;
    size_t j = i;
    if (j < len && (s[j] == L'+' || s[j] == L'-')) j++;
    while (j < len && AWK_IS_DIGIT(s[j])) { j++; digits++; }
    if (j < len && s[j] == L'.') {
        j++;
        while (j < len && AWK_IS_DIGIT(s[j])) { j++; digits++; }
    }
    if (digits == 0) return false;
    if (j < len && (s[j] == L'e' || s[j] == L'E')) {
        size_t k = j + 1;
        if (k < len && (s[k] == L'+' || s[k] == L'-')) k++;
        if (k < len && AWK_IS_DIGIT(s[k])) {
            while (k < len && AWK_IS_DIGIT(s[k])) k++;
            j = k;
        }
    }
    *end = j;
    return true;
}

static double AwkSpanToDouble(AwkCtx* c, const wchar_t* s, size_t start, size_t end) {
    wchar_t* t = AwkDupN(c, s + start, end - start);
    double d = wcstod(t, NULL);
    free(t);
    return d;
}

static double AwkStrToNum(AwkCtx* c, const wchar_t* s, size_t len) {
    size_t st, en;
    if (!AwkScanNumber(s, len, &st, &en)) return 0.0;
    return AwkSpanToDouble(c, s, st, en);
}

static AwkValue AwkStrnumOwnV(AwkCtx* c, wchar_t* s) {
    AwkValue v = AwkStrOwnV(s);
    size_t len = wcslen(s);
    size_t st, en, k;
    if (AwkScanNumber(s, len, &st, &en)) {
        k = en;
        while (k < len && AwkIsBlank(s[k])) k++;
        if (k == len) {
            v.flags |= AWKF_NUM;
            v.num = AwkSpanToDouble(c, s, st, en);
        }
    }
    return v;
}

static double AwkNumOf(AwkCtx* c, const AwkValue* v) {
    if (v->flags & AWKF_NUM) return v->num;
    if (!(v->flags & AWKF_STR)) return 0.0;
    return AwkStrToNum(c, v->str, wcslen(v->str));
}

static bool AwkTruthy(const AwkValue* v) {
    if (v->flags & AWKF_NUM) return v->num != 0.0;
    if (v->flags & AWKF_STR) return v->str[0] != L'\0';
    return false;
}

static bool AwkIsIntegral(double d) {
    return d == floor(d) && fabs(d) < 1e18;
}

static long long AwkToLL(double d) {
    if (isnan(d)) return 0;
    if (d >= 9.2e18) return LLONG_MAX;
    if (d <= -9.2e18) return LLONG_MIN;
    return (long long)d;
}

static wchar_t* AwkFormatOwn(AwkCtx* c, const wchar_t* fmt, const AwkValue* args, size_t nargs);

static wchar_t* AwkNumToStr(AwkCtx* c, double d, const wchar_t* fmt) {
    if (AwkIsIntegral(d)) {
        wchar_t* s = (wchar_t*)AwkAllocRaw(c, 32 * sizeof(wchar_t));
        _snwprintf(s, 32, L"%lld", (long long)d);
        return s;
    }
    AwkValue v = AwkNumV(d);
    return AwkFormatOwn(c, fmt ? fmt : L"%.6g", &v, 1);
}

static const wchar_t* AwkFmtPtr(AwkCtx* c, int idx) {
    const AwkValue* v = &c->globals[idx].v;
    if (v->flags & AWKF_STR) return v->str;
    return L"%.6g";
}

static wchar_t* AwkToStr(AwkCtx* c, const AwkValue* v) {
    if (v->flags & AWKF_STR) return AwkDupN(c, v->str, wcslen(v->str));
    if (!(v->flags & AWKF_NUM)) return AwkDupN(c, L"", 0);
    return AwkNumToStr(c, v->num, AwkFmtPtr(c, AWKV_CONVFMT));
}

static wchar_t* AwkToOutStr(AwkCtx* c, const AwkValue* v) {
    if (v->flags & AWKF_STR) return AwkDupN(c, v->str, wcslen(v->str));
    if (!(v->flags & AWKF_NUM)) return AwkDupN(c, L"", 0);
    return AwkNumToStr(c, v->num, AwkFmtPtr(c, AWKV_OFMT));
}

static void AwkFmtNum(AwkCtx* c, AwkBuf* out, const AwkFmtSpec* sp, wchar_t conv, const wchar_t* lenmod, ...) {
    AwkBuf spec = { NULL, 0, 0 };
    wchar_t num[24];
    va_list ap;
    AwkBufAdd(c, &spec, L"%", 1);
    if (sp->left) AwkBufAdd(c, &spec, L"-", 1);
    if (sp->plus) AwkBufAdd(c, &spec, L"+", 1);
    if (sp->space) AwkBufAdd(c, &spec, L" ", 1);
    if (sp->alt) AwkBufAdd(c, &spec, L"#", 1);
    if (sp->zero) AwkBufAdd(c, &spec, L"0", 1);
    if (sp->width >= 0) {
        _snwprintf(num, 24, L"%d", sp->width);
        AwkBufAddStr(c, &spec, num);
    }
    if (sp->prec >= 0) {
        _snwprintf(num, 24, L".%d", sp->prec);
        AwkBufAddStr(c, &spec, num);
    }
    AwkBufAddStr(c, &spec, lenmod);
    AwkBufAdd(c, &spec, &conv, 1);
    va_start(ap, lenmod);
    int n = _vscwprintf(spec.p, ap);
    va_end(ap);
    if (n > 0) {
        AwkBufReserve(c, out, (size_t)n);
        va_start(ap, lenmod);
        _vsnwprintf(out->p + out->len, (size_t)n + 1, spec.p, ap);
        va_end(ap);
        out->len += (size_t)n;
    }
    AwkBufFree(&spec);
}

static void AwkPadOut(AwkCtx* c, AwkBuf* out, const wchar_t* s, size_t n, int width, bool left) {
    size_t pad = (width > 0 && (size_t)width > n) ? (size_t)width - n : 0;
    wchar_t sp = L' ';
    if (!left) { for (size_t i = 0; i < pad; i++) AwkBufAdd(c, out, &sp, 1); }
    AwkBufAdd(c, out, s, n);
    if (left) { for (size_t i = 0; i < pad; i++) AwkBufAdd(c, out, &sp, 1); }
}

static double AwkArgNum(AwkCtx* c, const AwkValue* args, size_t nargs, size_t idx) {
    return idx < nargs ? AwkNumOf(c, &args[idx]) : 0.0;
}

static wchar_t* AwkArgStr(AwkCtx* c, const AwkValue* args, size_t nargs, size_t idx) {
    if (idx < nargs) return AwkToStr(c, &args[idx]);
    return AwkDupN(c, L"", 0);
}

static int AwkDigitStep(int acc, wchar_t ch) {
    if (acc > 100000000) return acc;
    return acc * 10 + (ch - L'0');
}

static void AwkFormatTo(AwkCtx* c, AwkBuf* out, const wchar_t* fmt, const AwkValue* args, size_t nargs) {
    size_t ai = 0;
    size_t n = wcslen(fmt);
    size_t i = 0;
    while (i < n) {
        if (fmt[i] != L'%') {
            size_t run = i;
            while (i < n && fmt[i] != L'%') i++;
            AwkBufAdd(c, out, fmt + run, i - run);
            continue;
        }
        if (i + 1 < n && fmt[i + 1] == L'%') {
            AwkBufAdd(c, out, L"%", 1);
            i += 2;
            continue;
        }
        size_t start = i;
        size_t j = i + 1;
        AwkFmtSpec sp;
        memset(&sp, 0, sizeof sp);
        sp.width = -1;
        sp.prec = -1;
        while (j < n && wcschr(L"-+ #0", fmt[j])) {
            switch (fmt[j]) {
            case L'-': sp.left = true; break;
            case L'+': sp.plus = true; break;
            case L' ': sp.space = true; break;
            case L'#': sp.alt = true; break;
            default: sp.zero = true; break;
            }
            j++;
        }
        if (j < n && fmt[j] == L'*') {
            int w = (int)AwkToLL(AwkArgNum(c, args, nargs, ai++));
            if (w < 0) { sp.left = true; w = -w; }
            sp.width = w;
            j++;
        } else {
            while (j < n && AWK_IS_DIGIT(fmt[j])) {
                sp.width = AwkDigitStep(sp.width < 0 ? 0 : sp.width, fmt[j]);
                j++;
            }
        }
        if (j < n && fmt[j] == L'.') {
            j++;
            sp.prec = 0;
            if (j < n && fmt[j] == L'*') {
                int p = (int)AwkToLL(AwkArgNum(c, args, nargs, ai++));
                sp.prec = p < 0 ? -1 : p;
                j++;
            } else {
                while (j < n && AWK_IS_DIGIT(fmt[j])) {
                    sp.prec = AwkDigitStep(sp.prec, fmt[j]);
                    j++;
                }
            }
        }
        while (j < n && wcschr(L"hlLqjzt", fmt[j])) j++;
        if (j >= n) {
            AwkBufAdd(c, out, fmt + start, n - start);
            break;
        }
        wchar_t conv = fmt[j];
        i = j + 1;
        switch (conv) {
        case L'd':
        case L'i': {
            long long v = AwkToLL(AwkArgNum(c, args, nargs, ai++));
            AwkFmtNum(c, out, &sp, L'd', L"ll", v);
            break;
        }
        case L'o':
        case L'x':
        case L'X':
        case L'u': {
            unsigned long long v = (unsigned long long)AwkToLL(AwkArgNum(c, args, nargs, ai++));
            AwkFmtNum(c, out, &sp, conv, L"ll", v);
            break;
        }
        case L'e':
        case L'E':
        case L'f':
        case L'F':
        case L'g':
        case L'G': {
            double v = AwkArgNum(c, args, nargs, ai++);
            AwkFmtNum(c, out, &sp, conv == L'F' ? L'f' : conv, L"", v);
            break;
        }
        case L'c': {
            wchar_t ch = L'\0';
            size_t len = 0;
            if (ai < nargs && (args[ai].flags & AWKF_NUM) &&
                !((args[ai].flags & AWKF_STR) && args[ai].str[0] != L'\0')) {
                ch = (wchar_t)AwkToLL(args[ai].num);
                len = 1;
            } else if (ai < nargs) {
                wchar_t* s = AwkToStr(c, &args[ai]);
                if (s[0] != L'\0') { ch = s[0]; len = 1; }
                free(s);
            }
            ai++;
            AwkPadOut(c, out, &ch, len, sp.width, sp.left);
            break;
        }
        case L's': {
            wchar_t* s = AwkArgStr(c, args, nargs, ai++);
            size_t len = wcslen(s);
            if (sp.prec >= 0 && (size_t)sp.prec < len) len = (size_t)sp.prec;
            AwkPadOut(c, out, s, len, sp.width, sp.left);
            free(s);
            break;
        }
        default:
            AwkBufAdd(c, out, fmt + start, i - start);
            break;
        }
    }
}

static wchar_t* AwkFormatOwn(AwkCtx* c, const wchar_t* fmt, const AwkValue* args, size_t nargs) {
    AwkBuf b = { NULL, 0, 0 };
    AwkFormatTo(c, &b, fmt, args, nargs);
    return AwkBufTake(c, &b);
}

static size_t AwkHashKey(const wchar_t* s) {
    size_t h = 2166136261u;
    while (*s) {
        h ^= (size_t)*s++;
        h *= 16777619u;
    }
    return h;
}

static AwkArray* AwkArrayNew(AwkCtx* c) {
    AwkArray* a = (AwkArray*)AwkAllocRaw(c, sizeof(AwkArray));
    a->nbuckets = 16;
    a->buckets = (AwkElem**)AwkAllocRaw(c, a->nbuckets * sizeof(AwkElem*));
    a->count = 0;
    return a;
}

static AwkElem* AwkArrayFind(const AwkArray* a, const wchar_t* key) {
    if (!a || !a->nbuckets) return NULL;
    AwkElem* e = a->buckets[AwkHashKey(key) % a->nbuckets];
    while (e && wcscmp(e->key, key) != 0) e = e->next;
    return e;
}

static void AwkArrayRehash(AwkCtx* c, AwkArray* a) {
    size_t nb = a->nbuckets * 2;
    AwkElem** nbk = (AwkElem**)AwkAllocRaw(c, nb * sizeof(AwkElem*));
    for (size_t i = 0; i < a->nbuckets; i++) {
        AwkElem* e = a->buckets[i];
        while (e) {
            AwkElem* nx = e->next;
            size_t h = AwkHashKey(e->key) % nb;
            e->next = nbk[h];
            nbk[h] = e;
            e = nx;
        }
    }
    free(a->buckets);
    a->buckets = nbk;
    a->nbuckets = nb;
}

static AwkElem* AwkArrayGet(AwkCtx* c, AwkArray* a, const wchar_t* key) {
    AwkElem* e = AwkArrayFind(a, key);
    if (e) return e;
    if (a->count >= a->nbuckets * 2) AwkArrayRehash(c, a);
    e = (AwkElem*)AwkAllocRaw(c, sizeof(AwkElem));
    e->key = AwkDupN(c, key, wcslen(key));
    e->v = AwkUninitV(c);
    size_t h = AwkHashKey(key) % a->nbuckets;
    e->next = a->buckets[h];
    a->buckets[h] = e;
    a->count++;
    return e;
}

static void AwkArrayRemove(AwkArray* a, const wchar_t* key) {
    if (!a || !a->nbuckets) return;
    AwkElem** pp = &a->buckets[AwkHashKey(key) % a->nbuckets];
    while (*pp) {
        if (wcscmp((*pp)->key, key) == 0) {
            AwkElem* e = *pp;
            *pp = e->next;
            AwkValFree(&e->v);
            free(e->key);
            free(e);
            a->count--;
            return;
        }
        pp = &(*pp)->next;
    }
}

static void AwkArrayClear(AwkArray* a) {
    if (!a) return;
    for (size_t i = 0; i < a->nbuckets; i++) {
        AwkElem* e = a->buckets[i];
        while (e) {
            AwkElem* nx = e->next;
            AwkValFree(&e->v);
            free(e->key);
            free(e);
            e = nx;
        }
        a->buckets[i] = NULL;
    }
    a->count = 0;
}

static void AwkArrayDestroy(AwkArray* a) {
    if (!a) return;
    AwkArrayClear(a);
    free(a->buckets);
    free(a);
}

static wchar_t** AwkArrayKeys(AwkCtx* c, const AwkArray* a, size_t* n) {
    if (!a || a->count == 0) {
        *n = 0;
        return NULL;
    }
    wchar_t** keys = (wchar_t**)AwkAllocRaw(c, a->count * sizeof(wchar_t*));
    size_t k = 0;
    for (size_t i = 0; i < a->nbuckets; i++) {
        for (AwkElem* e = a->buckets[i]; e; e = e->next) {
            keys[k++] = AwkDupN(c, e->key, wcslen(e->key));
        }
    }
    *n = k;
    return keys;
}

/* ========================================================================= */
/*                          AST and Node helpers                             */
/* ========================================================================= */

static RegexProg* AwkGetRegex(AwkCtx* c, const wchar_t* pat) {
    for (int i = 0; i < c->nrx; i++) {
        if (wcscmp(c->rx[i].pat, pat) == 0) return c->rx[i].prog;
    }
    RegexProg* prog = NULL;
    const wchar_t* err = NULL;
    if (!RegexCompile(pat, true, false, &prog, &err)) {
        AwkFatal(c, L"invalid regex /%ls/: %ls", pat, err ? err : L"syntax error");
    }
    if (c->nrx < AWK_RX_CACHE) {
        c->rx[c->nrx].pat = AwkPoolDupN(c, pat, wcslen(pat));
        c->rx[c->nrx].prog = prog;
        c->nrx++;
    }
    return prog;
}

static AwkNode* AwkNodeNew(AwkCtx* c, int op) {
    AwkNode* n = (AwkNode*)AwkAllocRaw(c, sizeof(AwkNode));
    n->op = op;
    n->line = c->line;
    n->idx = -1;
    n->sub = -1;
    AwkPoolAdd(c, n, AWKP_NODE);
    return n;
}

static void AwkNodeAdd(AwkCtx* c, AwkNode* parent, AwkNode* child) {
    if (parent->nlist == parent->cap) {
        size_t nc = parent->cap ? parent->cap * 2 : 4;
        AwkNode** nl = (AwkNode**)realloc(parent->list, nc * sizeof(AwkNode*));
        if (!nl) AwkFatal(c, L"out of memory");
        parent->list = nl;
        parent->cap = nc;
    }
    parent->list[parent->nlist++] = child;
}

static int AwkFindOrAddGlobal(AwkCtx* c, const wchar_t* name) {
    for (int i = 0; i < AWKV_COUNT; i++) {
        if (wcscmp(name, AwkSpecialNames[i]) == 0) return i;
    }
    for (int i = 0; i < c->ngnames; i++) {
        if (wcscmp(name, c->gnames[i]) == 0) return AWKV_COUNT + i;
    }
    if (c->ngnames == c->cgnames) {
        int nc = c->cgnames ? c->cgnames * 2 : 16;
        wchar_t** ng = (wchar_t**)realloc(c->gnames, nc * sizeof(wchar_t*));
        if (!ng) AwkFatal(c, L"out of memory");
        c->gnames = ng;
        c->cgnames = nc;
    }
    c->gnames[c->ngnames] = AwkPoolDupN(c, name, wcslen(name));
    return AWKV_COUNT + (c->ngnames++);
}

static int AwkFindParam(AwkCtx* c, const wchar_t* name) {
    if (c->cur_func < 0 || c->cur_func >= c->nfuncs) return -1;
    AwkFunc* fn = &c->funcs[c->cur_func];
    for (size_t i = 0; i < fn->nparams; i++) {
        if (wcscmp(name, fn->params[i]) == 0) return (int)i;
    }
    return -1;
}

/* ========================================================================= */
/*                                Lexer                                      */
/* ========================================================================= */

static const struct { const wchar_t* name; int tok; int bi; } kKeywords[] = {
    { L"BEGIN", AWKT_BEGIN, -1 }, { L"END", AWKT_END, -1 },
    { L"if", AWKT_IF, -1 }, { L"else", AWKT_ELSE, -1 },
    { L"while", AWKT_WHILE, -1 }, { L"for", AWKT_FOR, -1 }, { L"do", AWKT_DO, -1 },
    { L"break", AWKT_BREAK, -1 }, { L"continue", AWKT_CONTINUE, -1 },
    { L"next", AWKT_NEXT, -1 }, { L"nextfile", AWKT_NEXTFILE, -1 },
    { L"exit", AWKT_EXIT, -1 }, { L"return", AWKT_RETURN, -1 },
    { L"delete", AWKT_DELETE, -1 }, { L"getline", AWKT_GETLINE, -1 },
    { L"print", AWKT_PRINT, -1 }, { L"printf", AWKT_PRINTF, -1 },
    { L"in", AWKT_IN, -1 }, { L"function", AWKT_FUNCTION, -1 },
    { L"length", AWKT_BUILTIN, AWKB_LENGTH }, { L"substr", AWKT_BUILTIN, AWKB_SUBSTR },
    { L"index", AWKT_BUILTIN, AWKB_INDEX }, { L"split", AWKT_BUILTIN, AWKB_SPLIT },
    { L"sub", AWKT_BUILTIN, AWKB_SUB }, { L"gsub", AWKT_BUILTIN, AWKB_GSUB },
    { L"match", AWKT_BUILTIN, AWKB_MATCH }, { L"sprintf", AWKT_BUILTIN, AWKB_SPRINTF },
    { L"sin", AWKT_BUILTIN, AWKB_SIN }, { L"cos", AWKT_BUILTIN, AWKB_COS },
    { L"atan2", AWKT_BUILTIN, AWKB_ATAN2 }, { L"exp", AWKT_BUILTIN, AWKB_EXP },
    { L"log", AWKT_BUILTIN, AWKB_LOG }, { L"sqrt", AWKT_BUILTIN, AWKB_SQRT },
    { L"int", AWKT_BUILTIN, AWKB_INT }, { L"rand", AWKT_BUILTIN, AWKB_RAND },
    { L"srand", AWKT_BUILTIN, AWKB_SRAND }, { L"tolower", AWKT_BUILTIN, AWKB_TOLOWER },
    { L"toupper", AWKT_BUILTIN, AWKB_TOUPPER }, { L"fflush", AWKT_BUILTIN, AWKB_FFLUSH },
    { L"system", AWKT_BUILTIN, AWKB_SYSTEM }, { L"close", AWKT_BUILTIN, AWKB_CLOSE }
};

static void AwkLexNext(AwkCtx* c, bool want_regex) {
    const wchar_t* s = c->src;
    size_t len = c->srclen;

    while (c->pos < len) {
        wchar_t ch = s[c->pos];
        if (ch == L' ' || ch == L'\t' || ch == L'\r') {
            c->pos++;
            continue;
        }
        if (ch == L'\\' && c->pos + 1 < len && s[c->pos + 1] == L'\n') {
            c->pos += 2;
            c->line++;
            continue;
        }
        if (ch == L'#') {
            while (c->pos < len && s[c->pos] != L'\n') c->pos++;
            continue;
        }
        break;
    }

    c->cur.line = c->line;
    c->cur.text = NULL;
    c->cur.num = 0;
    c->cur.builtin = -1;

    if (c->pos >= len) {
        c->cur.type = AWKT_EOF;
        return;
    }

    wchar_t ch = s[c->pos];

    if (ch == L'\n') {
        c->pos++;
        c->line++;
        c->cur.type = AWKT_NEWLINE;
        return;
    }

    if (want_regex && ch == L'/') {
        c->pos++;
        AwkBuf b = { NULL, 0, 0 };
        while (c->pos < len && s[c->pos] != L'/') {
            if (s[c->pos] == L'\\') {
                if (c->pos + 1 < len && s[c->pos + 1] == L'/') {
                    AwkBufAdd(c, &b, L"/", 1);
                    c->pos += 2;
                    continue;
                }
                AwkBufAdd(c, &b, s + c->pos, 1);
                c->pos++;
            }
            if (c->pos < len) {
                AwkBufAdd(c, &b, s + c->pos, 1);
                c->pos++;
            }
        }
        if (c->pos >= len) AwkFatal(c, L"unterminated regular expression");
        c->pos++; /* skip closing / */
        c->cur.type = AWKT_REGEX;
        c->cur.text = AwkPoolDupN(c, b.p ? b.p : L"", b.len);
        AwkBufFree(&b);
        return;
    }

    if (ch == L'"') {
        c->pos++;
        AwkBuf b = { NULL, 0, 0 };
        while (c->pos < len && s[c->pos] != L'"') {
            if (s[c->pos] == L'\\') {
                c->pos++;
                if (c->pos >= len) break;
                wchar_t esc = s[c->pos++];
                switch (esc) {
                case L'n': AwkBufAdd(c, &b, L"\n", 1); break;
                case L't': AwkBufAdd(c, &b, L"\t", 1); break;
                case L'r': AwkBufAdd(c, &b, L"\r", 1); break;
                case L'b': AwkBufAdd(c, &b, L"\b", 1); break;
                case L'f': AwkBufAdd(c, &b, L"\f", 1); break;
                case L'v': AwkBufAdd(c, &b, L"\v", 1); break;
                case L'a': AwkBufAdd(c, &b, L"\a", 1); break;
                case L'\\': AwkBufAdd(c, &b, L"\\", 1); break;
                case L'"': AwkBufAdd(c, &b, L"\"", 1); break;
                case L'x': {
                    wchar_t hex[3] = {0};
                    int k = 0;
                    while (k < 2 && c->pos < len && iswxdigit(s[c->pos])) hex[k++] = s[c->pos++];
                    wchar_t code = (wchar_t)wcstol(hex, NULL, 16);
                    AwkBufAdd(c, &b, &code, 1);
                    break;
                }
                default:
                    if (esc >= L'0' && esc <= L'7') {
                        wchar_t oct[4] = { esc, 0, 0, 0 };
                        int k = 1;
                        while (k < 3 && c->pos < len && s[c->pos] >= L'0' && s[c->pos] <= L'7') oct[k++] = s[c->pos++];
                        wchar_t code = (wchar_t)wcstol(oct, NULL, 8);
                        AwkBufAdd(c, &b, &code, 1);
                    } else {
                        AwkBufAdd(c, &b, &esc, 1);
                    }
                    break;
                }
            } else {
                AwkBufAdd(c, &b, s + c->pos, 1);
                c->pos++;
            }
        }
        if (c->pos >= len) AwkFatal(c, L"unterminated string constant");
        c->pos++; /* skip closing quote */
        c->cur.type = AWKT_STRING;
        c->cur.text = AwkPoolDupN(c, b.p ? b.p : L"", b.len);
        AwkBufFree(&b);
        return;
    }

    if (AWK_IS_DIGIT(ch) || (ch == L'.' && c->pos + 1 < len && AWK_IS_DIGIT(s[c->pos + 1]))) {
        size_t start = c->pos;
        size_t st, en;
        if (AwkScanNumber(s + start, len - start, &st, &en)) {
            c->cur.type = AWKT_NUMBER;
            c->cur.num = AwkSpanToDouble(c, s + start, st, en);
            c->pos = start + en;
            return;
        }
    }

    if (iswalpha(ch) || ch == L'_') {
        size_t start = c->pos;
        while (c->pos < len && (iswalnum(s[c->pos]) || s[c->pos] == L'_')) c->pos++;
        size_t n = c->pos - start;
        wchar_t* name = AwkPoolDupN(c, s + start, n);
        c->cur.text = name;

        for (size_t i = 0; i < sizeof(kKeywords) / sizeof(kKeywords[0]); i++) {
            if (wcscmp(name, kKeywords[i].name) == 0) {
                c->cur.type = kKeywords[i].tok;
                c->cur.builtin = kKeywords[i].bi;
                return;
            }
        }
        if (c->pos < len && s[c->pos] == L'(') {
            c->cur.type = AWKT_FUNC_NAME;
        } else {
            c->cur.type = AWKT_NAME;
        }
        return;
    }

    c->pos++;
    wchar_t c2 = (c->pos < len) ? s[c->pos] : L'\0';

    switch (ch) {
    case L'{': c->cur.type = AWKT_LBRACE; return;
    case L'}': c->cur.type = AWKT_RBRACE; return;
    case L'(': c->cur.type = AWKT_LPAREN; return;
    case L')': c->cur.type = AWKT_RPAREN; return;
    case L'[': c->cur.type = AWKT_LBRACKET; return;
    case L']': c->cur.type = AWKT_RBRACKET; return;
    case L';': c->cur.type = AWKT_SEMI; return;
    case L',': c->cur.type = AWKT_COMMA; return;
    case L'?': c->cur.type = AWKT_QUESTION; return;
    case L':': c->cur.type = AWKT_COLON; return;
    case L'$': c->cur.type = AWKT_DOLLAR; return;
    case L'+':
        if (c2 == L'+') { c->pos++; c->cur.type = AWKT_INCR; return; }
        if (c2 == L'=') { c->pos++; c->cur.type = AWKT_ADD_ASSIGN; return; }
        c->cur.type = AWKT_PLUS; return;
    case L'-':
        if (c2 == L'-') { c->pos++; c->cur.type = AWKT_DECR; return; }
        if (c2 == L'=') { c->pos++; c->cur.type = AWKT_SUB_ASSIGN; return; }
        c->cur.type = AWKT_MINUS; return;
    case L'*':
        if (c2 == L'*') {
            c->pos++;
            if (c->pos < len && s[c->pos] == L'=') { c->pos++; c->cur.type = AWKT_POW_ASSIGN; return; }
            c->cur.type = AWKT_CARET; return;
        }
        if (c2 == L'=') { c->pos++; c->cur.type = AWKT_MUL_ASSIGN; return; }
        c->cur.type = AWKT_STAR; return;
    case L'/':
        if (c2 == L'=') { c->pos++; c->cur.type = AWKT_DIV_ASSIGN; return; }
        c->cur.type = AWKT_SLASH; return;
    case L'%':
        if (c2 == L'=') { c->pos++; c->cur.type = AWKT_MOD_ASSIGN; return; }
        c->cur.type = AWKT_PERCENT; return;
    case L'^':
        if (c2 == L'=') { c->pos++; c->cur.type = AWKT_POW_ASSIGN; return; }
        c->cur.type = AWKT_CARET; return;
    case L'=':
        if (c2 == L'=') { c->pos++; c->cur.type = AWKT_EQ; return; }
        c->cur.type = AWKT_ASSIGN; return;
    case L'!':
        if (c2 == L'=') { c->pos++; c->cur.type = AWKT_NE; return; }
        if (c2 == L'~') { c->pos++; c->cur.type = AWKT_NOMATCH; return; }
        c->cur.type = AWKT_NOT; return;
    case L'<':
        if (c2 == L'=') { c->pos++; c->cur.type = AWKT_LE; return; }
        c->cur.type = AWKT_LT; return;
    case L'>':
        if (c2 == L'=') { c->pos++; c->cur.type = AWKT_GE; return; }
        if (c2 == L'>') { c->pos++; c->cur.type = AWKT_APPEND; return; }
        c->cur.type = AWKT_GT; return;
    case L'&':
        if (c2 == L'&') { c->pos++; c->cur.type = AWKT_AND; return; }
        break;
    case L'|':
        if (c2 == L'|') { c->pos++; c->cur.type = AWKT_OR; return; }
        c->cur.type = AWKT_PIPE; return;
    case L'~':
        c->cur.type = AWKT_TILDE; return;
    }

    AwkFatal(c, L"unexpected character '%lc' (line %d)", ch, c->line);
}

static void AwkSkipNewlines(AwkCtx* c) {
    while (c->cur.type == AWKT_NEWLINE) AwkLexNext(c, false);
}

static void AwkExpect(AwkCtx* c, int type, const wchar_t* msg) {
    if (c->cur.type != type) {
        AwkFatal(c, L"syntax error at line %d: %ls", c->line, msg);
    }
    AwkLexNext(c, false);
}

/* ========================================================================= */
/*                              Parser                                       */
/* ========================================================================= */

static AwkNode* AwkParseExpr(AwkCtx* c, int min_prec);
static AwkNode* AwkParseStmt(AwkCtx* c);

static AwkNode* AwkParsePrimary(AwkCtx* c) {
    int tok = c->cur.type;

    if (tok == AWKT_NUMBER) {
        AwkNode* n = AwkNodeNew(c, AWKN_NUM);
        n->num = c->cur.num;
        AwkLexNext(c, false);
        return n;
    }
    if (tok == AWKT_STRING) {
        AwkNode* n = AwkNodeNew(c, AWKN_STR);
        n->str = c->cur.text;
        AwkLexNext(c, false);
        return n;
    }
    if (tok == AWKT_REGEX) {
        AwkNode* n = AwkNodeNew(c, AWKN_REGEX);
        n->str = c->cur.text;
        AwkLexNext(c, false);
        return n;
    }
    if (tok == AWKT_DOLLAR) {
        AwkLexNext(c, true);
        AwkNode* n = AwkNodeNew(c, AWKN_FIELD);
        n->a = AwkParseExpr(c, 11); /* high precedence for $ */
        return n;
    }
    if (tok == AWKT_NOT) {
        AwkLexNext(c, true);
        AwkNode* n = AwkNodeNew(c, AWKN_NOT);
        n->a = AwkParseExpr(c, 10);
        return n;
    }
    if (tok == AWKT_PLUS) {
        AwkLexNext(c, true);
        AwkNode* n = AwkNodeNew(c, AWKN_PLUS);
        n->a = AwkParseExpr(c, 10);
        return n;
    }
    if (tok == AWKT_MINUS) {
        AwkLexNext(c, true);
        AwkNode* n = AwkNodeNew(c, AWKN_NEG);
        n->a = AwkParseExpr(c, 10);
        return n;
    }
    if (tok == AWKT_INCR || tok == AWKT_DECR) {
        int op = tok;
        AwkLexNext(c, false);
        AwkNode* target = AwkParsePrimary(c);
        AwkNode* n = AwkNodeNew(c, AWKN_INCDEC);
        n->a = target;
        n->idx = (op == AWKT_INCR) ? 1 : -1;
        n->flag = false; /* pre-increment */
        return n;
    }
    if (tok == AWKT_LPAREN) {
        AwkLexNext(c, true);
        AwkNode* n = AwkParseExpr(c, 0);
        if (c->cur.type == AWKT_COMMA) {
            /* Grouping: (a, b, ...) e.g. for `(i, j) in arr` */
            AwkNode* grp = AwkNodeNew(c, AWKN_GROUP);
            AwkNodeAdd(c, grp, n);
            while (c->cur.type == AWKT_COMMA) {
                AwkLexNext(c, true);
                AwkNodeAdd(c, grp, AwkParseExpr(c, 0));
            }
            n = grp;
        }
        AwkExpect(c, AWKT_RPAREN, L"expected ')'");
        return n;
    }
    if (tok == AWKT_GETLINE) {
        AwkLexNext(c, false);
        AwkNode* n = AwkNodeNew(c, AWKN_GETLINE);
        n->idx = AWKG_PLAIN;
        if (c->cur.type == AWKT_NAME) {
            n->a = AwkParsePrimary(c);
        }
        if (c->cur.type == AWKT_LT) {
            AwkLexNext(c, false);
            n->idx = AWKG_FILE;
            n->b = AwkParseExpr(c, 9);
        }
        return n;
    }
    if (tok == AWKT_BUILTIN) {
        int bi = c->cur.builtin;
        AwkLexNext(c, false);
        AwkNode* n = AwkNodeNew(c, AWKN_BUILTIN);
        n->idx = bi;
        if (c->cur.type == AWKT_LPAREN) {
            AwkLexNext(c, true);
            if (c->cur.type != AWKT_RPAREN) {
                for (;;) {
                    AwkNodeAdd(c, n, AwkParseExpr(c, 0));
                    if (c->cur.type == AWKT_COMMA) {
                        AwkLexNext(c, true);
                    } else break;
                }
            }
            AwkExpect(c, AWKT_RPAREN, L"expected ')' after builtin arguments");
        } else if (bi == AWKB_LENGTH) {
            /* length without parentheses defaults to length($0) */
        }
        return n;
    }
    if (tok == AWKT_FUNC_NAME || tok == AWKT_NAME) {
        wchar_t* name = c->cur.text;
        AwkLexNext(c, false);
        if (c->cur.type == AWKT_LPAREN) {
            /* Function call */
            AwkLexNext(c, true);
            AwkNode* n = AwkNodeNew(c, AWKN_CALL);
            n->str = name;
            if (c->cur.type != AWKT_RPAREN) {
                for (;;) {
                    AwkNodeAdd(c, n, AwkParseExpr(c, 0));
                    if (c->cur.type == AWKT_COMMA) {
                        AwkLexNext(c, true);
                    } else break;
                }
            }
            AwkExpect(c, AWKT_RPAREN, L"expected ')' after function arguments");
            return n;
        }

        int pidx = AwkFindParam(c, name);
        AwkNode* var = AwkNodeNew(c, AWKN_VAR);
        var->str = name;
        if (pidx >= 0) {
            var->local = true;
            var->idx = pidx;
        } else {
            var->local = false;
            var->idx = AwkFindOrAddGlobal(c, name);
        }

        if (c->cur.type == AWKT_LBRACKET) {
            /* Array indexing */
            AwkLexNext(c, true);
            AwkNode* elem = AwkNodeNew(c, AWKN_ELEM);
            elem->a = var;
            for (;;) {
                AwkNodeAdd(c, elem, AwkParseExpr(c, 0));
                if (c->cur.type == AWKT_COMMA) {
                    AwkLexNext(c, true);
                } else break;
            }
            AwkExpect(c, AWKT_RBRACKET, L"expected ']' after array subscript");
            return elem;
        }
        return var;
    }

    AwkFatal(c, L"unexpected token in expression at line %d", c->line);
    return NULL;
}

static bool AwkCanStartConcat(int tok) {
    return tok == AWKT_NUMBER || tok == AWKT_STRING || tok == AWKT_NAME ||
           tok == AWKT_FUNC_NAME || tok == AWKT_DOLLAR || tok == AWKT_LPAREN ||
           tok == AWKT_BUILTIN;
}

static int AwkOpPrecedence(int tok) {
    switch (tok) {
    case AWKT_ASSIGN: case AWKT_ADD_ASSIGN: case AWKT_SUB_ASSIGN:
    case AWKT_MUL_ASSIGN: case AWKT_DIV_ASSIGN: case AWKT_MOD_ASSIGN:
    case AWKT_POW_ASSIGN:
        return 1;
    case AWKT_QUESTION:
        return 2;
    case AWKT_OR:
        return 3;
    case AWKT_AND:
        return 4;
    case AWKT_IN:
        return 5;
    case AWKT_TILDE: case AWKT_NOMATCH:
        return 6;
    case AWKT_EQ: case AWKT_NE: case AWKT_LT: case AWKT_LE: case AWKT_GT: case AWKT_GE:
        return 7;
    case AWKT_PLUS: case AWKT_MINUS:
        return 9;
    case AWKT_STAR: case AWKT_SLASH: case AWKT_PERCENT:
        return 10;
    case AWKT_CARET:
        return 12;
    default:
        return 0;
    }
}

static AwkNode* AwkParseExpr(AwkCtx* c, int min_prec) {
    AwkNode* left = AwkParsePrimary(c);

    for (;;) {
        /* Postfix ++ and -- */
        if (c->cur.type == AWKT_INCR || c->cur.type == AWKT_DECR) {
            int op = c->cur.type;
            AwkLexNext(c, false);
            AwkNode* n = AwkNodeNew(c, AWKN_INCDEC);
            n->a = left;
            n->idx = (op == AWKT_INCR) ? 1 : -1;
            n->flag = true; /* post-increment */
            left = n;
            continue;
        }

        /* Pipe for `cmd | getline` */
        if (c->cur.type == AWKT_PIPE) {
            size_t save_pos = c->pos;
            int save_line = c->line;
            AwkTokn save_cur = c->cur;
            AwkLexNext(c, false);
            if (c->cur.type == AWKT_GETLINE) {
                AwkLexNext(c, false);
                AwkNode* gn = AwkNodeNew(c, AWKN_GETLINE);
                gn->idx = AWKG_CMD;
                gn->b = left;
                if (c->cur.type == AWKT_NAME) gn->a = AwkParsePrimary(c);
                left = gn;
                continue;
            } else {
                c->pos = save_pos;
                c->line = save_line;
                c->cur = save_cur;
            }
        }

        int prec = AwkOpPrecedence(c->cur.type);
        if (prec > 0) {
            if (prec < min_prec) break;
            int op = c->cur.type;
            if (op == AWKT_GT && c->no_gt) break;

            AwkLexNext(c, (op == AWKT_TILDE || op == AWKT_NOMATCH || op == AWKT_SLASH));

            if (op == AWKT_QUESTION) {
                AwkNode* cond = left;
                AwkNode* then_e = AwkParseExpr(c, 0);
                AwkExpect(c, AWKT_COLON, L"expected ':' in ternary expression");
                AwkNode* else_e = AwkParseExpr(c, 2);
                AwkNode* n = AwkNodeNew(c, AWKN_COND);
                n->a = cond; n->b = then_e; n->c = else_e;
                left = n;
            } else if (op >= AWKT_ASSIGN && op <= AWKT_POW_ASSIGN) {
                AwkNode* right = AwkParseExpr(c, prec);
                AwkNode* n = AwkNodeNew(c, AWKN_ASSIGN);
                n->a = left; n->b = right; n->sub = op;
                left = n;
            } else if (op == AWKT_CARET) {
                AwkNode* right = AwkParseExpr(c, prec);
                AwkNode* n = AwkNodeNew(c, AWKN_ARITH);
                n->a = left; n->b = right; n->sub = op;
                left = n;
            } else if (op == AWKT_AND) {
                AwkNode* right = AwkParseExpr(c, prec + 1);
                AwkNode* n = AwkNodeNew(c, AWKN_AND);
                n->a = left; n->b = right;
                left = n;
            } else if (op == AWKT_OR) {
                AwkNode* right = AwkParseExpr(c, prec + 1);
                AwkNode* n = AwkNodeNew(c, AWKN_OR);
                n->a = left; n->b = right;
                left = n;
            } else if (op == AWKT_IN) {
                AwkNode* right = AwkParsePrimary(c);
                AwkNode* n = AwkNodeNew(c, AWKN_IN);
                n->a = left; n->b = right;
                left = n;
            } else if (op == AWKT_TILDE || op == AWKT_NOMATCH) {
                AwkNode* right = AwkParseExpr(c, prec + 1);
                AwkNode* n = AwkNodeNew(c, AWKN_MATCH);
                n->a = left; n->b = right; n->sub = op;
                left = n;
            } else if (op >= AWKT_EQ && op <= AWKT_GE) {
                AwkNode* right = AwkParseExpr(c, prec + 1);
                AwkNode* n = AwkNodeNew(c, AWKN_CMP);
                n->a = left; n->b = right; n->sub = op;
                left = n;
            } else {
                AwkNode* right = AwkParseExpr(c, prec + 1);
                AwkNode* n = AwkNodeNew(c, AWKN_ARITH);
                n->a = left; n->b = right; n->sub = op;
                left = n;
            }
            continue;
        }

        /* String concatenation (adjacent expressions) */
        if (min_prec <= 8 && AwkCanStartConcat(c->cur.type)) {
            AwkNode* right = AwkParseExpr(c, 8 + 1);
            AwkNode* cat = AwkNodeNew(c, AWKN_CONCAT);
            cat->a = left;
            cat->b = right;
            left = cat;
            continue;
        }

        break;
    }
    return left;
}

static AwkNode* AwkParseStmtList(AwkCtx* c) {
    AwkNode* blk = AwkNodeNew(c, AWKN_BLOCK);
    while (c->cur.type != AWKT_RBRACE && c->cur.type != AWKT_EOF) {
        AwkSkipNewlines(c);
        if (c->cur.type == AWKT_RBRACE || c->cur.type == AWKT_EOF) break;
        AwkNode* st = AwkParseStmt(c);
        if (st) AwkNodeAdd(c, blk, st);
        while (c->cur.type == AWKT_SEMI || c->cur.type == AWKT_NEWLINE) {
            AwkLexNext(c, true);
        }
    }
    return blk;
}

static AwkNode* AwkParseStmt(AwkCtx* c) {
    AwkSkipNewlines(c);

    if (c->cur.type == AWKT_LBRACE) {
        AwkLexNext(c, true);
        AwkNode* blk = AwkParseStmtList(c);
        AwkExpect(c, AWKT_RBRACE, L"expected '}'");
        return blk;
    }
    if (c->cur.type == AWKT_SEMI) {
        AwkLexNext(c, true);
        return AwkNodeNew(c, AWKN_NOP);
    }
    if (c->cur.type == AWKT_IF) {
        AwkLexNext(c, false);
        AwkExpect(c, AWKT_LPAREN, L"expected '(' after if");
        AwkNode* cond = AwkParseExpr(c, 0);
        AwkExpect(c, AWKT_RPAREN, L"expected ')' after if condition");
        AwkSkipNewlines(c);
        AwkNode* then_s = AwkParseStmt(c);
        AwkNode* else_s = NULL;
        while (c->cur.type == AWKT_SEMI || c->cur.type == AWKT_NEWLINE) {
            AwkLexNext(c, true);
        }
        if (c->cur.type == AWKT_ELSE) {
            AwkLexNext(c, true);
            AwkSkipNewlines(c);
            else_s = AwkParseStmt(c);
        }
        AwkNode* n = AwkNodeNew(c, AWKN_IF);
        n->a = cond; n->b = then_s; n->c = else_s;
        return n;
    }
    if (c->cur.type == AWKT_WHILE) {
        AwkLexNext(c, false);
        AwkExpect(c, AWKT_LPAREN, L"expected '(' after while");
        AwkNode* cond = AwkParseExpr(c, 0);
        AwkExpect(c, AWKT_RPAREN, L"expected ')' after while condition");
        AwkSkipNewlines(c);
        AwkNode* body = AwkParseStmt(c);
        AwkNode* n = AwkNodeNew(c, AWKN_WHILE);
        n->a = cond; n->b = body;
        return n;
    }
    if (c->cur.type == AWKT_DO) {
        AwkLexNext(c, true);
        AwkSkipNewlines(c);
        AwkNode* body = AwkParseStmt(c);
        AwkSkipNewlines(c);
        AwkExpect(c, AWKT_WHILE, L"expected 'while' after do body");
        AwkExpect(c, AWKT_LPAREN, L"expected '(' after while");
        AwkNode* cond = AwkParseExpr(c, 0);
        AwkExpect(c, AWKT_RPAREN, L"expected ')' after while condition");
        AwkNode* n = AwkNodeNew(c, AWKN_DO);
        n->a = body; n->b = cond;
        return n;
    }
    if (c->cur.type == AWKT_FOR) {
        AwkLexNext(c, false);
        AwkExpect(c, AWKT_LPAREN, L"expected '(' after for");

        /* Check for `for (var in array)` */
        if (c->cur.type == AWKT_NAME) {
            size_t save_pos = c->pos;
            int save_line = c->line;
            AwkTokn save_cur = c->cur;
            wchar_t* vname = c->cur.text;
            AwkLexNext(c, false);
            if (c->cur.type == AWKT_IN) {
                AwkLexNext(c, false);
                AwkNode* arr = AwkParsePrimary(c);
                AwkExpect(c, AWKT_RPAREN, L"expected ')' after for (var in array)");
                AwkSkipNewlines(c);
                AwkNode* body = AwkParseStmt(c);
                AwkNode* n = AwkNodeNew(c, AWKN_FORIN);
                int pidx = AwkFindParam(c, vname);
                AwkNode* vn = AwkNodeNew(c, AWKN_VAR);
                vn->str = vname;
                if (pidx >= 0) { vn->local = true; vn->idx = pidx; }
                else { vn->local = false; vn->idx = AwkFindOrAddGlobal(c, vname); }
                n->a = vn; n->b = arr; n->c = body;
                return n;
            }
            c->pos = save_pos;
            c->line = save_line;
            c->cur = save_cur;
        }

        AwkNode* init = (c->cur.type != AWKT_SEMI) ? AwkParseExpr(c, 0) : NULL;
        AwkExpect(c, AWKT_SEMI, L"expected ';' after for init");
        AwkNode* cond = (c->cur.type != AWKT_SEMI) ? AwkParseExpr(c, 0) : NULL;
        AwkExpect(c, AWKT_SEMI, L"expected ';' after for condition");
        AwkNode* step = (c->cur.type != AWKT_RPAREN) ? AwkParseExpr(c, 0) : NULL;
        AwkExpect(c, AWKT_RPAREN, L"expected ')' after for header");
        AwkSkipNewlines(c);
        AwkNode* body = AwkParseStmt(c);
        AwkNode* n = AwkNodeNew(c, AWKN_FOR);
        n->a = init; n->b = cond; n->c = step; n->d = body;
        return n;
    }
    if (c->cur.type == AWKT_BREAK) {
        AwkLexNext(c, false);
        return AwkNodeNew(c, AWKN_BREAK);
    }
    if (c->cur.type == AWKT_CONTINUE) {
        AwkLexNext(c, false);
        return AwkNodeNew(c, AWKN_CONTINUE);
    }
    if (c->cur.type == AWKT_NEXT) {
        AwkLexNext(c, false);
        return AwkNodeNew(c, AWKN_NEXT);
    }
    if (c->cur.type == AWKT_NEXTFILE) {
        AwkLexNext(c, false);
        return AwkNodeNew(c, AWKN_NEXTFILE);
    }
    if (c->cur.type == AWKT_EXIT) {
        AwkLexNext(c, true);
        AwkNode* n = AwkNodeNew(c, AWKN_EXIT);
        if (c->cur.type != AWKT_SEMI && c->cur.type != AWKT_NEWLINE && c->cur.type != AWKT_RBRACE && c->cur.type != AWKT_EOF) {
            n->a = AwkParseExpr(c, 0);
        }
        return n;
    }
    if (c->cur.type == AWKT_RETURN) {
        AwkLexNext(c, true);
        AwkNode* n = AwkNodeNew(c, AWKN_RETURN);
        if (c->cur.type != AWKT_SEMI && c->cur.type != AWKT_NEWLINE && c->cur.type != AWKT_RBRACE && c->cur.type != AWKT_EOF) {
            n->a = AwkParseExpr(c, 0);
        }
        return n;
    }
    if (c->cur.type == AWKT_DELETE) {
        AwkLexNext(c, false);
        AwkNode* target = AwkParsePrimary(c);
        AwkNode* n = AwkNodeNew(c, AWKN_DELETE);
        n->a = target;
        return n;
    }
    if (c->cur.type == AWKT_PRINT || c->cur.type == AWKT_PRINTF) {
        bool is_printf = (c->cur.type == AWKT_PRINTF);
        AwkLexNext(c, true);
        AwkNode* n = AwkNodeNew(c, is_printf ? AWKN_PRINTF : AWKN_PRINT);
        n->idx = AWKR_NONE;

        c->no_gt = 1; /* prevent > from being parsed as comparison */
        if (c->cur.type == AWKT_LPAREN) {
            AwkLexNext(c, true);
            if (c->cur.type != AWKT_RPAREN) {
                for (;;) {
                    AwkNodeAdd(c, n, AwkParseExpr(c, 0));
                    if (c->cur.type == AWKT_COMMA) {
                        AwkLexNext(c, true);
                    } else break;
                }
            }
            AwkExpect(c, AWKT_RPAREN, L"expected ')' after print arguments");
        } else if (c->cur.type != AWKT_SEMI && c->cur.type != AWKT_NEWLINE && c->cur.type != AWKT_RBRACE &&
            c->cur.type != AWKT_GT && c->cur.type != AWKT_APPEND && c->cur.type != AWKT_PIPE && c->cur.type != AWKT_EOF) {
            for (;;) {
                AwkNodeAdd(c, n, AwkParseExpr(c, 0));
                if (c->cur.type == AWKT_COMMA) {
                    AwkLexNext(c, true);
                } else break;
            }
        }
        c->no_gt = 0;

        if (c->cur.type == AWKT_GT) {
            AwkLexNext(c, false);
            n->idx = AWKR_FILE;
            n->a = AwkParseExpr(c, 0);
        } else if (c->cur.type == AWKT_APPEND) {
            AwkLexNext(c, false);
            n->idx = AWKR_APPEND;
            n->a = AwkParseExpr(c, 0);
        } else if (c->cur.type == AWKT_PIPE) {
            AwkLexNext(c, false);
            n->idx = AWKR_PIPE;
            n->a = AwkParseExpr(c, 0);
        }
        return n;
    }

    AwkNode* expr = AwkParseExpr(c, 0);
    AwkNode* n = AwkNodeNew(c, AWKN_EXPR);
    n->a = expr;
    return n;
}

static void AwkParseFunction(AwkCtx* c) {
    AwkLexNext(c, false);
    if (c->cur.type != AWKT_NAME && c->cur.type != AWKT_FUNC_NAME) {
        AwkFatal(c, L"expected function name at line %d", c->line);
    }
    wchar_t* fname = c->cur.text;
    AwkLexNext(c, false);
    AwkExpect(c, AWKT_LPAREN, L"expected '(' after function name");

    if (c->nfuncs == c->cfuncs) {
        int nc = c->cfuncs ? c->cfuncs * 2 : 8;
        AwkFunc* nf = (AwkFunc*)realloc(c->funcs, nc * sizeof(AwkFunc));
        if (!nf) AwkFatal(c, L"out of memory");
        c->funcs = nf;
        c->cfuncs = nc;
    }
    int fidx = c->nfuncs++;
    AwkFunc* fn = &c->funcs[fidx];
    memset(fn, 0, sizeof(AwkFunc));
    fn->name = fname;
    c->cur_func = fidx;

    size_t pcap = 4;
    fn->params = (wchar_t**)malloc(pcap * sizeof(wchar_t*));
    if (!fn->params) AwkFatal(c, L"out of memory");

    if (c->cur.type != AWKT_RPAREN) {
        for (;;) {
            if (c->cur.type != AWKT_NAME) {
                AwkFatal(c, L"expected parameter name at line %d", c->line);
            }
            if (fn->nparams == pcap) {
                pcap *= 2;
                wchar_t** new_params = (wchar_t**)realloc(fn->params, pcap * sizeof(wchar_t*));
                if (!new_params) AwkFatal(c, L"out of memory");
                fn->params = new_params;
            }
            fn->params[fn->nparams++] = c->cur.text;
            AwkLexNext(c, false);
            if (c->cur.type == AWKT_COMMA) {
                AwkLexNext(c, false);
            } else break;
        }
    }
    AwkExpect(c, AWKT_RPAREN, L"expected ')' after parameter list");
    AwkSkipNewlines(c);
    fn->body = AwkParseStmt(c);
    fn->defined = true;
    c->cur_func = -1;
}

static void AwkAddItem(AwkCtx* c, int kind, AwkNode* p1, AwkNode* p2, AwkNode* act) {
    if (c->nitems == c->citems) {
        size_t nc = c->citems ? c->citems * 2 : 16;
        AwkItem* ni = (AwkItem*)realloc(c->items, nc * sizeof(AwkItem));
        if (!ni) AwkFatal(c, L"out of memory");
        c->items = ni;
        c->citems = nc;
    }
    AwkItem* it = &c->items[c->nitems++];
    it->kind = kind;
    it->pat1 = p1;
    it->pat2 = p2;
    it->action = act;
    it->in_range = false;
}

static void AwkParseProgram(AwkCtx* c) {
    AwkLexNext(c, true);

    while (c->cur.type != AWKT_EOF) {
        AwkSkipNewlines(c);
        if (c->cur.type == AWKT_EOF) break;

        if (c->cur.type == AWKT_SEMI) {
            AwkLexNext(c, true);
            continue;
        }

        if (c->cur.type == AWKT_FUNCTION) {
            AwkParseFunction(c);
            continue;
        }

        if (c->cur.type == AWKT_BEGIN) {
            AwkLexNext(c, true);
            AwkSkipNewlines(c);
            AwkNode* act = AwkParseStmt(c);
            AwkAddItem(c, AWKI_BEGIN, NULL, NULL, act);
            continue;
        }

        if (c->cur.type == AWKT_END) {
            AwkLexNext(c, true);
            AwkSkipNewlines(c);
            AwkNode* act = AwkParseStmt(c);
            AwkAddItem(c, AWKI_END, NULL, NULL, act);
            continue;
        }

        if (c->cur.type == AWKT_LBRACE) {
            /* Action only */
            AwkNode* act = AwkParseStmt(c);
            AwkAddItem(c, AWKI_MAIN, NULL, NULL, act);
            continue;
        }

        /* Pattern [ , Pattern ] [ Action ] */
        AwkNode* pat1 = AwkParseExpr(c, 0);
        AwkNode* pat2 = NULL;
        if (c->cur.type == AWKT_COMMA) {
            AwkLexNext(c, true);
            pat2 = AwkParseExpr(c, 0);
        }

        AwkNode* act = NULL;
        AwkSkipNewlines(c);
        if (c->cur.type == AWKT_LBRACE) {
            act = AwkParseStmt(c);
        } else {
            /* Default action: { print } */
            act = AwkNodeNew(c, AWKN_PRINT);
            act->idx = AWKR_NONE;
        }
        AwkAddItem(c, AWKI_MAIN, pat1, pat2, act);
    }
}

/* ========================================================================= */
/*                              Runtime                                      */
/* ========================================================================= */

static AwkValue AwkEval(AwkCtx* c, AwkNode* n);
static int AwkExecStmt(AwkCtx* c, AwkNode* n);
static void AwkEvalLValue(AwkCtx* c, AwkNode* n, AwkRef* out_ref);
static void AwkEnsureFields(AwkCtx* c);
static void AwkEnsureRecord(AwkCtx* c);

static AwkVar* AwkGetVar(AwkCtx* c, int idx, bool is_local) {
    if (is_local) {
        if (!c->frame) AwkFatal(c, L"local variable outside function frame");
        return &c->frame[idx];
    }
    return &c->globals[idx];
}

static AwkValue AwkRefGet(AwkCtx* c, const AwkRef* ref) {
    if (ref->kind == AWKL_VAR) {
        if (ref->var->arr) AwkFatal(c, L"attempt to use array as scalar");
        return AwkValDup(c, &ref->var->v);
    }
    if (ref->kind == AWKL_ELEM) {
        AwkElem* e = AwkArrayFind(ref->arr, ref->key);
        if (!e) return AwkUninitV(c);
        return AwkValDup(c, &e->v);
    }
    if (ref->kind == AWKL_FIELD) {
        AwkEnsureFields(c);
        if (ref->field == 0) {
            AwkEnsureRecord(c);
            return AwkStrOwnV(AwkDupN(c, c->rec, c->reclen));
        }
        if (ref->field > c->nf) return AwkUninitV(c);
        return AwkValDup(c, &c->fields[ref->field - 1]);
    }
    return AwkUninitV(c);
}

static void AwkRefSet(AwkCtx* c, const AwkRef* ref, AwkValue val) {
    if (ref->kind == AWKL_VAR) {
        if (ref->var->arr && !ref->var->borrowed) {
            AwkArrayDestroy(ref->var->arr);
            ref->var->arr = NULL;
        }
        AwkValFree(&ref->var->v);
        ref->var->v = val;
        return;
    }
    if (ref->kind == AWKL_ELEM) {
        AwkElem* e = AwkArrayGet(c, ref->arr, ref->key);
        AwkValFree(&e->v);
        e->v = val;
        return;
    }
    if (ref->kind == AWKL_FIELD) {
        AwkEnsureFields(c);
        if (ref->field == 0) {
            free(c->rec);
            c->rec = AwkToStr(c, &val);
            c->reclen = wcslen(c->rec);
            c->rec_valid = true;
            c->split_valid = false;
            AwkValFree(&val);
            AwkEnsureFields(c);
            return;
        }
        if (ref->field > c->nf) {
            size_t req = ref->field;
            if (req > c->capf) {
                size_t nc = c->capf ? c->capf * 2 : 16;
                while (nc < req) nc *= 2;
                AwkValue* nflds = (AwkValue*)realloc(c->fields, nc * sizeof(AwkValue));
                if (!nflds) AwkFatal(c, L"out of memory");
                c->fields = nflds;
                c->capf = nc;
            }
            while (c->nf < req) c->fields[c->nf++] = AwkUninitV(c);
            AwkValFree(&c->globals[AWKV_NF].v);
            c->globals[AWKV_NF].v = AwkNumV((double)c->nf);
        }
        AwkValFree(&c->fields[ref->field - 1]);
        c->fields[ref->field - 1] = val;
        c->rec_valid = false;
        return;
    }
}

static void AwkEvalLValue(AwkCtx* c, AwkNode* n, AwkRef* out_ref) {
    if (n->op == AWKN_VAR) {
        out_ref->kind = AWKL_VAR;
        out_ref->var = AwkGetVar(c, n->idx, n->local);
        out_ref->arr = NULL;
        out_ref->key = NULL;
        return;
    }
    if (n->op == AWKN_FIELD) {
        AwkValue fv = AwkEval(c, n->a);
        long long f = AwkToLL(AwkNumOf(c, &fv));
        AwkValFree(&fv);
        if (f < 0) AwkFatal(c, L"negative field index $%lld", f);
        out_ref->kind = AWKL_FIELD;
        out_ref->field = (size_t)f;
        return;
    }
    if (n->op == AWKN_ELEM) {
        AwkVar* v = AwkGetVar(c, n->a->idx, n->a->local);
        if (!v->arr) v->arr = AwkArrayNew(c);
        AwkBuf b = { NULL, 0, 0 };
        const wchar_t* subsep = AwkToStr(c, &c->globals[SUBSEP_IDX].v);
        for (size_t i = 0; i < n->nlist; i++) {
            if (i > 0) AwkBufAddStr(c, &b, subsep);
            AwkValue kv = AwkEval(c, n->list[i]);
            wchar_t* ks = AwkToStr(c, &kv);
            AwkBufAddStr(c, &b, ks);
            free(ks);
            AwkValFree(&kv);
        }
        free((wchar_t*)subsep);
        out_ref->kind = AWKL_ELEM;
        out_ref->arr = v->arr;
        out_ref->key = AwkBufTake(c, &b);
        AwkPoolAdd(c, out_ref->key, AWKP_RAW);
        return;
    }
    AwkFatal(c, L"invalid lvalue at line %d", n->line);
}

static void AwkEnsureRecord(AwkCtx* c) {
    if (c->rec_valid) return;
    AwkBuf b = { NULL, 0, 0 };
    const wchar_t* ofs = AwkToStr(c, &c->globals[AWKV_OFS].v);
    for (size_t i = 0; i < c->nf; i++) {
        if (i > 0) AwkBufAddStr(c, &b, ofs);
        wchar_t* s = AwkToOutStr(c, &c->fields[i]);
        AwkBufAddStr(c, &b, s);
        free(s);
    }
    free((wchar_t*)ofs);
    free(c->rec);
    c->rec = AwkBufTake(c, &b);
    c->reclen = wcslen(c->rec);
    c->rec_valid = true;
}

static void AwkEnsureFields(AwkCtx* c) {
    if (c->split_valid) return;
    for (size_t i = 0; i < c->nf; i++) AwkValFree(&c->fields[i]);
    c->nf = 0;

    const wchar_t* text = c->rec ? c->rec : L"";
    size_t tlen = c->reclen;

    wchar_t* fs_str = AwkToStr(c, &c->globals[AWKV_FS].v);
    size_t fslen = wcslen(fs_str);

    if (fslen == 1 && fs_str[0] == L' ') {
        /* Default: runs of whitespace */
        size_t i = 0;
        while (i < tlen) {
            while (i < tlen && AwkIsBlank(text[i])) i++;
            if (i >= tlen) break;
            size_t start = i;
            while (i < tlen && !AwkIsBlank(text[i])) i++;
            AwkValue fv = AwkStrnumOwnV(c, AwkDupN(c, text + start, i - start));
            if (c->nf == c->capf) {
                size_t nc = c->capf ? c->capf * 2 : 16;
                AwkValue* new_fields = (AwkValue*)realloc(c->fields, nc * sizeof(AwkValue));
                if (!new_fields) AwkFatal(c, L"out of memory");
                c->fields = new_fields;
                c->capf = nc;
            }
            c->fields[c->nf++] = fv;
        }
    } else if (fslen == 1) {
        wchar_t sep = fs_str[0];
        size_t start = 0;
        for (size_t i = 0; i <= tlen; i++) {
            if (i == tlen || text[i] == sep) {
                AwkValue fv = AwkStrnumOwnV(c, AwkDupN(c, text + start, i - start));
                if (c->nf == c->capf) {
                    size_t nc = c->capf ? c->capf * 2 : 16;
                    AwkValue* new_fields = (AwkValue*)realloc(c->fields, nc * sizeof(AwkValue));
                    if (!new_fields) AwkFatal(c, L"out of memory");
                    c->fields = new_fields;
                    c->capf = nc;
                }
                c->fields[c->nf++] = fv;
                start = i + 1;
            }
        }
    } else if (fslen > 1) {
        RegexProg* prog = AwkGetRegex(c, fs_str);
        size_t from = 0;
        while (from <= tlen) {
            RegexMatch m;
            if (RegexSearch(prog, text, tlen, from, 0, &m)) {
                AwkValue fv = AwkStrnumOwnV(c, AwkDupN(c, text + from, m.s[0] - from));
                if (c->nf == c->capf) {
                    size_t nc = c->capf ? c->capf * 2 : 16;
                    AwkValue* new_fields = (AwkValue*)realloc(c->fields, nc * sizeof(AwkValue));
                    if (!new_fields) AwkFatal(c, L"out of memory");
                    c->fields = new_fields;
                    c->capf = nc;
                }
                c->fields[c->nf++] = fv;
                from = (m.e[0] > m.s[0]) ? m.e[0] : (m.s[0] + 1);
            } else {
                AwkValue fv = AwkStrnumOwnV(c, AwkDupN(c, text + from, tlen - from));
                if (c->nf == c->capf) {
                    size_t nc = c->capf ? c->capf * 2 : 16;
                    AwkValue* new_fields = (AwkValue*)realloc(c->fields, nc * sizeof(AwkValue));
                    if (!new_fields) AwkFatal(c, L"out of memory");
                    c->fields = new_fields;
                    c->capf = nc;
                }
                c->fields[c->nf++] = fv;
                break;
            }
        }
    } else {
        /* Empty FS: each character is a field */
        for (size_t i = 0; i < tlen; i++) {
            AwkValue fv = AwkStrnumOwnV(c, AwkDupN(c, text + i, 1));
            if (c->nf == c->capf) {
                size_t nc = c->capf ? c->capf * 2 : 16;
                AwkValue* new_fields = (AwkValue*)realloc(c->fields, nc * sizeof(AwkValue));
                if (!new_fields) AwkFatal(c, L"out of memory");
                c->fields = new_fields;
                c->capf = nc;
            }
            c->fields[c->nf++] = fv;
        }
    }
    free(fs_str);
    AwkValFree(&c->globals[AWKV_NF].v);
    c->globals[AWKV_NF].v = AwkNumV((double)c->nf);
    c->split_valid = true;
}

static FILE* AwkGetOutStream(AwkCtx* c, const wchar_t* name, int kind) {
    for (AwkStream* s = c->streams; s; s = s->next) {
        if (s->kind == kind && wcscmp(s->name, name) == 0) return s->fp;
    }
    if (c->safe_mode && (kind == AWK_ST_OUTPIPE || kind == AWK_ST_INPIPE)) {
        AwkFatal(c, L"safe mode prevents pipes");
    }
    FILE* fp = NULL;
    if (kind == AWK_ST_OUTFILE) fp = _wfsopen(name, L"w", _SH_DENYNO);
    else if (kind == AWK_ST_OUTPIPE) fp = _wpopen(name, L"w");
    if (!fp) AwkFatal(c, L"cannot open output stream '%ls'", name);

    AwkStream* st = (AwkStream*)AwkAllocRaw(c, sizeof(AwkStream));
    st->name = AwkDupN(c, name, wcslen(name));
    st->kind = kind;
    st->fp = fp;
    st->next = c->streams;
    c->streams = st;
    return fp;
}

static void AwkCloseStream(AwkCtx* c, const wchar_t* name) {
    AwkStream** pp = &c->streams;
    while (*pp) {
        if (wcscmp((*pp)->name, name) == 0) {
            AwkStream* s = *pp;
            *pp = s->next;
            if (s->kind == AWK_ST_OUTPIPE || s->kind == AWK_ST_INPIPE) _pclose(s->fp);
            else fclose(s->fp);
            free(s->name);
            free(s);
            return;
        }
        pp = &(*pp)->next;
    }
}

static AwkValue AwkCallFunc(AwkCtx* c, int fidx, AwkNode* args_node) {
    if (fidx < 0 || fidx >= c->nfuncs) AwkFatal(c, L"undefined function call");
    AwkFunc* fn = &c->funcs[fidx];
    if (c->depth >= AWK_MAX_DEPTH) AwkFatal(c, L"maximum call depth exceeded");

    AwkVar* new_frame = (AwkVar*)calloc(fn->nparams ? fn->nparams : 1, sizeof(AwkVar));
    for (size_t i = 0; i < fn->nparams; i++) new_frame[i].v = AwkUninitV(c);

    size_t passed = args_node ? args_node->nlist : 0;
    for (size_t i = 0; i < fn->nparams; i++) {
        if (i < passed) {
            AwkNode* an = args_node->list[i];
            /* Check if passing array */
            if (an->op == AWKN_VAR) {
                AwkVar* av = AwkGetVar(c, an->idx, an->local);
                if (av->arr) {
                    new_frame[i].arr = av->arr;
                    new_frame[i].borrowed = true;
                    continue;
                }
            }
            new_frame[i].v = AwkEval(c, an);
        }
    }

    AwkVar* save_frame = c->frame;
    c->frame = new_frame;
    c->depth++;

    AwkExecStmt(c, fn->body);

    AwkValue ret = c->retval;
    memset(&c->retval, 0, sizeof(AwkValue));

    for (size_t i = 0; i < fn->nparams; i++) {
        if (!new_frame[i].borrowed && new_frame[i].arr) {
            AwkArrayDestroy(new_frame[i].arr);
        }
        AwkValFree(&new_frame[i].v);
    }
    free(new_frame);

    c->frame = save_frame;
    c->depth--;
    return ret;
}

static AwkValue AwkCallBuiltin(AwkCtx* c, int bi, AwkNode* args_node) {
    size_t nargs = args_node ? args_node->nlist : 0;

    switch (bi) {
    case AWKB_LENGTH: {
        if (nargs == 0) {
            AwkEnsureRecord(c);
            return AwkNumV((double)c->reclen);
        }
        if (args_node->list[0]->op == AWKN_VAR) {
            AwkVar* v = AwkGetVar(c, args_node->list[0]->idx, args_node->list[0]->local);
            if (v->arr) return AwkNumV((double)v->arr->count);
        }
        AwkValue v = AwkEval(c, args_node->list[0]);
        wchar_t* s = AwkToStr(c, &v);
        double len = (double)wcslen(s);
        free(s);
        AwkValFree(&v);
        return AwkNumV(len);
    }
    case AWKB_SUBSTR: {
        AwkValue sv = AwkEval(c, args_node->list[0]);
        wchar_t* s = AwkToStr(c, &sv);
        size_t slen = wcslen(s);
        AwkValue pv = AwkEval(c, args_node->list[1]);
        long long pos = AwkToLL(AwkNumOf(c, &pv));
        AwkValFree(&pv);
        long long len = (long long)slen;
        if (nargs >= 3) {
            AwkValue lv = AwkEval(c, args_node->list[2]);
            len = AwkToLL(AwkNumOf(c, &lv));
            AwkValFree(&lv);
        }
        AwkValFree(&sv);
        if (pos < 1) pos = 1;
        if ((size_t)pos > slen || len <= 0) {
            free(s);
            return AwkStrOwnV(AwkDupN(c, L"", 0));
        }
        size_t st = (size_t)(pos - 1);
        size_t take = (size_t)len;
        if (st + take > slen) take = slen - st;
        wchar_t* res = AwkDupN(c, s + st, take);
        free(s);
        return AwkStrOwnV(res);
    }
    case AWKB_INDEX: {
        AwkValue s1v = AwkEval(c, args_node->list[0]);
        wchar_t* s1 = AwkToStr(c, &s1v);
        AwkValue s2v = AwkEval(c, args_node->list[1]);
        wchar_t* s2 = AwkToStr(c, &s2v);
        const wchar_t* p = wcsstr(s1, s2);
        double idx = p ? (double)(p - s1 + 1) : 0.0;
        free(s1); free(s2);
        AwkValFree(&s1v); AwkValFree(&s2v);
        return AwkNumV(idx);
    }
    case AWKB_SPLIT: {
        AwkValue sv = AwkEval(c, args_node->list[0]);
        wchar_t* str = AwkToStr(c, &sv);
        size_t slen = wcslen(str);
        AwkNode* target = args_node->list[1];
        if (target->op != AWKN_VAR) AwkFatal(c, L"second argument to split must be array");
        AwkVar* arr_var = AwkGetVar(c, target->idx, target->local);
        if (!arr_var->arr) arr_var->arr = AwkArrayNew(c);
        else AwkArrayClear(arr_var->arr);

        wchar_t* fs_str = NULL;
        if (nargs >= 3) {
            AwkValue fsv = AwkEval(c, args_node->list[2]);
            fs_str = AwkToStr(c, &fsv);
            AwkValFree(&fsv);
        } else {
            fs_str = AwkToStr(c, &c->globals[AWKV_FS].v);
        }
        size_t fslen = wcslen(fs_str);
        size_t count = 0;

        if (fslen == 1 && fs_str[0] == L' ') {
            size_t i = 0;
            while (i < slen) {
                while (i < slen && AwkIsBlank(str[i])) i++;
                if (i >= slen) break;
                size_t start = i;
                while (i < slen && !AwkIsBlank(str[i])) i++;
                count++;
                wchar_t kbuf[32];
                _snwprintf(kbuf, 32, L"%zu", count);
                AwkElem* e = AwkArrayGet(c, arr_var->arr, kbuf);
                AwkValFree(&e->v);
                e->v = AwkStrnumOwnV(c, AwkDupN(c, str + start, i - start));
            }
        } else {
            RegexProg* prog = AwkGetRegex(c, fs_str);
            size_t from = 0;
            while (from <= slen) {
                RegexMatch m;
                if (RegexSearch(prog, str, slen, from, 0, &m)) {
                    count++;
                    wchar_t kbuf[32];
                    _snwprintf(kbuf, 32, L"%zu", count);
                    AwkElem* e = AwkArrayGet(c, arr_var->arr, kbuf);
                    AwkValFree(&e->v);
                    e->v = AwkStrnumOwnV(c, AwkDupN(c, str + from, m.s[0] - from));
                    from = (m.e[0] > m.s[0]) ? m.e[0] : (m.s[0] + 1);
                } else {
                    count++;
                    wchar_t kbuf[32];
                    _snwprintf(kbuf, 32, L"%zu", count);
                    AwkElem* e = AwkArrayGet(c, arr_var->arr, kbuf);
                    AwkValFree(&e->v);
                    e->v = AwkStrnumOwnV(c, AwkDupN(c, str + from, slen - from));
                    break;
                }
            }
        }
        free(str); free(fs_str);
        AwkValFree(&sv);
        return AwkNumV((double)count);
    }
    case AWKB_SUB:
    case AWKB_GSUB: {
        bool global = (bi == AWKB_GSUB);
        AwkValue rv = AwkEval(c, args_node->list[0]);
        wchar_t* pat = AwkToStr(c, &rv);
        RegexProg* prog = AwkGetRegex(c, pat);
        AwkValue repv = AwkEval(c, args_node->list[1]);
        wchar_t* repl = AwkToStr(c, &repv);

        AwkRef ref;
        if (nargs >= 3) {
            AwkEvalLValue(c, args_node->list[2], &ref);
        } else {
            ref.kind = AWKL_FIELD;
            ref.field = 0;
        }

        AwkValue orig_val = AwkRefGet(c, &ref);
        wchar_t* orig = AwkToStr(c, &orig_val);
        size_t olen = wcslen(orig);

        AwkBuf out = { NULL, 0, 0 };
        size_t from = 0;
        int subs = 0;

        while (from <= olen) {
            RegexMatch m;
            if (RegexSearch(prog, orig, olen, from, 0, &m)) {
                AwkBufAdd(c, &out, orig + from, m.s[0] - from);
                /* Replace with repl, expanding '&' */
                for (size_t j = 0; repl[j]; j++) {
                    if (repl[j] == L'&') {
                        AwkBufAdd(c, &out, orig + m.s[0], m.e[0] - m.s[0]);
                    } else if (repl[j] == L'\\' && repl[j + 1] == L'&') {
                        AwkBufAdd(c, &out, L"&", 1);
                        j++;
                    } else {
                        AwkBufAdd(c, &out, &repl[j], 1);
                    }
                }
                subs++;
                from = (m.e[0] > m.s[0]) ? m.e[0] : (m.s[0] + 1);
                if (!global) {
                    AwkBufAdd(c, &out, orig + from, olen - from);
                    break;
                }
            } else {
                AwkBufAdd(c, &out, orig + from, olen - from);
                break;
            }
        }

        wchar_t* new_str = AwkBufTake(c, &out);
        AwkRefSet(c, &ref, AwkStrOwnV(new_str));
        free(pat); free(repl); free(orig);
        AwkValFree(&rv); AwkValFree(&repv); AwkValFree(&orig_val);
        return AwkNumV((double)subs);
    }
    case AWKB_MATCH: {
        AwkValue sv = AwkEval(c, args_node->list[0]);
        wchar_t* s = AwkToStr(c, &sv);
        size_t slen = wcslen(s);
        AwkValue pv = AwkEval(c, args_node->list[1]);
        wchar_t* p = AwkToStr(c, &pv);
        RegexProg* prog = AwkGetRegex(c, p);
        RegexMatch m;
        bool found = RegexSearch(prog, s, slen, 0, 0, &m);
        double rstart = 0, rlen = -1;
        if (found) {
            rstart = (double)(m.s[0] + 1);
            rlen = (double)(m.e[0] - m.s[0]);
        }
        AwkValFree(&c->globals[AWKV_RSTART].v);
        c->globals[AWKV_RSTART].v = AwkNumV(rstart);
        AwkValFree(&c->globals[AWKV_RLENGTH].v);
        c->globals[AWKV_RLENGTH].v = AwkNumV(rlen);
        free(s); free(p);
        AwkValFree(&sv); AwkValFree(&pv);
        return AwkNumV(rstart);
    }
    case AWKB_SPRINTF: {
        if (nargs == 0) return AwkStrOwnV(AwkDupN(c, L"", 0));
        AwkValue fv = AwkEval(c, args_node->list[0]);
        wchar_t* fmt = AwkToStr(c, &fv);
        AwkValue* args = (AwkValue*)malloc((nargs - 1 ? nargs - 1 : 1) * sizeof(AwkValue));
        for (size_t i = 1; i < nargs; i++) args[i - 1] = AwkEval(c, args_node->list[i]);
        wchar_t* formatted = AwkFormatOwn(c, fmt, args, nargs - 1);
        for (size_t i = 1; i < nargs; i++) AwkValFree(&args[i - 1]);
        free(args); free(fmt); AwkValFree(&fv);
        return AwkStrOwnV(formatted);
    }
    case AWKB_SIN: {
        AwkValue v = AwkEval(c, args_node->list[0]);
        double r = sin(AwkNumOf(c, &v)); AwkValFree(&v); return AwkNumV(r);
    }
    case AWKB_COS: {
        AwkValue v = AwkEval(c, args_node->list[0]);
        double r = cos(AwkNumOf(c, &v)); AwkValFree(&v); return AwkNumV(r);
    }
    case AWKB_ATAN2: {
        AwkValue y = AwkEval(c, args_node->list[0]);
        AwkValue x = AwkEval(c, args_node->list[1]);
        double r = atan2(AwkNumOf(c, &y), AwkNumOf(c, &x));
        AwkValFree(&y); AwkValFree(&x); return AwkNumV(r);
    }
    case AWKB_EXP: {
        AwkValue v = AwkEval(c, args_node->list[0]);
        double r = exp(AwkNumOf(c, &v)); AwkValFree(&v); return AwkNumV(r);
    }
    case AWKB_LOG: {
        AwkValue v = AwkEval(c, args_node->list[0]);
        double r = log(AwkNumOf(c, &v)); AwkValFree(&v); return AwkNumV(r);
    }
    case AWKB_SQRT: {
        AwkValue v = AwkEval(c, args_node->list[0]);
        double r = sqrt(AwkNumOf(c, &v)); AwkValFree(&v); return AwkNumV(r);
    }
    case AWKB_INT: {
        AwkValue v = AwkEval(c, args_node->list[0]);
        double num = AwkNumOf(c, &v);
        double r = (num >= 0) ? floor(num) : ceil(num);
        AwkValFree(&v); return AwkNumV(r);
    }
    case AWKB_RAND: {
        c->rng = c->rng * 1103515245 + 12345;
        double r = (double)((c->rng >> 16) & 0x7fff) / 32768.0;
        return AwkNumV(r);
    }
    case AWKB_SRAND: {
        unsigned int prev = c->rng;
        if (nargs >= 1) {
            AwkValue v = AwkEval(c, args_node->list[0]);
            c->rng = (unsigned int)AwkToLL(AwkNumOf(c, &v));
            AwkValFree(&v);
        } else {
            c->rng = (unsigned int)time(NULL);
        }
        return AwkNumV((double)prev);
    }
    case AWKB_TOLOWER: {
        AwkValue v = AwkEval(c, args_node->list[0]);
        wchar_t* s = AwkToStr(c, &v);
        for (size_t i = 0; s[i]; i++) s[i] = (wchar_t)towlower(s[i]);
        AwkValFree(&v);
        return AwkStrOwnV(s);
    }
    case AWKB_TOUPPER: {
        AwkValue v = AwkEval(c, args_node->list[0]);
        wchar_t* s = AwkToStr(c, &v);
        for (size_t i = 0; s[i]; i++) s[i] = (wchar_t)towupper(s[i]);
        AwkValFree(&v);
        return AwkStrOwnV(s);
    }
    case AWKB_FFLUSH: {
        if (nargs == 0) {
            fflush(stdout);
            for (AwkStream* s = c->streams; s; s = s->next) fflush(s->fp);
        } else {
            AwkValue v = AwkEval(c, args_node->list[0]);
            wchar_t* name = AwkToStr(c, &v);
            for (AwkStream* s = c->streams; s; s = s->next) {
                if (wcscmp(s->name, name) == 0) fflush(s->fp);
            }
            free(name); AwkValFree(&v);
        }
        return AwkNumV(0);
    }
    case AWKB_SYSTEM: {
        if (c->safe_mode) AwkFatal(c, L"safe mode prevents system()");
        AwkValue v = AwkEval(c, args_node->list[0]);
        wchar_t* cmd = AwkToStr(c, &v);
        int res = _wsystem(cmd);
        free(cmd); AwkValFree(&v);
        return AwkNumV((double)res);
    }
    case AWKB_CLOSE: {
        AwkValue v = AwkEval(c, args_node->list[0]);
        wchar_t* name = AwkToStr(c, &v);
        AwkCloseStream(c, name);
        free(name); AwkValFree(&v);
        return AwkNumV(0);
    }
    }
    return AwkUninitV(c);
}

static AwkValue AwkEval(AwkCtx* c, AwkNode* n) {
    if (!n) return AwkUninitV(c);

    switch (n->op) {
    case AWKN_NUM: return AwkNumV(n->num);
    case AWKN_STR: return AwkStrOwnV(AwkDupN(c, n->str, wcslen(n->str)));
    case AWKN_REGEX: return AwkStrOwnV(AwkDupN(c, n->str, wcslen(n->str)));
    case AWKN_VAR: {
        AwkVar* v = AwkGetVar(c, n->idx, n->local);
        if (v->arr) AwkFatal(c, L"attempt to use array '%ls' as scalar", n->str);
        return AwkValDup(c, &v->v);
    }
    case AWKN_FIELD: {
        AwkRef ref;
        AwkEvalLValue(c, n, &ref);
        return AwkRefGet(c, &ref);
    }
    case AWKN_ELEM: {
        AwkRef ref;
        AwkEvalLValue(c, n, &ref);
        return AwkRefGet(c, &ref);
    }
    case AWKN_ASSIGN: {
        AwkRef ref;
        AwkEvalLValue(c, n->a, &ref);
        AwkValue rval = AwkEval(c, n->b);
        if (n->sub == AWKT_ASSIGN) {
            AwkRefSet(c, &ref, AwkValDup(c, &rval));
            return rval;
        }
        AwkValue cur = AwkRefGet(c, &ref);
        double d1 = AwkNumOf(c, &cur);
        double d2 = AwkNumOf(c, &rval);
        double res = 0;
        switch (n->sub) {
        case AWKT_ADD_ASSIGN: res = d1 + d2; break;
        case AWKT_SUB_ASSIGN: res = d1 - d2; break;
        case AWKT_MUL_ASSIGN: res = d1 * d2; break;
        case AWKT_DIV_ASSIGN: res = (d2 != 0) ? (d1 / d2) : 0; break;
        case AWKT_MOD_ASSIGN: res = (d2 != 0) ? fmod(d1, d2) : 0; break;
        case AWKT_POW_ASSIGN: res = pow(d1, d2); break;
        }
        AwkValFree(&cur); AwkValFree(&rval);
        AwkValue final_val = AwkNumV(res);
        AwkRefSet(c, &ref, AwkValDup(c, &final_val));
        return final_val;
    }
    case AWKN_INCDEC: {
        AwkRef ref;
        AwkEvalLValue(c, n->a, &ref);
        AwkValue cur = AwkRefGet(c, &ref);
        double d = AwkNumOf(c, &cur);
        double new_d = d + (double)n->idx;
        AwkRefSet(c, &ref, AwkNumV(new_d));
        if (n->flag) {
            /* post-increment: return old value */
            return cur;
        }
        AwkValFree(&cur);
        return AwkNumV(new_d);
    }
    case AWKN_NOT: {
        AwkValue v = AwkEval(c, n->a);
        bool t = AwkTruthy(&v);
        AwkValFree(&v);
        return AwkNumV(t ? 0.0 : 1.0);
    }
    case AWKN_PLUS: {
        AwkValue v = AwkEval(c, n->a);
        double d = AwkNumOf(c, &v);
        AwkValFree(&v);
        return AwkNumV(d);
    }
    case AWKN_NEG: {
        AwkValue v = AwkEval(c, n->a);
        double d = -AwkNumOf(c, &v);
        AwkValFree(&v);
        return AwkNumV(d);
    }
    case AWKN_COND: {
        AwkValue cv = AwkEval(c, n->a);
        bool t = AwkTruthy(&cv);
        AwkValFree(&cv);
        return t ? AwkEval(c, n->b) : AwkEval(c, n->c);
    }
    case AWKN_AND: {
        AwkValue lv = AwkEval(c, n->a);
        if (!AwkTruthy(&lv)) { AwkValFree(&lv); return AwkNumV(0.0); }
        AwkValFree(&lv);
        AwkValue rv = AwkEval(c, n->b);
        bool t = AwkTruthy(&rv);
        AwkValFree(&rv);
        return AwkNumV(t ? 1.0 : 0.0);
    }
    case AWKN_OR: {
        AwkValue lv = AwkEval(c, n->a);
        if (AwkTruthy(&lv)) { AwkValFree(&lv); return AwkNumV(1.0); }
        AwkValFree(&lv);
        AwkValue rv = AwkEval(c, n->b);
        bool t = AwkTruthy(&rv);
        AwkValFree(&rv);
        return AwkNumV(t ? 1.0 : 0.0);
    }
    case AWKN_IN: {
        AwkBuf b = { NULL, 0, 0 };
        const wchar_t* subsep = AwkToStr(c, &c->globals[SUBSEP_IDX].v);
        if (n->a->op == AWKN_GROUP) {
            for (size_t i = 0; i < n->a->nlist; i++) {
                if (i > 0) AwkBufAddStr(c, &b, subsep);
                AwkValue kv = AwkEval(c, n->a->list[i]);
                wchar_t* ks = AwkToStr(c, &kv);
                AwkBufAddStr(c, &b, ks);
                free(ks); AwkValFree(&kv);
            }
        } else {
            AwkValue kv = AwkEval(c, n->a);
            wchar_t* ks = AwkToStr(c, &kv);
            AwkBufAddStr(c, &b, ks);
            free(ks); AwkValFree(&kv);
        }
        free((wchar_t*)subsep);
        AwkVar* arr_var = AwkGetVar(c, n->b->idx, n->b->local);
        bool found = arr_var->arr && (AwkArrayFind(arr_var->arr, b.p ? b.p : L"") != NULL);
        AwkBufFree(&b);
        return AwkNumV(found ? 1.0 : 0.0);
    }
    case AWKN_MATCH: {
        AwkValue lv = AwkEval(c, n->a);
        wchar_t* text = AwkToStr(c, &lv);
        AwkValue rv = AwkEval(c, n->b);
        wchar_t* pat = AwkToStr(c, &rv);
        RegexProg* prog = AwkGetRegex(c, pat);
        RegexMatch m;
        bool match = RegexSearch(prog, text, wcslen(text), 0, 0, &m);
        free(text); free(pat);
        AwkValFree(&lv); AwkValFree(&rv);
        if (n->sub == AWKT_NOMATCH) match = !match;
        return AwkNumV(match ? 1.0 : 0.0);
    }
    case AWKN_CMP: {
        AwkValue lv = AwkEval(c, n->a);
        AwkValue rv = AwkEval(c, n->b);
        int res = 0;
        if ((lv.flags & AWKF_NUM) && (rv.flags & AWKF_NUM)) {
            double d1 = lv.num, d2 = rv.num;
            res = (d1 < d2) ? -1 : (d1 > d2 ? 1 : 0);
        } else {
            wchar_t* s1 = AwkToStr(c, &lv);
            wchar_t* s2 = AwkToStr(c, &rv);
            res = wcscmp(s1, s2);
            free(s1); free(s2);
        }
        AwkValFree(&lv); AwkValFree(&rv);
        bool matched = false;
        switch (n->sub) {
        case AWKT_EQ: matched = (res == 0); break;
        case AWKT_NE: matched = (res != 0); break;
        case AWKT_LT: matched = (res < 0); break;
        case AWKT_LE: matched = (res <= 0); break;
        case AWKT_GT: matched = (res > 0); break;
        case AWKT_GE: matched = (res >= 0); break;
        }
        return AwkNumV(matched ? 1.0 : 0.0);
    }
    case AWKN_ARITH: {
        AwkValue lv = AwkEval(c, n->a);
        AwkValue rv = AwkEval(c, n->b);
        double d1 = AwkNumOf(c, &lv);
        double d2 = AwkNumOf(c, &rv);
        AwkValFree(&lv); AwkValFree(&rv);
        double res = 0;
        switch (n->sub) {
        case AWKT_PLUS: res = d1 + d2; break;
        case AWKT_MINUS: res = d1 - d2; break;
        case AWKT_STAR: res = d1 * d2; break;
        case AWKT_SLASH: res = (d2 != 0) ? (d1 / d2) : 0; break;
        case AWKT_PERCENT: res = (d2 != 0) ? fmod(d1, d2) : 0; break;
        case AWKT_CARET: res = pow(d1, d2); break;
        }
        return AwkNumV(res);
    }
    case AWKN_CONCAT: {
        AwkValue lv = AwkEval(c, n->a);
        wchar_t* s1 = AwkToStr(c, &lv);
        AwkValue rv = AwkEval(c, n->b);
        wchar_t* s2 = AwkToStr(c, &rv);
        size_t l1 = wcslen(s1), l2 = wcslen(s2);
        wchar_t* cat = (wchar_t*)malloc((l1 + l2 + 1) * sizeof(wchar_t));
        memcpy(cat, s1, l1 * sizeof(wchar_t));
        memcpy(cat + l1, s2, (l2 + 1) * sizeof(wchar_t));
        free(s1); free(s2);
        AwkValFree(&lv); AwkValFree(&rv);
        return AwkStrOwnV(cat);
    }
    case AWKN_GROUP: {
        if (n->nlist > 0) return AwkEval(c, n->list[n->nlist - 1]);
        return AwkUninitV(c);
    }
    case AWKN_CALL: {
        for (int i = 0; i < c->nfuncs; i++) {
            if (wcscmp(c->funcs[i].name, n->str) == 0) {
                return AwkCallFunc(c, i, n);
            }
        }
        AwkFatal(c, L"call to undefined function '%ls'", n->str);
        break;
    }
    case AWKN_BUILTIN: return AwkCallBuiltin(c, n->idx, n);
    default: break;
    }
    return AwkUninitV(c);
}

static int AwkExecStmt(AwkCtx* c, AwkNode* n) {
    if (!n) return AWKX_NORMAL;

    switch (n->op) {
    case AWKN_NOP: return AWKX_NORMAL;
    case AWKN_BLOCK: {
        for (size_t i = 0; i < n->nlist; i++) {
            int r = AwkExecStmt(c, n->list[i]);
            if (r != AWKX_NORMAL) return r;
        }
        return AWKX_NORMAL;
    }
    case AWKN_EXPR: {
        AwkValue v = AwkEval(c, n->a);
        AwkValFree(&v);
        return AWKX_NORMAL;
    }
    case AWKN_IF: {
        AwkValue cv = AwkEval(c, n->a);
        bool t = AwkTruthy(&cv);
        AwkValFree(&cv);
        if (t) return AwkExecStmt(c, n->b);
        else if (n->c) return AwkExecStmt(c, n->c);
        return AWKX_NORMAL;
    }
    case AWKN_WHILE: {
        for (;;) {
            AwkValue cv = AwkEval(c, n->a);
            bool t = AwkTruthy(&cv);
            AwkValFree(&cv);
            if (!t) break;
            int r = AwkExecStmt(c, n->b);
            if (r == AWKX_BREAK) break;
            if (r == AWKX_CONTINUE) continue;
            if (r != AWKX_NORMAL) return r;
        }
        return AWKX_NORMAL;
    }
    case AWKN_DO: {
        for (;;) {
            int r = AwkExecStmt(c, n->a);
            if (r == AWKX_BREAK) break;
            if (r != AWKX_CONTINUE && r != AWKX_NORMAL) return r;
            AwkValue cv = AwkEval(c, n->b);
            bool t = AwkTruthy(&cv);
            AwkValFree(&cv);
            if (!t) break;
        }
        return AWKX_NORMAL;
    }
    case AWKN_FOR: {
        if (n->a) {
            AwkValue v = AwkEval(c, n->a);
            AwkValFree(&v);
        }
        for (;;) {
            if (n->b) {
                AwkValue cv = AwkEval(c, n->b);
                bool t = AwkTruthy(&cv);
                AwkValFree(&cv);
                if (!t) break;
            }
            int r = AwkExecStmt(c, n->d);
            if (r == AWKX_BREAK) break;
            if (r != AWKX_CONTINUE && r != AWKX_NORMAL) return r;
            if (n->c) {
                AwkValue sv = AwkEval(c, n->c);
                AwkValFree(&sv);
            }
        }
        return AWKX_NORMAL;
    }
    case AWKN_FORIN: {
        AwkVar* arr_var = AwkGetVar(c, n->b->idx, n->b->local);
        if (!arr_var->arr) return AWKX_NORMAL;
        size_t nkeys = 0;
        wchar_t** keys = AwkArrayKeys(c, arr_var->arr, &nkeys);
        AwkRef vref;
        AwkEvalLValue(c, n->a, &vref);
        for (size_t i = 0; i < nkeys; i++) {
            AwkRefSet(c, &vref, AwkStrOwnV(keys[i]));
            int r = AwkExecStmt(c, n->c);
            if (r == AWKX_BREAK) break;
            if (r == AWKX_CONTINUE) continue;
            if (r != AWKX_NORMAL) {
                for (size_t j = i + 1; j < nkeys; j++) free(keys[j]);
                free(keys);
                return r;
            }
        }
        free(keys);
        return AWKX_NORMAL;
    }
    case AWKN_BREAK: return AWKX_BREAK;
    case AWKN_CONTINUE: return AWKX_CONTINUE;
    case AWKN_NEXT: return AWKX_NEXT;
    case AWKN_NEXTFILE: return AWKX_NEXTFILE;
    case AWKN_EXIT: {
        if (n->a) {
            AwkValue v = AwkEval(c, n->a);
            c->exit_code = (int)AwkToLL(AwkNumOf(c, &v));
            AwkValFree(&v);
        }
        return AWKX_EXIT;
    }
    case AWKN_RETURN: {
        if (n->a) {
            c->retval = AwkEval(c, n->a);
        } else {
            c->retval = AwkUninitV(c);
        }
        return AWKX_RETURN;
    }
    case AWKN_DELETE: {
        if (n->a->op == AWKN_ELEM) {
            AwkVar* v = AwkGetVar(c, n->a->a->idx, n->a->a->local);
            if (v->arr) {
                AwkBuf b = { NULL, 0, 0 };
                const wchar_t* subsep = AwkToStr(c, &c->globals[SUBSEP_IDX].v);
                for (size_t i = 0; i < n->a->nlist; i++) {
                    if (i > 0) AwkBufAddStr(c, &b, subsep);
                    AwkValue kv = AwkEval(c, n->a->list[i]);
                    wchar_t* ks = AwkToStr(c, &kv);
                    AwkBufAddStr(c, &b, ks);
                    free(ks); AwkValFree(&kv);
                }
                free((wchar_t*)subsep);
                AwkArrayRemove(v->arr, b.p ? b.p : L"");
                AwkBufFree(&b);
            }
        } else if (n->a->op == AWKN_VAR) {
            AwkVar* v = AwkGetVar(c, n->a->idx, n->a->local);
            if (v->arr) AwkArrayClear(v->arr);
        }
        return AWKX_NORMAL;
    }
    case AWKN_PRINT: {
        FILE* fp = stdout;
        if (n->idx != AWKR_NONE) {
            AwkValue dv = AwkEval(c, n->a);
            wchar_t* dest = AwkToStr(c, &dv);
            int kind = (n->idx == AWKR_FILE || n->idx == AWKR_APPEND) ? AWK_ST_OUTFILE : AWK_ST_OUTPIPE;
            fp = AwkGetOutStream(c, dest, kind);
            free(dest); AwkValFree(&dv);
        }
        AwkNode** items = n->list;
        size_t nitems = n->nlist;
        if (nitems == 1 && items[0]->op == AWKN_GROUP) {
            nitems = items[0]->nlist;
            items = items[0]->list;
        }
        if (nitems == 0) {
            AwkEnsureRecord(c);
            fputws(c->rec ? c->rec : L"", fp);
        } else {
            const wchar_t* ofs = AwkToStr(c, &c->globals[AWKV_OFS].v);
            for (size_t i = 0; i < nitems; i++) {
                if (i > 0) fputws(ofs, fp);
                AwkValue v = AwkEval(c, items[i]);
                wchar_t* s = AwkToOutStr(c, &v);
                fputws(s, fp);
                free(s); AwkValFree(&v);
            }
            free((wchar_t*)ofs);
        }
        const wchar_t* ors = AwkToStr(c, &c->globals[AWKV_ORS].v);
        fputws(ors, fp);
        free((wchar_t*)ors);
        return AWKX_NORMAL;
    }
    case AWKN_PRINTF: {
        FILE* fp = stdout;
        if (n->idx != AWKR_NONE) {
            AwkValue dv = AwkEval(c, n->a);
            wchar_t* dest = AwkToStr(c, &dv);
            int kind = (n->idx == AWKR_FILE || n->idx == AWKR_APPEND) ? AWK_ST_OUTFILE : AWK_ST_OUTPIPE;
            fp = AwkGetOutStream(c, dest, kind);
            free(dest); AwkValFree(&dv);
        }
        AwkNode** items = n->list;
        size_t nitems = n->nlist;
        if (nitems == 1 && items[0]->op == AWKN_GROUP) {
            nitems = items[0]->nlist;
            items = items[0]->list;
        }
        if (nitems > 0) {
            AwkValue fv = AwkEval(c, items[0]);
            wchar_t* fmt = AwkToStr(c, &fv);
            size_t nargs = nitems - 1;
            AwkValue* args = (AwkValue*)malloc((nargs ? nargs : 1) * sizeof(AwkValue));
            for (size_t i = 0; i < nargs; i++) args[i] = AwkEval(c, items[i + 1]);
            wchar_t* formatted = AwkFormatOwn(c, fmt, args, nargs);
            fputws(formatted, fp);
            free(formatted);
            for (size_t i = 0; i < nargs; i++) AwkValFree(&args[i]);
            free(args); free(fmt); AwkValFree(&fv);
        }
        return AWKX_NORMAL;
    }
    default: break;
    }
    return AWKX_NORMAL;
}

static bool AwkReadRecord(AwkCtx* c, FILE* fp, const wchar_t* rs, bool is_para) {
    if (!fp) return false;
    free(c->rec);
    c->rec = NULL;
    c->reclen = 0;
    c->rec_valid = false;
    c->split_valid = false;

    AwkBuf b = { NULL, 0, 0 };

    if (is_para) {
        /* Paragraph mode: skip leading blank lines, then read until double newline */
        wint_t wch;
        while ((wch = fgetwc(fp)) != WEOF) {
            if (wch != L'\n' && wch != L'\r') {
                ungetwc(wch, fp);
                break;
            }
        }
        if (wch == WEOF) return false;
        int consecutive_nl = 0;
        while ((wch = fgetwc(fp)) != WEOF) {
            if (wch == L'\r') continue;
            if (wch == L'\n') {
                consecutive_nl++;
                if (consecutive_nl >= 2) break;
                AwkBufAdd(c, &b, L"\n", 1);
            } else {
                consecutive_nl = 0;
                wchar_t ch = (wchar_t)wch;
                AwkBufAdd(c, &b, &ch, 1);
            }
        }
    } else {
        wchar_t term = (rs && rs[0]) ? rs[0] : L'\n';
        wint_t wch;
        bool any = false;
        while ((wch = fgetwc(fp)) != WEOF) {
            any = true;
            if ((wchar_t)wch == term) break;
            if (term == L'\n' && wch == L'\r') continue;
            wchar_t ch = (wchar_t)wch;
            AwkBufAdd(c, &b, &ch, 1);
        }
        if (!any && wch == WEOF) return false;
    }

    c->rec = AwkBufTake(c, &b);
    c->reclen = wcslen(c->rec);
    c->rec_valid = true;
    c->split_valid = false;
    return true;
}

static void AwkExecuteAll(AwkCtx* c, int argc, wchar_t** argv, int file_start) {
    /* 1. Run BEGIN blocks */
    for (size_t i = 0; i < c->nitems; i++) {
        if (c->items[i].kind == AWKI_BEGIN) {
            int r = AwkExecStmt(c, c->items[i].action);
            if (r == AWKX_EXIT) goto run_end;
        }
    }

    /* Check if we have any MAIN or END items */
    bool have_main = false;
    for (size_t i = 0; i < c->nitems; i++) {
        if (c->items[i].kind == AWKI_MAIN) { have_main = true; break; }
    }

    if (!have_main) goto run_end;

    int nfiles = (file_start < argc) ? (argc - file_start) : 0;
    const wchar_t* default_stdin[1] = { L"-" };
    const wchar_t* const* files = (nfiles > 0) ? (const wchar_t* const*)(argv + file_start) : default_stdin;
    if (nfiles <= 0) nfiles = 1;

    double nr = 0;

    for (int fi = 0; fi < nfiles; fi++) {
        const wchar_t* fname = files[fi];

        /* Check for inline assignment var=val */
        const wchar_t* eq = wcschr(fname, L'=');
        if (eq && wcscmp(fname, L"-") != 0) {
            size_t nlen = (size_t)(eq - fname);
            wchar_t* vname = AwkDupN(c, fname, nlen);
            int vidx = AwkFindOrAddGlobal(c, vname);
            free(vname);
            AwkRefSet(c, &(AwkRef){ AWKL_VAR, &c->globals[vidx], NULL, NULL, 0 },
                      AwkStrnumOwnV(c, AwkDupN(c, eq + 1, wcslen(eq + 1))));
            continue;
        }

        bool is_stdin = (wcscmp(fname, L"-") == 0);
        FILE* fp = is_stdin ? stdin : _wfsopen(fname, L"r", _SH_DENYNO);
        if (!fp) {
            fwprintf(stderr, L"awk: cannot open %ls\n", fname);
            c->exit_code = 2;
            continue;
        }

        AwkValFree(&c->globals[AWKV_FILENAME].v);
        c->globals[AWKV_FILENAME].v = AwkStrOwnV(AwkDupN(c, fname, wcslen(fname)));
        double fnr = 0;

        for (;;) {
            wchar_t* rs_str = AwkToStr(c, &c->globals[AWKV_RS].v);
            bool is_para = (rs_str[0] == L'\0');
            bool ok = AwkReadRecord(c, fp, rs_str, is_para);
            free(rs_str);
            if (!ok) break;

            nr += 1.0;
            fnr += 1.0;
            AwkValFree(&c->globals[AWKV_NR].v);
            c->globals[AWKV_NR].v = AwkNumV(nr);
            AwkValFree(&c->globals[AWKV_FNR].v);
            c->globals[AWKV_FNR].v = AwkNumV(fnr);

            bool next_record = false;

            for (size_t i = 0; i < c->nitems; i++) {
                AwkItem* it = &c->items[i];
                if (it->kind != AWKI_MAIN) continue;

                bool run_action = false;
                if (!it->pat1) {
                    run_action = true;
                } else if (it->pat2) {
                    /* Range pattern */
                    if (!it->in_range) {
                        AwkValue v1 = AwkEval(c, it->pat1);
                        if (AwkTruthy(&v1)) {
                            it->in_range = true;
                            run_action = true;
                        }
                        AwkValFree(&v1);
                    } else {
                        run_action = true;
                        AwkValue v2 = AwkEval(c, it->pat2);
                        if (AwkTruthy(&v2)) it->in_range = false;
                        AwkValFree(&v2);
                    }
                } else {
                    AwkValue v1 = AwkEval(c, it->pat1);
                    run_action = AwkTruthy(&v1);
                    AwkValFree(&v1);
                }

                if (run_action) {
                    int r = AwkExecStmt(c, it->action);
                    if (r == AWKX_NEXT) { next_record = true; break; }
                    if (r == AWKX_NEXTFILE) {
                        if (!is_stdin) fclose(fp);
                        goto next_file;
                    }
                    if (r == AWKX_EXIT) {
                        if (!is_stdin) fclose(fp);
                        goto run_end;
                    }
                }
            }
            if (next_record) continue;
        }

        if (!is_stdin) fclose(fp);
next_file:;
    }

run_end:
    /* Run END blocks */
    for (size_t i = 0; i < c->nitems; i++) {
        if (c->items[i].kind == AWKI_END) {
            int r = AwkExecStmt(c, c->items[i].action);
            if (r == AWKX_EXIT) break;
        }
    }
}

/* ========================================================================= */
/*                          Builtin_Awk Entry Point                          */
/* ========================================================================= */

bool Builtin_Awk(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: awk [-safe] [-V] [-d[n]] [-F fs] [-v var=val] [-f progfile] [-e prog] ['program'] [file...]\n"
                L"Pattern scanning and processing language (BSD/POSIX awk).\n\n"
                L"  -F fs         Set field separator\n"
                L"  -v var=val    Assign variable before execution\n"
                L"  -f progfile   Read program text from file\n"
                L"  -e prog       Specify program text directly\n"
                L"  -safe         Disable system(), pipes, and redirection\n"
                L"  -V, --version Print version information\n");
        *code = 0;
        return true;
    }

    AwkCtx ctx;
    memset(&ctx, 0, sizeof(AwkCtx));
    ctx.cur_func = -1;
    ctx.rng = 1;

    /* Initialize memory pool */
    ctx.cappool = 256;
    ctx.pool = (AwkPoolEnt*)calloc(ctx.cappool, sizeof(AwkPoolEnt));

    /* Initialize special variables in globals */
    ctx.globals = (AwkVar*)calloc(AWKV_COUNT + 64, sizeof(AwkVar));
    if (!ctx.pool || !ctx.globals) {
        free(ctx.pool);
        free(ctx.globals);
        *code = 1;
        return true;
    }
    for (int i = 0; i < AWKV_COUNT; i++) ctx.globals[i].v = AwkUninitV(&ctx);

    ctx.globals[AWKV_FS].v = AwkStrOwnV(AwkDupN(&ctx, L" ", 1));
    ctx.globals[AWKV_OFS].v = AwkStrOwnV(AwkDupN(&ctx, L" ", 1));
    ctx.globals[AWKV_RS].v = AwkStrOwnV(AwkDupN(&ctx, L"\n", 1));
    ctx.globals[AWKV_ORS].v = AwkStrOwnV(AwkDupN(&ctx, L"\n", 1));
    ctx.globals[SUBSEP_IDX].v = AwkStrOwnV(AwkDupN(&ctx, L"\034", 1));
    ctx.globals[AWKV_CONVFMT].v = AwkStrOwnV(AwkDupN(&ctx, L"%.6g", 4));
    ctx.globals[AWKV_OFMT].v = AwkStrOwnV(AwkDupN(&ctx, L"%.6g", 4));
    ctx.globals[AWKV_NR].v = AwkNumV(0.0);
    ctx.globals[AWKV_FNR].v = AwkNumV(0.0);
    ctx.globals[AWKV_NF].v = AwkNumV(0.0);
    ctx.globals[AWKV_RSTART].v = AwkNumV(0.0);
    ctx.globals[AWKV_RLENGTH].v = AwkNumV(-1.0);

    /* ARGV and ENVIRON arrays */
    ctx.globals[AWKV_ARGV].arr = AwkArrayNew(&ctx);
    ctx.globals[AWKV_ENVIRON].arr = AwkArrayNew(&ctx);

    /* Populate ENVIRON */
    for (wchar_t** env = _wenviron; env && *env; env++) {
        wchar_t* eq = wcschr(*env, L'=');
        if (eq) {
            size_t nlen = (size_t)(eq - *env);
            wchar_t* k = AwkDupN(&ctx, *env, nlen);
            AwkElem* e = AwkArrayGet(&ctx, ctx.globals[AWKV_ENVIRON].arr, k);
            free(k);
            AwkValFree(&e->v);
            e->v = AwkStrOwnV(AwkDupN(&ctx, eq + 1, wcslen(eq + 1)));
        }
    }

    AwkBuf prog_buf = { NULL, 0, 0 };
    bool have_prog = false;
    int file_start = cmd->argc;

    /* BSD awk qualifier parsing */
    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (wcscmp(arg, L"--") == 0) {
            file_start = i + 1;
            break;
        }
        if (wcscmp(arg, L"-V") == 0 || wcscmp(arg, L"--version") == 0) {
            wprintf(L"awk (BSD/POSIX) 1.0 (ntksh)\n");
            *code = 0;
            return true;
        }
        if (wcscmp(arg, L"-safe") == 0) {
            ctx.safe_mode = true;
            continue;
        }
        if (wcsncmp(arg, L"-d", 2) == 0 || wcsncmp(arg, L"-m", 2) == 0) {
            /* -d[n] debug, -mr [n], -mf [n] legacy BSD memory limit: accepted and ignored */
            if ((wcscmp(arg, L"-mr") == 0 || wcscmp(arg, L"-mf") == 0 || wcscmp(arg, L"-d") == 0) &&
                i + 1 < cmd->argc && iswdigit(cmd->argv[i + 1][0])) {
                i++;
            }
            continue;
        }
        if (wcsncmp(arg, L"-F", 2) == 0) {
            const wchar_t* fs_val = (arg[2] != L'\0') ? arg + 2 : ((i + 1 < cmd->argc) ? cmd->argv[++i] : L" ");
            if (wcscmp(fs_val, L"\\t") == 0) fs_val = L"\t";
            AwkValFree(&ctx.globals[AWKV_FS].v);
            ctx.globals[AWKV_FS].v = AwkStrOwnV(AwkDupN(&ctx, fs_val, wcslen(fs_val)));
            continue;
        }
        if (wcscmp(arg, L"-v") == 0 && i + 1 < cmd->argc) {
            const wchar_t* assign = cmd->argv[++i];
            const wchar_t* eq = wcschr(assign, L'=');
            if (eq) {
                size_t nlen = (size_t)(eq - assign);
                wchar_t* vname = AwkDupN(&ctx, assign, nlen);
                int vidx = AwkFindOrAddGlobal(&ctx, vname);
                free(vname);
                AwkValFree(&ctx.globals[vidx].v);
                ctx.globals[vidx].v = AwkStrnumOwnV(&ctx, AwkDupN(&ctx, eq + 1, wcslen(eq + 1)));
            }
            continue;
        }
        if (wcscmp(arg, L"-f") == 0 && i + 1 < cmd->argc) {
            const wchar_t* fname = cmd->argv[++i];
            bool is_stdin_prog = (wcscmp(fname, L"-") == 0);
            FILE* pfp = is_stdin_prog ? stdin : _wfsopen(fname, L"rt, ccs=UTF-8", _SH_DENYNO);
            if (!pfp && !is_stdin_prog) pfp = _wfsopen(fname, L"r", _SH_DENYNO);
            if (!pfp) {
                fwprintf(stderr, L"awk: cannot open program file %ls\n", fname);
                *code = 2;
                return true;
            }
            wchar_t lbuf[1024];
            while (fgetws(lbuf, 1024, pfp)) AwkBufAddStr(&ctx, &prog_buf, lbuf);
            AwkBufAddStr(&ctx, &prog_buf, L"\n");
            if (!is_stdin_prog) fclose(pfp);
            have_prog = true;
            continue;
        }
        if (wcscmp(arg, L"-e") == 0 && i + 1 < cmd->argc) {
            const wchar_t* pcode = cmd->argv[++i];
            AwkBufAddStr(&ctx, &prog_buf, pcode);
            AwkBufAddStr(&ctx, &prog_buf, L"\n");
            have_prog = true;
            continue;
        }
        if (arg[0] == L'-' && arg[1] != L'\0') {
            fwprintf(stderr, L"awk: unrecognized option '%ls'\n", arg);
            *code = 2;
            return true;
        }

        if (!have_prog) {
            AwkBufAddStr(&ctx, &prog_buf, arg);
            have_prog = true;
        } else {
            file_start = i;
            break;
        }
    }

    if (!have_prog) {
        fwprintf(stderr, L"awk: no program specified\n");
        *code = 1;
        return true;
    }

    /* Populate ARGC and ARGV */
    int awk_argc = 1 + ((file_start < cmd->argc) ? (cmd->argc - file_start) : 0);
    AwkValFree(&ctx.globals[AWKV_ARGC].v);
    ctx.globals[AWKV_ARGC].v = AwkNumV((double)awk_argc);

    AwkElem* e0 = AwkArrayGet(&ctx, ctx.globals[AWKV_ARGV].arr, L"0");
    AwkValFree(&e0->v);
    e0->v = AwkStrOwnV(AwkDupN(&ctx, L"awk", 3));

    int arg_idx = 1;
    for (int i = file_start; i < cmd->argc; i++) {
        wchar_t kbuf[32];
        _snwprintf(kbuf, 32, L"%d", arg_idx++);
        AwkElem* e = AwkArrayGet(&ctx, ctx.globals[AWKV_ARGV].arr, kbuf);
        AwkValFree(&e->v);
        e->v = AwkStrOwnV(AwkDupN(&ctx, cmd->argv[i], wcslen(cmd->argv[i])));
    }

    /* Set up parsing source */
    ctx.src = prog_buf.p ? prog_buf.p : L"";
    ctx.srclen = prog_buf.len;
    ctx.pos = 0;
    ctx.line = 1;

    if (setjmp(ctx.jb) == 0) {
        AwkParseProgram(&ctx);
        AwkExecuteAll(&ctx, cmd->argc, cmd->argv, file_start);
        *code = ctx.exit_code;
    } else {
        *code = 1;
    }

    /* Cleanup streams and memory */
    while (ctx.streams) {
        AwkStream* s = ctx.streams;
        ctx.streams = s->next;
        if (s->kind == AWK_ST_OUTPIPE || s->kind == AWK_ST_INPIPE) _pclose(s->fp);
        else fclose(s->fp);
        free(s->name);
        free(s);
    }
    for (int i = 0; i < ctx.nrx; i++) RegexFree(ctx.rx[i].prog);
    for (int i = 0; i < ctx.nfuncs; i++) free(ctx.funcs[i].params);
    free(ctx.funcs);
    free(ctx.globals);
    for (size_t i = 0; i < ctx.npool; i++) free(ctx.pool[i].p);
    free(ctx.pool);
    AwkBufFree(&prog_buf);
    return true;
}
