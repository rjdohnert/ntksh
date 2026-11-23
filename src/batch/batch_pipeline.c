#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

bool Batch_IsNumber(const wchar_t* text, long long* value) {
    wchar_t* end = NULL;
    errno = 0;
    long long parsed = wcstoll(text, &end, 10);
    if (errno || end == text || *end) return false;
    *value = parsed;
    return true;
}

static int Batch_ExecuteRecursiveFor(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, BatchLoopVariable* variable,
    const wchar_t* root, const wchar_t* pattern, const wchar_t* body,
    bool directories, unsigned int depth) {
    if (depth > 64) return context->errorLevel;
    int status = context->errorLevel;
    wchar_t path[MAX_PATH * 2];
    if (!wcspbrk(pattern, L"*?")) {
        /* Without wildcards CMD yields root\item for every directory. */
        if (swprintf_s(path, _countof(path), L"%ls\\%ls", root, pattern) < 0)
            return 2;
        Batch_SetLoopValue(context, variable, path);
        status = Batch_ExecuteStatements(shell, context, lines, body);
    } else {
        if (swprintf_s(path, _countof(path), L"%ls\\%ls", root, pattern) < 0)
            return 2;
        WIN32_FIND_DATAW data;
        HANDLE search = FindFirstFileW(path, &data);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                bool isDirectory =
                    (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (wcscmp(data.cFileName, L".") == 0 ||
                    wcscmp(data.cFileName, L"..") == 0 ||
                    isDirectory != directories ||
                    (data.dwFileAttributes &
                        (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                    continue;
                wchar_t fullPath[MAX_PATH * 2];
                if (swprintf_s(fullPath, _countof(fullPath), L"%ls\\%ls",
                        root, data.cFileName) < 0)
                    continue;
                Batch_SetLoopValue(context, variable, fullPath);
                status = Batch_ExecuteStatements(shell, context, lines, body);
                if (Batch_FlowStopped(shell, context)) break;
            } while (FindNextFileW(search, &data));
            FindClose(search);
        }
    }
    if (Batch_FlowStopped(shell, context)) return status;
    if (swprintf_s(path, _countof(path), L"%ls\\*", root) < 0) return 2;
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(path, &data);
    if (search == INVALID_HANDLE_VALUE) return status;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            wcscmp(data.cFileName, L".") == 0 ||
            wcscmp(data.cFileName, L"..") == 0)
            continue;
        wchar_t child[MAX_PATH * 2];
        if (swprintf_s(child, _countof(child), L"%ls\\%ls", root,
                data.cFileName) < 0)
            continue;
        status = Batch_ExecuteRecursiveFor(shell, context, lines, variable,
            child, pattern, body, directories, depth + 1);
        if (Batch_FlowStopped(shell, context)) break;
    } while (FindNextFileW(search, &data));
    FindClose(search);
    return status;
}

typedef struct BatchArithmeticParser {
    const wchar_t* cursor;
    IShell* shell;
    BatchContext* context;
    bool invalid;
    int depth;
} BatchArithmeticParser;

static void Batch_ArithmeticSpaces(BatchArithmeticParser* parser) {
    while (iswspace(*parser->cursor)) parser->cursor++;
}

static long long Batch_ArithmeticLogicalOr(BatchArithmeticParser* parser);

static long long Batch_ArithmeticPrimary(BatchArithmeticParser* parser) {
    Batch_ArithmeticSpaces(parser);
    if (*parser->cursor == L'(') {
        parser->cursor++;
        long long value = Batch_ArithmeticLogicalOr(parser);
        Batch_ArithmeticSpaces(parser);
        if (*parser->cursor != L')') parser->invalid = true;
        else parser->cursor++;
        return value;
    }
    if (iswalpha(*parser->cursor) || *parser->cursor == L'_') {
        wchar_t name[256];
        size_t length = 0;
        while (iswalnum(*parser->cursor) || *parser->cursor == L'_') {
            if (length + 1 >= _countof(name)) {
                parser->invalid = true;
                return 0;
            }
            name[length++] = *parser->cursor++;
        }
        name[length] = L'\0';
        wchar_t scratch[256];
        const wchar_t* text = Batch_GetVariable(parser->shell,
            parser->context, name, scratch, _countof(scratch));
        wchar_t* inherited = text ? NULL : GetEnvironmentValue(name);
        if (!text) text = inherited;
        long long value = 0;
        if (text && !Batch_IsNumber(text, &value)) value = 0;
        free(inherited);

        Batch_ArithmeticSpaces(parser);
        if (*parser->cursor == L'(') {
            parser->cursor++;
            long long arg = Batch_ArithmeticLogicalOr(parser);
            long long arg2 = 0;
            Batch_ArithmeticSpaces(parser);
            if (*parser->cursor == L',') {
                parser->cursor++;
                arg2 = Batch_ArithmeticLogicalOr(parser);
                Batch_ArithmeticSpaces(parser);
            }
            if (*parser->cursor == L')') parser->cursor++;
            if (_wcsicmp(name, L"abs") == 0) return llabs(arg);
            if (_wcsicmp(name, L"sqrt") == 0) return (long long)sqrt((double)arg);
            if (_wcsicmp(name, L"pow") == 0) return (long long)pow((double)arg, (double)arg2);
            if (_wcsicmp(name, L"sin") == 0) return (long long)sin((double)arg);
            if (_wcsicmp(name, L"cos") == 0) return (long long)cos((double)arg);
            if (_wcsicmp(name, L"floor") == 0) return (long long)floor((double)arg);
            if (_wcsicmp(name, L"ceil") == 0) return (long long)ceil((double)arg);
            if (_wcsicmp(name, L"round") == 0) return (long long)round((double)arg);
            if (_wcsicmp(name, L"int") == 0) return arg;
            parser->invalid = true;
            return 0;
        }
        if (parser->cursor[0] == L'+' && parser->cursor[1] == L'+') {
            parser->cursor += 2;
            long long newVal = value + 1;
            wchar_t formatted[64];
            _snwprintf_s(formatted, _countof(formatted), _TRUNCATE, L"%lld", newVal);
            Batch_SetVariable(parser->shell, parser->context, name, formatted);
            return value;
        }
        if (parser->cursor[0] == L'-' && parser->cursor[1] == L'-') {
            parser->cursor += 2;
            long long newVal = value - 1;
            wchar_t formatted[64];
            _snwprintf_s(formatted, _countof(formatted), _TRUNCATE, L"%lld", newVal);
            Batch_SetVariable(parser->shell, parser->context, name, formatted);
            return value;
        }
        return value;
    }
    wchar_t* end = NULL;
    errno = 0;
    int base = parser->cursor[0] == L'0' &&
        (parser->cursor[1] == L'x' || parser->cursor[1] == L'X') ? 0 : 10;
    long long value = wcstoll(parser->cursor, &end, base);
    if (end == parser->cursor || errno) {
        parser->invalid = true;
        return 0;
    }
    parser->cursor = end;
    return value;
}

