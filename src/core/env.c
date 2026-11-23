#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

ShellArray* Env_FindArray(IEnvironment* self, const wchar_t* name) {
    for (ShellArray* array = self->arrays; array; array = array->next) {
        if (_wcsicmp(array->name, name) == 0) return array;
    }
    return NULL;
}

ShellArray* Env_CreateArray(IEnvironment* self, const wchar_t* name,
    bool associative) {
    ShellArray* array = Env_FindArray(self, name);
    if (array) {
        if (array->associative != associative) return NULL;
        return array;
    }
    array = (ShellArray*)calloc(1, sizeof(ShellArray));
    if (!array) return NULL;
    array->name = SafeStrDup(name);
    if (!array->name) {
        free(array);
        return NULL;
    }
    array->associative = associative;
    array->next = self->arrays;
    self->arrays = array;
    return array;
}

bool Env_ArraySet(ShellArray* array, const wchar_t* key,
    const wchar_t* value) {
    if (!array || !key || !value) return false;
    for (ArrayElement* element = array->elements; element;
        element = element->next) {
        if (wcscmp(element->key, key) == 0) {
            wchar_t* replacement = SafeStrDup(value);
            if (!replacement) return false;
            free(element->value);
            element->value = replacement;
            return true;
        }
    }
    ArrayElement* element = (ArrayElement*)calloc(1, sizeof(ArrayElement));
    if (!element) return false;
    element->key = SafeStrDup(key);
    element->value = SafeStrDup(value);
    if (!element->key || !element->value) {
        free(element->key);
        free(element->value);
        free(element);
        return false;
    }
    ArrayElement** tail = &array->elements;
    while (*tail) tail = &(*tail)->next;
    *tail = element;
    array->count++;
    return true;
}

void Env_ArrayClear(ShellArray* array) {
    if (!array) return;
    ArrayElement* element = array->elements;
    while (element) {
        ArrayElement* next = element->next;
        free(element->key);
        free(element->value);
        free(element);
        element = next;
    }
    array->elements = NULL;
    array->count = 0;
}

const wchar_t* Env_ArrayGet(const ShellArray* array,
    const wchar_t* key) {
    if (!array || !key) return NULL;
    for (const ArrayElement* element = array->elements; element;
        element = element->next) {
        if (wcscmp(element->key, key) == 0) return element->value;
    }
    return NULL;
}

bool Env_ArraySetIndex(ShellArray* array, size_t index,
    const wchar_t* value) {
    wchar_t key[32];
    if (swprintf_s(key, sizeof(key) / sizeof(key[0]), L"%zu", index) < 0)
        return false;
    return Env_ArraySet(array, key, value);
}

wchar_t* Env_ArrayJoin(const ShellArray* array) {
    size_t length = 1;
    for (const ArrayElement* element = array ? array->elements : NULL;
        element; element = element->next) {
        size_t valueLength = wcslen(element->value);
        if (valueLength > SIZE_MAX - length - 1) return NULL;
        length += valueLength + 1;
    }
    wchar_t* joined = (wchar_t*)malloc(length * sizeof(wchar_t));
    if (!joined) return NULL;
    joined[0] = L'\0';
    for (const ArrayElement* element = array ? array->elements : NULL;
        element; element = element->next) {
        if (*joined) wcscat_s(joined, length, L" ");
        wcscat_s(joined, length, element->value);
    }
    return joined;
}

EnvEntry* Env_FindEntry(IEnvironment* self, const wchar_t* key) {
    for (EnvEntry* entry = self->head; entry; entry = entry->next) {
        if (_wcsicmp(entry->key, key) == 0) return entry;
    }
    return NULL;
}

static bool ParseTypesetInteger(const wchar_t* text, unsigned int base,
    long long* result) {
    if (!text || !*text || !result) return false;
    const wchar_t* digits = text;
    bool negative = false;
    if (*digits == L'+' || *digits == L'-') {
        negative = *digits == L'-';
        digits++;
    }
    const wchar_t* marker = wcschr(digits, L'#');
    if (marker) {
        wchar_t baseText[8];
        size_t length = (size_t)(marker - digits);
        if (!length || length >= sizeof(baseText) / sizeof(baseText[0]))
            return false;
        memcpy(baseText, digits, length * sizeof(wchar_t));
        baseText[length] = L'\0';
        wchar_t* end = NULL;
        unsigned long parsedBase = wcstoul(baseText, &end, 10);
        if (end == baseText || *end || parsedBase < 2 || parsedBase > 36)
            return false;
        base = (unsigned int)parsedBase;
        digits = marker + 1;
    }
    if (base < 2 || base > 36) return false;
    wchar_t* end = NULL;
    errno = 0;
    unsigned long long magnitude = wcstoull(digits, &end, (int)base);
    if (errno || end == digits || *end) return false;
    if (negative) {
        if (magnitude > (unsigned long long)LLONG_MAX + 1ULL) return false;
        *result = magnitude == (unsigned long long)LLONG_MAX + 1ULL ?
            LLONG_MIN : -(long long)magnitude;
    } else {
        if (magnitude > (unsigned long long)LLONG_MAX) return false;
        *result = (long long)magnitude;
    }
    return true;
}

static wchar_t* FormatTypesetInteger(long long value, unsigned int base) {
    static const wchar_t digits[] = L"0123456789abcdefghijklmnopqrstuvwxyz";
    if (base < 2 || base > 36) base = 10;
    wchar_t buffer[96];
    size_t position = sizeof(buffer) / sizeof(buffer[0]);
    buffer[--position] = L'\0';
    bool negative = value < 0;
    unsigned long long magnitude = negative ?
        (unsigned long long)(-(value + 1)) + 1ULL :
        (unsigned long long)value;
    do {
        buffer[--position] = digits[magnitude % base];
        magnitude /= base;
    } while (magnitude);
    if (negative) buffer[--position] = L'-';
    if (base != 10) {
        wchar_t prefix[8];
        int prefixLength = swprintf_s(prefix,
            sizeof(prefix) / sizeof(prefix[0]), L"%u#", base);
        if (prefixLength < 0 ||
            position < (size_t)prefixLength) return NULL;
        position -= (size_t)prefixLength;
        memcpy(buffer + position, prefix,
            (size_t)prefixLength * sizeof(wchar_t));
    }
    return SafeStrDup(buffer + position);
}

