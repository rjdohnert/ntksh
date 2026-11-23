/* ========================================================================= */
/*              Category: Regex engine (BRE / ERE)                           */
/* ========================================================================= */

#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>
#include <wctype.h>
#include <stdlib.h>
#include <string.h>

enum {
    RX_CHAR, RX_ANY, RX_SET, RX_BOL, RX_EOL, RX_WORDB, RX_NWORDB,
    RX_WSTART, RX_WEND, RX_WORDCH, RX_NWORDCH, RX_SPACE, RX_NSPACE,
    RX_GROUP, RX_REPEAT, RX_BACKREF
};

enum {
    RXC_ALPHA = 1 << 0, RXC_DIGIT = 1 << 1, RXC_ALNUM = 1 << 2, RXC_UPPER = 1 << 3,
    RXC_LOWER = 1 << 4, RXC_SPACE = 1 << 5, RXC_BLANK = 1 << 6, RXC_PUNCT = 1 << 7,
    RXC_PRINT = 1 << 8, RXC_GRAPH = 1 << 9, RXC_CNTRL = 1 << 10, RXC_XDIGIT = 1 << 11
};

typedef struct RxNode RxNode;
typedef struct RxSeq {
    RxNode** items;
    int count;
    int cap;
} RxSeq;

typedef struct RxRange {
    wchar_t lo;
    wchar_t hi;
} RxRange;

typedef struct RxSet {
    RxRange* ranges;
    int nranges;
    unsigned classes;
    bool neg;
} RxSet;

struct RxNode {
    int type;
    wchar_t ch;
    RxSet set;
    int group;
    int min;
    int max;
    RxSeq* alts;
    int nalts;
    RxNode* child;
};

struct RegexProg {
    RxNode* root;
    bool icase;
};

typedef struct RxParser {
    const wchar_t* p;
    bool ere;
    int ngroups;
    int closed_groups;
    const wchar_t* err;
} RxParser;

static RxNode* RxNewNode(int type) {
    RxNode* n = (RxNode*)calloc(1, sizeof(RxNode));
    if (n) {
        n->type = type;
        n->group = -1;
        n->max = -1;
    }
    return n;
}

static void RxSeqPush(RxSeq* s, RxNode* n) {
    if (s->count == s->cap) {
        int ncap = s->cap ? s->cap * 2 : 4;
        RxNode** items = (RxNode**)realloc(s->items, sizeof(RxNode*) * ncap);
        if (!items) return;
        s->items = items;
        s->cap = ncap;
    }
    s->items[s->count++] = n;
}

static void RxFreeNode(RxNode* n);

static void RxSeqFree(RxSeq* s) {
    for (int i = 0; i < s->count; i++) RxFreeNode(s->items[i]);
    free(s->items);
    s->items = NULL;
    s->count = s->cap = 0;
}

static void RxFreeNode(RxNode* n) {
    if (!n) return;
    if (n->type == RX_SET) free(n->set.ranges);
    if (n->type == RX_GROUP) {
        for (int i = 0; i < n->nalts; i++) RxSeqFree(&n->alts[i]);
        free(n->alts);
    }
    if (n->type == RX_REPEAT) RxFreeNode(n->child);
    free(n);
}

static bool RxAtAlt(const RxParser* ps) {
    if (ps->ere) return *ps->p == L'|';
    return ps->p[0] == L'\\' && ps->p[1] == L'|';
}

static bool RxAtClose(const RxParser* ps) {
    if (ps->ere) return *ps->p == L')';
    return ps->p[0] == L'\\' && ps->p[1] == L')';
}

static bool RxIsWordChar(wchar_t c) {
    return c == L'_' || iswalnum(c);
}

static bool RxCharEq(wchar_t a, wchar_t b, bool icase) {
    if (a == b) return true;
    return icase && towlower(a) == towlower(b);
}