static long long Batch_ArithmeticUnaryInner(BatchArithmeticParser* parser);

/* Bounds recursion through parentheses and unary operators to avoid stack exhaustion. */
static long long Batch_ArithmeticUnary(BatchArithmeticParser* parser) {
    long long v;
    if (parser->depth >= 128) { parser->invalid = true; return 0; }
    parser->depth++;
    v = Batch_ArithmeticUnaryInner(parser);
    parser->depth--;
    return v;
}

static long long Batch_ArithmeticUnaryInner(BatchArithmeticParser* parser) {
    Batch_ArithmeticSpaces(parser);
    if (parser->cursor[0] == L'+' && parser->cursor[1] == L'+') {
        parser->cursor += 2;
        Batch_ArithmeticSpaces(parser);
        wchar_t name[256];
        size_t length = 0;
        while (iswalnum(*parser->cursor) || *parser->cursor == L'_') {
            if (length + 1 >= _countof(name)) { parser->invalid = true; return 0; }
            name[length++] = *parser->cursor++;
        }
        name[length] = L'\0';
        if (!*name) { parser->invalid = true; return 0; }
        wchar_t scratch[256];
        const wchar_t* text = Batch_GetVariable(parser->shell, parser->context, name, scratch, _countof(scratch));
        wchar_t* inherited = text ? NULL : GetEnvironmentValue(name);
        if (!text) text = inherited;
        long long val = 0;
        if (text && !Batch_IsNumber(text, &val)) val = 0;
        free(inherited);
        val++;
        wchar_t formatted[64];
        _snwprintf_s(formatted, _countof(formatted), _TRUNCATE, L"%lld", val);
        Batch_SetVariable(parser->shell, parser->context, name, formatted);
        return val;
    }
    if (parser->cursor[0] == L'-' && parser->cursor[1] == L'-') {
        parser->cursor += 2;
        Batch_ArithmeticSpaces(parser);
        wchar_t name[256];
        size_t length = 0;
        while (iswalnum(*parser->cursor) || *parser->cursor == L'_') {
            if (length + 1 >= _countof(name)) { parser->invalid = true; return 0; }
            name[length++] = *parser->cursor++;
        }
        name[length] = L'\0';
        if (!*name) { parser->invalid = true; return 0; }
        wchar_t scratch[256];
        const wchar_t* text = Batch_GetVariable(parser->shell, parser->context, name, scratch, _countof(scratch));
        wchar_t* inherited = text ? NULL : GetEnvironmentValue(name);
        if (!text) text = inherited;
        long long val = 0;
        if (text && !Batch_IsNumber(text, &val)) val = 0;
        free(inherited);
        val--;
        wchar_t formatted[64];
        _snwprintf_s(formatted, _countof(formatted), _TRUNCATE, L"%lld", val);
        Batch_SetVariable(parser->shell, parser->context, name, formatted);
        return val;
    }
    wchar_t operation = *parser->cursor;
    if (operation == L'+' || operation == L'-' ||
        operation == L'!' || operation == L'~') {
        parser->cursor++;
        unsigned long long value = (unsigned long long)
            Batch_ArithmeticUnary(parser);
        if (operation == L'-') return (long long)(0ULL - value);
        if (operation == L'!') return value == 0;
        if (operation == L'~') return (long long)~value;
        return (long long)value;
    }
    return Batch_ArithmeticPrimary(parser);
}

static long long Batch_ArithmeticMultiply(BatchArithmeticParser* parser) {
    long long value = Batch_ArithmeticUnary(parser);
    for (;;) {
        Batch_ArithmeticSpaces(parser);
        wchar_t operation = *parser->cursor;
        if (operation != L'*' && operation != L'/' && operation != L'%')
            return value;
        parser->cursor++;
        long long right = Batch_ArithmeticUnary(parser);
        if (operation == L'*') {
            value = (long long)((unsigned long long)value *
                (unsigned long long)right);
        } else if (right == 0 ||
            (value == LLONG_MIN && right == -1)) {
            parser->invalid = true;
            value = 0;
        } else if (operation == L'/') {
            value /= right;
        } else {
            value %= right;
        }
    }
}

static long long Batch_ArithmeticAdd(BatchArithmeticParser* parser) {
    long long value = Batch_ArithmeticMultiply(parser);
    for (;;) {
        Batch_ArithmeticSpaces(parser);
        wchar_t operation = *parser->cursor;
        if (operation != L'+' && operation != L'-') return value;
        parser->cursor++;
        long long right = Batch_ArithmeticMultiply(parser);
        if (operation == L'+')
            value = (long long)((unsigned long long)value +
                (unsigned long long)right);
        else
            value = (long long)((unsigned long long)value -
                (unsigned long long)right);
    }
}

static long long Batch_ArithmeticShift(BatchArithmeticParser* parser) {
    long long value = Batch_ArithmeticAdd(parser);
    for (;;) {
        Batch_ArithmeticSpaces(parser);
        bool left = parser->cursor[0] == L'<' &&
            parser->cursor[1] == L'<';
        bool right = parser->cursor[0] == L'>' &&
            parser->cursor[1] == L'>';
        if (!left && !right) return value;
        parser->cursor += 2;
        long long count = Batch_ArithmeticAdd(parser);
        if (count < 0 || count >= 64) parser->invalid = true;
        else if (left)
            value = (long long)((unsigned long long)value << count);
        else
            value >>= count;
    }
}

static long long Batch_ArithmeticRelational(BatchArithmeticParser* parser) {
    long long value = Batch_ArithmeticShift(parser);
    for (;;) {
        Batch_ArithmeticSpaces(parser);
        if (parser->cursor[0] == L'<' && parser->cursor[1] == L'=') {
            parser->cursor += 2;
            value = (value <= Batch_ArithmeticShift(parser)) ? 1 : 0;
        } else if (parser->cursor[0] == L'>' && parser->cursor[1] == L'=') {
            parser->cursor += 2;
            value = (value >= Batch_ArithmeticShift(parser)) ? 1 : 0;
        } else if (parser->cursor[0] == L'<' && parser->cursor[1] != L'<') {
            parser->cursor++;
            value = (value < Batch_ArithmeticShift(parser)) ? 1 : 0;
        } else if (parser->cursor[0] == L'>' && parser->cursor[1] != L'>') {
            parser->cursor++;
            value = (value > Batch_ArithmeticShift(parser)) ? 1 : 0;
        } else {
            return value;
        }
    }
}

