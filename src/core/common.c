#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>


wchar_t* SafeStrDup(const wchar_t* src) {
    if (!src) return NULL;
    size_t len = wcslen(src) + 1;
    wchar_t* dest = (wchar_t*)malloc(len * sizeof(wchar_t));
    if (dest) wcscpy_s(dest, len, src);
    return dest;
}

wchar_t* GetEnvironmentValue(const wchar_t* name) {
    DWORD required = GetEnvironmentVariableW(name, NULL, 0);
    if (!required) return NULL;
    wchar_t* value = (wchar_t*)malloc((size_t)required * sizeof(wchar_t));
    if (!value) return NULL;
    DWORD length = GetEnvironmentVariableW(name, value, required);
    if (!length || length >= required) {
        free(value);
        return NULL;
    }
    return value;
}

wchar_t* GetCurrentDirectoryAlloc(void) {
    DWORD required = GetCurrentDirectoryW(0, NULL);
    if (!required) return NULL;
    wchar_t* path = (wchar_t*)malloc((size_t)required * sizeof(wchar_t));
    if (!path) return NULL;
    DWORD length = GetCurrentDirectoryW(required, path);
    if (!length || length >= required) {
        free(path);
        return NULL;
    }
    return path;
}

bool StrContainsW(const wchar_t* haystack, const wchar_t* needle, bool ignoreCase) {
    if (!haystack || !needle) return false;
    if (!*needle) return true;
    if (ignoreCase) {
        size_t nlen = wcslen(needle);
        for (; *haystack; haystack++) {
            if (_wcsnicmp(haystack, needle, nlen) == 0) return true;
        }
        return false;
    }
    return wcsstr(haystack, needle) != NULL;
}

void FormatBytesHuman(unsigned __int64 bytes, wchar_t* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    const wchar_t* units[] = { L"B", L"K", L"M", L"G", L"T", L"P" };
    double d = (double)bytes;
    int u = 0;
    while (d >= 1024.0 && u < 5) {
        d /= 1024.0;
        u++;
    }
    if (u == 0) {
        swprintf_s(out, out_sz, L"%lluB", (unsigned long long)bytes);
    } else if (d < 10.0) {
        swprintf_s(out, out_sz, L"%.1f%ls", d, units[u]);
    } else {
        swprintf_s(out, out_sz, L"%.0f%ls", d, units[u]);
    }
}

#include <math.h>

typedef struct KshArithParser {
    const wchar_t* cursor;
    IShell* shell;
    bool has_float;
    bool error;
    int depth;
} KshArithParser;

#define KSH_ARITH_MAX_DEPTH 128

static double KshArithExpr(KshArithParser* p);
static double KshArithPrimaryInner(KshArithParser* p);

static void KshArithSkip(KshArithParser* p) {
    while (*p->cursor && iswspace(*p->cursor)) p->cursor++;
}

/* Bounds recursion from nested parentheses and unary operators to avoid stack exhaustion. */
static double KshArithPrimary(KshArithParser* p) {
    double v;
    if (p->depth >= KSH_ARITH_MAX_DEPTH) { p->error = true; return 0.0; }
    p->depth++;
    v = KshArithPrimaryInner(p);
    p->depth--;
    return v;
}