static bool RxClassMatch(unsigned classes, wchar_t c) {
    if ((classes & RXC_ALPHA) && iswalpha(c)) return true;
    if ((classes & RXC_DIGIT) && c >= L'0' && c <= L'9') return true;
    if ((classes & RXC_ALNUM) && iswalnum(c)) return true;
    if ((classes & RXC_UPPER) && iswupper(c)) return true;
    if ((classes & RXC_LOWER) && iswlower(c)) return true;
    if ((classes & RXC_SPACE) && iswspace(c)) return true;
    if ((classes & RXC_BLANK) && (c == L' ' || c == L'\t')) return true;
    if ((classes & RXC_PUNCT) && iswpunct(c)) return true;
    if ((classes & RXC_PRINT) && iswprint(c)) return true;
    if ((classes & RXC_GRAPH) && iswgraph(c)) return true;
    if ((classes & RXC_CNTRL) && iswcntrl(c)) return true;
    if ((classes & RXC_XDIGIT) && iswxdigit(c)) return true;
    return false;
}

static bool RxSetHasRaw(const RxSet* s, wchar_t c) {
    for (int i = 0; i < s->nranges; i++) {
        if (c >= s->ranges[i].lo && c <= s->ranges[i].hi) return true;
    }
    return RxClassMatch(s->classes, c);
}

static bool RxSetMatch(const RxSet* s, wchar_t c, bool icase) {
    bool in = RxSetHasRaw(s, c);
    if (!in && icase) in = RxSetHasRaw(s, towlower(c)) || RxSetHasRaw(s, towupper(c));
    return in != s->neg;
}

static RxNode* RxParseAlt(RxParser* ps, int depth);

static bool RxAddRange(RxSet* set, wchar_t lo, wchar_t hi) {
    RxRange* r = (RxRange*)realloc(set->ranges, sizeof(RxRange) * (set->nranges + 1));
    if (!r) return false;
    set->ranges = r;
    set->ranges[set->nranges].lo = lo;
    set->ranges[set->nranges].hi = hi;
    set->nranges++;
    return true;
}

static unsigned RxClassBit(const wchar_t* name, size_t len) {
    static const struct { const char* name; unsigned bit; } table[] = {
        {"alpha", RXC_ALPHA}, {"digit", RXC_DIGIT}, {"alnum", RXC_ALNUM}, {"upper", RXC_UPPER},
        {"lower", RXC_LOWER}, {"space", RXC_SPACE}, {"blank", RXC_BLANK}, {"punct", RXC_PUNCT},
        {"print", RXC_PRINT}, {"graph", RXC_GRAPH}, {"cntrl", RXC_CNTRL}, {"xdigit", RXC_XDIGIT}
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        size_t n = strlen(table[i].name);
        if (n != len) continue;
        bool same = true;
        for (size_t j = 0; j < n && same; j++) same = (name[j] == (wchar_t)table[i].name[j]);
        if (same) return table[i].bit;
    }
    return 0;
}