static long long Batch_ArithmeticEquality(BatchArithmeticParser* parser) {
    long long value = Batch_ArithmeticRelational(parser);
    for (;;) {
        Batch_ArithmeticSpaces(parser);
        if (parser->cursor[0] == L'=' && parser->cursor[1] == L'=') {
            parser->cursor += 2;
            value = (value == Batch_ArithmeticRelational(parser)) ? 1 : 0;
        } else if (parser->cursor[0] == L'!' && parser->cursor[1] == L'=') {
            parser->cursor += 2;
            value = (value != Batch_ArithmeticRelational(parser)) ? 1 : 0;
        } else {
            return value;
        }
    }
}

static long long Batch_ArithmeticAnd(BatchArithmeticParser* parser) {
    long long value = Batch_ArithmeticEquality(parser);
    for (;;) {
        Batch_ArithmeticSpaces(parser);
        if (*parser->cursor != L'&' || parser->cursor[1] == L'&')
            return value;
        parser->cursor++;
        value &= Batch_ArithmeticEquality(parser);
    }
}

static long long Batch_ArithmeticXor(BatchArithmeticParser* parser) {
    long long value = Batch_ArithmeticAnd(parser);
    for (;;) {
        Batch_ArithmeticSpaces(parser);
        if (*parser->cursor != L'^') return value;
        parser->cursor++;
        value ^= Batch_ArithmeticAnd(parser);
    }
}

static long long Batch_ArithmeticOr(BatchArithmeticParser* parser) {
    long long value = Batch_ArithmeticXor(parser);
    for (;;) {
        Batch_ArithmeticSpaces(parser);
        if (*parser->cursor != L'|' || parser->cursor[1] == L'|')
            return value;
        parser->cursor++;
        value |= Batch_ArithmeticXor(parser);
    }
}

static long long Batch_ArithmeticLogicalAnd(BatchArithmeticParser* parser) {
    long long value = Batch_ArithmeticOr(parser);
    for (;;) {
        Batch_ArithmeticSpaces(parser);
        if (parser->cursor[0] != L'&' || parser->cursor[1] != L'&')
            return value;
        parser->cursor += 2;
        long long right = Batch_ArithmeticOr(parser);
        value = (value && right) ? 1 : 0;
    }
}

static long long Batch_ArithmeticLogicalOr(BatchArithmeticParser* parser) {
    long long value = Batch_ArithmeticLogicalAnd(parser);
    for (;;) {
        Batch_ArithmeticSpaces(parser);
        if (parser->cursor[0] != L'|' || parser->cursor[1] != L'|')
            return value;
        parser->cursor += 2;
        long long right = Batch_ArithmeticLogicalAnd(parser);
        value = (value || right) ? 1 : 0;
    }
}

bool Batch_EvaluateArithmetic(IShell* shell, BatchContext* context,
    const wchar_t* expression, long long* result) {
    wchar_t buffer[MAX_LINE_LEN];
    if (wcslen(expression) >= _countof(buffer)) return false;
    wcscpy_s(buffer, _countof(buffer), expression);
    wchar_t* cursor = buffer;
    long long value = 0;
    do {
        wchar_t* comma = NULL;
        int parentheses = 0;
        for (wchar_t* p = cursor; *p; ++p) {
            if (*p == L'(') parentheses++;
            else if (*p == L')') parentheses--;
            else if (*p == L',' && parentheses == 0) {
                comma = p;
                break;
            }
        }
        if (comma) *comma = L'\0';
        wchar_t* part = (wchar_t*)Batch_SkipSpaces(cursor);
        wchar_t* equal = NULL;
        parentheses = 0;
        for (wchar_t* p = part; *p; ++p) {
            if (*p == L'(') parentheses++;
            else if (*p == L')') parentheses--;
            else if (*p == L'=' && parentheses == 0 &&
                p[1] != L'=' && (p == part || p[-1] != L'=')) {
                /* <=, >= and != are comparisons; <<= and >>= are shift assignments. */
                if (p > part && (p[-1] == L'!' ||
                        ((p[-1] == L'<' || p[-1] == L'>') && !(p - part >= 2 && p[-2] == p[-1]))))
                    continue;
                equal = p;
                break;
            }
        }
        wchar_t* name = NULL;
        wchar_t compound = L'\0';
        if (equal) {
            if (equal - part >= 2 && (equal[-1] == L'<' || equal[-1] == L'>') && equal[-2] == equal[-1]) {
                compound = equal[-1];
                equal[-2] = L'\0';
            } else if (equal > part && wcschr(L"+-*/%&|^", equal[-1])) {
                compound = equal[-1];
                equal[-1] = L'\0';
            } else {
                *equal = L'\0';
            }
            name = part;
            while (*name && iswspace(*name)) name++;
            wchar_t* nameEnd = name + wcslen(name);
            while (nameEnd > name && iswspace(nameEnd[-1])) *--nameEnd = L'\0';
            if (!*name) return false;
            wchar_t* valueText = equal + 1;
            BatchArithmeticParser parser = {
                valueText, shell, context, false, 0
            };
            value = Batch_ArithmeticLogicalOr(&parser);
            Batch_ArithmeticSpaces(&parser);
            if (parser.invalid || *parser.cursor) return false;
            if (compound) {
                wchar_t scratch[256];
                const wchar_t* oldText = Batch_GetVariable(shell, context,
                    name, scratch, _countof(scratch));
                wchar_t* inherited = oldText ? NULL :
                    GetEnvironmentValue(name);
                if (!oldText) oldText = inherited;
                long long oldValue = 0;
                if (oldText) (void)Batch_IsNumber(oldText, &oldValue);
                free(inherited);
                if (compound == L'+')
                    value = (long long)((unsigned long long)oldValue +
                        (unsigned long long)value);
                else if (compound == L'-')
                    value = (long long)((unsigned long long)oldValue -
                        (unsigned long long)value);
                else if (compound == L'*')
                    value = (long long)((unsigned long long)oldValue *
                        (unsigned long long)value);
                else if (compound == L'/' || compound == L'%') {
                    if (!value) return false;
                    /* LLONG_MIN / -1 traps on x86. */
                    if (value == -1) value = compound == L'/' ? (long long)(0ULL - (unsigned long long)oldValue) : 0;
                    else value = compound == L'/' ? oldValue / value :
                        oldValue % value;
                } else if (compound == L'&') value = oldValue & value;
                else if (compound == L'|') value = oldValue | value;
                else if (compound == L'^') value = oldValue ^ value;
                else if (compound == L'<') value = (value < 0 || value > 63) ? 0 : (long long)((unsigned long long)oldValue << value);
                else if (compound == L'>') value = (value < 0 || value > 63) ? (oldValue < 0 ? -1 : 0) : oldValue >> value;
            }
            wchar_t formatted[64];
            _snwprintf_s(formatted, _countof(formatted), _TRUNCATE,
                L"%lld", value);
            if (!Batch_SetVariable(shell, context, name, formatted))
                return false;
        } else {
            BatchArithmeticParser parser = { part, shell, context, false, 0 };
            value = Batch_ArithmeticLogicalOr(&parser);
            Batch_ArithmeticSpaces(&parser);
            if (parser.invalid || *parser.cursor) return false;
        }
        if (!comma) break;
        cursor = comma + 1;
    } while (true);
    *result = value;
    return true;
}