wchar_t* Env_TransformValue(const EnvEntry* entry,
    const wchar_t* value) {
    wchar_t* transformed;
    if (entry->is_float) {
        double fval = 0.0;
        if (!*value) fval = 0.0;
        else {
            wchar_t* endp = NULL;
            fval = wcstod(value, &endp);
            if (endp == value) return NULL;
        }
        wchar_t buf[128];
        if (entry->float_precision > 0) {
            if (entry->float_exponential) {
                swprintf_s(buf, _countof(buf), L"%.*e", entry->float_precision, fval);
            } else {
                swprintf_s(buf, _countof(buf), L"%.*f", entry->float_precision, fval);
            }
        } else {
            if (entry->float_exponential) {
                swprintf_s(buf, _countof(buf), L"%e", fval);
            } else {
                swprintf_s(buf, _countof(buf), L"%.10g", fval);
            }
        }
        transformed = SafeStrDup(buf);
    } else if (entry->integer) {
        long long numericValue;
        if (!*value) numericValue = 0;
        else if (!ParseTypesetInteger(value,
                entry->integerBase ? entry->integerBase : 10,
                &numericValue))
            return NULL;
        transformed = FormatTypesetInteger(numericValue,
            entry->integerBase ? entry->integerBase : 10);
    } else {
        transformed = SafeStrDup(value);
    }
    if (!transformed) return NULL;
    for (wchar_t* p = transformed; *p; ++p) {
        if (entry->lowercase) *p = (wchar_t)towlower(*p);
        else if (entry->uppercase) *p = (wchar_t)towupper(*p);
    }
    if (entry->width) {
        size_t length = wcslen(transformed);
        if (length > entry->width) {
            if (entry->alignment == L'L') transformed[entry->width] = L'\0';
            else {
                memmove(transformed,
                    transformed + length - entry->width,
                    entry->width * sizeof(wchar_t));
                transformed[entry->width] = L'\0';
            }
        } else if (length < entry->width) {
            wchar_t padding = entry->alignment == L'Z' ? L'0' : L' ';
            size_t paddingLength = entry->width - length;
            wchar_t* resized = (wchar_t*)realloc(transformed,
                ((size_t)entry->width + 1) * sizeof(wchar_t));
            if (!resized) {
                free(transformed);
                return NULL;
            }
            transformed = resized;
            if (entry->alignment == L'L') {
                for (size_t i = length; i < entry->width; ++i)
                    transformed[i] = padding;
                transformed[entry->width] = L'\0';
            } else if (entry->alignment == L'Z' &&
                transformed[0] == L'-') {
                memmove(transformed + paddingLength + 1, transformed + 1,
                    (length) * sizeof(wchar_t));
                transformed[0] = L'-';
                for (size_t i = 1; i <= paddingLength; ++i)
                    transformed[i] = L'0';
            } else {
                memmove(transformed + paddingLength, transformed,
                    (length + 1) * sizeof(wchar_t));
                for (size_t i = 0; i < paddingLength; ++i)
                    transformed[i] = padding;
            }
        }
    }
    return transformed;
}

static int s_in_discipline = 0;

static bool SetupDisciplineContext(IEnvironment* self, const wchar_t* key, const wchar_t* val, wchar_t* outHookName, size_t outHookCap, const wchar_t* hookType) {
    if (!self || !key || !self->shell) return false;
    
    // Check if key has array subscript e.g. arr[sub]
    const wchar_t* openBr = wcschr(key, L'[');
    const wchar_t* closeBr = openBr ? wcschr(openBr, L']') : NULL;
    wchar_t arrName[128] = {0};
    wchar_t arrSub[128] = {0};
    if (openBr && closeBr && closeBr > openBr) {
        wcsncpy_s(arrName, _countof(arrName), key, (size_t)(openBr - key));
        wcsncpy_s(arrSub, _countof(arrSub), openBr + 1, (size_t)(closeBr - openBr - 1));
    }

    // Try hook on exact key first (e.g. key.set or arr[0].set)
    swprintf_s(outHookName, outHookCap, L"%ls.%ls", key, hookType);
    bool hasHook = Shell_HasFunction(self->shell, outHookName);
    if (!hasHook && arrName[0]) {
        // Try array-level hook (e.g. arr.set)
        swprintf_s(outHookName, outHookCap, L"%ls.%ls", arrName, hookType);
        hasHook = Shell_HasFunction(self->shell, outHookName);
    }
    if (!hasHook) return false;

    // Found hook! Populate .sh.name, .sh.subscript, .sh.value, .sh.type
    if (arrName[0]) {
        self->Set(self, L".sh.name", arrName);
        self->Set(self, L".sh.subscript", arrSub);
    } else {
        self->Set(self, L".sh.name", key);
        self->Unset(self, L".sh.subscript");
    }
    if (val) {
        self->Set(self, L".sh.value", val);
    }

    // Check for type definition context (e.g. p1.x -> p1._type)
    const wchar_t* firstDot = wcschr(key, L'.');
    if (firstDot && firstDot > key) {
        wchar_t instName[128] = {0};
        wcsncpy_s(instName, _countof(instName), key, (size_t)(firstDot - key));
        wchar_t typeKey[256];
        swprintf_s(typeKey, _countof(typeKey), L"%ls._type", instName);
        const wchar_t* tVal = self->Get(self, typeKey);
        if (tVal) {
            self->Set(self, L".sh.type", tVal);
        } else {
            self->Unset(self, L".sh.type");
        }
    } else {
        self->Unset(self, L".sh.type");
    }
    return true;
}

static bool Env_Set(IEnvironment* self, const wchar_t* key, const wchar_t* val) {
    if (!self || !key || !*key || !val) return false;

    wchar_t resolvedKeyBuf[256];
    const wchar_t* cur_dot = wcschr(key, L'.');
    if (cur_dot && cur_dot > key) {
        wchar_t baseName[128];
        wcsncpy_s(baseName, _countof(baseName), key, (size_t)(cur_dot - key));
        for (EnvEntry* e = self->head; e; e = e->next) {
            if (_wcsicmp(e->key, baseName) == 0 && e->is_nameref && e->value && *e->value) {
                swprintf_s(resolvedKeyBuf, _countof(resolvedKeyBuf), L"%ls%ls", e->value, cur_dot);
                key = resolvedKeyBuf;
                break;
            }
        }
    }

    wchar_t mutatedVal[MAX_LINE_LEN];
    const wchar_t* effectiveVal = val;
    if (!s_in_discipline && self->shell && key && key[0] != L'.' && !wcsstr(key, L".sh.")) {
        wchar_t hookName[256];
        if (SetupDisciplineContext(self, key, val, hookName, _countof(hookName), L"set")) {
            s_in_discipline++;
            CommandAST hookCmd = {0};
            hookCmd.argc = 1;
            hookCmd.argv[0] = hookName;
            int hookCode = Shell_InvokeFunction(self->shell, &hookCmd);
            const wchar_t* resVal = self->Get(self, L".sh.value");
            if (resVal) {
                wcsncpy_s(mutatedVal, _countof(mutatedVal), resVal, _TRUNCATE);
                effectiveVal = mutatedVal;
            }
            s_in_discipline--;
            if (hookCode != 0) return false;
        }
    }

    EnvEntry* curr = self->head;
    while (curr) {
        if (_wcsicmp(curr->key, key) == 0) {
            if (curr->is_nameref && curr->value && *curr->value && _wcsicmp(curr->value, key) != 0) {
                return Env_Set(self, curr->value, effectiveVal);
            }
            if (curr->readonly && wcscmp(curr->value, effectiveVal) != 0)
                return false;
            wchar_t* replacement = Env_TransformValue(curr, effectiveVal);
            if (!replacement ||
                !SetEnvironmentVariableW(key, replacement)) {
                free(replacement);
                return false;
            }
            free(curr->value);
            curr->value = replacement;
            return true;
        }
        curr = curr->next;
    }
    EnvEntry* node = (EnvEntry*)malloc(sizeof(EnvEntry));
    if (!node) return false;
    node->key = SafeStrDup(key);
    node->value = SafeStrDup(effectiveVal);
    node->readonly = false;
    node->integer = false;
    node->is_float = false;
    node->float_exponential = false;
    node->float_precision = 0;
    node->lowercase = false;
    node->uppercase = false;
    node->is_nameref = false;
    node->alignment = L'R';
    node->integerBase = 10;
    node->width = 0;
    if (!node->key || !node->value || !SetEnvironmentVariableW(key, effectiveVal)) {
        free(node->key);
        free(node->value);
        free(node);
        return false;
    }
    node->next = self->head;
    self->head = node;
    return true;
}