/* Parses a bracket expression; ps->p points at '['. */
static RxNode* RxParseBracket(RxParser* ps) {
    RxNode* n = RxNewNode(RX_SET);
    if (!n) return NULL;
    ps->p++;
    if (*ps->p == L'^') {
        n->set.neg = true;
        ps->p++;
    }
    bool first = true;
    for (;;) {
        if (!*ps->p) {
            ps->err = L"Unmatched [, [^, [:, [., or [=";
            RxFreeNode(n);
            return NULL;
        }
        if (*ps->p == L']' && !first) {
            ps->p++;
            break;
        }
        first = false;
        if (ps->p[0] == L'[' && ps->p[1] == L':') {
            const wchar_t* end = wcsstr(ps->p + 2, L":]");
            unsigned bit = end ? RxClassBit(ps->p + 2, (size_t)(end - (ps->p + 2))) : 0;
            if (!end || !bit) {
                ps->err = end ? L"Invalid character class name" : L"Unmatched [, [^, [:, [., or [=";
                RxFreeNode(n);
                return NULL;
            }
            n->set.classes |= bit;
            ps->p = end + 2;
            continue;
        }
        wchar_t lo;
        if (ps->p[0] == L'[' && (ps->p[1] == L'=' || ps->p[1] == L'.')) {
            wchar_t term = ps->p[1];
            if (!ps->p[2] || ps->p[3] != term || ps->p[4] != L']') {
                ps->err = L"Unmatched [, [^, [:, [., or [=";
                RxFreeNode(n);
                return NULL;
            }
            lo = ps->p[2];
            ps->p += 5;
        } else {
            lo = *ps->p++;
        }
        wchar_t hi = lo;
        if (ps->p[0] == L'-' && ps->p[1] && ps->p[1] != L']') {
            hi = ps->p[1];
            ps->p += 2;
            if (hi < lo) {
                ps->err = L"Invalid range end";
                RxFreeNode(n);
                return NULL;
            }
        }
        if (!RxAddRange(&n->set, lo, hi)) {
            RxFreeNode(n);
            return NULL;
        }
    }
    return n;
}

/* Parses an interval body after the opening brace; leaves ps->p after the close token. */
static bool RxParseInterval(RxParser* ps, int* min, int* max) {
    const wchar_t* q = ps->p;
    long lo = -1, hi = -1;
    bool comma = false;
    while (*q >= L'0' && *q <= L'9') {
        lo = (lo < 0 ? 0 : lo) * 10 + (*q - L'0');
        q++;
    }
    if (*q == L',') {
        comma = true;
        q++;
        while (*q >= L'0' && *q <= L'9') {
            hi = (hi < 0 ? 0 : hi) * 10 + (*q - L'0');
            q++;
        }
    }
    bool close = ps->ere ? (*q == L'}') : (q[0] == L'\\' && q[1] == L'}');
    if (!close || (lo < 0 && !comma) || lo > 255 || hi > 255) return false;
    if (lo < 0) lo = 0;
    if (comma && hi < 0) hi = -1;
    else if (!comma) hi = lo;
    if (hi >= 0 && hi < lo) return false;
    *min = (int)lo;
    *max = (int)hi;
    ps->p = q + (ps->ere ? 1 : 2);
    return true;
}

/* Consumes one postfix repetition operator if present. */
static bool RxParsePostfix(RxParser* ps, int* min, int* max, bool* error) {
    *error = false;
    if (*ps->p == L'*') { *min = 0; *max = -1; ps->p++; return true; }
    if (ps->ere) {
        if (*ps->p == L'+') { *min = 1; *max = -1; ps->p++; return true; }
        if (*ps->p == L'?') { *min = 0; *max = 1; ps->p++; return true; }
        if (*ps->p == L'{') {
            const wchar_t* save = ps->p;
            ps->p++;
            if (RxParseInterval(ps, min, max)) return true;
            ps->p = save;
            return false;
        }
        return false;
    }
    if (ps->p[0] == L'\\' && ps->p[1] == L'+') { *min = 1; *max = -1; ps->p += 2; return true; }
    if (ps->p[0] == L'\\' && ps->p[1] == L'?') { *min = 0; *max = 1; ps->p += 2; return true; }
    if (ps->p[0] == L'\\' && ps->p[1] == L'{') {
        ps->p += 2;
        if (RxParseInterval(ps, min, max)) return true;
        ps->err = L"Invalid content of \\{\\}";
        *error = true;
        return false;
    }
    return false;
}