int Batch_ExecuteSet(IShell* shell, BatchContext* context,
    const wchar_t* rest) {
    rest = Batch_SkipSpaces(rest);
    if (!*rest) {
        LPWCH environment = GetEnvironmentStringsW();
        if (!environment) return 1;
        for (const wchar_t* item = environment; *item;
            item += wcslen(item) + 1) {
            if (*item != L'=') wprintf(L"%ls\n", item);
        }
        FreeEnvironmentStringsW(environment);
        return 0;
    }
    bool prompt = false;
    if (_wcsnicmp(rest, L"/p", 2) == 0 &&
        (!rest[2] || iswspace(rest[2]))) {
        prompt = true;
        rest = Batch_SkipSpaces(rest + 2);
    } else if (_wcsnicmp(rest, L"/a", 2) == 0 &&
        (!rest[2] || iswspace(rest[2]))) {
        rest = Batch_SkipSpaces(rest + 2);
        wchar_t expression[MAX_LINE_LEN];
        size_t length = 0;
        for (; *rest; ++rest) {
            if (*rest == L'"') continue;
            if (length + 1 >= _countof(expression)) return 2;
            expression[length++] = *rest;
        }
        expression[length] = L'\0';
        long long result;
        if (!Batch_EvaluateArithmetic(shell, context, expression, &result)) {
            fwprintf(stderr, L"ksh: batch set /a: invalid arithmetic expression\n");
            return 2;
        }
        return 0;
    }
    wchar_t assignment[MAX_LINE_LEN];
    rest = Batch_SkipSpaces(rest);
    if (!*rest) return 0;
    if (wcslen(rest) >= _countof(assignment)) return 2;
    wcscpy_s(assignment, _countof(assignment), rest);
    if (assignment[0] == L'"') {
        /* set "NAME=value" ignores everything after the last quote. */
        wchar_t* lastQuote = wcsrchr(assignment, L'"');
        if (lastQuote > assignment) *lastQuote = L'\0';
        memmove(assignment, assignment + 1,
            (wcslen(assignment + 1) + 1) * sizeof(wchar_t));
    }
    wchar_t* equal = wcschr(assignment, L'=');
    if (!equal) {
        const wchar_t* prefix = assignment;
        size_t prefixLength = wcslen(prefix);
        while (prefixLength && iswspace(prefix[prefixLength - 1]))
            prefixLength--;
        LPWCH environment = GetEnvironmentStringsW();
        if (!environment) return 1;
        bool found = false;
        for (const wchar_t* item = environment; *item;
            item += wcslen(item) + 1) {
            const wchar_t* itemEqual = wcschr(item + 1, L'=');
            if (*item != L'=' && itemEqual &&
                (size_t)(itemEqual - item) >= prefixLength &&
                _wcsnicmp(item, prefix, prefixLength) == 0) {
                wprintf(L"%ls\n", item);
                found = true;
            }
        }
        FreeEnvironmentStringsW(environment);
        if (!found) {
            fwprintf(stderr, L"Environment variable %.*ls not defined\n",
                (int)prefixLength, prefix);
            return 1;
        }
        return 0;
    }
    if (equal == assignment) {
        if (prompt) {
            /* In CMD, "set /p =PromptText" (empty variable name) emits PromptText without a newline. */
            wchar_t* value = equal + 1;
            fputws(value, stdout);
            fflush(stdout);
            wchar_t input[MAX_LINE_LEN];
            (void)fgetws(input, (int)_countof(input), stdin);
            return 0;
        }
        fwprintf(stderr, L"ksh: batch set: missing variable name\n");
        return 1;
    }
    *equal = L'\0';
    wchar_t* value = equal + 1;
    if (prompt) {
        fputws(value, stdout);
        fflush(stdout);
        wchar_t input[MAX_LINE_LEN];
        if (!fgetws(input, (int)_countof(input), stdin)) return 1;
        size_t length = wcslen(input);
        while (length && (input[length - 1] == L'\r' ||
                input[length - 1] == L'\n'))
            input[--length] = L'\0';
        if (!length) return 1;
        return Batch_SetVariable(shell, context, assignment, input) ? 0 : 1;
    }
    if (!*value) {
        if (!Batch_RecordVariableChange(shell, context, assignment))
            return 1;
        return shell->env->Unset(shell->env, assignment) ? 0 : 1;
    }
    return Batch_SetVariable(shell, context, assignment, value) ? 0 : 1;
}

/* Reads one IF operand, keeping quotes and carets for the final phase. */
static bool Batch_ReadIfOperand(const wchar_t** cursor, wchar_t* word,
    size_t capacity) {
    const wchar_t* p = Batch_SkipSpaces(*cursor);
    size_t length = 0;
    bool quoted = false;
    while (*p) {
        if (*p == L'"') {
            quoted = !quoted;
        } else if (!quoted) {
            if (iswspace(*p) || (p[0] == L'=' && p[1] == L'=')) break;
            if (*p == L'^' && p[1]) {
                if (length + 2 >= capacity) return false;
                word[length++] = *p++;
            }
        }
        if (length + 1 >= capacity) return false;
        word[length++] = *p++;
    }
    word[length] = L'\0';
    *cursor = p;
    return length > 0;
}

static bool Batch_IsIfKeyword(const wchar_t* word, const wchar_t* after,
    const wchar_t* keyword) {
    if (_wcsicmp(word, keyword) != 0) return false;
    after = Batch_SkipSpaces(after);
    return !(after[0] == L'=' && after[1] == L'=');
}

static bool Batch_ParseIfNumber(const wchar_t* text, long long* value) {
    wchar_t* end = NULL;
    errno = 0;
    long long parsed = wcstoll(text, &end, 0);
    if (errno || end == text || *end) return false;
    *value = parsed;
    return true;
}

void Batch_StripQuotes(const wchar_t* source, wchar_t* target,
    size_t capacity) {
    size_t length = 0;
    for (; *source && length + 1 < capacity; ++source) {
        if (*source != L'"') target[length++] = *source;
    }
    target[length] = L'\0';
}