static const wchar_t* Env_Get(IEnvironment* self, const wchar_t* key) {
    if (!key) return NULL;

    if (!s_in_discipline && self->shell && key && key[0] != L'.' && !wcsstr(key, L".sh.")) {
        wchar_t hookName[256];
        const wchar_t* curVal = NULL;
        for (EnvEntry* e = self->head; e; e = e->next) {
            if (_wcsicmp(e->key, key) == 0) { curVal = e->value; break; }
        }
        if (SetupDisciplineContext(self, key, curVal ? curVal : L"", hookName, _countof(hookName), L"get")) {
            s_in_discipline++;
            CommandAST hookCmd = {0};
            hookCmd.argc = 1;
            hookCmd.argv[0] = hookName;
            Shell_InvokeFunction(self->shell, &hookCmd);
            const wchar_t* dynamicVal = self->Get(self, L".sh.value");
            s_in_discipline--;
            return dynamicVal;
        }
    }

    int depth = 0;
    const wchar_t* cur_key = key;
    wchar_t resolvedKeyBuf[256];
    const wchar_t* cur_dot = wcschr(cur_key, L'.');
    if (cur_dot && cur_dot > cur_key) {
        wchar_t baseName[128];
        wcsncpy_s(baseName, _countof(baseName), cur_key, (size_t)(cur_dot - cur_key));
        for (EnvEntry* e = self->head; e; e = e->next) {
            if (_wcsicmp(e->key, baseName) == 0 && e->is_nameref && e->value && *e->value) {
                swprintf_s(resolvedKeyBuf, _countof(resolvedKeyBuf), L"%ls%ls", e->value, cur_dot);
                cur_key = resolvedKeyBuf;
                break;
            }
        }
    }
    while (depth < 16) {
        EnvEntry* curr = self->head;
        bool found = false;
        while (curr) {
            if (_wcsicmp(curr->key, cur_key) == 0) {
                if (curr->is_nameref && curr->value && *curr->value && _wcsicmp(curr->value, cur_key) != 0) {
                    cur_key = curr->value;
                    depth++;
                    found = true;
                    break;
                }
                return curr->value;
            }
            curr = curr->next;
        }
        if (!found) break;
    }
    return NULL;
}

static bool Env_Unset(IEnvironment* self, const wchar_t* key) {
    if (!self || !key || !*key) return false;

    if (!s_in_discipline && self->shell && key && key[0] != L'.' && !wcsstr(key, L".sh.")) {
        wchar_t hookName[256];
        if (SetupDisciplineContext(self, key, NULL, hookName, _countof(hookName), L"unset")) {
            s_in_discipline++;
            CommandAST hookCmd = {0};
            hookCmd.argc = 1;
            hookCmd.argv[0] = hookName;
            Shell_InvokeFunction(self->shell, &hookCmd);
            s_in_discipline--;
        }
    }

    for (EnvEntry* entry = self->head; entry; entry = entry->next) {
        if (_wcsicmp(entry->key, key) == 0) {
            if (entry->is_nameref && entry->value && *entry->value && _wcsicmp(entry->value, key) != 0) {
                return Env_Unset(self, entry->value);
            }
            if (entry->readonly) return false;
        }
    }
    if (!SetEnvironmentVariableW(key, NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ENVVAR_NOT_FOUND) return false;
    }
    EnvEntry** curr = &self->head;
    while (*curr) {
        if (_wcsicmp((*curr)->key, key) == 0) {
            EnvEntry* to_del = *curr;
            *curr = (*curr)->next;
            free(to_del->key);
            free(to_del->value);
            free(to_del);
            return true;
        }
        curr = &((*curr)->next);
    }
    return true;
}

bool Env_MakeReadonly(IEnvironment* self, const wchar_t* key) {
    for (EnvEntry* entry = self->head; entry; entry = entry->next) {
        if (_wcsicmp(entry->key, key) == 0) {
            entry->readonly = true;
            return true;
        }
    }
    if (!Env_Set(self, key, L"")) return false;
    for (EnvEntry* entry = self->head; entry; entry = entry->next) {
        if (_wcsicmp(entry->key, key) == 0) {
            entry->readonly = true;
            return true;
        }
    }
    return false;
}

bool Env_SetPositional(IEnvironment* self, const wchar_t* zero,
    int argc, wchar_t* const argv[]) {
    if (!zero || argc < 0 || argc >= MAX_ARGS) return false;
    wchar_t* values[MAX_ARGS] = {0};
    values[0] = _wcsdup(zero);
    if (!values[0]) return false;
    for (int i = 0; i < argc; ++i) {
        values[i + 1] = _wcsdup(argv[i]);
        if (!values[i + 1]) {
            for (int j = 0; j <= i; ++j) free(values[j]);
            return false;
        }
    }
    for (int i = 0; i < self->positionalCount; ++i)
        free(self->positional[i]);
    memcpy(self->positional, values, sizeof(values));
    self->positionalCount = argc + 1;
    return true;
}

bool Env_ShiftPositional(IEnvironment* self, int count) {
    int parameterCount = self->positionalCount > 0 ?
        self->positionalCount - 1 : 0;
    if (count < 0 || count > parameterCount) return false;
    for (int i = 1; i <= count; ++i) free(self->positional[i]);
    if (count)
        memmove(&self->positional[1], &self->positional[count + 1],
            (size_t)(parameterCount - count) * sizeof(self->positional[0]));
    for (int i = parameterCount - count + 1; i < self->positionalCount; ++i)
        self->positional[i] = NULL;
    self->positionalCount -= count;
    return true;
}

static bool AppendExpandedValue(wchar_t** output, size_t* capacity,
    size_t* used, const wchar_t* value, size_t length) {
    if (length > SIZE_MAX - *used - 1) return false;
    while (*used + length >= *capacity) {
        if (*capacity > SIZE_MAX / 2 / sizeof(wchar_t)) return false;
        size_t newCapacity = *capacity * 2;
        wchar_t* resized = (wchar_t*)realloc(*output,
            newCapacity * sizeof(wchar_t));
        if (!resized) return false;
        *output = resized;
        *capacity = newCapacity;
    }
    memcpy(*output + *used, value, length * sizeof(wchar_t));
    *used += length;
    (*output)[*used] = L'\0';
    return true;
}


/* Resolves both POSIX ($VAR, ${VAR}) and Windows (%VAR%) variables safely */
/* Appends a substituted value. When the result is going to be re-tokenized by the parser,
   shell metacharacters are backslash-escaped so data cannot become syntax (command injection).
   Whitespace and glob characters are left alone so word splitting and globbing still apply. */
static bool AppendSubstValue(bool esc, wchar_t quote, wchar_t** output, size_t* capacity,
    size_t* used, const wchar_t* value, size_t length) {
    if (!esc || quote == L'\'') return AppendExpandedValue(output, capacity, used, value, length);
    for (size_t k = 0; k < length; k++) {
        wchar_t ch = value[k];
        bool special = quote == L'"'
            ? (ch == L'"' || ch == L'\\' || ch == L'$' || ch == L'`')
            : (wcschr(L"\"'\\|&;<>()$`#", ch) != NULL && ch != 0);
        if (special && !AppendExpandedValue(output, capacity, used, L"\\", 1)) return false;
        if (!AppendExpandedValue(output, capacity, used, &value[k], 1)) return false;
    }
    return true;
}

/* Formats the active option letters for $-, in the same order as `set` lists them. */
static int FormatOptionFlags(IShell* shell, wchar_t* buf, size_t cap) {
    if (!shell || cap == 0) return 0;
    const struct { bool on; wchar_t letter; } table[] = {
        { shell->allexport, L'a' }, { shell->notify, L'b' },
        { shell->noclobber, L'C' }, { shell->errexit, L'e' },
        { shell->noglob, L'f' }, { shell->hashall, L'h' },
        { shell->interactive, L'i' }, { shell->monitor, L'm' },
        { shell->noexec, L'n' }, { shell->env->nounset, L'u' },
        { shell->verbose, L'v' }, { shell->xtrace, L'x' },
    };
    size_t n = 0;
    for (size_t k = 0; k < sizeof(table) / sizeof(table[0]) && n + 1 < cap; k++) {
        if (table[k].on) buf[n++] = table[k].letter;
    }
    buf[n] = L'\0';
    return (int)n;
}