static RxNode* RxParseEscape(RxParser* ps) {
    wchar_t e = ps->p[1];
    if (!e) {
        ps->err = L"Trailing backslash";
        return NULL;
    }
    int type = -1;
    switch (e) {
        case L'w': type = RX_WORDCH; break;
        case L'W': type = RX_NWORDCH; break;
        case L's': type = RX_SPACE; break;
        case L'S': type = RX_NSPACE; break;
        case L'b': type = RX_WORDB; break;
        case L'B': type = RX_NWORDB; break;
        case L'<': type = RX_WSTART; break;
        case L'>': type = RX_WEND; break;
        case L'`': type = RX_BOL; break;
        case L'\'': type = RX_EOL; break;
    }
    if (e >= L'1' && e <= L'9') {
        int g = e - L'0';
        if (g > ps->closed_groups) {
            ps->err = L"Invalid back reference";
            return NULL;
        }
        RxNode* n = RxNewNode(RX_BACKREF);
        if (n) n->group = g;
        ps->p += 2;
        return n;
    }
    if (type >= 0) {
        ps->p += 2;
        return RxNewNode(type);
    }
    RxNode* n = RxNewNode(RX_CHAR);
    if (n) n->ch = e;
    ps->p += 2;
    return n;
}

static RxNode* RxParseAtom(RxParser* ps, RxSeq* seq, int depth) {
    wchar_t c = *ps->p;
    bool open_bre = !ps->ere && ps->p[0] == L'\\' && ps->p[1] == L'(';
    if (open_bre || (ps->ere && c == L'(')) {
        ps->p += open_bre ? 2 : 1;
        int idx = ++ps->ngroups;
        RxNode* g = RxParseAlt(ps, depth + 1);
        if (!g) return NULL;
        bool closed = ps->ere ? (*ps->p == L')') : (ps->p[0] == L'\\' && ps->p[1] == L')');
        if (!closed) {
            ps->err = L"Unmatched ( or \\(";
            RxFreeNode(g);
            return NULL;
        }
        ps->p += ps->ere ? 1 : 2;
        g->group = idx;
        ps->closed_groups = idx;
        return g;
    }
    if (c == L'[') return RxParseBracket(ps);
    if (c == L'.') {
        ps->p++;
        return RxNewNode(RX_ANY);
    }
    if (c == L'\\') return RxParseEscape(ps);
    if (c == L'^') {
        if (ps->ere || seq->count == 0) {
            ps->p++;
            return RxNewNode(RX_BOL);
        }
    }
    if (c == L'$') {
        const wchar_t* after = ps->p + 1;
        bool at_end = !*after || (!ps->ere && after[0] == L'\\' && (after[1] == L')' || after[1] == L'|'));
        if (ps->ere || at_end) {
            ps->p++;
            return RxNewNode(RX_EOL);
        }
    }
    ps->p++;
    RxNode* n = RxNewNode(RX_CHAR);
    if (n) n->ch = c;
    return n;
}

static bool RxParseSeq(RxParser* ps, RxSeq* seq, int depth) {
    while (*ps->p && !RxAtAlt(ps)) {
        if (RxAtClose(ps)) {
            if (depth == 0) {
                ps->err = L"Unmatched ) or \\)";
                return false;
            }
            break;
        }
        RxNode* atom = RxParseAtom(ps, seq, depth);
        if (!atom) return false;
        if (atom->type == RX_BOL) {
            RxSeqPush(seq, atom);
            continue;
        }
        for (;;) {
            int min = 0, max = 0;
            bool error = false;
            if (!RxParsePostfix(ps, &min, &max, &error)) {
                if (error) {
                    RxFreeNode(atom);
                    return false;
                }
                break;
            }
            RxNode* rep = RxNewNode(RX_REPEAT);
            if (!rep) {
                RxFreeNode(atom);
                return false;
            }
            rep->min = min;
            rep->max = max;
            rep->child = atom;
            atom = rep;
        }
        RxSeqPush(seq, atom);
    }
    return true;
}

static RxNode* RxParseAlt(RxParser* ps, int depth) {
    RxNode* g = RxNewNode(RX_GROUP);
    if (!g) return NULL;
    for (;;) {
        RxSeq seq = {0};
        if (!RxParseSeq(ps, &seq, depth)) {
            RxSeqFree(&seq);
            RxFreeNode(g);
            return NULL;
        }
        RxSeq* grown = (RxSeq*)realloc(g->alts, sizeof(RxSeq) * (g->nalts + 1));
        if (!grown) {
            RxSeqFree(&seq);
            RxFreeNode(g);
            return NULL;
        }
        g->alts = grown;
        g->alts[g->nalts++] = seq;
        if (!RxAtAlt(ps)) break;
        ps->p += ps->ere ? 1 : 2;
    }
    return g;
}