int Batch_ExecuteIf(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, const wchar_t* rest) {
    bool negate = false;
    bool insensitive = false;
    bool result = false;
    wchar_t word[MAX_LINE_LEN];
    wchar_t operand[MAX_LINE_LEN];
    const wchar_t* cursor = rest;
    if (!Batch_ReadIfOperand(&cursor, word, _countof(word))) goto syntax;
    if (Batch_IsIfKeyword(word, cursor, L"/i")) {
        insensitive = true;
        if (!Batch_ReadIfOperand(&cursor, word, _countof(word))) goto syntax;
    }
    if (Batch_IsIfKeyword(word, cursor, L"not")) {
        negate = true;
        if (!Batch_ReadIfOperand(&cursor, word, _countof(word))) goto syntax;
    }
    if (Batch_IsIfKeyword(word, cursor, L"errorlevel") ||
        Batch_IsIfKeyword(word, cursor, L"cmdextversion") ||
        Batch_IsIfKeyword(word, cursor, L"exist") ||
        Batch_IsIfKeyword(word, cursor, L"defined")) {
        if (!Batch_ReadIfOperand(&cursor, operand, _countof(operand)))
            goto syntax;
        wchar_t* expanded = Batch_Expand(shell, context, operand,
            BATCH_EXPAND_FINAL);
        if (!expanded) return 1;
        wchar_t value[MAX_LINE_LEN];
        Batch_StripQuotes(expanded, value, _countof(value));
        free(expanded);
        long long number;
        if (_wcsicmp(word, L"errorlevel") == 0) {
            if (!Batch_ParseIfNumber(value, &number)) goto syntax;
            result = context->errorLevel >= number;
        } else if (_wcsicmp(word, L"cmdextversion") == 0) {
            if (!Batch_ParseIfNumber(value, &number)) goto syntax;
            result = number <= 2;
        } else if (_wcsicmp(word, L"exist") == 0) {
            DWORD attributes = GetFileAttributesW(value);
            if (attributes == INVALID_FILE_ATTRIBUTES &&
                wcspbrk(value, L"*?")) {
                WIN32_FIND_DATAW data;
                HANDLE search = FindFirstFileW(value, &data);
                result = search != INVALID_HANDLE_VALUE;
                if (search != INVALID_HANDLE_VALUE) FindClose(search);
            } else {
                result = attributes != INVALID_FILE_ATTRIBUTES;
            }
        } else {
            const wchar_t* local = shell->env->Get(shell->env, value);
            wchar_t* inherited = local ? NULL : GetEnvironmentValue(value);
            result = local != NULL || inherited != NULL;
            free(inherited);
        }
    } else {
        wchar_t* left = Batch_Expand(shell, context, word,
            BATCH_EXPAND_FINAL);
        if (!left) return 1;
        const wchar_t* p = Batch_SkipSpaces(cursor);
        wchar_t* right = NULL;
        if (p[0] == L'=' && p[1] == L'=') {
            cursor = p + 2;
            if (!Batch_ReadIfOperand(&cursor, operand, _countof(operand)) ||
                !(right = Batch_Expand(shell, context, operand,
                    BATCH_EXPAND_FINAL))) {
                free(left);
                goto syntax;
            }
            result = insensitive ? _wcsicmp(left, right) == 0 :
                wcscmp(left, right) == 0;
        } else {
            wchar_t operation[32];
            if (!Batch_ReadIfOperand(&cursor, operation,
                    _countof(operation)) ||
                !Batch_ReadIfOperand(&cursor, operand, _countof(operand)) ||
                !(right = Batch_Expand(shell, context, operand,
                    BATCH_EXPAND_FINAL))) {
                free(left);
                goto syntax;
            }
            long long leftNumber, rightNumber;
            int comparison;
            if (Batch_ParseIfNumber(left, &leftNumber) &&
                Batch_ParseIfNumber(right, &rightNumber)) {
                comparison = leftNumber < rightNumber ? -1 :
                    leftNumber > rightNumber ? 1 : 0;
            } else {
                comparison = insensitive ? _wcsicmp(left, right) :
                    wcscmp(left, right);
            }
            if (_wcsicmp(operation, L"equ") == 0) result = comparison == 0;
            else if (_wcsicmp(operation, L"neq") == 0) result = comparison != 0;
            else if (_wcsicmp(operation, L"lss") == 0) result = comparison < 0;
            else if (_wcsicmp(operation, L"leq") == 0) result = comparison <= 0;
            else if (_wcsicmp(operation, L"gtr") == 0) result = comparison > 0;
            else if (_wcsicmp(operation, L"geq") == 0) result = comparison >= 0;
            else {
                free(left);
                free(right);
                goto syntax;
            }
        }
        free(left);
        free(right);
    }
    if (negate) result = !result;
    cursor = Batch_SkipSpaces(cursor);
    if (!*cursor) goto syntax;
    if (*cursor != L'(')
        return result ?
            Batch_ExecuteStatements(shell, context, lines, cursor) :
            context->errorLevel;
    size_t close = Batch_FindBlockEnd(cursor, 0);
    if (close == SIZE_MAX)
        return result ?
            Batch_ExecuteStatements(shell, context, lines, cursor) :
            context->errorLevel;
    if (result) {
        wchar_t* block = (wchar_t*)malloc((close + 2) * sizeof(wchar_t));
        if (!block) return 1;
        memcpy(block, cursor, (close + 1) * sizeof(wchar_t));
        block[close + 1] = L'\0';
        int status = Batch_ExecuteStatements(shell, context, lines, block);
        free(block);
        return status;
    }
    const wchar_t* after = Batch_SkipSpaces(cursor + close + 1);
    if (_wcsnicmp(after, L"else", 4) == 0 &&
        (!after[4] || iswspace(after[4]) || after[4] == L'(')) {
        after = Batch_SkipSpaces(after + 4);
        if (*after) return Batch_ExecuteStatements(shell, context, lines, after);
    }
    return context->errorLevel;

syntax:
    fwprintf(stderr, L"ksh: batch if: invalid syntax\n");
    return 2;
}

typedef struct BatchForFOptions {
    wchar_t delimiters[MAX_LINE_LEN];
    wchar_t eol;
    unsigned long skipLines;
    unsigned long tokenNumbers[32];
    size_t tokenCount;
    bool allTokens;
    bool remainder;
    bool useBackquote;
} BatchForFOptions;

static void Batch_FreeForFVariables(BatchContext* context,
    BatchLoopVariable* previous, BatchLoopVariable* variables,
    size_t variableCount) {
    context->loopVariables = previous;
    for (size_t i = 0; i < variableCount; ++i)
        free(variables[i].value);
}