static wchar_t* Env_Expand(IEnvironment* self, const wchar_t* input) {
    if (!input) return NULL;
    /* Consume the flag so nested expansions (e.g. inside $(...)) don't inherit it. */
    bool esc = self->escapeExpansions;
    self->escapeExpansions = false;
    bool heredoc = self->heredocMode;
    self->heredocMode = false;
    size_t in_len = wcslen(input);
    size_t out_cap = in_len * 2 + 128;
    wchar_t* out = (wchar_t*)malloc(out_cap * sizeof(wchar_t));
    if (!out) return NULL;

    size_t i = 0, j = 0;
    wchar_t quote = L'\0';
    while (i < in_len) {
        if (heredoc && input[i] == L'\\') {
            wchar_t nx = input[i + 1];
            bool special = nx == L'$' || nx == L'`' || nx == L'\\';
            if (!AppendExpandedValue(&out, &out_cap, &j, special ? &input[i + 1] : &input[i], 1)) { free(out); return NULL; }
            i += special ? 2 : 1;
            continue;
        }
        if (input[i] == L'\\' && quote != L'\'') {
            size_t count = input[i + 1] ? 2 : 1;
            while (j + count >= out_cap) {
                out_cap *= 2;
                wchar_t* reout = (wchar_t*)realloc(out,
                    out_cap * sizeof(wchar_t));
                if (!reout) { free(out); return NULL; }
                out = reout;
            }
            memcpy(out + j, input + i, count * sizeof(wchar_t));
            i += count;
            j += count;
            continue;
        }
        if (!heredoc && input[i] == L'\'' && quote != L'"') {
            quote = quote == L'\'' ? L'\0' : L'\'';
            if (!AppendExpandedValue(&out, &out_cap, &j, &input[i], 1)) { free(out); return NULL; }
            i++;
            continue;
        }
        if (!heredoc && input[i] == L'"' && quote != L'\'') {
            quote = quote == L'"' ? L'\0' : L'"';
            if (!AppendExpandedValue(&out, &out_cap, &j, &input[i], 1)) { free(out); return NULL; }
            i++;
            continue;
        }
        if (quote == L'\'') {
            if (j + 1 >= out_cap) {
                out_cap *= 2;
                wchar_t* reout = (wchar_t*)realloc(out,
                    out_cap * sizeof(wchar_t));
                if (!reout) { free(out); return NULL; }
                out = reout;
            }
            out[j++] = input[i++];
            continue;
        }
        if (!heredoc && quote == L'\0' && input[i] == L'~') {
            bool is_start = (i == 0 || iswspace(input[i - 1]) ||
                             input[i - 1] == L';' || input[i - 1] == L'|' ||
                             input[i - 1] == L'&' || input[i - 1] == L'(');
            if (is_start) {
                wchar_t next = input[i + 1];
                if (next == L'\0' || iswspace(next) || next == L'/' ||
                    next == L'\\' || next == L';' || next == L'|' ||
                    next == L'&' || next == L')') {
                    const wchar_t* home = self->Get(self, L"HOME");
                    wchar_t* allocHome = NULL;
                    if (!home) home = allocHome = GetEnvironmentValue(L"HOME");
                    if (!home) home = allocHome = GetEnvironmentValue(L"USERPROFILE");
                    if (home) {
                        size_t hlen = wcslen(home);
                        wchar_t* normHome = (wchar_t*)malloc((hlen + 1) * sizeof(wchar_t));
                        if (normHome) {
                            for (size_t k = 0; k <= hlen; k++) {
                                normHome[k] = (home[k] == L'\\') ? L'/' : home[k];
                            }
                            AppendSubstValue(esc, quote, &out, &out_cap, &j, normHome, hlen);
                            free(normHome);
                        }
                        free(allocHome);
                        i++;
                        continue;
                    }
                    free(allocHome);
                }
            }
        }
        if (input[i] == L'`' && quote != L'\'') {
            size_t end = i + 1;
            while (end < in_len && input[end] != L'`') end++;
            if (end < in_len) {
                size_t clen = end - (i + 1);
                wchar_t cmdStr[MAX_LINE_LEN];
                if (clen < MAX_LINE_LEN) {
                    memcpy(cmdStr, input + i + 1, clen * sizeof(wchar_t));
                    cmdStr[clen] = L'\0';
                    wchar_t* captured = Shell_CaptureCommandOutput(self->shell, cmdStr);
                    if (captured) {
                        AppendSubstValue(esc, quote, &out, &out_cap, &j, captured, wcslen(captured));
                        free(captured);
                    }
                    i = end + 1;
                    continue;
                }
            }
        }
        if ((input[i] == L'<' || input[i] == L'>') && input[i + 1] == L'(' && quote != L'\'') {
            wchar_t procMode = input[i];
            int depth = 1;
            size_t end = i + 2;
            while (end < in_len && depth > 0) {
                if (input[end] == L'(') depth++;
                else if (input[end] == L')') depth--;
                end++;
            }
            if (depth == 0) {
                size_t clen = (end - 1) - (i + 2);
                wchar_t cmdStr[MAX_LINE_LEN];
                if (clen < MAX_LINE_LEN) {
                    memcpy(cmdStr, input + i + 2, clen * sizeof(wchar_t));
                    cmdStr[clen] = L'\0';
                    wchar_t tempDir[MAX_PATH], tempPath[MAX_PATH];
                    GetTempPathW(MAX_PATH, tempDir);
                    GetTempFileNameW(tempDir, procMode == L'<' ? L"kpi" : L"kpo", 0, tempPath);
                    for (wchar_t* p = tempPath; *p; ++p) {
                        if (*p == L'\\') *p = L'/';
                    }
                    if (procMode == L'<') {
                        wchar_t* captured = Shell_CaptureCommandOutput(self->shell, cmdStr);
                        if (captured) {
                            FILE* tf = NULL;
                            if (_wfopen_s(&tf, tempPath, L"w, ccs=UTF-8") == 0 && tf) {
                                fputws(captured, tf);
                                fclose(tf);
                            }
                            free(captured);
                        }
                    }
                    ProcessSubstEntry* pse = (ProcessSubstEntry*)calloc(1, sizeof(ProcessSubstEntry));
                    if (pse) {
                        wcscpy_s(pse->tempPath, MAX_PATH, tempPath);
                        wcscpy_s(pse->cmd, MAX_LINE_LEN, cmdStr);
                        pse->isOutput = (procMode == L'>');
                        pse->next = self->pendingProcSubst;
                        self->pendingProcSubst = pse;
                    }
                    AppendSubstValue(esc, quote, &out, &out_cap, &j, tempPath, wcslen(tempPath));
                    i = end;
                    continue;
                }
            }
        }
        if (input[i] == L'$') {
            i++;
            if (!heredoc && input[i] == L'\'' && quote != L'\'') {
                size_t end = i + 1;
                /* Decoded text is never longer than the source, so size from the input. */
                wchar_t* decBuf = (wchar_t*)malloc((in_len - i + 1) * sizeof(wchar_t));
                size_t dlen = 0;
                if (!decBuf) { free(out); return NULL; }
                while (end < in_len && input[end] != L'\'') {
                    if (input[end] == L'\\' && end + 1 < in_len) {
                        end++;
                        wchar_t esc = input[end];
                        if (esc == L'n') decBuf[dlen++] = L'\n';
                        else if (esc == L'r') decBuf[dlen++] = L'\r';
                        else if (esc == L't') decBuf[dlen++] = L'\t';
                        else if (esc == L'a') decBuf[dlen++] = L'\a';
                        else if (esc == L'b') decBuf[dlen++] = L'\b';
                        else if (esc == L'f') decBuf[dlen++] = L'\f';
                        else if (esc == L'v') decBuf[dlen++] = L'\v';
                        else if (esc == L'e' || esc == L'E') decBuf[dlen++] = L'\x1b';
                        else if (esc == L'\\') decBuf[dlen++] = L'\\';
                        else if (esc == L'\'') decBuf[dlen++] = L'\'';
                        else if (esc == L'"') decBuf[dlen++] = L'"';
                        else if (esc == L'x' && end + 2 < in_len && iswxdigit(input[end+1]) && iswxdigit(input[end+2])) {
                            wchar_t hbuf[3] = { input[end+1], input[end+2], 0 };
                            decBuf[dlen++] = (wchar_t)wcstoul(hbuf, NULL, 16);
                            end += 2;
                        } else decBuf[dlen++] = esc;
                    } else {
                        decBuf[dlen++] = input[end];
                    }
                    end++;
                }
                decBuf[dlen] = L'\0';
                if (quote == L'\0') {
                    AppendExpandedValue(&out, &out_cap, &j, L"'", 1);
                    for (size_t di = 0; di < dlen; di++) {
                        if (decBuf[di] == L'\'') {
                            AppendExpandedValue(&out, &out_cap, &j, L"'\\''", 4);
                        } else {
                            AppendExpandedValue(&out, &out_cap, &j, &decBuf[di], 1);
                        }
                    }
                    AppendExpandedValue(&out, &out_cap, &j, L"'", 1);
                } else {
                    AppendSubstValue(esc, quote, &out, &out_cap, &j, decBuf, dlen);
                }
                free(decBuf);
                if (end < in_len && input[end] == L'\'') i = end + 1;
                else i = end;
                continue;
            }
            if (input[i] == L'(' && input[i + 1] == L'(') {
                size_t end = i + 2;
                while (end + 1 < in_len && !(input[end] == L')' && input[end + 1] == L')')) end++;
                if (end + 1 < in_len) {
                    size_t elen = end - (i + 2);
                    wchar_t expr[MAX_LINE_LEN];
                    if (elen < MAX_LINE_LEN) {
                        memcpy(expr, input + i + 2, elen * sizeof(wchar_t));
                        expr[elen] = L'\0';
                        long long val = 0;
                        double fval = 0.0;
                        bool is_float = false;
                        if (Ksh_EvaluateArithmetic(self->shell, expr, &val, &fval, &is_float)) {
                            wchar_t buf[64];
                            if (is_float) swprintf_s(buf, 64, L"%.10g", fval);
                            else swprintf_s(buf, 64, L"%lld", val);
                            AppendSubstValue(esc, quote, &out, &out_cap, &j, buf, wcslen(buf));
                        }
                        i = end + 2;
                        continue;
                    }
                }
            }
            if (input[i] == L'(') {
                int depth = 1;
                size_t end = i + 1;
                while (end < in_len && depth > 0) {
                    if (input[end] == L'(') depth++;
                    else if (input[end] == L')') depth--;
                    end++;
                }
                if (depth == 0) {
                    size_t clen = (end - 1) - (i + 1);
                    wchar_t cmdStr[MAX_LINE_LEN];
                    if (clen < MAX_LINE_LEN) {
                        memcpy(cmdStr, input + i + 1, clen * sizeof(wchar_t));
                        cmdStr[clen] = L'\0';
                        wchar_t* captured = Shell_CaptureCommandOutput(self->shell, cmdStr);
                        if (captured) {
                            AppendSubstValue(esc, quote, &out, &out_cap, &j, captured, wcslen(captured));
                            free(captured);
                        }
                        i = end;
                        continue;
                    }
                }
            }
            if (input[i] == L'#' || input[i] == L'?' ||
                input[i] == L'@' || input[i] == L'*' ||
                input[i] == L'$' || input[i] == L'!' || input[i] == L'-') {
                wchar_t special[32];
                int formattedLength = 0;
                wchar_t parameter = input[i++];
                if (parameter == L'#') {
                    int count = self->positionalCount > 0 ?
                        self->positionalCount - 1 : 0;
                    formattedLength = swprintf_s(special,
                        sizeof(special) / sizeof(special[0]), L"%d", count);
                } else if (parameter == L'?') {
                    formattedLength = swprintf_s(special,
                        sizeof(special) / sizeof(special[0]), L"%d",
                        self->lastStatus);
                } else if (parameter == L'$') {
                    formattedLength = swprintf_s(special,
                        sizeof(special) / sizeof(special[0]), L"%lu",
                        (unsigned long)GetCurrentProcessId());
                } else if (parameter == L'!') {
                    DWORD bgPid = Shell_LastBackgroundPid();
                    if (bgPid != 0) {
                        formattedLength = swprintf_s(special,
                            sizeof(special) / sizeof(special[0]), L"%lu",
                            (unsigned long)bgPid);
                    }
                } else if (parameter == L'-') {
                    formattedLength = FormatOptionFlags(self->shell,
                        special, sizeof(special) / sizeof(special[0]));
                } else if (parameter == L'@' && quote == L'"') {
                    /* Quoted "$@": fields are separated by closing and reopening the
                       surrounding quote, so the input's own quotes frame the first and
                       last fields. */
                    int count = self->positionalCount - 1;
                    if (count == 0) {
                        bool exactQuoted = i >= 3 && input[i - 3] == L'"' &&
                            i < in_len && input[i] == L'"';
                        if (exactQuoted && j > 0 && out[j - 1] == L'"') {
                            /* "$@" with no parameters produces no field at all. */
                            j--;
                            out[j] = L'\0';
                            quote = L'\0';
                            i++;
                        }
                        continue;
                    }
                    for (int p = 1; p <= count; ++p) {
                        if (p > 1 && !AppendExpandedValue(&out, &out_cap, &j, L"\" \"", 3)) {
                            free(out);
                            return NULL;
                        }
                        if (!AppendSubstValue(true, L'"', &out, &out_cap, &j,
                                self->positional[p], wcslen(self->positional[p]))) {
                            free(out);
                            return NULL;
                        }
                    }
                    continue;
                } else {
                    for (int p = 1; p < self->positionalCount; ++p) {
                        if (p > 1 && !AppendSubstValue(esc, quote, &out, &out_cap, &j, L" ", 1)) {
                            free(out);
                            return NULL;
                        }
                        size_t length = wcslen(self->positional[p]);
                        if (!AppendSubstValue(esc, quote, &out, &out_cap, &j, self->positional[p], length)) {
                            free(out);
                            return NULL;
                        }
                    }
                    continue;
                }
                if (formattedLength < 0) {
                    free(out);
                    return NULL;
                }
                if (formattedLength > 0 &&
                    !AppendSubstValue(esc, quote, &out, &out_cap, &j, special,
                        (size_t)formattedLength)) {
                    free(out);
                    return NULL;
                }
                continue;
            }
            if (input[i] == L'{') {
                size_t close = i + 1;
                while (close < in_len && input[close] != L'}') close++;
                if (close < in_len) {
                    size_t start = i + 1;

                    if (input[start] == L'!' && start + 1 < close) {
                        size_t exprStart = start + 1;
                        size_t exprLen = close - exprStart;
                        if (exprLen < 256) {
                            wchar_t spec[256];
                            memcpy(spec, input + exprStart, exprLen * sizeof(wchar_t));
                            spec[exprLen] = L'\0';

                            // 1. Array index introspection: ${!arr[@]} or ${!arr[*]}
                            if (wcsstr(spec, L"[@]") || wcsstr(spec, L"[*]")) {
                                wchar_t arrName[256];
                                const wchar_t* brk = wcschr(spec, L'[');
                                wcsncpy_s(arrName, _countof(arrName), spec, (size_t)(brk - spec));
                                ShellArray* arr = Env_FindArray(self, arrName);
                                if (arr) {
                                    wchar_t keyList[MAX_LINE_LEN] = {0};
                                    for (ArrayElement* el = arr->elements; el; el = el->next) {
                                        if (keyList[0]) wcscat_s(keyList, _countof(keyList), L" ");
                                        wcscat_s(keyList, _countof(keyList), el->key);
                                    }
                                    AppendSubstValue(esc, quote, &out, &out_cap, &j, keyList, wcslen(keyList));
                                }
                                i = close + 1;
                                continue;
                            }

                            // 2. Compound member introspection: ${!compound.@} or ${!compound.*}
                            size_t slen = wcslen(spec);
                            if (slen > 2 && spec[slen - 2] == L'.' && (spec[slen - 1] == L'@' || spec[slen - 1] == L'*')) {
                                wchar_t compPrefix[256];
                                wcsncpy_s(compPrefix, _countof(compPrefix), spec, slen - 1);
                                size_t pfxLen = wcslen(compPrefix);
                                wchar_t memberList[MAX_LINE_LEN] = {0};
                                wchar_t seen[64][64] = {{0}};
                                int seenCount = 0;
                                for (EnvEntry* e = self->head; e; e = e->next) {
                                    if (_wcsnicmp(e->key, compPrefix, pfxLen) == 0) {
                                        const wchar_t* rest = e->key + pfxLen;
                                        if (!rest || !*rest) continue;
                                        if (rest[0] == L'_') continue;
                                        wchar_t memName[64] = {0};
                                        const wchar_t* nextDot = wcschr(rest, L'.');
                                        if (nextDot) {
                                            wcsncpy_s(memName, _countof(memName), rest, (size_t)(nextDot - rest));
                                        } else {
                                            wcsncpy_s(memName, _countof(memName), rest, _TRUNCATE);
                                        }
                                        bool alreadySeen = false;
                                        for (int s = 0; s < seenCount; s++) {
                                            if (_wcsicmp(seen[s], memName) == 0) { alreadySeen = true; break; }
                                        }
                                        if (!alreadySeen && seenCount < 64) {
                                            wcsncpy_s(seen[seenCount++], 64, memName, _TRUNCATE);
                                            if (memberList[0]) wcscat_s(memberList, _countof(memberList), L" ");
                                            wcscat_s(memberList, _countof(memberList), memName);
                                        }
                                    }
                                }
                                AppendSubstValue(esc, quote, &out, &out_cap, &j, memberList, wcslen(memberList));
                                i = close + 1;
                                continue;
                            }

                            // 3. Variable prefix match: ${!prefix*} or ${!prefix@}
                            if (slen > 1 && (spec[slen - 1] == L'*' || spec[slen - 1] == L'@') && !wcschr(spec, L'.') && !wcschr(spec, L'[')) {
                                wchar_t prefix[256];
                                wcsncpy_s(prefix, _countof(prefix), spec, slen - 1);
                                size_t pfxLen = wcslen(prefix);
                                wchar_t varList[MAX_LINE_LEN] = {0};
                                for (EnvEntry* e = self->head; e; e = e->next) {
                                    if (_wcsnicmp(e->key, prefix, pfxLen) == 0 && !wcschr(e->key, L'.')) {
                                        if (varList[0]) wcscat_s(varList, _countof(varList), L" ");
                                        wcscat_s(varList, _countof(varList), e->key);
                                    }
                                }
                                AppendSubstValue(esc, quote, &out, &out_cap, &j, varList, wcslen(varList));
                                i = close + 1;
                                continue;
                            }

                            // 4. Nameref target or indirect variable lookup: ${!ref}
                            bool isNameref = false;
                            const wchar_t* namerefTarget = NULL;
                            for (EnvEntry* e = self->head; e; e = e->next) {
                                if (_wcsicmp(e->key, spec) == 0) {
                                    if (e->is_nameref) {
                                        isNameref = true;
                                        namerefTarget = e->value;
                                    }
                                    break;
                                }
                            }
                            if (isNameref && namerefTarget) {
                                AppendSubstValue(esc, quote, &out, &out_cap, &j, namerefTarget, wcslen(namerefTarget));
                            } else {
                                const wchar_t* indirectTarget = self->Get(self, spec);
                                if (indirectTarget && *indirectTarget) {
                                    const wchar_t* finalVal = self->Get(self, indirectTarget);
                                    if (finalVal) {
                                        AppendSubstValue(esc, quote, &out, &out_cap, &j, finalVal, wcslen(finalVal));
                                    }
                                }
                            }
                            i = close + 1;
                            continue;
                        }
                    }

                    bool lengthExpansion = input[start] == L'#';
                    if (lengthExpansion) start++;
                    size_t bracket = start;
                    while (bracket < close && input[bracket] != L'[')
                        bracket++;
                    if (bracket > start && bracket < close && bracket - start < 256) {
                        wchar_t baseName[256];
                        size_t nameLength = bracket - start;
                        memcpy(baseName, input + start, nameLength * sizeof(wchar_t));
                        baseName[nameLength] = L'\0';

                        wchar_t resolvedFullKey[512] = {0};
                        wcscpy_s(resolvedFullKey, _countof(resolvedFullKey), baseName);

                        const wchar_t* p = input + bracket;
                        const wchar_t* endClose = input + close;
                        wchar_t firstIdx[128] = {0};
                        bool hasFirstIdx = false;

                        while (p < endClose) {
                            if (*p == L'[') {
                                const wchar_t* cb = wcschr(p, L']');
                                if (!cb || cb > endClose) break;
                                size_t idxLen = (size_t)(cb - p - 1);
                                wchar_t idxStr[128];
                                if (idxLen >= sizeof(idxStr) / sizeof(idxStr[0])) idxLen = sizeof(idxStr) / sizeof(idxStr[0]) - 1;
                                wcsncpy_s(idxStr, _countof(idxStr), p + 1, idxLen);
                                const wchar_t* resIdx = idxStr;
                                if (idxStr[0] == L'$') {
                                    const wchar_t* v = self->Get(self, idxStr + 1);
                                    if (v) resIdx = v;
                                } else if (!iswdigit(idxStr[0]) && wcscmp(idxStr, L"@") != 0 && wcscmp(idxStr, L"*") != 0) {
                                    const wchar_t* v = self->Get(self, idxStr);
                                    if (v) resIdx = v;
                                }
                                if (!hasFirstIdx) {
                                    wcsncpy_s(firstIdx, _countof(firstIdx), resIdx, _TRUNCATE);
                                    hasFirstIdx = true;
                                }
                                wchar_t seg[136];
                                swprintf_s(seg, _countof(seg), L"[%ls]", resIdx);
                                wcscat_s(resolvedFullKey, _countof(resolvedFullKey), seg);
                                p = cb + 1;
                            } else if (*p == L'.') {
                                const wchar_t* nextB = wcschr(p, L'[');
                                size_t memLen = (nextB && nextB < endClose) ? (size_t)(nextB - p) : (size_t)(endClose - p);
                                wchar_t memStr[128];
                                if (memLen >= sizeof(memStr) / sizeof(memStr[0])) memLen = sizeof(memStr) / sizeof(memStr[0]) - 1;
                                wcsncpy_s(memStr, _countof(memStr), p, memLen);
                                wcscat_s(resolvedFullKey, _countof(resolvedFullKey), memStr);
                                p += memLen;
                            } else {
                                size_t curLen = wcslen(resolvedFullKey);
                                if (curLen + 1 < _countof(resolvedFullKey)) {
                                    resolvedFullKey[curLen] = *p;
                                    resolvedFullKey[curLen + 1] = L'\0';
                                }
                                p++;
                            }
                        }

                        const wchar_t* compVal = self->Get(self, resolvedFullKey);
                        ShellArray* array = Env_FindArray(self, baseName);

                        if (compVal) {
                            if (lengthExpansion) {
                                wchar_t lenText[32];
                                swprintf_s(lenText, _countof(lenText), L"%zu", wcslen(compVal));
                                AppendSubstValue(esc, quote, &out, &out_cap, &j, lenText, wcslen(lenText));
                            } else {
                                AppendSubstValue(esc, quote, &out, &out_cap, &j, compVal, wcslen(compVal));
                            }
                            i = close + 1;
                            continue;
                        } else if (array) {
                            wchar_t countText[32];
                            wchar_t* allocatedArrayValue = NULL;
                            const wchar_t* arrayValue = NULL;
                            if (lengthExpansion) {
                                size_t length;
                                if (wcscmp(firstIdx, L"@") == 0 || wcscmp(firstIdx, L"*") == 0) {
                                    length = array->count;
                                } else {
                                    const wchar_t* element = Env_ArrayGet(array, firstIdx);
                                    length = element ? wcslen(element) : 0;
                                }
                                swprintf_s(countText, sizeof(countText) / sizeof(countText[0]), L"%zu", length);
                                arrayValue = countText;
                            } else if (wcscmp(firstIdx, L"@") == 0 || wcscmp(firstIdx, L"*") == 0) {
                                allocatedArrayValue = Env_ArrayJoin(array);
                                arrayValue = allocatedArrayValue;
                            } else {
                                arrayValue = Env_ArrayGet(array, firstIdx);
                            }
                            if (arrayValue && !AppendSubstValue(esc, quote, &out, &out_cap, &j, arrayValue, wcslen(arrayValue))) {
                                free(allocatedArrayValue);
                                free(out);
                                return NULL;
                            }
                            free(allocatedArrayValue);
                            i = close + 1;
                            continue;
                        }
                    }

                    if (start < close) {
                        if (input[i + 1] == L'#' && i + 2 < close) {
                            size_t vlen = close - (i + 2);
                            if (vlen < 256) {
                                wchar_t vname[256];
                                memcpy(vname, input + i + 2, vlen * sizeof(wchar_t));
                                vname[vlen] = L'\0';
                                bool validIdent = true;
                                for (size_t k = 0; k < vlen; k++) {
                                    if (!iswalnum(vname[k]) && vname[k] != L'_') { validIdent = false; break; }
                                }
                                if (validIdent) {
                                    const wchar_t* val = self->Get(self, vname);
                                    wchar_t* allocated = val ? NULL : GetEnvironmentValue(vname);
                                    if (!val) val = allocated;
                                    size_t length = val ? wcslen(val) : 0;
                                    free(allocated);
                                    wchar_t lenBuf[32];
                                    swprintf_s(lenBuf, 32, L"%zu", length);
                                    if (!AppendSubstValue(esc, quote, &out, &out_cap, &j, lenBuf, wcslen(lenBuf))) {
                                        free(out);
                                        return NULL;
                                    }
                                    i = close + 1;
                                    continue;
                                }
                            }
                        }

                        size_t opPos = 0;
                        int opType = 0;
                        size_t opLen = 1;
                        for (size_t k = i + 1; k < close; k++) {
                            if (input[k] == L'/' && k > i + 1) {
                                if (input[k + 1] == L'/') {
                                    opType = 7; opPos = k; opLen = 2; break;
                                } else {
                                    opType = 8; opPos = k; opLen = 1; break;
                                }
                            } else if (input[k] == L':' && k + 1 < close) {
                                if (input[k + 1] == L'-') { opType = 1; opPos = k; opLen = 2; break; }
                                else if (input[k + 1] == L'=') { opType = 2; opPos = k; opLen = 2; break; }
                                else if (input[k + 1] == L'+') { opType = 3; opPos = k; opLen = 2; break; }
                                else if (iswdigit(input[k + 1]) || input[k + 1] == L' ' || input[k + 1] == L'-') {
                                    opType = 6; opPos = k; opLen = 1; break;
                                }
                            } else if (input[k] == L'#' && k > i + 1) {
                                if (k + 1 < close && input[k + 1] == L'#') {
                                    opType = 9; opPos = k; opLen = 2; break;
                                } else {
                                    opType = 4; opPos = k; opLen = 1; break;
                                }
                            } else if (input[k] == L'%' && k > i + 1) {
                                if (k + 1 < close && input[k + 1] == L'%') {
                                    opType = 10; opPos = k; opLen = 2; break;
                                } else {
                                    opType = 5; opPos = k; opLen = 1; break;
                                }
                            }
                        }

                        if (opType != 0) {
                            size_t vlen = opPos - (i + 1);
                            size_t arglen = close - (opPos + opLen);
                            if (vlen < 256 && arglen < MAX_LINE_LEN) {
                                wchar_t vname[256];
                                memcpy(vname, input + i + 1, vlen * sizeof(wchar_t));
                                vname[vlen] = L'\0';

                                wchar_t operand[MAX_LINE_LEN];
                                memcpy(operand, input + opPos + opLen, arglen * sizeof(wchar_t));
                                operand[arglen] = L'\0';

                                const wchar_t* val = self->Get(self, vname);
                                wchar_t* allocated = val ? NULL : GetEnvironmentValue(vname);
                                if (!val) val = allocated;

                                if (opType == 1) {
                                    const wchar_t* res = (val && *val) ? val : operand;
                                    AppendSubstValue(esc, quote, &out, &out_cap, &j, res, wcslen(res));
                                } else if (opType == 2) {
                                    const wchar_t* res = (val && *val) ? val : operand;
                                    if (!val || !*val) {
                                        self->Set(self, vname, operand);
                                    }
                                    AppendSubstValue(esc, quote, &out, &out_cap, &j, res, wcslen(res));
                                } else if (opType == 3) {
                                    if (val && *val) {
                                        AppendSubstValue(esc, quote, &out, &out_cap, &j, operand, wcslen(operand));
                                    }
                                } else if (opType == 4 || opType == 9) {
                                    /* 4 = shortest prefix (#), 9 = longest prefix (##) */
                                    if (val) {
                                        size_t vlen_s = wcslen(val);
                                        size_t trim_len = 0;
                                        bool found = false;
                                        if (opType == 4) {
                                            for (size_t p = 1; p <= vlen_s; p++) {
                                                wchar_t prefix[MAX_LINE_LEN];
                                                wcsncpy_s(prefix, _countof(prefix), val, p);
                                                if (CasePatternMatch(operand, prefix)) {
                                                    trim_len = p;
                                                    found = true;
                                                    break;
                                                }
                                            }
                                        } else {
                                            for (size_t p = vlen_s; p > 0; p--) {
                                                wchar_t prefix[MAX_LINE_LEN];
                                                wcsncpy_s(prefix, _countof(prefix), val, p);
                                                if (CasePatternMatch(operand, prefix)) {
                                                    trim_len = p;
                                                    found = true;
                                                    break;
                                                }
                                            }
                                        }
                                        if (found) {
                                            AppendSubstValue(esc, quote, &out, &out_cap, &j, val + trim_len, vlen_s - trim_len);
                                        } else {
                                            AppendSubstValue(esc, quote, &out, &out_cap, &j, val, vlen_s);
                                        }
                                    }
                                } else if (opType == 5 || opType == 10) {
                                    /* 5 = shortest suffix (%), 10 = longest suffix (%%) */
                                    if (val) {
                                        size_t vlen_s = wcslen(val);
                                        size_t cut_pos = vlen_s;
                                        bool found = false;
                                        if (opType == 5) {
                                            for (size_t p = vlen_s; p > 0; p--) {
                                                if (CasePatternMatch(operand, val + p)) {
                                                    cut_pos = p;
                                                    found = true;
                                                    break;
                                                }
                                            }
                                        } else {
                                            for (size_t p = 0; p < vlen_s; p++) {
                                                if (CasePatternMatch(operand, val + p)) {
                                                    cut_pos = p;
                                                    found = true;
                                                    break;
                                                }
                                            }
                                        }
                                        if (found) {
                                            AppendSubstValue(esc, quote, &out, &out_cap, &j, val, cut_pos);
                                        } else {
                                            AppendSubstValue(esc, quote, &out, &out_cap, &j, val, vlen_s);
                                        }
                                    }
                                } else if (opType == 6) {
                                    if (val) {
                                        size_t vlen_s = wcslen(val);
                                        long offset = wcstol(operand, NULL, 10);
                                        const wchar_t* col2 = wcschr(operand, L':');
                                        long sublen = -1;
                                        if (col2) sublen = wcstol(col2 + 1, NULL, 10);
                                        if (offset < 0) offset = (long)vlen_s + offset;
                                        if (offset < 0) offset = 0;
                                        if ((size_t)offset < vlen_s) {
                                            size_t avail = vlen_s - (size_t)offset;
                                            size_t take = (sublen >= 0 && (size_t)sublen < avail) ? (size_t)sublen : avail;
                                            AppendSubstValue(esc, quote, &out, &out_cap, &j, val + offset, take);
                                        }
                                    }
                                } else if (opType == 7 || opType == 8) {
                                    if (val) {
                                        wchar_t pat[MAX_LINE_LEN] = {0};
                                        const wchar_t* rep = L"";
                                        const wchar_t* slash = wcschr(operand, L'/');
                                        if (slash) {
                                            size_t plen = (size_t)(slash - operand);
                                            wcsncpy_s(pat, _countof(pat), operand, plen);
                                            rep = slash + 1;
                                        } else {
                                            wcsncpy_s(pat, _countof(pat), operand, _TRUNCATE);
                                        }
                                        size_t plen = wcslen(pat);
                                        size_t rlen = wcslen(rep);
                                        if (plen == 0) {
                                            AppendSubstValue(esc, quote, &out, &out_cap, &j, val, wcslen(val));
                                        } else {
                                            const wchar_t* vp = val;
                                            bool replaced = false;
                                            while (*vp) {
                                                if ((opType == 7 || !replaced) && wcsncmp(vp, pat, plen) == 0) {
                                                    AppendSubstValue(esc, quote, &out, &out_cap, &j, rep, rlen);
                                                    vp += plen;
                                                    replaced = true;
                                                } else {
                                                    AppendSubstValue(esc, quote, &out, &out_cap, &j, vp, 1);
                                                    vp++;
                                                }
                                            }
                                        }
                                    }
                                }
                                free(allocated);
                                i = close + 1;
                                continue;
                            }
                        }
                    }
                }
            }
            wchar_t var_name[256] = {0};
            size_t vn_len = 0;
            bool braced = (input[i] == L'{');
            if (braced) i++;

            while (i < in_len && (iswalnum(input[i]) || input[i] == L'_' || (braced && input[i] == L'.')) && vn_len < 255) {
                var_name[vn_len++] = input[i++];
            }
            if (braced && i < in_len && input[i] == L'}') i++;

            const wchar_t* val = NULL;
            bool positional = vn_len > 0;
            for (size_t digit = 0; digit < vn_len; ++digit) {
                if (!iswdigit(var_name[digit])) {
                    positional = false;
                    break;
                }
            }
            if (positional) {
                wchar_t* end;
                unsigned long index = wcstoul(var_name, &end, 10);
                if (end != var_name && !*end &&
                    index < (unsigned long)self->positionalCount)
                    val = self->positional[index];
            } else if (vn_len) {
                val = self->Get(self, var_name);
                ShellArray* array = Env_FindArray(self, var_name);
                if (array) val = Env_ArrayGet(array, L"0");
            } else {
                const wchar_t* literal = braced ? L"${" : L"$";
                size_t literalLength = braced ? 2 : 1;
                if (!AppendExpandedValue(&out, &out_cap, &j, literal,
                        literalLength)) {
                    free(out);
                    return NULL;
                }
                continue;
            }
            wchar_t* allocatedValue = val ? NULL :
                GetEnvironmentValue(var_name);
            if (!val && !positional) val = allocatedValue;
            if (val) {
                size_t vlen = wcslen(val);
                if (!AppendSubstValue(esc, quote, &out, &out_cap, &j, val, vlen)) {
                    free(allocatedValue);
                    free(out);
                    return NULL;
                }
            } else if (self->nounset) {
                fwprintf(stderr, L"ksh: %ls: parameter not set\n",
                    var_name);
                self->expansionError = true;
            }
            free(allocatedValue);
        } else if (input[i] == L'%' && self->batchMode) {
            i++;
            wchar_t var_name[256] = {0};
            size_t vn_len = 0;
            while (i < in_len && input[i] != L'%' && vn_len < 255) {
                var_name[vn_len++] = input[i++];
            }
            if (i < in_len && input[i] == L'%') i++;

            const wchar_t* val = self->Get(self, var_name);
            wchar_t* allocatedValue = val ? NULL :
                GetEnvironmentValue(var_name);
            if (!val) val = allocatedValue;
            if (val) {
                size_t vlen = wcslen(val);
                while (j + vlen >= out_cap) {
                    out_cap *= 2;
                    wchar_t* reout = (wchar_t*)realloc(out, out_cap * sizeof(wchar_t));
                    if (!reout) { free(out); return NULL; }
                    out = reout;
                }
                wcscpy_s(&out[j], out_cap - j, val);
                j += vlen;
            }
            free(allocatedValue);
        } else {
            if (j + 1 >= out_cap) {
                out_cap *= 2;
                wchar_t* reout = (wchar_t*)realloc(out, out_cap * sizeof(wchar_t));
                if (!reout) { free(out); return NULL; }
                out = reout;
            }
            out[j++] = input[i++];
        }
    }
    out[j] = L'\0';
    return out;
}

static void Env_Destroy(IEnvironment* self) {
    for (int i = 0; i < self->positionalCount; ++i)
        free(self->positional[i]);
    EnvEntry* curr = self->head;
    while (curr) {
        EnvEntry* next = curr->next;
        free(curr->key);
        free(curr->value);
        free(curr);
        curr = next;
    }
    ShellArray* array = self->arrays;
    while (array) {
        ShellArray* next = array->next;
        Env_ArrayClear(array);
        free(array->name);
        free(array);
        array = next;
    }
    free(self);
}

IEnvironment* CreateEnvironment(void) {
    IEnvironment* env = (IEnvironment*)calloc(1, sizeof(IEnvironment));
    if (!env) return NULL;
    env->head = NULL;
    env->Set = Env_Set;
    env->Get = Env_Get;
    env->Unset = Env_Unset;
    env->Expand = Env_Expand;
    env->Destroy = Env_Destroy;
    return env;
}

/* ========================================================================= */
/*                   OOP Interface: Command & Parser                         */
/* ========================================================================= */