bool RegexCompile(const wchar_t* pattern, bool ere, bool icase, RegexProg** out, const wchar_t** err) {
    RxParser ps = {pattern, ere, 0, 0, NULL};
    RxNode* root = RxParseAlt(&ps, 0);
    if (!root) {
        if (err) *err = ps.err ? ps.err : L"Invalid regular expression";
        return false;
    }
    if (*ps.p) {
        RxFreeNode(root);
        if (err) *err = L"Unmatched ) or \\)";
        return false;
    }
    RegexProg* prog = (RegexProg*)calloc(1, sizeof(RegexProg));
    if (!prog) {
        RxFreeNode(root);
        if (err) *err = L"Out of memory";
        return false;
    }
    prog->root = root;
    prog->icase = icase;
    *out = prog;
    return true;
}

void RegexFree(RegexProg* prog) {
    if (!prog) return;
    RxFreeNode(prog->root);
    free(prog);
}

/* ---- Matcher: backtracking with explicit continuations ---- */

typedef struct RxCtx {
    const wchar_t* text;
    size_t len;
    bool icase;
    unsigned flags;
    size_t mstart;
    size_t cs[REGEX_GROUPS];
    size_t ce[REGEX_GROUPS];
    bool found;
    RegexMatch* out;
} RxCtx;

enum { RXK_SEQ, RXK_GROUP, RXK_REPEAT };

typedef struct RxCont {
    int kind;
    const RxSeq* seq;
    int idx;
    const RxNode* node;
    int count;
    size_t start;
    const struct RxCont* up;
} RxCont;

static bool RxRun(RxCtx* c, const RxCont* k, size_t pos);
static bool RxNodeMatch(RxCtx* c, const RxNode* n, size_t pos, const RxCont* k);

static bool RxFinal(RxCtx* c, size_t pos) {
    if ((c->flags & REGEX_LINE) && !(c->mstart == 0 && pos == c->len)) return false;
    if (c->flags & REGEX_WORD) {
        if (c->mstart > 0 && RxIsWordChar(c->text[c->mstart - 1])) return false;
        if (pos < c->len && RxIsWordChar(c->text[pos])) return false;
    }
    bool longest = (c->flags & REGEX_LONGEST) != 0;
    if (longest && c->found && pos <= c->out->e[0]) return false;
    c->found = true;
    c->out->s[0] = c->mstart;
    c->out->e[0] = pos;
    for (int g = 1; g < REGEX_GROUPS; g++) {
        c->out->s[g] = c->cs[g];
        c->out->e[g] = c->ce[g];
    }
    return !longest;
}

static bool RxSeqMatch(RxCtx* c, const RxSeq* seq, int idx, size_t pos, const RxCont* up) {
    if (idx == seq->count) return RxRun(c, up, pos);
    RxCont k = {RXK_SEQ, seq, idx + 1, NULL, 0, 0, up};
    return RxNodeMatch(c, seq->items[idx], pos, &k);
}

static bool RxRepeatStep(RxCtx* c, const RxNode* rep, int count, size_t pos, const RxCont* up) {
    if (rep->max < 0 || count < rep->max) {
        RxCont bk = {RXK_REPEAT, NULL, 0, rep, count + 1, pos, up};
        if (RxNodeMatch(c, rep->child, pos, &bk)) return true;
    }
    if (count >= rep->min) return RxRun(c, up, pos);
    return false;
}