static bool Batch_ReadForSource(const wchar_t** cursor, wchar_t* source,
    size_t capacity, wchar_t* quote) {
    const wchar_t* p = Batch_SkipSpaces(*cursor);
    if (!*p) {
        *cursor = p;
        return false;
    }
    *quote = L'\0';
    size_t length = 0;
    if (*p == L'\'' || *p == L'"' || *p == L'`') {
        *quote = *p++;
        const wchar_t* end = wcsrchr(p, *quote);
        if (!end) return false;
        while (p < end) {
            if (length + 1 >= capacity) return false;
            source[length++] = *p++;
        }
        p++;
    } else {
        while (*p && !iswspace(*p)) {
            if (length + 1 >= capacity) return false;
            source[length++] = *p++;
        }
    }
    source[length] = L'\0';
    *cursor = p;
    return true;
}

static int Batch_ExecuteForFLine(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, wchar_t firstName,
    const BatchForFOptions* options, wchar_t* line, const wchar_t* body) {
    wchar_t* value = line;
    while (*value && wcschr(options->delimiters, *value)) value++;
    if (!*value || (options->eol && *value == options->eol))
        return context->errorLevel;

    size_t variableCount = options->allTokens ? 1 :
        options->tokenCount + (options->remainder ? 1 : 0);
    if (!variableCount || variableCount > 32 ||
        (unsigned int)towupper(firstName) + variableCount - 1 > L'Z')
        return 2;
    BatchLoopVariable variables[32] = { 0 };
    BatchLoopVariable* previous = context->loopVariables;
    for (size_t i = 0; i < variableCount; ++i) {
        variables[i].name = (wchar_t)(towupper(firstName) + i);
        variables[i].next = i ? &variables[i - 1] : previous;
    }
    context->loopVariables = &variables[variableCount - 1];
    if (options->allTokens) {
        variables[0].value = SafeStrDup(value);
    } else {
        wchar_t* tokens[256];
        wchar_t* afterToken[256];
        size_t tokenCount = 0;
        wchar_t* cursor = value;
        while (*cursor && tokenCount < _countof(tokens)) {
            while (*cursor && wcschr(options->delimiters, *cursor)) cursor++;
            if (!*cursor) break;
            size_t currentToken = tokenCount++;
            tokens[currentToken] = cursor;
            while (*cursor && !wcschr(options->delimiters, *cursor)) cursor++;
            if (*cursor) {
                *cursor++ = L'\0';
                while (*cursor && wcschr(options->delimiters, *cursor))
                    cursor++;
            }
            afterToken[currentToken] = cursor;
        }
        for (size_t i = 0; i < options->tokenCount; ++i) {
            unsigned long number = options->tokenNumbers[i];
            variables[i].value = SafeStrDup(number && number <= tokenCount ?
                tokens[number - 1] : L"");
        }
        if (options->remainder) {
            unsigned long last = options->tokenNumbers[
                options->tokenCount - 1];
            wchar_t* remainder = value;
            if (last && last <= tokenCount) {
                remainder = afterToken[last - 1];
            } else {
                remainder += wcslen(remainder);
            }
            variables[options->tokenCount].value = SafeStrDup(remainder);
        }
    }
    bool allocated = true;
    for (size_t i = 0; i < variableCount; ++i)
        if (!variables[i].value) allocated = false;
    int status = allocated ?
        Batch_ExecuteStatements(shell, context, lines, body) : 1;
    Batch_FreeForFVariables(context, previous, variables, variableCount);
    return status;
}

static int Batch_ExecuteForFFile(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, wchar_t firstName,
    const BatchForFOptions* options, const wchar_t* filename,
    const wchar_t* body, bool literal) {
    if (literal) {
        wchar_t* text = SafeStrDup(filename);
        if (!text) return 1;
        int status = Batch_ExecuteForFLine(shell, context, lines,
            firstName, options, text, body);
        free(text);
        return status;
    }
    wchar_t* text = NULL;
    if (!Batch_ReadTextFile(filename, &text)) {
        fwprintf(stderr, L"ksh: batch for /F: cannot open %ls\n", filename);
        return 1;
    }
    unsigned long lineNumber = 0;
    int status = context->errorLevel;
    wchar_t* cursor = text;
    while (*cursor) {
        wchar_t* newline = wcschr(cursor, L'\n');
        wchar_t* next = newline ? newline + 1 : cursor + wcslen(cursor);
        if (newline) *newline = L'\0';
        size_t length = wcslen(cursor);
        while (length && cursor[length - 1] == L'\r') cursor[--length] = L'\0';
        lineNumber++;
        if (lineNumber > options->skipLines) {
            status = Batch_ExecuteForFLine(shell, context, lines,
                firstName, options, cursor, body);
            if (Batch_FlowStopped(shell, context)) break;
        }
        cursor = next;
    }
    free(text);
    return status;
}

static int Batch_ExecuteForFOutputLine(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, wchar_t firstName,
    const BatchForFOptions* options, const char* bytes, size_t length,
    unsigned long* lineNumber, const wchar_t* body) {
    while (length && bytes[length - 1] == '\r') length--;
    (*lineNumber)++;
    if (*lineNumber <= options->skipLines) return context->errorLevel;
    /* Child ksh output is UTF-8; external programs may emit OEM text. */
    wchar_t* line = Batch_DecodeText(bytes, length, *lineNumber == 1);
    if (!line) return 1;
    int status = Batch_ExecuteForFLine(shell, context, lines, firstName,
        options, line, body);
    free(line);
    return status;
}

/* Streams a FOR /F command's output from a child shell, line by line. */
static int Batch_ExecuteForFCommand(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, wchar_t firstName,
    const BatchForFOptions* options, const wchar_t* command,
    const wchar_t* body) {
    HANDLE readEnd = NULL;
    HANDLE writeEnd = NULL;
    if (!CreatePipe(&readEnd, &writeEnd, NULL, 0)) return 1;
    fflush(stdout);
    fflush(stderr);
    HANDLE process = Batch_SpawnStage(context, command,
        GetStdHandle(STD_INPUT_HANDLE), writeEnd,
        GetStdHandle(STD_ERROR_HANDLE));
    CloseHandle(writeEnd);
    if (!process) {
        CloseHandle(readEnd);
        return 1;
    }
    int status = context->errorLevel;
    unsigned long lineNumber = 0;
    char* pending = NULL;
    size_t pendingLength = 0;
    size_t pendingCapacity = 0;
    bool stopped = false;
    char buffer[4096];
    DWORD read = 0;
    while (!stopped && ReadFile(readEnd, buffer, sizeof(buffer), &read, NULL) &&
        read) {
        if (pendingLength + read > pendingCapacity) {
            size_t capacity = pendingCapacity ? pendingCapacity : 4096;
            while (capacity < pendingLength + read) {
                if (capacity > SIZE_MAX / 2) {
                    capacity = pendingLength + read;
                    break;
                }
                capacity *= 2;
            }
            char* resized = (char*)realloc(pending, capacity);
            if (!resized) {
                status = 1;
                break;
            }
            pending = resized;
            pendingCapacity = capacity;
        }
        memcpy(pending + pendingLength, buffer, read);
        pendingLength += read;
        size_t start = 0;
        for (size_t i = 0; i < pendingLength; ++i) {
            if (pending[i] != '\n') continue;
            status = Batch_ExecuteForFOutputLine(shell, context, lines,
                firstName, options, pending + start, i - start, &lineNumber,
                body);
            start = i + 1;
            if (Batch_FlowStopped(shell, context)) {
                stopped = true;
                break;
            }
        }
        memmove(pending, pending + start, pendingLength - start);
        pendingLength -= start;
    }
    if (!stopped && pendingLength)
        status = Batch_ExecuteForFOutputLine(shell, context, lines, firstName,
            options, pending, pendingLength, &lineNumber, body);
    free(pending);
    /* Closing the read end unblocks a child still writing after a GOTO. */
    CloseHandle(readEnd);
    WaitForSingleObject(process, INFINITE);
    CloseHandle(process);
    return status;
}