static double KshArithPrimaryInner(KshArithParser* p) {
    KshArithSkip(p);
    if (!*p->cursor) { p->error = true; return 0.0; }

    if (*p->cursor == L'+') {
        p->cursor++;
        return KshArithPrimary(p);
    }
    if (*p->cursor == L'-') {
        p->cursor++;
        return -KshArithPrimary(p);
    }
    if (*p->cursor == L'!') {
        p->cursor++;
        return KshArithPrimary(p) == 0.0 ? 1.0 : 0.0;
    }
    if (*p->cursor == L'~') {
        p->cursor++;
        return (double)(~((long long)KshArithPrimary(p)));
    }
    if (*p->cursor == L'(') {
        p->cursor++;
        double val = KshArithExpr(p);
        KshArithSkip(p);
        if (*p->cursor == L')') p->cursor++;
        else p->error = true;
        return val;
    }

    /* Number literal */
    if (iswdigit(*p->cursor) || *p->cursor == L'.') {
        wchar_t* endp = NULL;
        const wchar_t* start = p->cursor;
        double val = wcstod(start, &endp);
        if (endp == start) { p->error = true; return 0.0; }
        for (const wchar_t* ch = start; ch < endp; ch++) {
            if (*ch == L'.' || *ch == L'e' || *ch == L'E') p->has_float = true;
        }
        p->cursor = endp;
        return val;
    }

    /* Identifier or function */
    if (iswalpha(*p->cursor) || *p->cursor == L'_') {
        const wchar_t* start = p->cursor;
        while (iswalnum(*p->cursor) || *p->cursor == L'_' || *p->cursor == L'.') p->cursor++;
        size_t len = (size_t)(p->cursor - start);
        wchar_t name[128];
        if (len < _countof(name)) {
            wcsncpy_s(name, _countof(name), start, len);
            KshArithSkip(p);
            if (*p->cursor == L'(') {
                p->cursor++;
                double arg = KshArithExpr(p);
                double arg2 = 0.0;
                KshArithSkip(p);
                if (*p->cursor == L',') {
                    p->cursor++;
                    arg2 = KshArithExpr(p);
                    KshArithSkip(p);
                }
                if (*p->cursor == L')') p->cursor++;
                p->has_float = true;
                if (_wcsicmp(name, L"sqrt") == 0) return sqrt(arg);
                if (_wcsicmp(name, L"sin") == 0) return sin(arg);
                if (_wcsicmp(name, L"cos") == 0) return cos(arg);
                if (_wcsicmp(name, L"tan") == 0) return tan(arg);
                if (_wcsicmp(name, L"asin") == 0) return asin(arg);
                if (_wcsicmp(name, L"acos") == 0) return acos(arg);
                if (_wcsicmp(name, L"atan") == 0) return atan(arg);
                if (_wcsicmp(name, L"atan2") == 0) return atan2(arg, arg2);
                if (_wcsicmp(name, L"pow") == 0) return pow(arg, arg2);
                if (_wcsicmp(name, L"hypot") == 0) return hypot(arg, arg2);
                if (_wcsicmp(name, L"fmod") == 0) return fmod(arg, arg2);
                if (_wcsicmp(name, L"exp") == 0) return exp(arg);
                if (_wcsicmp(name, L"log") == 0) return log(arg);
                if (_wcsicmp(name, L"log10") == 0) return log10(arg);
                if (_wcsicmp(name, L"log2") == 0) return log2(arg);
                if (_wcsicmp(name, L"floor") == 0) return floor(arg);
                if (_wcsicmp(name, L"ceil") == 0) return ceil(arg);
                if (_wcsicmp(name, L"round") == 0) return round(arg);
                if (_wcsicmp(name, L"trunc") == 0) return trunc(arg);
                if (_wcsicmp(name, L"cbrt") == 0) return cbrt(arg);
                if (_wcsicmp(name, L"sinh") == 0) return sinh(arg);
                if (_wcsicmp(name, L"cosh") == 0) return cosh(arg);
                if (_wcsicmp(name, L"tanh") == 0) return tanh(arg);
                if (_wcsicmp(name, L"abs") == 0 || _wcsicmp(name, L"fabs") == 0) return fabs(arg);
                if (_wcsicmp(name, L"int") == 0) { p->has_float = false; return (double)((long long)arg); }
                p->error = true;
                return 0.0;
            }
            if (p->shell && p->shell->env) {
                const wchar_t* valStr = p->shell->env->Get(p->shell->env, name);
                if (valStr && *valStr) {
                    wchar_t* endp = NULL;
                    double v = wcstod(valStr, &endp);
                    if (wcschr(valStr, L'.') || wcschr(valStr, L'e') || wcschr(valStr, L'E')) p->has_float = true;
                    return v;
                }
            }
            return 0.0;
        }
    }

    p->error = true;
    return 0.0;
}

/* Parses one operand and reports whether it is floating-point typed (ksh93 keeps integer
   and float arithmetic distinct: 7/2 is 3, 7.0/2 is 3.5). The global has_float flag is
   preserved as the OR of everything parsed so far. */
static double KshArithPowerTyped(KshArithParser* p, bool* isFloat);

static double KshArithPowerTyped(KshArithParser* p, bool* isFloat) {
    bool outer = p->has_float;
    p->has_float = false;
    double left = KshArithPrimary(p);
    bool leftFloat = p->has_float;
    KshArithSkip(p);
    if (*p->cursor == L'*' && p->cursor[1] == L'*') {
        double right;
        bool rightFloat;
        p->cursor += 2;
        if (p->depth >= KSH_ARITH_MAX_DEPTH) { p->error = true; return 0.0; }
        p->depth++;
        right = KshArithPowerTyped(p, &rightFloat);
        p->depth--;
        leftFloat = leftFloat || rightFloat || right < 0.0;
        left = pow(left, right);
        if (!leftFloat) left = (double)(long long)left;
    }
    *isFloat = leftFloat;
    p->has_float = outer || leftFloat;
    return left;
}