static bool RxRun(RxCtx* c, const RxCont* k, size_t pos) {
    if (!k) return RxFinal(c, pos);
    switch (k->kind) {
        case RXK_SEQ:
            return RxSeqMatch(c, k->seq, k->idx, pos, k->up);
        case RXK_GROUP: {
            int g = k->node->group;
            size_t os = 0, oe = 0;
            if (g >= 0) {
                os = c->cs[g];
                oe = c->ce[g];
                c->cs[g] = k->start;
                c->ce[g] = pos;
            }
            bool r = RxRun(c, k->up, pos);
            if (!r && g >= 0) {
                c->cs[g] = os;
                c->ce[g] = oe;
            }
            return r;
        }
        case RXK_REPEAT:
            if (pos == k->start && k->count > k->node->min) return false;
            return RxRepeatStep(c, k->node, k->count, pos, k->up);
    }
    return false;
}

static bool RxNodeMatch(RxCtx* c, const RxNode* n, size_t pos, const RxCont* k) {
    bool has = pos < c->len;
    wchar_t t = has ? c->text[pos] : 0;
    bool prev_word = pos > 0 && RxIsWordChar(c->text[pos - 1]);
    bool next_word = has && RxIsWordChar(t);
    switch (n->type) {
        case RX_CHAR:
            return has && RxCharEq(t, n->ch, c->icase) && RxRun(c, k, pos + 1);
        case RX_ANY:
            return has && RxRun(c, k, pos + 1);
        case RX_SET:
            return has && RxSetMatch(&n->set, t, c->icase) && RxRun(c, k, pos + 1);
        case RX_WORDCH:
            return has && RxIsWordChar(t) && RxRun(c, k, pos + 1);
        case RX_NWORDCH:
            return has && !RxIsWordChar(t) && RxRun(c, k, pos + 1);
        case RX_SPACE:
            return has && iswspace(t) && RxRun(c, k, pos + 1);
        case RX_NSPACE:
            return has && !iswspace(t) && RxRun(c, k, pos + 1);
        case RX_BOL:
            return pos == 0 && RxRun(c, k, pos);
        case RX_EOL:
            return pos == c->len && RxRun(c, k, pos);
        case RX_WORDB:
            return prev_word != next_word && RxRun(c, k, pos);
        case RX_NWORDB:
            return prev_word == next_word && RxRun(c, k, pos);
        case RX_WSTART:
            return !prev_word && next_word && RxRun(c, k, pos);
        case RX_WEND:
            return prev_word && !next_word && RxRun(c, k, pos);
        case RX_BACKREF: {
            int g = n->group;
            if (c->cs[g] == REGEX_UNSET_POS) return false;
            size_t len = c->ce[g] - c->cs[g];
            if (pos + len > c->len) return false;
            for (size_t i = 0; i < len; i++) {
                if (!RxCharEq(c->text[pos + i], c->text[c->cs[g] + i], c->icase)) return false;
            }
            return RxRun(c, k, pos + len);
        }
        case RX_GROUP: {
            for (int i = 0; i < n->nalts; i++) {
                RxCont gk = {RXK_GROUP, NULL, 0, n, 0, pos, k};
                if (RxSeqMatch(c, &n->alts[i], 0, pos, &gk)) return true;
            }
            return false;
        }
        case RX_REPEAT:
            return RxRepeatStep(c, n, 0, pos, k);
    }
    return false;
}

bool RegexSearch(RegexProg* prog, const wchar_t* text, size_t len, size_t from, unsigned flags, RegexMatch* m) {
    RxCtx c;
    c.text = text;
    c.len = len;
    c.icase = prog->icase;
    c.flags = flags;
    c.out = m;
    for (size_t s = from; s <= len; s++) {
        c.mstart = s;
        c.found = false;
        for (int g = 0; g < REGEX_GROUPS; g++) {
            c.cs[g] = REGEX_UNSET_POS;
            c.ce[g] = REGEX_UNSET_POS;
        }
        RxNodeMatch(&c, prog->root, s, NULL);
        if (c.found) return true;
    }
    return false;
}