static int Batch_ExecuteForF(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, wchar_t firstName,
    const wchar_t* optionText, const wchar_t* list, const wchar_t* body) {
    BatchForFOptions options = { 0 };
    wcscpy_s(options.delimiters, _countof(options.delimiters), L" \t");
    options.eol = L';';
    options.tokenCount = 1;
    options.tokenNumbers[0] = 1;
    wchar_t optionBuffer[MAX_LINE_LEN];
    if (wcslen(optionText) >= _countof(optionBuffer)) return 2;
    wcscpy_s(optionBuffer, _countof(optionBuffer), optionText);
    wchar_t* option = optionBuffer;
    while (*option) {
        option = (wchar_t*)Batch_SkipSpaces(option);
        if (!*option) break;
        wchar_t* end = option;
        while (*end && !iswspace(*end)) end++;
        /* A trailing "delims= " or "eol= " keeps its blank character. */
        if ((_wcsnicmp(option, L"delims=", 7) == 0 ||
                _wcsnicmp(option, L"eol=", 4) == 0) &&
            !*Batch_SkipSpaces(end))
            end += wcslen(end);
        wchar_t saved = *end;
        *end = L'\0';
        if (_wcsnicmp(option, L"delims=", 7) == 0) {
            wcscpy_s(options.delimiters, _countof(options.delimiters),
                option + 7);
        } else if (_wcsnicmp(option, L"eol=", 4) == 0) {
            options.eol = option[4];
        } else if (_wcsnicmp(option, L"skip=", 5) == 0) {
            wchar_t* numberEnd = NULL;
            options.skipLines = wcstoul(option + 5, &numberEnd, 10);
            if (numberEnd == option + 5 || *numberEnd) return 2;
        } else if (_wcsnicmp(option, L"tokens=", 7) == 0) {
            options.tokenCount = 0;
            wchar_t* token = option + 7;
            while (*token) {
                if (*token == L'*') {
                    options.remainder = true;
                    token++;
                } else {
                    wchar_t* numberEnd = NULL;
                    unsigned long first = wcstoul(token, &numberEnd, 10);
                    if (numberEnd == token || first == 0) return 2;
                    unsigned long last = first;
                    if (*numberEnd == L'-') {
                        token = numberEnd + 1;
                        last = wcstoul(token, &numberEnd, 10);
                        if (numberEnd == token || last < first) return 2;
                    }
                    for (unsigned long number = first; number <= last;
                        ++number) {
                        if (options.tokenCount ==
                            _countof(options.tokenNumbers))
                            return 2;
                        options.tokenNumbers[options.tokenCount++] = number;
                        if (number == ULONG_MAX) break;
                    }
                    token = numberEnd;
                }
                if (*token == L',') token++;
                else if (*token && *token != L'*') return 2;
            }
            options.allTokens = options.tokenCount == 0 &&
                options.remainder;
        } else if (_wcsicmp(option, L"usebackq") == 0) {
            options.useBackquote = true;
        } else {
            fwprintf(stderr, L"ksh: batch for /F: invalid option %ls\n",
                option);
            return 2;
        }
        *end = saved;
        option = end;
    }
    wchar_t* sourceList = SafeStrDup(list);
    if (!sourceList) return 1;
    const wchar_t* cursor = sourceList;
    wchar_t source[MAX_LINE_LEN];
    int status = context->errorLevel;
    wchar_t sourceQuote;
    while (Batch_ReadForSource(&cursor, source, _countof(source),
            &sourceQuote)) {
        bool commandSource = sourceQuote == L'`' ||
            (sourceQuote == L'\'' && !options.useBackquote);
        bool literal = (sourceQuote == L'"' && !options.useBackquote) ||
            (sourceQuote == L'\'' && options.useBackquote);
        if (commandSource) {
            status = Batch_ExecuteForFCommand(shell, context, lines,
                firstName, &options, source, body);
        } else {
            status = Batch_ExecuteForFFile(shell, context, lines,
                firstName, &options, source, body, literal);
        }
        if (Batch_FlowStopped(shell, context)) break;
    }
    free(sourceList);
    return status;
}

/* Reads a token separated by blanks, commas, semicolons or equal signs. */
bool Batch_ReadDelimitedToken(const wchar_t** cursor, wchar_t* item,
    size_t capacity) {
    const wchar_t* p = *cursor;
    while (*p && (iswspace(*p) || *p == L',' || *p == L';' || *p == L'='))
        p++;
    if (!*p) {
        *cursor = p;
        return false;
    }
    size_t length = 0;
    bool quoted = false;
    while (*p && (quoted || !(iswspace(*p) || *p == L',' || *p == L';' ||
            *p == L'='))) {
        if (*p == L'"') quoted = !quoted;
        if (length + 1 >= capacity) return false;
        item[length++] = *p++;
    }
    item[length] = L'\0';
    *cursor = p;
    return true;
}