static double KshArithMul(KshArithParser* p) {
    bool leftFloat;
    double left = KshArithPowerTyped(p, &leftFloat);
    for (;;) {
        KshArithSkip(p);
        wchar_t op = *p->cursor;
        if (op != L'*' && op != L'/' && op != L'%') break;
        if (op == L'*' && p->cursor[1] == L'*') break;
        p->cursor++;
        bool rightFloat;
        double right = KshArithPowerTyped(p, &rightFloat);
        if (op == L'*') {
            left *= right;
            leftFloat = leftFloat || rightFloat;
        } else if (op == L'/') {
            if (right == 0.0) { p->error = true; return 0.0; }
            if (leftFloat || rightFloat) {
                left = left / right;
                leftFloat = true;
            } else if (right == -1.0) {
                /* LLONG_MIN / -1 traps on x86. */
                left = -left;
            } else {
                left = (double)((long long)left / (long long)right);
            }
        } else {
            if (leftFloat || rightFloat) {
                if (right == 0.0) { p->error = true; return 0.0; }
                left = fmod(left, right);
                leftFloat = true;
            } else {
                if ((long long)right == 0) { p->error = true; return 0.0; }
                if ((long long)right == -1) left = 0.0;
                else left = (double)((long long)left % (long long)right);
            }
        }
    }
    return left;
}

static double KshArithAdd(KshArithParser* p) {
    double left = KshArithMul(p);
    for (;;) {
        KshArithSkip(p);
        wchar_t op = *p->cursor;
        if (op != L'+' && op != L'-') break;
        p->cursor++;
        double right = KshArithMul(p);
        if (op == L'+') left += right;
        else left -= right;
    }
    return left;
}

static double KshArithRelational(KshArithParser* p) {
    double left = KshArithAdd(p);
    for (;;) {
        KshArithSkip(p);
        if (p->cursor[0] == L'<' && p->cursor[1] == L'=') {
            p->cursor += 2;
            left = (left <= KshArithAdd(p)) ? 1.0 : 0.0;
        } else if (p->cursor[0] == L'>' && p->cursor[1] == L'=') {
            p->cursor += 2;
            left = (left >= KshArithAdd(p)) ? 1.0 : 0.0;
        } else if (p->cursor[0] == L'<' && p->cursor[1] != L'<') {
            p->cursor++;
            left = (left < KshArithAdd(p)) ? 1.0 : 0.0;
        } else if (p->cursor[0] == L'>' && p->cursor[1] != L'>') {
            p->cursor++;
            left = (left > KshArithAdd(p)) ? 1.0 : 0.0;
        } else break;
    }
    return left;
}

static double KshArithEquality(KshArithParser* p) {
    double left = KshArithRelational(p);
    for (;;) {
        KshArithSkip(p);
        if (p->cursor[0] == L'=' && p->cursor[1] == L'=') {
            p->cursor += 2;
            left = (left == KshArithRelational(p)) ? 1.0 : 0.0;
        } else if (p->cursor[0] == L'!' && p->cursor[1] == L'=') {
            p->cursor += 2;
            left = (left != KshArithRelational(p)) ? 1.0 : 0.0;
        } else break;
    }
    return left;
}

static double KshArithLogicAnd(KshArithParser* p) {
    double left = KshArithEquality(p);
    for (;;) {
        KshArithSkip(p);
        if (p->cursor[0] == L'&' && p->cursor[1] == L'&') {
            p->cursor += 2;
            double right = KshArithEquality(p);
            left = (left != 0.0 && right != 0.0) ? 1.0 : 0.0;
        } else break;
    }
    return left;
}

static double KshArithLogicOr(KshArithParser* p) {
    double left = KshArithLogicAnd(p);
    for (;;) {
        KshArithSkip(p);
        if (p->cursor[0] == L'|' && p->cursor[1] == L'|') {
            p->cursor += 2;
            double right = KshArithLogicAnd(p);
            left = (left != 0.0 || right != 0.0) ? 1.0 : 0.0;
        } else break;
    }
    return left;
}

static double KshArithExpr(KshArithParser* p) {
    double cond = KshArithLogicOr(p);
    KshArithSkip(p);
    if (*p->cursor == L'?') {
        double trueVal, falseVal;
        p->cursor++;
        if (p->depth >= KSH_ARITH_MAX_DEPTH) { p->error = true; return 0.0; }
        p->depth++;
        trueVal = KshArithExpr(p);
        KshArithSkip(p);
        if (*p->cursor == L':') p->cursor++;
        else p->error = true;
        falseVal = KshArithExpr(p);
        p->depth--;
        return (cond != 0.0) ? trueVal : falseVal;
    }
    return cond;
}

bool Ksh_EvaluateArithmetic(IShell* shell, const wchar_t* expr, long long* out_int, double* out_float, bool* is_float) {
    if (!expr) return false;
    /* The float evaluator has no side effects: ++x would parse as +(+x). Route increments and
       assignments (=, +=, ... but not ==, !=, <=, >=) to the integer evaluator that performs them. */
    bool sideEffects = wcsstr(expr, L"++") != NULL || wcsstr(expr, L"--") != NULL;
    for (const wchar_t* q = expr; *q && !sideEffects; q++) {
        if (*q == L'=' && q[1] != L'=' && (q == expr || !wcschr(L"=!<>", q[-1]) ||
                (q - expr >= 2 && (q[-1] == L'<' || q[-1] == L'>') && q[-2] == q[-1])))
            sideEffects = true;
    }
    if (sideEffects) {
        long long bval = 0;
        if (!Batch_EvaluateArithmetic(shell, NULL, expr, &bval)) return false;
        *out_int = bval;
        *out_float = (double)bval;
        *is_float = false;
        return true;
    }
    KshArithParser parser = { expr, shell, false, false, 0 };
    double val = KshArithExpr(&parser);
    KshArithSkip(&parser);
    if (parser.error || *parser.cursor) {
        /* Fallback to integer batch arithmetic evaluator */
        long long bval = 0;
        if (Batch_EvaluateArithmetic(shell, NULL, expr, &bval)) {
            *out_int = bval;
            *out_float = (double)bval;
            *is_float = false;
            return true;
        }
        return false;
    }
    *is_float = parser.has_float;
    *out_float = val;
    *out_int = (long long)val;
    return true;
}

bool Ksh_AppendShellWord(wchar_t* buf, size_t cap, const wchar_t* word) {
    size_t used = wcslen(buf);
    bool plain = *word != L'\0';
    for (const wchar_t* p = word; *p && plain; p++) {
        if (!(iswalnum(*p) || wcschr(L"_-./:=+,@%", *p))) plain = false;
    }
    if (used && used + 1 < cap) buf[used++] = L' ';
    else if (used) return false;
    if (plain) {
        size_t wl = wcslen(word);
        if (used + wl >= cap) return false;
        memcpy(buf + used, word, (wl + 1) * sizeof(wchar_t));
        return true;
    }
    /* Single quotes suppress all expansion; embedded ' becomes '\'' */
    if (used + 1 >= cap) return false;
    buf[used++] = L'\'';
    for (const wchar_t* p = word; *p; p++) {
        if (*p == L'\'') {
            if (used + 4 >= cap) return false;
            memcpy(buf + used, L"'\\''", 4 * sizeof(wchar_t));
            used += 4;
        } else {
            if (used + 1 >= cap) return false;
            buf[used++] = *p;
        }
    }
    if (used + 2 > cap) return false;
    buf[used++] = L'\'';
    buf[used] = L'\0';
    return true;
}

bool Ksh_CreateTempFile(const wchar_t* prefix, wchar_t* out, size_t cap) {
    static volatile LONG counter;
    wchar_t dir[MAX_PATH];
    DWORD dl = GetTempPathW(MAX_PATH, dir);
    if (!dl || dl >= MAX_PATH) return false;
    for (int attempt = 0; attempt < 100; attempt++) {
        LONG n = InterlockedIncrement(&counter);
        if (swprintf_s(out, cap, L"%ls%ls%lu_%ld.tmp", dir, prefix, (unsigned long)GetCurrentProcessId(), (long)n) < 0)
            return false;
        HANDLE h = CreateFileW(out, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
        if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); return true; }
        if (GetLastError() != ERROR_FILE_EXISTS) return false;
    }
    return false;
}

/* Deletes heredoc temp files whose owning shell process no longer exists. */
void Ksh_SweepStaleTempFiles(void) {
    static const wchar_t* prefixes[] = { L"khd", L"khx" };
    wchar_t dir[MAX_PATH], pattern[MAX_PATH], path[MAX_PATH];
    DWORD dl = GetTempPathW(MAX_PATH, dir);
    DWORD self = GetCurrentProcessId();
    if (!dl || dl >= MAX_PATH) return;
    for (size_t k = 0; k < _countof(prefixes); k++) {
        WIN32_FIND_DATAW fd;
        if (swprintf_s(pattern, MAX_PATH, L"%ls%ls*_*.tmp", dir, prefixes[k]) < 0) continue;
        HANDLE hf = FindFirstFileW(pattern, &fd);
        if (hf == INVALID_HANDLE_VALUE) continue;
        do {
            const wchar_t* p = fd.cFileName + 3;
            wchar_t* end = NULL;
            if (!iswdigit(*p)) continue;
            unsigned long pid = wcstoul(p, &end, 10);
            if (!end || *end != L'_' || pid == self) continue;
            HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
            if (proc) {
                DWORD exitCode = 0;
                bool alive = GetExitCodeProcess(proc, &exitCode) && exitCode == STILL_ACTIVE;
                CloseHandle(proc);
                if (alive) continue;
            } else if (GetLastError() != ERROR_INVALID_PARAMETER) {
                continue; /* exists but inaccessible: assume alive */
            }
            if (swprintf_s(path, MAX_PATH, L"%ls%ls", dir, fd.cFileName) >= 0) DeleteFileW(path);
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }
}