int Batch_ExecuteFor(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, const wchar_t* rest) {
    bool directoryMode = false;
    bool recursiveMode = false;
    bool numericMode = false;
    bool textMode = false;
    wchar_t forOptions[MAX_LINE_LEN] = { 0 };
    wchar_t recursiveRoot[MAX_PATH * 2] = { 0 };
    rest = Batch_SkipSpaces(rest);
    while (*rest == L'/') {
        wchar_t qualifier = (wchar_t)towlower(rest[1]);
        if (!rest[1] || (rest[2] && !iswspace(rest[2]))) goto syntax;
        if (qualifier == L'l') numericMode = true;
        else if (qualifier == L'd') directoryMode = true;
        else if (qualifier == L'r') recursiveMode = true;
        else if (qualifier == L'f') textMode = true;
        else {
            fwprintf(stderr, L"ksh: batch for: unsupported FOR qualifier\n");
            return 2;
        }
        rest = Batch_SkipSpaces(rest + 2);
    }
    if (textMode && *rest == L'"') {
        if (!Batch_ReadWord(&rest, forOptions, _countof(forOptions)))
            goto syntax;
        rest = Batch_SkipSpaces(rest);
    }
    if (recursiveMode && rest[0] != L'%') {
        wchar_t root[MAX_PATH * 2];
        if (!Batch_ReadWord(&rest, root, _countof(root))) goto syntax;
        wchar_t* expandedRoot = Batch_Expand(shell, context, root,
            BATCH_EXPAND_FINAL);
        if (!expandedRoot) return 1;
        Batch_StripQuotes(expandedRoot, recursiveRoot,
            _countof(recursiveRoot));
        free(expandedRoot);
        rest = Batch_SkipSpaces(rest);
    }
    if (rest[0] != L'%' || rest[1] != L'%' || !rest[2] || iswspace(rest[2]))
        goto syntax;
    wchar_t variableName = rest[2];
    rest = Batch_SkipSpaces(rest + 3);
    if (_wcsnicmp(rest, L"in", 2) != 0 ||
        (rest[2] && !iswspace(rest[2]) && rest[2] != L'('))
        goto syntax;
    rest = Batch_SkipSpaces(rest + 2);
    if (*rest != L'(') goto syntax;
    size_t close = Batch_FindBlockEnd(rest, 0);
    if (close == SIZE_MAX) goto syntax;
    const wchar_t* body = Batch_SkipSpaces(rest + close + 1);
    if (_wcsnicmp(body, L"do", 2) != 0 ||
        (body[2] && !iswspace(body[2]) && body[2] != L'('))
        goto syntax;
    body = Batch_SkipSpaces(body + 2);
    if (!*body) goto syntax;

    wchar_t* rawSet = (wchar_t*)malloc(close * sizeof(wchar_t));
    if (!rawSet) return 1;
    memcpy(rawSet, rest + 1, (close - 1) * sizeof(wchar_t));
    rawSet[close - 1] = L'\0';
    wchar_t* set = Batch_Expand(shell, context, rawSet, BATCH_EXPAND_FINAL);
    free(rawSet);
    if (!set) return 1;
    for (wchar_t* p = set; *p; ++p)
        if (*p == L'\n') *p = L' ';

    BatchLoopVariable variable = { variableName, NULL,
        context->loopVariables };
    context->loopVariables = &variable;
    int status = context->errorLevel;
    bool valid = true;
    if (numericMode) {
        long long values[3] = { 0, 0, 0 };
        const wchar_t* cursor = set;
        for (int i = 0; i < 3 && valid; ++i) {
            while (*cursor && (iswspace(*cursor) || *cursor == L',' ||
                    *cursor == L';' || *cursor == L'='))
                cursor++;
            if (!*cursor) break;
            wchar_t* end = NULL;
            values[i] = wcstoll(cursor, &end, 10);
            if (end == cursor) valid = false;
            cursor = end;
        }
        long long start = values[0], step = values[1], end = values[2];
        if (step == 0) valid = false;
        for (long long value = start; valid &&
            ((step > 0 && value <= end) || (step < 0 && value >= end));) {
            wchar_t formatted[64];
            _snwprintf_s(formatted, _countof(formatted), _TRUNCATE,
                L"%lld", value);
            Batch_SetLoopValue(context, &variable, formatted);
            status = Batch_ExecuteStatements(shell, context, lines, body);
            if (Batch_FlowStopped(shell, context) ||
                (step > 0 && value > LLONG_MAX - step) ||
                (step < 0 && value < LLONG_MIN - step))
                break;
            value += step;
        }
    } else if (textMode) {
        status = Batch_ExecuteForF(shell, context, lines, variableName,
            forOptions, set, body);
    } else {
        if (recursiveMode && !*recursiveRoot &&
            !GetCurrentDirectoryW(_countof(recursiveRoot), recursiveRoot))
            valid = false;
        const wchar_t* cursor = set;
        wchar_t item[MAX_LINE_LEN];
        while (valid && Batch_ReadDelimitedToken(&cursor, item,
                _countof(item))) {
            wchar_t pattern[MAX_LINE_LEN];
            Batch_StripQuotes(item, pattern, _countof(pattern));
            if (recursiveMode) {
                status = Batch_ExecuteRecursiveFor(shell, context, lines,
                    &variable, recursiveRoot, pattern, body, directoryMode,
                    0);
                if (Batch_FlowStopped(shell, context)) break;
                continue;
            }
            const wchar_t* separator = NULL;
            for (const wchar_t* p = pattern; *p; ++p)
                if (*p == L'\\' || *p == L'/' || *p == L':') separator = p;
            const wchar_t* leaf = separator ? separator + 1 : pattern;
            if (!wcspbrk(leaf, L"*?")) {
                Batch_SetLoopValue(context, &variable, item);
                status = Batch_ExecuteStatements(shell, context, lines, body);
                if (Batch_FlowStopped(shell, context)) break;
                continue;
            }
            WIN32_FIND_DATAW data;
            HANDLE search = FindFirstFileW(pattern, &data);
            if (search == INVALID_HANDLE_VALUE) continue;
            size_t prefixLength = separator ?
                (size_t)(separator - pattern) + 1 : 0;
            do {
                bool isDirectory =
                    (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (isDirectory != directoryMode ||
                    wcscmp(data.cFileName, L".") == 0 ||
                    wcscmp(data.cFileName, L"..") == 0 ||
                    (data.dwFileAttributes &
                        (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                    continue;
                wchar_t fullPath[MAX_PATH * 2];
                if (prefixLength + wcslen(data.cFileName) >=
                    _countof(fullPath))
                    continue;
                if (prefixLength)
                    memcpy(fullPath, pattern,
                        prefixLength * sizeof(wchar_t));
                wcscpy_s(fullPath + prefixLength,
                    _countof(fullPath) - prefixLength, data.cFileName);
                Batch_SetLoopValue(context, &variable, fullPath);
                status = Batch_ExecuteStatements(shell, context, lines, body);
                if (Batch_FlowStopped(shell, context)) break;
            } while (FindNextFileW(search, &data));
            FindClose(search);
            if (Batch_FlowStopped(shell, context)) break;
        }
    }
    free(variable.value);
    context->loopVariables = variable.next;
    free(set);
    if (valid) return status;
    fwprintf(stderr, L"ksh: batch for: invalid FOR syntax or range\n");
    return 2;

syntax:
    fwprintf(stderr, L"ksh: batch for: invalid FOR syntax or range\n");
    return 2;
}

