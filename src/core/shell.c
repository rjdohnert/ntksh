#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

ShellFunction* Shell_FindFunction(IShell* shell, const wchar_t* name) {
    for (ShellFunction* function = shell->functions; function;
        function = function->next) {
        if (wcscmp(function->name, name) == 0) return function;
    }
    return NULL;
}

bool Shell_HasFunction(IShell* shell, const wchar_t* name) {
    return Shell_FindFunction(shell, name) != NULL;
}

ShellAlias* Shell_FindAlias(IShell* shell, const wchar_t* name) {
    for (ShellAlias* alias = shell->aliases; alias; alias = alias->next) {
        if (wcscmp(alias->name, name) == 0) return alias;
    }
    return NULL;
}

bool Shell_SetAlias(IShell* shell, const wchar_t* name,
    const wchar_t* value) {
    ShellAlias* alias = Shell_FindAlias(shell, name);
    wchar_t* replacement = SafeStrDup(value);
    if (!replacement) return false;
    if (alias) {
        free(alias->value);
        alias->value = replacement;
        return true;
    }
    alias = (ShellAlias*)calloc(1, sizeof(ShellAlias));
    if (!alias) {
        free(replacement);
        return false;
    }
    alias->name = SafeStrDup(name);
    alias->value = replacement;
    if (!alias->name) {
        free(alias->value);
        free(alias);
        return false;
    }
    alias->next = shell->aliases;
    shell->aliases = alias;
    return true;
}

static bool Shell_AppendText(wchar_t** output, size_t* length,
    size_t* capacity, const wchar_t* text, size_t textLength) {
    if (textLength > SIZE_MAX - *length - 1) return false;
    size_t required = *length + textLength + 1;
    if (required > *capacity) {
        size_t nextCapacity = *capacity ? *capacity : 64;
        while (nextCapacity < required) {
            if (nextCapacity > SIZE_MAX / 2 / sizeof(wchar_t))
                return false;
            nextCapacity *= 2;
        }
        wchar_t* next = (wchar_t*)realloc(*output,
            nextCapacity * sizeof(wchar_t));
        if (!next) return false;
        *output = next;
        *capacity = nextCapacity;
    }
    memcpy(*output + *length, text, textLength * sizeof(wchar_t));
    *length += textLength;
    (*output)[*length] = L'\0';
    return true;
}

static wchar_t* Shell_ExpandAliasesInner(IShell* shell, const wchar_t* input,
    const wchar_t** chain, size_t chainCount) {
    if (chainCount >= 32) return NULL;
    wchar_t* output = NULL;
    size_t length = 0, capacity = 0, position = 0;
    bool commandStart = true;
    wchar_t quote = L'\0';
    while (input[position]) {
        wchar_t current = input[position];
        if (current == L'\\' && quote != L'\'') {
            size_t count = input[position + 1] ? 2 : 1;
            if (!Shell_AppendText(&output, &length, &capacity,
                    input + position, count))
                goto fail;
            position += count;
            if (commandStart && !iswspace(current)) commandStart = false;
            continue;
        }
        if (quote) {
            if (current == quote) quote = L'\0';
            if (!Shell_AppendText(&output, &length, &capacity,
                    input + position, 1))
                goto fail;
            position++;
            continue;
        }
        if (current == L'\'' || current == L'"') {
            quote = current;
            commandStart = false;
            if (!Shell_AppendText(&output, &length, &capacity,
                    input + position++, 1))
                goto fail;
            continue;
        }
        if (current == L'|' ) {
            commandStart = true;
            if (!Shell_AppendText(&output, &length, &capacity,
                    input + position++, 1))
                goto fail;
            continue;
        }
        if (current == L'>' || current == L'<') {
            commandStart = false;
            if (!Shell_AppendText(&output, &length, &capacity,
                    input + position++, 1))
                goto fail;
            continue;
        }
        if (iswspace(current)) {
            if (!Shell_AppendText(&output, &length, &capacity,
                    input + position++, 1))
                goto fail;
            continue;
        }
        if (commandStart) {
            size_t start = position;
            while (input[position] && !iswspace(input[position]) &&
                input[position] != L'|' && input[position] != L'>' &&
                input[position] != L'<')
                position++;
            size_t wordLength = position - start;
            wchar_t* word = (wchar_t*)malloc((wordLength + 1) *
                sizeof(wchar_t));
            if (!word) goto fail;
            memcpy(word, input + start, wordLength * sizeof(wchar_t));
            word[wordLength] = L'\0';
            ShellAlias* alias = Shell_FindAlias(shell, word);
            bool recursive = false;
            for (size_t i = 0; alias && i < chainCount; ++i) {
                if (wcscmp(chain[i], alias->name) == 0) {
                    recursive = true;
                    break;
                }
            }
            if (alias && !recursive) {
                const wchar_t* nextChain[32];
                for (size_t i = 0; i < chainCount; ++i)
                    nextChain[i] = chain[i];
                nextChain[chainCount] = alias->name;
                wchar_t* expanded = Shell_ExpandAliasesInner(shell,
                    alias->value, nextChain, chainCount + 1);
                free(word);
                if (!expanded || !Shell_AppendText(&output, &length,
                        &capacity, expanded, wcslen(expanded))) {
                    free(expanded);
                    goto fail;
                }
                free(expanded);
            } else {
                if (!Shell_AppendText(&output, &length, &capacity,
                        input + start, wordLength)) {
                    free(word);
                    goto fail;
                }
                free(word);
            }
            commandStart = false;
            continue;
        }
        if (!Shell_AppendText(&output, &length, &capacity,
                input + position++, 1))
            goto fail;
    }
    if (!output) output = SafeStrDup(L"");
    return output;

fail:
    free(output);
    return NULL;
}

wchar_t* Shell_ExpandAliases(IShell* shell, const wchar_t* input) {
    return Shell_ExpandAliasesInner(shell, input, NULL, 0);
}

bool Shell_AddLocal(IShell* shell, const wchar_t* name,
    const wchar_t* value) {
    LocalScope* scope = shell->localScope;
    if (!scope) return false;
    for (LocalBinding* binding = scope->bindings; binding;
        binding = binding->next) {
        if (_wcsicmp(binding->entry->key, name) == 0)
            return shell->env->Set(shell->env, name, value);
    }
    EnvEntry* previous = Env_FindEntry(shell->env, name);
    wchar_t* inherited = previous ? SafeStrDup(previous->value) :
        GetEnvironmentValue(name);
    if (!inherited) inherited = SafeStrDup(L"");
    EnvEntry* entry = (EnvEntry*)calloc(1, sizeof(EnvEntry));
    LocalBinding* binding = (LocalBinding*)malloc(sizeof(LocalBinding));
    if (!entry || !binding || !inherited) {
        free(entry);
        free(binding);
        free(inherited);
        return false;
    }
    entry->key = SafeStrDup(name);
    entry->value = previous ? Env_TransformValue(previous, inherited) :
        SafeStrDup(inherited);
    free(inherited);
    if (!entry->key || !entry->value) {
        free(entry->key);
        free(entry->value);
        free(entry);
        free(binding);
        return false;
    }
    if (previous) {
        entry->integer = previous->integer;
        entry->lowercase = previous->lowercase;
        entry->uppercase = previous->uppercase;
        entry->alignment = previous->alignment;
        entry->integerBase = previous->integerBase;
        entry->width = previous->width;
    } else {
        entry->alignment = L'R';
        entry->integerBase = 10;
    }
    entry->next = shell->env->head;
    shell->env->head = entry;
    binding->entry = entry;
    binding->next = scope->bindings;
    scope->bindings = binding;
    return shell->env->Set(shell->env, name, value);
}

static void Shell_PopLocalScope(IShell* shell, LocalScope* scope) {
    for (LocalBinding* binding = scope->bindings; binding;) {
        LocalBinding* nextBinding = binding->next;
        EnvEntry** link = &shell->env->head;
        while (*link && *link != binding->entry) link = &(*link)->next;
        if (*link) {
            EnvEntry* entry = *link;
            *link = entry->next;
            EnvEntry* outer = Env_FindEntry(shell->env, entry->key);
            (void)SetEnvironmentVariableW(entry->key,
                outer ? outer->value : NULL);
            free(entry->key);
            free(entry->value);
            free(entry);
        }
        free(binding);
        binding = nextBinding;
    }
}

bool Shell_SetFunction(IShell* shell, const wchar_t* name,
    const wchar_t* body) {
    ShellFunction* function = Shell_FindFunction(shell, name);
    wchar_t* replacement = SafeStrDup(body);
    if (!replacement) return false;
    if (function) {
        free(function->body);
        function->body = replacement;
        return true;
    }
    function = (ShellFunction*)calloc(1, sizeof(ShellFunction));
    if (!function) {
        free(replacement);
        return false;
    }
    function->name = SafeStrDup(name);
    function->body = replacement;
    if (!function->name) {
        free(function->body);
        free(function);
        return false;
    }
    function->next = shell->functions;
    shell->functions = function;
    return true;
}

bool Shell_UnsetFunction(IShell* shell, const wchar_t* name) {
    ShellFunction** link = &shell->functions;
    while (*link) {
        ShellFunction* function = *link;
        if (wcscmp(function->name, name) == 0) {
            *link = function->next;
            free(function->name);
            free(function->body);
            free(function);
            return true;
        }
        link = &function->next;
    }
    return false;
}

static bool Shell_ParseFunctionHeader(const wchar_t* line, wchar_t* name,
    size_t nameCapacity, size_t* bodyStart) {
    const wchar_t* p = line;
    while (iswspace(*p)) p++;
    bool keyword = false;
    if (wcsncmp(p, L"function", 8) == 0 && iswspace(p[8])) {
        p += 8;
        while (iswspace(*p)) p++;
        keyword = true;
    }
    const wchar_t* nameStart = p;
    if (!(iswalpha(*p) || *p == L'_')) return false;
    p++;
    while (iswalnum(*p) || *p == L'_' || *p == L'.') p++;
    size_t nameLength = (size_t)(p - nameStart);
    if (nameLength >= nameCapacity) return false;
    memcpy(name, nameStart, nameLength * sizeof(wchar_t));
    name[nameLength] = L'\0';
    while (iswspace(*p)) p++;
    if (*p == L'(') {
        p++;
        while (iswspace(*p)) p++;
        if (*p++ != L')') return false;
        while (iswspace(*p)) p++;
    } else if (!keyword) {
        return false;
    }
    if (*p++ != L'{') return false;
    *bodyStart = (size_t)(p - line);
    return true;
}

static bool Shell_AppendBody(wchar_t** body, size_t* length,
    size_t* capacity, wchar_t value) {
    if (*length + 1 >= *capacity) {
        size_t nextCapacity = *capacity ? *capacity * 2 : 256;
        if (nextCapacity <= *capacity ||
            nextCapacity > SIZE_MAX / sizeof(wchar_t))
            return false;
        wchar_t* next = (wchar_t*)realloc(*body,
            nextCapacity * sizeof(wchar_t));
        if (!next) return false;
        *body = next;
        *capacity = nextCapacity;
    }
    (*body)[(*length)++] = value;
    (*body)[*length] = L'\0';
    return true;
}

typedef struct ScriptHeredocs {
    wchar_t (*paths)[MAX_PATH];
    size_t count;
} ScriptHeredocs;

/* Finds a here-document operator (<< but not <<<) outside quotes and parentheses, so arithmetic
   shifts such as $(( a << 2 )) or (( x <<= 1 )) are not mistaken for here-documents. */
static wchar_t* Script_FindHeredocOp(wchar_t* line) {
    wchar_t quote = L'\0';
    int depth = 0;
    for (size_t i = 0; line[i]; i++) {
        wchar_t c = line[i];
        if (c == L'\\' && quote != L'\'' && line[i + 1]) { i++; continue; }
        if (quote) { if (c == quote) quote = L'\0'; continue; }
        if (c == L'\'' || c == L'"') { quote = c; continue; }
        if (c == L'(') { depth++; continue; }
        if (c == L')') { if (depth > 0) depth--; continue; }
        if (depth == 0 && c == L'<' && line[i + 1] == L'<') {
            if (line[i + 2] == L'<') { i += 2; continue; }
            return line + i;
        }
    }
    return NULL;
}

/* Replaces each "<< DELIM" in line with a redirection from a temp file holding the body read
   from fp. Unquoted delimiters register the file for expansion at execution time. */
static void Script_ProcessHeredocs(wchar_t* line, size_t cap, FILE* fp, ScriptHeredocs* st) {
    wchar_t* hd;
    while ((hd = Script_FindHeredocOp(line)) != NULL) {
        const wchar_t* dp = hd + 2;
        bool stripTabs = false;
        if (*dp == L'-') { stripTabs = true; dp++; }
        while (*dp && iswspace(*dp)) dp++;
        wchar_t delim[128] = {0};
        size_t dlen = 0;
        wchar_t dquote = L'\0';
        if (*dp == L'\'' || *dp == L'"') dquote = *dp++;
        while (*dp && (dquote ? (*dp != dquote) : (!iswspace(*dp) && *dp != L';' && *dp != L'|' && *dp != L'&' && *dp != L')')) && dlen < 127) {
            delim[dlen++] = *dp++;
        }
        delim[dlen] = L'\0';
        if (dquote && *dp == dquote) dp++;
        bool hdQuoted = dquote != L'\0';
        if (wcschr(delim, L'\\')) {
            /* <<\EOF quotes the delimiter: strip backslashes and disable expansion. */
            size_t w = 0;
            for (size_t r = 0; delim[r]; r++) if (delim[r] != L'\\') delim[w++] = delim[r];
            delim[w] = L'\0';
            hdQuoted = true;
        }
        if (!delim[0]) return;

        wchar_t tempFile[MAX_PATH];
        FILE* tf = NULL;
        if (!Ksh_CreateTempFile(L"khd", tempFile, MAX_PATH)) return;
        wchar_t (*grown)[MAX_PATH] = (wchar_t (*)[MAX_PATH])realloc(st->paths, (st->count + 1) * sizeof(*st->paths));
        if (!grown) { DeleteFileW(tempFile); return; }
        st->paths = grown;
        wcscpy_s(st->paths[st->count++], MAX_PATH, tempFile);
        if (!hdQuoted) Ksh_HeredocRegister(tempFile);
        if (_wfopen_s(&tf, tempFile, L"wb") != 0 || !tf) return;
        wchar_t hdLine[MAX_LINE_LEN];
        while (fgetws(hdLine, MAX_LINE_LEN, fp)) {
            size_t hlen = wcslen(hdLine);
            while (hlen > 0 && (hdLine[hlen - 1] == L'\n' || hdLine[hlen - 1] == L'\r')) hdLine[--hlen] = L'\0';
            const wchar_t* body = hdLine;
            if (stripTabs) while (*body == L'\t') body++;
            if (wcscmp(body, delim) == 0) break;
            char u8[MAX_LINE_LEN * 4];
            int u8len = WideCharToMultiByte(CP_UTF8, 0, body, -1, u8, sizeof(u8), NULL, NULL);
            if (u8len > 1) fwrite(u8, 1, (size_t)u8len - 1, tf);
            fwrite("\n", 1, 1, tf);
        }
        fclose(tf);

        size_t prefixLen = (size_t)(hd - line);
        size_t newCap = prefixLen + wcslen(tempFile) + wcslen(dp) + 8;
        wchar_t* newLine = (wchar_t*)malloc(newCap * sizeof(wchar_t));
        if (!newLine) return;
        swprintf_s(newLine, newCap, L"%.*ls< \"%ls\" %ls", (int)prefixLen, line, tempFile, dp);
        if (wcslen(newLine) < cap) wcscpy_s(line, cap, newLine);
        else { free(newLine); fwprintf(stderr, L"ksh: here-document line too long\n"); return; }
        free(newLine);
    }
}

/* Function bodies outlive the script that defines them (e.g. ksh.ini), so here-documents inside
   function definitions are kept until the shell exits. */
static ScriptHeredocs s_functionHeredocs;

static void Script_CleanupFunctionHeredocs(void) {
    for (size_t k = 0; k < s_functionHeredocs.count; k++) _wremove(s_functionHeredocs.paths[k]);
    free(s_functionHeredocs.paths);
    s_functionHeredocs.paths = NULL;
    s_functionHeredocs.count = 0;
}

static ScriptHeredocs* Script_FunctionHeredocs(void) {
    static bool registered;
    if (!registered) { atexit(Script_CleanupFunctionHeredocs); registered = true; }
    return &s_functionHeredocs;
}

static bool Shell_CollectFunctionBody(FILE* fp, wchar_t* line,
    size_t firstIndex, wchar_t** result, size_t* outEndIndex, ScriptHeredocs* heredocs) {
    wchar_t* body = NULL;
    size_t length = 0, capacity = 0;
    unsigned int depth = 1;
    wchar_t quote = L'\0';
    bool escaped = false;
    size_t index = firstIndex;
    for (;;) {
        wchar_t current = line[index];
        if (!current) {
            if (depth == 0) break;
            if (length && !Shell_AppendBody(&body, &length, &capacity, L'\n'))
                goto fail;
            if (!fgetws(line, MAX_LINE_LEN, fp)) goto fail;
            size_t lineLength = wcslen(line);
            while (lineLength && (line[lineLength - 1] == L'\n' ||
                line[lineLength - 1] == L'\r'))
                line[--lineLength] = L'\0';
            if (heredocs) Script_ProcessHeredocs(line, MAX_LINE_LEN, fp, heredocs);
            index = 0;
            continue;
        }
        if (escaped) {
            escaped = false;
            if (!Shell_AppendBody(&body, &length, &capacity, current))
                goto fail;
            index++;
            continue;
        }
        if (!quote && current == L'#' &&
            (index == 0 || iswspace(line[index - 1]))) {
            while (line[index]) {
                if (!Shell_AppendBody(&body, &length, &capacity,
                        line[index++]))
                    goto fail;
            }
            continue;
        }
        if (current == L'\\' && quote != L'\'') {
            escaped = true;
            if (!Shell_AppendBody(&body, &length, &capacity, current))
                goto fail;
            index++;
            continue;
        }
        if (quote) {
            if (current == quote) quote = L'\0';
            if (!Shell_AppendBody(&body, &length, &capacity, current))
                goto fail;
            index++;
            continue;
        }
        if (current == L'\'' || current == L'"') {
            quote = current;
            if (!Shell_AppendBody(&body, &length, &capacity, current))
                goto fail;
        } else if (current == L'{') {
            depth++;
            if (!Shell_AppendBody(&body, &length, &capacity, current))
                goto fail;
        } else if (current == L'}') {
            depth--;
            if (depth && !Shell_AppendBody(&body, &length, &capacity, current))
                goto fail;
        } else if (!Shell_AppendBody(&body, &length, &capacity, current)) {
            goto fail;
        }
        index++;
        if (!depth) {
            if (!body) {
                body = SafeStrDup(L"");
                if (!body) goto fail;
            }
            *result = body;
            if (outEndIndex) *outEndIndex = index;
            return true;
        }
    }
fail:
    free(body);
    return false;
}

ShellTypeDef* Shell_FindType(IShell* shell, const wchar_t* name) {
    if (!shell || !name) return NULL;
    for (ShellTypeDef* t = shell->types; t; t = t->next) {
        if (_wcsicmp(t->name, name) == 0) return t;
    }
    return NULL;
}

bool Shell_RegisterType(IShell* shell, const wchar_t* name, ShellTypeMember* members) {
    if (!shell || !name || !*name) return false;
    ShellTypeDef* t = Shell_FindType(shell, name);
    if (!t) {
        t = (ShellTypeDef*)calloc(1, sizeof(ShellTypeDef));
        if (!t) return false;
        wcsncpy_s(t->name, _countof(t->name), name, _TRUNCATE);
        t->next = shell->types;
        shell->types = t;
    }
    t->members = members;
    return true;
}

int Shell_InvokeFunction(IShell* shell, CommandAST* cmd) {
    ShellFunction* function = Shell_FindFunction(shell, cmd->argv[0]);
    if (!function || shell->functionDepth >= 128) {
        fwprintf(stderr, L"ksh: function call depth exceeded\n");
        return 2;
    }

    const wchar_t* funcFullName = cmd->argv[0];
    const wchar_t* lastDot = wcsrchr(funcFullName, L'.');
    wchar_t instanceName[128] = {0};
    wchar_t savedUnderscore[MAX_LINE_LEN] = {0};
    wchar_t savedShName[256] = {0};
    const wchar_t* oldShName = shell->env->Get(shell->env, L".sh.name");
    if (oldShName) wcsncpy_s(savedShName, _countof(savedShName), oldShName, _TRUNCATE);
    shell->env->Set(shell->env, L".sh.name", funcFullName);

    bool hasInstance = false;
    if (lastDot && lastDot > funcFullName) {
        size_t instLen = (size_t)(lastDot - funcFullName);
        if (instLen < _countof(instanceName)) {
            wcsncpy_s(instanceName, _countof(instanceName), funcFullName, instLen);
            hasInstance = true;
            const wchar_t* old_ = shell->env->Get(shell->env, L"_");
            if (old_) wcsncpy_s(savedUnderscore, _countof(savedUnderscore), old_, _TRUNCATE);
            shell->env->Set(shell->env, L"_", instanceName);
            size_t prefixLen = wcslen(instanceName);
            for (EnvEntry* e = shell->env->head; e; e = e->next) {
                if (_wcsnicmp(e->key, instanceName, prefixLen) == 0 && e->key[prefixLen] == L'.') {
                    wchar_t selfKey[256];
                    swprintf_s(selfKey, _countof(selfKey), L"_%ls", e->key + prefixLen);
                    shell->env->Set(shell->env, selfKey, e->value);
                }
            }
            wchar_t typeProp[256];
            swprintf_s(typeProp, _countof(typeProp), L"%ls._type", instanceName);
            const wchar_t* tVal = shell->env->Get(shell->env, typeProp);
            if (tVal) shell->env->Set(shell->env, L".sh.type", tVal);
            else shell->env->Unset(shell->env, L".sh.type");

            EnvEntry* selfEntry = NULL;
            EnvEntry* thisEntry = NULL;
            for (EnvEntry* e = shell->env->head; e; e = e->next) {
                if (_wcsicmp(e->key, L"self") == 0) selfEntry = e;
                if (_wcsicmp(e->key, L"this") == 0) thisEntry = e;
            }
            if (!selfEntry) {
                shell->env->Set(shell->env, L"self", instanceName);
                for (EnvEntry* e = shell->env->head; e; e = e->next) {
                    if (_wcsicmp(e->key, L"self") == 0) { e->is_nameref = true; break; }
                }
            } else {
                selfEntry->is_nameref = true;
                free(selfEntry->value);
                selfEntry->value = SafeStrDup(instanceName);
            }
            if (!thisEntry) {
                shell->env->Set(shell->env, L"this", instanceName);
                for (EnvEntry* e = shell->env->head; e; e = e->next) {
                    if (_wcsicmp(e->key, L"this") == 0) { e->is_nameref = true; break; }
                }
            } else {
                thisEntry->is_nameref = true;
                free(thisEntry->value);
                thisEntry->value = SafeStrDup(instanceName);
            }
        }
    }

    int savedCount = shell->env->positionalCount;
    wchar_t* saved[MAX_ARGS] = { 0 };
    for (int i = 0; i < savedCount; ++i) {
        saved[i] = SafeStrDup(shell->env->positional[i]);
        if (!saved[i]) {
            for (int j = 0; j < i; ++j) free(saved[j]);
            fwprintf(stderr, L"ksh: out of memory saving function parameters\n");
            return 1;
        }
    }
    bool previousReturn = shell->returnRequested;
    int previousReturnStatus = shell->returnStatus;
    if (!Env_SetPositional(shell->env, saved[0], cmd->argc - 1,
            cmd->argc > 1 ? &cmd->argv[1] : NULL)) {
        for (int i = 0; i < savedCount; ++i) free(saved[i]);
        fwprintf(stderr, L"ksh: cannot set function parameters\n");
        return 1;
    }
    LocalScope scope = { 0 };
    scope.parent = shell->localScope;
    shell->localScope = &scope;
    shell->returnRequested = false;
    shell->functionDepth++;
    const wchar_t* line = function->body;
    int status = shell->last_status;
    while (*line && shell->running && !shell->returnRequested) {
        const wchar_t* end = wcschr(line, L'\n');
        size_t length = end ? (size_t)(end - line) : wcslen(line);
        wchar_t* command = (wchar_t*)malloc((length + 1) * sizeof(wchar_t));
        if (!command) {
            status = 1;
            fwprintf(stderr, L"ksh: out of memory executing function\n");
            break;
        }
        memcpy(command, line, length * sizeof(wchar_t));
        command[length] = L'\0';
        status = shell->ExecuteLine(shell, command);
        free(command);
        if (!end) break;
        line = end + 1;
    }
    if (shell->returnRequested) status = shell->returnStatus;
    shell->functionDepth--;
    Shell_PopLocalScope(shell, &scope);
    shell->localScope = scope.parent;
    shell->returnRequested = previousReturn;
    shell->returnStatus = previousReturnStatus;
    (void)Env_SetPositional(shell->env, saved[0], savedCount - 1,
        savedCount > 1 ? &saved[1] : NULL);
    for (int i = 0; i < savedCount; ++i) free(saved[i]);

    if (hasInstance) {
        for (EnvEntry* e = shell->env->head; e; e = e->next) {
            if (_wcsnicmp(e->key, L"_.", 2) == 0) {
                wchar_t instKey[256];
                swprintf_s(instKey, _countof(instKey), L"%ls.%ls", instanceName, e->key + 2);
                shell->env->Set(shell->env, instKey, e->value);
            }
        }
        EnvEntry* e = shell->env->head;
        while (e) {
            EnvEntry* nxt = e->next;
            if (_wcsnicmp(e->key, L"_.", 2) == 0) shell->env->Unset(shell->env, e->key);
            e = nxt;
        }
        if (savedUnderscore[0]) shell->env->Set(shell->env, L"_", savedUnderscore);
        else shell->env->Unset(shell->env, L"_");
        shell->env->Unset(shell->env, L".sh.type");
        shell->env->Unset(shell->env, L"self");
        shell->env->Unset(shell->env, L"this");
    }

    if (savedShName[0]) shell->env->Set(shell->env, L".sh.name", savedShName);
    else shell->env->Unset(shell->env, L".sh.name");

    shell->last_status = status;
    shell->env->lastStatus = status;
    return status;
}

static bool MatchKeyword(const wchar_t* p, const wchar_t* kw, size_t* matchLen) {
    if (!p || !kw) return false;
    size_t len = wcslen(kw);
    if (wcsncmp(p, kw, len) == 0) {
        wchar_t next = p[len];
        if (next == L'\0' || iswspace(next) || next == L';' || next == L'\n' || next == L'\r' || next == L'#') {
            if (matchLen) *matchLen = len;
            return true;
        }
    }
    return false;
}

static bool FindCompoundKeyword(const wchar_t* src, const wchar_t** keywords, int numKeywords,
                                 const wchar_t** matchedKeyword, size_t* outPos) {
    if (!src) return false;
    size_t len = wcslen(src);
    wchar_t quote = L'\0';
    int ifDepth = 0;
    int loopDepth = 0;

    for (size_t i = 0; i < len; ++i) {
        wchar_t c = src[i];
        if (quote) {
            if (c == L'\\' && i + 1 < len && quote != L'\'') {
                i++;
                continue;
            }
            if (c == quote) quote = L'\0';
            continue;
        }
        if (c == L'\'' || c == L'"') {
            quote = c;
            continue;
        }
        if (c == L'`') {
            quote = L'`';
            continue;
        }

        bool atWordStart = (i == 0 || iswspace(src[i - 1]) || src[i - 1] == L';' || src[i - 1] == L'\n');
        if (!atWordStart) continue;

        if (MatchKeyword(src + i, L"if", NULL)) {
            ifDepth++;
            continue;
        }
        if (MatchKeyword(src + i, L"fi", NULL)) {
            if (ifDepth > 0) {
                ifDepth--;
                continue;
            }
        }
        if (MatchKeyword(src + i, L"for", NULL) ||
            MatchKeyword(src + i, L"while", NULL) ||
            MatchKeyword(src + i, L"until", NULL) ||
            MatchKeyword(src + i, L"case", NULL)) {
            loopDepth++;
            continue;
        }
        if (MatchKeyword(src + i, L"done", NULL) ||
            MatchKeyword(src + i, L"esac", NULL)) {
            if (loopDepth > 0) {
                loopDepth--;
                continue;
            }
        }

        if (ifDepth == 0 && loopDepth == 0) {
            for (int k = 0; k < numKeywords; ++k) {
                size_t kwLen = 0;
                if (MatchKeyword(src + i, keywords[k], &kwLen)) {
                    if (matchedKeyword) *matchedKeyword = keywords[k];
                    if (outPos) *outPos = i;
                    return true;
                }
            }
        }
    }
    return false;
}

static void TrimTrailingSemicolonAndSpace(wchar_t* str) {
    if (!str) return;
    size_t len = wcslen(str);
    while (len > 0 && (iswspace(str[len - 1]) || str[len - 1] == L';' || str[len - 1] == L'\n' || str[len - 1] == L'\r')) {
        str[--len] = L'\0';
    }
}

static int Shell_GlobSortCompare(const void* a, const void* b) {
    return _wcsicmp(*(const wchar_t**)a, *(const wchar_t**)b);
}

static bool Shell_HasWildcardPattern(const wchar_t* str) {
    if (!str) return false;
    for (const wchar_t* p = str; *p; p++) {
        if (*p == L'*' || *p == L'?') return true;
        if (*p == L'[' && wcschr(p + 1, L']')) return true;
        if ((*p == L'@' || *p == L'!' || *p == L'+' || *p == L'?' || *p == L'*') && p[1] == L'(') return true;
    }
    return false;
}

static void ForLoop_Execute(IShell* self, const wchar_t* varName, const wchar_t* itemsStr, const wchar_t* body, int* status) {
    *status = 0;
    if (!varName || !*varName || !body) return;

    if (!itemsStr) {
        for (int i = 1; i < self->env->positionalCount && self->running; ++i) {
            self->env->Set(self->env, varName, self->env->positional[i]);
            *status = self->ExecuteLine(self, body);
            if (self->loopBreak > 0) {
                self->loopBreak--;
                break;
            }
            if (self->loopContinue > 0) {
                self->loopContinue--;
                if (self->loopContinue > 0) break;
                continue;
            }
        }
        return;
    }

    wchar_t* expanded = self->env->Expand(self->env, itemsStr);
    if (!expanded) return;

    size_t inLen = wcslen(expanded);
    wchar_t token[MAX_LINE_LEN];
    size_t tIndex = 0;
    bool inQuote = false;
    wchar_t qChar = L'\0';
    bool tokenStarted = false;
    bool wasQuoted = false;

    for (size_t i = 0; i <= inLen && self->running; ++i) {
        wchar_t c = expanded[i];
        if (inQuote) {
            if (c == qChar) {
                inQuote = false;
            } else if (c == L'\\' && qChar == L'"' && i + 1 <= inLen) {
                i++;
                if (tIndex < MAX_LINE_LEN - 1) token[tIndex++] = expanded[i];
            } else {
                if (tIndex < MAX_LINE_LEN - 1) token[tIndex++] = c;
            }
            continue;
        }
        if (c == L'\'' || c == L'"') {
            inQuote = true;
            qChar = c;
            tokenStarted = true;
            wasQuoted = true;
            continue;
        }
        if (iswspace(c) || c == L'\0') {
            if (tokenStarted) {
                token[tIndex] = L'\0';
                bool hasWildcard = (!self->noglob && !wasQuoted && Shell_HasWildcardPattern(token));
                bool expandedGlob = false;
                if (hasWildcard) {
                    wchar_t dirPrefix[MAX_PATH] = {0};
                    const wchar_t* lastSlash = wcsrchr(token, L'\\');
                    if (!lastSlash) lastSlash = wcsrchr(token, L'/');
                    const wchar_t* patternLeaf = token;
                    if (lastSlash) {
                        size_t dlen = (size_t)(lastSlash - token + 1);
                        wcsncpy_s(dirPrefix, MAX_PATH, token, dlen);
                        patternLeaf = lastSlash + 1;
                    }
                    wchar_t searchQuery[MAX_PATH];
                    swprintf_s(searchQuery, MAX_PATH, L"%ls*", dirPrefix[0] ? dirPrefix : L"");
                    WIN32_FIND_DATAW wfd;
                    HANDLE hF = FindFirstFileW(searchQuery, &wfd);
                    if (hF != INVALID_HANDLE_VALUE) {
                        wchar_t* matches[MAX_ARGS];
                        int matchCount = 0;
                        do {
                            if (wcscmp(wfd.cFileName, L".") == 0 || wcscmp(wfd.cFileName, L"..") == 0) continue;
                            if (wfd.cFileName[0] == L'.' && patternLeaf[0] != L'.') continue;
                            if (CasePatternMatch(patternLeaf, wfd.cFileName)) {
                                wchar_t full[MAX_PATH];
                                swprintf_s(full, MAX_PATH, L"%ls%ls", dirPrefix, wfd.cFileName);
                                if (matchCount < MAX_ARGS) matches[matchCount++] = SafeStrDup(full);
                            }
                        } while (FindNextFileW(hF, &wfd));
                        FindClose(hF);
                        if (matchCount > 0) {
                            qsort(matches, matchCount, sizeof(wchar_t*), Shell_GlobSortCompare);
                            for (int m = 0; m < matchCount; m++) {
                                expandedGlob = true;
                                self->env->Set(self->env, varName, matches[m]);
                                *status = self->ExecuteLine(self, body);
                                free(matches[m]);
                                if (self->loopBreak > 0 || !self->running || g_interrupted) break;
                                if (self->loopContinue > 0) {
                                    self->loopContinue--;
                                    if (self->loopContinue > 0) break;
                                    continue;
                                }
                            }
                        }
                    }
                }
                if (!expandedGlob && self->running && !g_interrupted) {
                    self->env->Set(self->env, varName, token);
                    *status = self->ExecuteLine(self, body);
                    if (self->loopBreak > 0 || g_interrupted) {
                        if (self->loopBreak > 0) self->loopBreak--;
                        tIndex = 0;
                        tokenStarted = false;
                        wasQuoted = false;
                        break;
                    }
                    if (self->loopContinue > 0) {
                        self->loopContinue--;
                        if (self->loopContinue > 0) break;
                    }
                }
                tIndex = 0;
                tokenStarted = false;
                wasQuoted = false;
            }
            continue;
        }
        tokenStarted = true;
        if (tIndex < MAX_LINE_LEN - 1) token[tIndex++] = c;
    }

    free(expanded);
}

static void SelectLoop_Execute(IShell* self, const wchar_t* varName, const wchar_t* itemsStr, const wchar_t* body, int* status) {
    *status = 0;
    if (!varName || !*varName || !body) return;

    wchar_t* items[MAX_ARGS];
    int itemCount = 0;

    if (!itemsStr) {
        for (int i = 1; i < self->env->positionalCount && itemCount < MAX_ARGS; ++i) {
            items[itemCount++] = SafeStrDup(self->env->positional[i]);
        }
    } else {
        wchar_t* expanded = self->env->Expand(self->env, itemsStr);
        if (!expanded) return;
        size_t inLen = wcslen(expanded);
        wchar_t token[MAX_LINE_LEN];
        size_t tIndex = 0;
        bool inQuote = false;
        wchar_t qChar = L'\0';
        bool tokenStarted = false;
        for (size_t i = 0; i <= inLen; ++i) {
            wchar_t c = expanded[i];
            if (inQuote) {
                if (c == qChar) inQuote = false;
                else if (c == L'\\' && qChar == L'"' && i + 1 <= inLen) {
                    i++;
                    if (tIndex < MAX_LINE_LEN - 1) token[tIndex++] = expanded[i];
                } else {
                    if (tIndex < MAX_LINE_LEN - 1) token[tIndex++] = c;
                }
                continue;
            }
            if (c == L'\'' || c == L'"') {
                inQuote = true;
                qChar = c;
                tokenStarted = true;
                continue;
            }
            if (iswspace(c) || c == L'\0') {
                if (tokenStarted) {
                    token[tIndex] = L'\0';
                    if (itemCount < MAX_ARGS) {
                        items[itemCount++] = SafeStrDup(token);
                    }
                    tIndex = 0;
                    tokenStarted = false;
                }
                continue;
            }
            tokenStarted = true;
            if (tIndex < MAX_LINE_LEN - 1) token[tIndex++] = c;
        }
        free(expanded);
    }

    if (itemCount == 0) return;

    bool showMenu = true;
    while (self->running && !g_interrupted) {
        if (showMenu) {
            for (int i = 0; i < itemCount; ++i) {
                fwprintf(stderr, L"%d) %ls\n", i + 1, items[i]);
            }
        }
        showMenu = false;

        const wchar_t* ps3 = self->env->Get(self->env, L"PS3");
        if (!ps3 || !*ps3) ps3 = L"#? ";
        fputws(ps3, stderr);
        fflush(stderr);

        wchar_t inputLine[MAX_LINE_LEN];
        if (!fgetws(inputLine, _countof(inputLine), stdin)) {
            break;
        }

        size_t len = wcslen(inputLine);
        while (len > 0 && (inputLine[len - 1] == L'\n' || inputLine[len - 1] == L'\r')) {
            inputLine[--len] = L'\0';
        }

        if (len == 0) {
            showMenu = true;
            continue;
        }

        self->env->Set(self->env, L"REPLY", inputLine);

        wchar_t* endp = NULL;
        long sel = wcstol(inputLine, &endp, 10);
        if (endp != inputLine && sel >= 1 && sel <= itemCount) {
            self->env->Set(self->env, varName, items[sel - 1]);
        } else {
            self->env->Set(self->env, varName, L"");
        }

        *status = self->ExecuteLine(self, body);
        if (!self->running || g_interrupted) break;
        if (self->loopBreak > 0) {
            self->loopBreak--;
            break;
        }
        if (self->loopContinue > 0) {
            self->loopContinue--;
            if (self->loopContinue > 0) break;
        }
    }

    for (int i = 0; i < itemCount; ++i) {
        free(items[i]);
    }
}

static bool ExtglobHelper(wchar_t op, const wchar_t* subList, size_t subListLen, const wchar_t* rest, const wchar_t* text) {
    wchar_t alts[16][MAX_LINE_LEN];
    int altCount = 0;
    size_t aIdx = 0;
    int pDepth = 0;
    for (size_t i = 0; i < subListLen && altCount < 16; i++) {
        wchar_t c = subList[i];
        if (c == L'(') pDepth++;
        else if (c == L')' && pDepth > 0) pDepth--;
        if (c == L'|' && pDepth == 0) {
            alts[altCount][aIdx] = L'\0';
            altCount++;
            aIdx = 0;
            continue;
        }
        if (aIdx + 1 < MAX_LINE_LEN) alts[altCount][aIdx++] = c;
    }
    if (altCount < 16) {
        alts[altCount][aIdx] = L'\0';
        altCount++;
    }

    size_t tLen = wcslen(text);

    if (op == L'@') {
        for (int a = 0; a < altCount; a++) {
            for (size_t k = 0; k <= tLen; k++) {
                wchar_t pfx[MAX_LINE_LEN];
                if (k >= MAX_LINE_LEN) break;
                wcsncpy_s(pfx, _countof(pfx), text, k);
                if (CasePatternMatch(alts[a], pfx)) {
                    if (CasePatternMatch(rest, text + k)) return true;
                }
            }
        }
        return false;
    } else if (op == L'?') {
        if (CasePatternMatch(rest, text)) return true;
        for (int a = 0; a < altCount; a++) {
            for (size_t k = 1; k <= tLen; k++) {
                wchar_t pfx[MAX_LINE_LEN];
                if (k >= MAX_LINE_LEN) break;
                wcsncpy_s(pfx, _countof(pfx), text, k);
                if (CasePatternMatch(alts[a], pfx)) {
                    if (CasePatternMatch(rest, text + k)) return true;
                }
            }
        }
        return false;
    } else if (op == L'*' || op == L'+') {
        if (op == L'*' && CasePatternMatch(rest, text)) return true;
        for (int a = 0; a < altCount; a++) {
            for (size_t k = 1; k <= tLen; k++) {
                wchar_t pfx[MAX_LINE_LEN];
                if (k >= MAX_LINE_LEN) break;
                wcsncpy_s(pfx, _countof(pfx), text, k);
                if (CasePatternMatch(alts[a], pfx)) {
                    if (CasePatternMatch(rest, text + k)) return true;
                    if (ExtglobHelper(L'+', subList, subListLen, rest, text + k)) return true;
                }
            }
        }
        return false;
    } else if (op == L'!') {
        if (!*rest) {
            for (int a = 0; a < altCount; a++) {
                if (CasePatternMatch(alts[a], text)) return false;
            }
            return true;
        }
        for (size_t k = (tLen > 0 ? 1 : 0); k <= tLen; k++) {
            wchar_t pfx[MAX_LINE_LEN];
            if (k >= MAX_LINE_LEN) break;
            wcsncpy_s(pfx, _countof(pfx), text, k);
            bool anyMatch = false;
            for (int a = 0; a < altCount; a++) {
                if (CasePatternMatch(alts[a], pfx)) {
                    anyMatch = true;
                    break;
                }
            }
            if (!anyMatch) {
                if (CasePatternMatch(rest, text + k)) return true;
            }
        }
        return false;
    }
    return false;
}

static bool MatchCharClass(const wchar_t* cls, size_t clsLen, wchar_t ch) {
    if (clsLen == 5 && _wcsnicmp(cls, L"alpha", 5) == 0) return iswalpha(ch) != 0;
    if (clsLen == 5 && _wcsnicmp(cls, L"alnum", 5) == 0) return iswalnum(ch) != 0;
    if (clsLen == 5 && _wcsnicmp(cls, L"digit", 5) == 0) return iswdigit(ch) != 0;
    if (clsLen == 5 && _wcsnicmp(cls, L"space", 5) == 0) return iswspace(ch) != 0;
    if (clsLen == 5 && _wcsnicmp(cls, L"blank", 5) == 0) return (ch == L' ' || ch == L'\t');
    if (clsLen == 5 && _wcsnicmp(cls, L"lower", 5) == 0) return iswlower(ch) != 0;
    if (clsLen == 5 && _wcsnicmp(cls, L"upper", 5) == 0) return iswupper(ch) != 0;
    if (clsLen == 6 && _wcsnicmp(cls, L"xdigit", 6) == 0) return iswxdigit(ch) != 0;
    if (clsLen == 5 && _wcsnicmp(cls, L"punct", 5) == 0) return iswpunct(ch) != 0;
    if (clsLen == 5 && _wcsnicmp(cls, L"print", 5) == 0) return iswprint(ch) != 0;
    if (clsLen == 5 && _wcsnicmp(cls, L"cntrl", 5) == 0) return iswcntrl(ch) != 0;
    if (clsLen == 5 && _wcsnicmp(cls, L"graph", 5) == 0) return iswgraph(ch) != 0;
    return false;
}

bool CasePatternMatch(const wchar_t* pattern, const wchar_t* text) {
    if (!pattern || !text) return false;

    int topDepth = 0;
    for (const wchar_t* cp = pattern; *cp; cp++) {
        if (*cp == L'(') topDepth++;
        else if (*cp == L')' && topDepth > 0) topDepth--;
        else if (*cp == L'|' && topDepth == 0) {
            const wchar_t* altStart = pattern;
            topDepth = 0;
            for (const wchar_t* p = pattern; ; p++) {
                if (*p == L'(') topDepth++;
                else if (*p == L')' && topDepth > 0) topDepth--;
                if ((*p == L'|' && topDepth == 0) || *p == L'\0') {
                    size_t altLen = (size_t)(p - altStart);
                    wchar_t altBuf[MAX_LINE_LEN];
                    if (altLen < MAX_LINE_LEN) {
                        wcsncpy_s(altBuf, _countof(altBuf), altStart, altLen);
                        if (CasePatternMatch(altBuf, text)) return true;
                    }
                    if (*p == L'\0') break;
                    altStart = p + 1;
                }
            }
            return false;
        }
    }

    while (*pattern) {
        if ((*pattern == L'?' || *pattern == L'*' || *pattern == L'+' || *pattern == L'@' || *pattern == L'!') && pattern[1] == L'(') {
            wchar_t op = *pattern;
            const wchar_t* openParen = pattern + 1;
            const wchar_t* p = openParen + 1;
            int depth = 1;
            while (*p && depth > 0) {
                if (*p == L'(') depth++;
                else if (*p == L')') depth--;
                if (depth == 0) break;
                p++;
            }
            if (depth == 0) {
                size_t subLen = (size_t)(p - (openParen + 1));
                const wchar_t* rest = p + 1;
                return ExtglobHelper(op, openParen + 1, subLen, rest, text);
            }
        }

        if (*pattern == L'*') {
            pattern++;
            if (!*pattern) return true;
            while (*text) {
                if (CasePatternMatch(pattern, text)) return true;
                text++;
            }
            return false;
        } else if (*pattern == L'?') {
            if (!*text) return false;
            pattern++;
            text++;
        } else if (*pattern == L'[') {
            if (!*text) return false;
            const wchar_t* scan = pattern + 1;
            if (*scan == L'!' || *scan == L'^') scan++;
            if (*scan == L']') scan++;
            const wchar_t* closeB = NULL;
            while (*scan) {
                if (scan[0] == L'[' && scan[1] == L':') {
                    const wchar_t* endCls = wcsstr(scan + 2, L":]");
                    if (endCls) {
                        scan = endCls + 2;
                        continue;
                    }
                }
                if (*scan == L']') {
                    closeB = scan;
                    break;
                }
                scan++;
            }
            if (closeB) {
                bool negated = (pattern[1] == L'!' || pattern[1] == L'^');
                const wchar_t* cp = pattern + 1 + (negated ? 1 : 0);
                bool matchedChar = false;
                while (cp < closeB) {
                    if (cp[0] == L'[' && cp[1] == L':') {
                        const wchar_t* endCls = wcsstr(cp + 2, L":]");
                        if (endCls && endCls <= closeB) {
                            if (MatchCharClass(cp + 2, (size_t)(endCls - (cp + 2)), *text)) {
                                matchedChar = true;
                            }
                            cp = endCls + 2;
                            continue;
                        }
                    }
                    if (cp + 2 < closeB && cp[1] == L'-') {
                        if (*text >= cp[0] && *text <= cp[2]) { matchedChar = true; break; }
                        cp += 3;
                    } else {
                        if (*text == *cp) { matchedChar = true; break; }
                        cp++;
                    }
                }
                if (negated) matchedChar = !matchedChar;
                if (!matchedChar) return false;
                pattern = closeB + 1;
                text++;
            } else {
                if (*pattern != *text) return false;
                pattern++;
                text++;
            }
        } else {
            if (*pattern != *text) return false;
            pattern++;
            text++;
        }
    }
    return *text == L'\0';
}

static bool TryExecuteCompound(IShell* self, const wchar_t* line, int* status) {
    if (!self || !line) return false;
    const wchar_t* p = line;
    while (*p && iswspace(*p)) p++;
    if (!*p) return false;

    wchar_t fnName[128];
    size_t bodyStart = 0;
    if (Shell_ParseFunctionHeader(p, fnName, _countof(fnName), &bodyStart)) {
        const wchar_t* bp = p + bodyStart;
        int depth = 1;
        wchar_t q = L'\0';
        size_t bLen = 0;
        wchar_t fBody[MAX_LINE_LEN * 2];
        while (*bp && bLen + 1 < _countof(fBody)) {
            if (q) {
                if (*bp == L'\\' && bp[1] && q != L'\'') {
                    fBody[bLen++] = *bp++;
                } else if (*bp == q) q = L'\0';
                fBody[bLen++] = *bp++;
                continue;
            }
            if (*bp == L'\'' || *bp == L'"') { q = *bp; fBody[bLen++] = *bp++; continue; }
            if (*bp == L'{') depth++;
            else if (*bp == L'}') {
                depth--;
                if (depth == 0) break;
            }
            fBody[bLen++] = *bp++;
        }
        if (depth == 0) {
            fBody[bLen] = L'\0';
            TrimTrailingSemicolonAndSpace(fBody);
            Shell_SetFunction(self, fnName, fBody);
            *status = 0;
            return true;
        }
    }

    if (*p == L'(' && p[1] != L'(') {
        size_t len = wcslen(p);
        wchar_t quote = L'\0';
        int parenDepth = 1;
        size_t closeParen = 0;
        for (size_t i = 1; i < len; ++i) {
            wchar_t c = p[i];
            if (quote) {
                if (c == quote) quote = L'\0';
                continue;
            }
            if (c == L'\'' || c == L'"' || c == L'`') {
                quote = c;
                continue;
            }
            if (c == L'(') parenDepth++;
            else if (c == L')') {
                parenDepth--;
                if (parenDepth == 0) {
                    closeParen = i;
                    break;
                }
            }
        }
        if (closeParen > 1) {
            wchar_t subCmd[MAX_LINE_LEN * 2];
            size_t subLen = closeParen - 1;
            if (subLen >= sizeof(subCmd)/sizeof(wchar_t)) return false;
            wcsncpy_s(subCmd, _countof(subCmd), p + 1, subLen);
            TrimTrailingSemicolonAndSpace(subCmd);

            wchar_t savedDir[MAX_PATH];
            GetCurrentDirectoryW(MAX_PATH, savedDir);

            IShell* subshell = CreateShell();
            if (subshell) {
                for (EnvEntry* e = self->env->head; e; e = e->next) {
                    subshell->env->Set(subshell->env, e->key, e->value);
                }
                for (ShellFunction* fn = self->functions; fn; fn = fn->next) {
                    Shell_SetFunction(subshell, fn->name, fn->body ? fn->body : L"");
                }
                for (ShellAlias* al = self->aliases; al; al = al->next) {
                    Shell_SetAlias(subshell, al->name, al->value);
                }
                subshell->types = self->types;
                subshell->last_status = self->last_status;
                *status = subshell->ExecuteLine(subshell, subCmd);
                self->last_status = *status;
                self->env->lastStatus = *status;
                for (EnvEntry* e = subshell->env->head; e; e = e->next) {
                    const wchar_t* orig = self->env->Get(self->env, e->key);
                    SetEnvironmentVariableW(e->key, orig);
                }
                subshell->Destroy(subshell);
            }
            SetCurrentDirectoryW(savedDir);
            return true;
        }
    }

    if (MatchKeyword(p, L"{", NULL)) {
        p++;
        while (*p && iswspace(*p)) p++;
        const wchar_t* braceKw[] = { L"}" };
        const wchar_t* matched = NULL;
        size_t bracePos = 0;
        if (!FindCompoundKeyword(p, braceKw, 1, &matched, &bracePos)) return false;

        wchar_t groupCmd[MAX_LINE_LEN * 2];
        if (bracePos >= _countof(groupCmd)) return false;
        wcsncpy_s(groupCmd, _countof(groupCmd), p, bracePos);
        TrimTrailingSemicolonAndSpace(groupCmd);

        *status = self->ExecuteLine(self, groupCmd);
        return true;
    }

    if (MatchKeyword(p, L"case", NULL)) {
        p += 4;
        while (*p && iswspace(*p)) p++;
        const wchar_t* inKw[] = { L"in" };
        const wchar_t* matched = NULL;
        size_t inPos = 0;
        if (!FindCompoundKeyword(p, inKw, 1, &matched, &inPos)) return false;

        wchar_t wordStr[MAX_LINE_LEN];
        if (inPos >= MAX_LINE_LEN) return false;
        wcsncpy_s(wordStr, MAX_LINE_LEN, p, inPos);
        TrimTrailingSemicolonAndSpace(wordStr);

        wchar_t* evalWord = self->env->Expand(self->env, wordStr);
        if (!evalWord) evalWord = SafeStrDup(L"");
        size_t ewLen = wcslen(evalWord);
        if (ewLen >= 2 && ((evalWord[0] == L'"' && evalWord[ewLen - 1] == L'"') ||
                           (evalWord[0] == L'\'' && evalWord[ewLen - 1] == L'\''))) {
            evalWord[ewLen - 1] = L'\0';
            memmove(evalWord, evalWord + 1, ewLen * sizeof(wchar_t));
        }

        const wchar_t* afterIn = p + inPos + 2;
        while (*afterIn && (iswspace(*afterIn) || *afterIn == L';')) afterIn++;

        const wchar_t* esacKw[] = { L"esac" };
        size_t esacPos = 0;
        if (!FindCompoundKeyword(afterIn, esacKw, 1, &matched, &esacPos)) {
            free(evalWord);
            return false;
        }

        wchar_t clauses[MAX_LINE_LEN * 4];
        if (esacPos >= _countof(clauses)) {
            free(evalWord);
            return false;
        }
        wcsncpy_s(clauses, _countof(clauses), afterIn, esacPos);

        *status = 0;
        const wchar_t* cp = clauses;
        while (*cp && self->running) {
            while (*cp && (iswspace(*cp) || *cp == L';')) cp++;
            if (!*cp) break;

            if (*cp == L'(') cp++;
            while (*cp && iswspace(*cp)) cp++;

            const wchar_t* patStart = cp;
            int parenDepth = 0;
            while (*cp) {
                if (*cp == L'(') parenDepth++;
                else if (*cp == L')') {
                    if (parenDepth == 0) break;
                    parenDepth--;
                }
                cp++;
            }
            if (!*cp) break;
            size_t patLen = (size_t)(cp - patStart);
            wchar_t patBuf[MAX_LINE_LEN];
            if (patLen >= MAX_LINE_LEN) break;
            wcsncpy_s(patBuf, MAX_LINE_LEN, patStart, patLen);
            cp++;
            TrimTrailingSemicolonAndSpace(patBuf);

            const wchar_t* bodyStart = cp;
            while (*cp) {
                if (*cp == L';' && *(cp + 1) == L';') break;
                cp++;
            }
            size_t bodyLen = (size_t)(cp - bodyStart);
            wchar_t bodyBuf[MAX_LINE_LEN * 2];
            if (bodyLen >= _countof(bodyBuf)) break;
            wcsncpy_s(bodyBuf, _countof(bodyBuf), bodyStart, bodyLen);
            TrimTrailingSemicolonAndSpace(bodyBuf);
            if (*cp == L';' && *(cp + 1) == L';') cp += 2;

            bool matchedPattern = false;
            const wchar_t* pCur = patBuf;
            while (*pCur) {
                while (*pCur && iswspace(*pCur)) pCur++;
                if (!*pCur) break;
                const wchar_t* altStart = pCur;
                int pDepth = 0;
                while (*pCur) {
                    if (*pCur == L'(') pDepth++;
                    else if (*pCur == L')' && pDepth > 0) pDepth--;
                    else if (*pCur == L'|' && pDepth == 0) break;
                    pCur++;
                }
                size_t tlen = (size_t)(pCur - altStart);
                wchar_t token[MAX_LINE_LEN];
                if (tlen < MAX_LINE_LEN) {
                    wcsncpy_s(token, MAX_LINE_LEN, altStart, tlen);
                    while (tlen > 0 && iswspace(token[tlen - 1])) token[--tlen] = L'\0';
                    wchar_t* tPtr = token;
                    if (tlen >= 2 && ((tPtr[0] == L'"' && tPtr[tlen - 1] == L'"') ||
                                      (tPtr[0] == L'\'' && tPtr[tlen - 1] == L'\''))) {
                        tPtr[tlen - 1] = L'\0';
                        tPtr++;
                    }
                    if (CasePatternMatch(tPtr, evalWord)) {
                        matchedPattern = true;
                        break;
                    }
                }
                if (*pCur == L'|') pCur++;
            }

            if (matchedPattern) {
                *status = self->ExecuteLine(self, bodyBuf);
                break;
            }
        }

        free(evalWord);
        return true;
    }

    if (MatchKeyword(p, L"if", NULL)) {
        p += 2;
        while (*p && iswspace(*p)) p++;
        const wchar_t* thenKw[] = { L"then" };
        const wchar_t* matched = NULL;
        size_t thenPos = 0;
        if (!FindCompoundKeyword(p, thenKw, 1, &matched, &thenPos)) return false;

        wchar_t condStr[MAX_LINE_LEN];
        if (thenPos >= MAX_LINE_LEN) return false;
        wcsncpy_s(condStr, MAX_LINE_LEN, p, thenPos);
        TrimTrailingSemicolonAndSpace(condStr);

        const wchar_t* afterThen = p + thenPos + 4;
        while (*afterThen && (iswspace(*afterThen) || *afterThen == L';')) afterThen++;

        const wchar_t* nextKw[] = { L"elif", L"else", L"fi" };
        size_t nextPos = 0;
        if (!FindCompoundKeyword(afterThen, nextKw, 3, &matched, &nextPos)) return false;

        wchar_t thenBody[MAX_LINE_LEN * 2];
        if (nextPos >= _countof(thenBody)) return false;
        wcsncpy_s(thenBody, _countof(thenBody), afterThen, nextPos);
        TrimTrailingSemicolonAndSpace(thenBody);

        int condCode = self->ExecuteLine(self, condStr);
        if (condCode == 0) {
            *status = self->ExecuteLine(self, thenBody);
            return true;
        }

        if (wcscmp(matched, L"elif") == 0) {
            const wchar_t* elifPart = afterThen + nextPos;
            wchar_t elifCmd[MAX_LINE_LEN * 2];
            swprintf_s(elifCmd, _countof(elifCmd), L"if %ls", elifPart + 4);
            return TryExecuteCompound(self, elifCmd, status);
        } else if (wcscmp(matched, L"else") == 0) {
            const wchar_t* afterElse = afterThen + nextPos + 4;
            while (*afterElse && (iswspace(*afterElse) || *afterElse == L';')) afterElse++;
            const wchar_t* fiKw[] = { L"fi" };
            size_t fiPos = 0;
            if (!FindCompoundKeyword(afterElse, fiKw, 1, &matched, &fiPos)) return false;

            wchar_t elseBody[MAX_LINE_LEN * 2];
            if (fiPos >= _countof(elseBody)) return false;
            wcsncpy_s(elseBody, _countof(elseBody), afterElse, fiPos);
            TrimTrailingSemicolonAndSpace(elseBody);
            *status = self->ExecuteLine(self, elseBody);
            return true;
        } else {
            *status = condCode;
            return true;
        }
    }

    bool isWhile = MatchKeyword(p, L"while", NULL);
    bool isUntil = MatchKeyword(p, L"until", NULL);
    if (isWhile || isUntil) {
        p += 5;
        while (*p && iswspace(*p)) p++;
        const wchar_t* doKw[] = { L"do" };
        const wchar_t* matched = NULL;
        size_t doPos = 0;
        if (!FindCompoundKeyword(p, doKw, 1, &matched, &doPos)) return false;

        wchar_t condStr[MAX_LINE_LEN];
        if (doPos >= MAX_LINE_LEN) return false;
        wcsncpy_s(condStr, MAX_LINE_LEN, p, doPos);
        TrimTrailingSemicolonAndSpace(condStr);

        const wchar_t* afterDo = p + doPos + 2;
        while (*afterDo && (iswspace(*afterDo) || *afterDo == L';')) afterDo++;

        const wchar_t* doneKw[] = { L"done" };
        size_t donePos = 0;
        if (!FindCompoundKeyword(afterDo, doneKw, 1, &matched, &donePos)) return false;

        wchar_t bodyStr[MAX_LINE_LEN * 2];
        if (donePos >= _countof(bodyStr)) return false;
        wcsncpy_s(bodyStr, _countof(bodyStr), afterDo, donePos);
        TrimTrailingSemicolonAndSpace(bodyStr);

        *status = 0;
        while (self->running && !g_interrupted) {
            int condCode = self->ExecuteLine(self, condStr);
            if (isUntil ? (condCode == 0) : (condCode != 0)) break;
            *status = self->ExecuteLine(self, bodyStr);
            if (!self->running || g_interrupted) break;
            if (self->loopBreak > 0) {
                self->loopBreak--;
                break;
            }
            if (self->loopContinue > 0) {
                self->loopContinue--;
                if (self->loopContinue > 0) break;
                continue;
            }
        }
        if (g_interrupted) {
            *status = 130;
            self->last_status = 130;
        }
        return true;
    }

    if (MatchKeyword(p, L"for", NULL)) {
        p += 3;
        while (*p && iswspace(*p)) p++;

        if (*p == L'(' && p[1] == L'(') {
            p += 2;
            const wchar_t* closeParens = wcsstr(p, L"))");
            if (!closeParens) return false;
            size_t arithLen = (size_t)(closeParens - p);
            wchar_t arithBuf[MAX_LINE_LEN];
            if (arithLen >= MAX_LINE_LEN) return false;
            wcsncpy_s(arithBuf, MAX_LINE_LEN, p, arithLen);

            wchar_t exprInit[MAX_LINE_LEN] = {0};
            wchar_t exprCond[MAX_LINE_LEN] = {0};
            wchar_t exprStep[MAX_LINE_LEN] = {0};
            const wchar_t* semi1 = wcschr(arithBuf, L';');
            if (!semi1) return false;
            const wchar_t* semi2 = wcschr(semi1 + 1, L';');
            if (!semi2) return false;

            size_t l1 = (size_t)(semi1 - arithBuf);
            wcsncpy_s(exprInit, _countof(exprInit), arithBuf, l1);
            TrimTrailingSemicolonAndSpace(exprInit);

            size_t l2 = (size_t)(semi2 - (semi1 + 1));
            wcsncpy_s(exprCond, _countof(exprCond), semi1 + 1, l2);
            TrimTrailingSemicolonAndSpace(exprCond);

            wcsncpy_s(exprStep, _countof(exprStep), semi2 + 1, _TRUNCATE);
            TrimTrailingSemicolonAndSpace(exprStep);

            const wchar_t* afterParens = closeParens + 2;
            while (*afterParens && (iswspace(*afterParens) || *afterParens == L';')) afterParens++;

            const wchar_t* doKw[] = { L"do" };
            const wchar_t* matched = NULL;
            size_t doPos = 0;
            if (!FindCompoundKeyword(afterParens, doKw, 1, &matched, &doPos)) return false;

            const wchar_t* afterDo = afterParens + doPos + 2;
            while (*afterDo && (iswspace(*afterDo) || *afterDo == L';')) afterDo++;

            const wchar_t* doneKw[] = { L"done" };
            size_t donePos = 0;
            if (!FindCompoundKeyword(afterDo, doneKw, 1, &matched, &donePos)) return false;

            wchar_t bodyStr[MAX_LINE_LEN * 2];
            if (donePos >= _countof(bodyStr)) return false;
            wcsncpy_s(bodyStr, _countof(bodyStr), afterDo, donePos);
            TrimTrailingSemicolonAndSpace(bodyStr);

            *status = 0;
            long long dummy = 0;
            if (exprInit[0]) Batch_EvaluateArithmetic(self, NULL, exprInit, &dummy);

            while (self->running && !g_interrupted) {
                if (exprCond[0]) {
                    long long condVal = 0;
                    Batch_EvaluateArithmetic(self, NULL, exprCond, &condVal);
                    if (condVal == 0) break;
                }
                *status = self->ExecuteLine(self, bodyStr);
                if (!self->running || g_interrupted) break;
                if (self->loopBreak > 0) {
                    self->loopBreak--;
                    break;
                }
                if (self->loopContinue > 0) {
                    self->loopContinue--;
                    if (self->loopContinue > 0) break;
                }
                if (exprStep[0]) {
                    long long stepVal = 0;
                    Batch_EvaluateArithmetic(self, NULL, exprStep, &stepVal);
                }
            }
            if (g_interrupted) {
                *status = 130;
                self->last_status = 130;
            }
            return true;
        }

        wchar_t varName[128] = {0};
        size_t vIdx = 0;
        while (*p && !iswspace(*p) && *p != L';' && vIdx < 127) {
            varName[vIdx++] = *p++;
        }
        varName[vIdx] = L'\0';
        while (*p && iswspace(*p)) p++;

        wchar_t itemsStr[MAX_LINE_LEN] = {0};
        bool hasIn = false;
        if (MatchKeyword(p, L"in", NULL)) {
            hasIn = true;
            p += 2;
            while (*p && iswspace(*p)) p++;
        }

        const wchar_t* doKw[] = { L"do" };
        const wchar_t* matched = NULL;
        size_t doPos = 0;
        if (!FindCompoundKeyword(p, doKw, 1, &matched, &doPos)) return false;

        if (hasIn) {
            if (doPos >= MAX_LINE_LEN) return false;
            wcsncpy_s(itemsStr, MAX_LINE_LEN, p, doPos);
            TrimTrailingSemicolonAndSpace(itemsStr);
        }

        const wchar_t* afterDo = p + doPos + 2;
        while (*afterDo && (iswspace(*afterDo) || *afterDo == L';')) afterDo++;

        const wchar_t* doneKw[] = { L"done" };
        size_t donePos = 0;
        if (!FindCompoundKeyword(afterDo, doneKw, 1, &matched, &donePos)) return false;

        wchar_t bodyStr[MAX_LINE_LEN * 2];
        if (donePos >= _countof(bodyStr)) return false;
        wcsncpy_s(bodyStr, _countof(bodyStr), afterDo, donePos);
        TrimTrailingSemicolonAndSpace(bodyStr);

        ForLoop_Execute(self, varName, hasIn ? itemsStr : NULL, bodyStr, status);
        return true;
    }

    if (MatchKeyword(p, L"select", NULL)) {
        p += 6;
        while (*p && iswspace(*p)) p++;

        wchar_t varName[128] = {0};
        size_t vIdx = 0;
        while (*p && !iswspace(*p) && *p != L';' && vIdx < 127) {
            varName[vIdx++] = *p++;
        }
        varName[vIdx] = L'\0';
        while (*p && iswspace(*p)) p++;

        wchar_t itemsStr[MAX_LINE_LEN] = {0};
        bool hasIn = false;
        if (MatchKeyword(p, L"in", NULL)) {
            hasIn = true;
            p += 2;
            while (*p && iswspace(*p)) p++;
        }

        const wchar_t* doKw[] = { L"do" };
        const wchar_t* matched = NULL;
        size_t doPos = 0;
        if (!FindCompoundKeyword(p, doKw, 1, &matched, &doPos)) return false;

        if (hasIn) {
            if (doPos >= MAX_LINE_LEN) return false;
            wcsncpy_s(itemsStr, MAX_LINE_LEN, p, doPos);
            TrimTrailingSemicolonAndSpace(itemsStr);
        }

        const wchar_t* afterDo = p + doPos + 2;
        while (*afterDo && (iswspace(*afterDo) || *afterDo == L';')) afterDo++;

        const wchar_t* doneKw[] = { L"done" };
        size_t donePos = 0;
        if (!FindCompoundKeyword(afterDo, doneKw, 1, &matched, &donePos)) return false;

        wchar_t bodyStr[MAX_LINE_LEN * 2];
        if (donePos >= _countof(bodyStr)) return false;
        wcsncpy_s(bodyStr, _countof(bodyStr), afterDo, donePos);
        TrimTrailingSemicolonAndSpace(bodyStr);

        SelectLoop_Execute(self, varName, hasIn ? itemsStr : NULL, bodyStr, status);
        return true;
    }

    return false;
}

static void Shell_PrintXtracePrefix(IShell* self) {
    const wchar_t* ps4 = self->env->Get(self->env, L"PS4");
    if (!ps4) {
        fputws(L"+ ", stderr);
        return;
    }
    self->env->expansionError = false;
    wchar_t* expanded = self->env->Expand(self->env, ps4);
    fputws(expanded ? expanded : ps4, stderr);
    free(expanded);
}

static int ExecuteLineSegment(IShell* self, const wchar_t* line,
    size_t length) {
    Shell_DispatchPendingSignals(self);
    if (g_interrupted) {
        g_interrupted = 0;
        self->last_status = 130;
        if (self->env) self->env->lastStatus = 130;
        return 130;
    }
    while (length && iswspace(line[length - 1])) length--;
    while (length && iswspace(*line)) {
        line++;
        length--;
    }
    if (!length) return self->last_status;
    wchar_t* segment = (wchar_t*)malloc((length + 1) * sizeof(wchar_t));
    if (!segment) {
        fwprintf(stderr, L"ksh: out of memory parsing command list\n");
        return 1;
    }
    memcpy(segment, line, length * sizeof(wchar_t));
    segment[length] = L'\0';
    History_Add(segment);
    wchar_t* aliasExpanded = Shell_ExpandAliases(self, segment);
    free(segment);
    if (!aliasExpanded) {
        fwprintf(stderr, L"ksh: alias: expansion failed or recursion limit reached\n");
        return 2;
    }
    {
        /* ksh arithmetic command: (( expr )) — status 0 when expr is non-zero, 1 otherwise. */
        const wchar_t* a = aliasExpanded;
        while (iswspace(*a)) a++;
        size_t alen = wcslen(a);
        while (alen && iswspace(a[alen - 1])) alen--;
        if (alen >= 4 && a[0] == L'(' && a[1] == L'(' && a[alen - 1] == L')' && a[alen - 2] == L')') {
            int depth = 0;
            bool wholeCommand = true;
            for (size_t k = 2; k < alen - 2; k++) {
                if (a[k] == L'(') depth++;
                else if (a[k] == L')' && --depth < 0) { wholeCommand = false; break; }
            }
            if (wholeCommand && depth == 0) {
                wchar_t* inner = (wchar_t*)malloc((alen - 3) * sizeof(wchar_t));
                int arithStatus = 1;
                if (inner) {
                    wmemcpy(inner, a + 2, alen - 4);
                    inner[alen - 4] = L'\0';
                    self->env->expansionError = false;
                    wchar_t* expandedExpr = self->env->Expand(self->env, inner);
                    long long iv = 0;
                    double fv = 0.0;
                    bool isFloat = false;
                    if (self->xtrace) {
                        Shell_PrintXtracePrefix(self);
                        fwprintf(stderr, L"((%ls))\n", expandedExpr ? expandedExpr : inner);
                    }
                    if (!expandedExpr || self->env->expansionError) {
                        arithStatus = 1;
                    } else if (Ksh_EvaluateArithmetic(self, expandedExpr, &iv, &fv, &isFloat)) {
                        arithStatus = (isFloat ? fv != 0.0 : iv != 0) ? 0 : 1;
                    } else {
                        fwprintf(stderr, L"ksh: ((%ls)): arithmetic syntax error\n", inner);
                        arithStatus = 2;
                    }
                    free(expandedExpr);
                    free(inner);
                }
                free(aliasExpanded);
                self->last_status = arithStatus;
                self->env->lastStatus = arithStatus;
                return arithStatus;
            }
        }
    }
    {
        /* Check for pipeline feeding a compound block/loop: cmd1 | while ... */
        const wchar_t* p = aliasExpanded;
        wchar_t quote = L'\0';
        int pDepth = 0;
        int bDepth = 0;
        const wchar_t* pipePos = NULL;
        for (const wchar_t* s = p; *s; s++) {
            if (quote) {
                if (*s == quote && *(s - 1) != L'\\') quote = L'\0';
                continue;
            }
            if (*s == L'\'' || *s == L'"' || *s == L'`') { quote = *s; continue; }
            if (*s == L'(') { pDepth++; continue; }
            if (*s == L')') { if (pDepth > 0) pDepth--; continue; }
            if (*s == L'{') { bDepth++; continue; }
            if (*s == L'}') { if (bDepth > 0) bDepth--; continue; }
            if (pDepth == 0 && bDepth == 0 && *s == L'|' && *(s + 1) != L'|' && *(s + 1) != L'&') {
                const wchar_t* rhs = s + 1;
                while (*rhs && iswspace(*rhs)) rhs++;
                if (MatchKeyword(rhs, L"while", NULL) ||
                    MatchKeyword(rhs, L"until", NULL) ||
                    MatchKeyword(rhs, L"for", NULL) ||
                    MatchKeyword(rhs, L"select", NULL) ||
                    MatchKeyword(rhs, L"if", NULL) ||
                    MatchKeyword(rhs, L"{", NULL) ||
                    (*rhs == L'(' && rhs[1] != L'(')) {
                    pipePos = s;
                    break;
                }
            }
        }
        if (pipePos) {
            size_t lhsLen = (size_t)(pipePos - p);
            while (lhsLen > 0 && iswspace(p[lhsLen - 1])) lhsLen--;
            wchar_t lhsCmd[MAX_LINE_LEN * 2] = {0};
            if (lhsLen < _countof(lhsCmd)) {
                wcsncpy_s(lhsCmd, _countof(lhsCmd), p, lhsLen);
                lhsCmd[lhsLen] = L'\0';
            }
            const wchar_t* rhs = pipePos + 1;
            while (*rhs && iswspace(*rhs)) rhs++;

            SECURITY_ATTRIBUTES sa;
            memset(&sa, 0, sizeof(sa));
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;
            HANDLE hRead = NULL, hWrite = NULL;
            if (CreatePipe(&hRead, &hWrite, &sa, 1048576)) {
                SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, FALSE);
                SetHandleInformation(hWrite, HANDLE_FLAG_INHERIT, TRUE);

                fflush(stdout);
                int savedStdOut = _dup(_fileno(stdout));
                HANDLE savedStdOutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
                int writeFd = _open_osfhandle((intptr_t)hWrite, _O_WRONLY | _O_BINARY);
                if (writeFd >= 0) {
                    _dup2(writeFd, _fileno(stdout));
                    _setmode(_fileno(stdout), _O_U8TEXT);
                    SetStdHandle(STD_OUTPUT_HANDLE, hWrite);
                }

                self->ExecuteLine(self, lhsCmd);

                fflush(stdout);
                if (savedStdOut >= 0) {
                    _dup2(savedStdOut, _fileno(stdout));
                    _close(savedStdOut);
                    _setmode(_fileno(stdout), _O_U8TEXT);
                    SetStdHandle(STD_OUTPUT_HANDLE, savedStdOutHandle);
                }
                if (writeFd >= 0) {
                    _close(writeFd);
                } else {
                    CloseHandle(hWrite);
                }

                fflush(stdin);
                int savedStdIn = _dup(_fileno(stdin));
                HANDLE savedStdInHandle = GetStdHandle(STD_INPUT_HANDLE);
                int readFd = _open_osfhandle((intptr_t)hRead, _O_RDONLY | _O_BINARY);
                if (readFd >= 0) {
                    _dup2(readFd, _fileno(stdin));
                    _setmode(_fileno(stdin), _O_U8TEXT);
                    clearerr(stdin);
                    SetStdHandle(STD_INPUT_HANDLE, hRead);
                }

                int rhsStatus = 0;
                if (!TryExecuteCompound(self, rhs, &rhsStatus)) {
                    rhsStatus = self->ExecuteLine(self, rhs);
                }

                fflush(stdin);
                if (savedStdIn >= 0) {
                    _dup2(savedStdIn, _fileno(stdin));
                    _close(savedStdIn);
                    _setmode(_fileno(stdin), _O_U8TEXT);
                    clearerr(stdin);
                    SetStdHandle(STD_INPUT_HANDLE, savedStdInHandle);
                }
                if (readFd >= 0) {
                    _close(readFd);
                } else {
                    CloseHandle(hRead);
                }

                free(aliasExpanded);
                self->last_status = rhsStatus;
                self->env->lastStatus = rhsStatus;
                return rhsStatus;
            }
        }
    }

    int compoundStatus = 0;
    if (TryExecuteCompound(self, aliasExpanded, &compoundStatus)) {
        free(aliasExpanded);
        self->last_status = compoundStatus;
        self->env->lastStatus = compoundStatus;
        return compoundStatus;
    }
    self->env->expansionError = false;
    CommandAST* ast = self->parser->Parse(self->parser, aliasExpanded);
    free(aliasExpanded);
    if (!ast) {
        if (self->env->expansionError) {
            self->last_status = 1;
            self->env->lastStatus = 1;
            if (!self->interactive) self->running = false;
        }
        return self->last_status;
    }
    if (self->xtrace) {
        Shell_PrintXtracePrefix(self);
        bool firstWord = true;
        for (CommandAST* command = ast; command;
            command = command->next_in_pipeline) {
            for (int i = 0; i < command->argc; ++i) {
                fputws(firstWord ? L"" : L" ", stderr);
                fputws(command->argv[i], stderr);
                firstWord = false;
            }
        }
        fputws(L"\n", stderr);
    }
    int status = self->executor->Execute(self->executor, ast);
    FreeAST(ast);
    self->last_status = status;
    self->env->lastStatus = status;
    return status;
}

void Shell_ProcessProcSubstCleanup(IShell* shell) {
    if (!shell || !shell->env) return;
    ProcessSubstEntry* curr = shell->env->pendingProcSubst;
    shell->env->pendingProcSubst = NULL;
    while (curr) {
        ProcessSubstEntry* next = curr->next;
        if (curr->isOutput && curr->cmd[0]) {
            wchar_t runCmd[MAX_LINE_LEN * 2];
            swprintf_s(runCmd, _countof(runCmd), L"{ %ls; } < \"%ls\"", curr->cmd, curr->tempPath);
            shell->ExecuteLine(shell, runCmd);
        }
        DeleteFileW(curr->tempPath);
        free(curr);
        curr = next;
    }
}

static __declspec(thread) int s_executeLineDepth = 0;

static void Shell_StartCoprocess(IShell* self, const wchar_t* cmdStr) {
    if (!self || !cmdStr || !*cmdStr) return;
    if (self->coproc_active) {
        if (self->coproc_in_write) CloseHandle(self->coproc_in_write);
        if (self->coproc_out_read) CloseHandle(self->coproc_out_read);
        if (self->coproc_proc) CloseHandle(self->coproc_proc);
        self->coproc_in_write = NULL;
        self->coproc_out_read = NULL;
        self->coproc_proc = NULL;
        self->coproc_active = false;
    }

    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hChildInRead = NULL, hParentInWrite = NULL;
    HANDLE hParentOutRead = NULL, hChildOutWrite = NULL;

    if (!CreatePipe(&hChildInRead, &hParentInWrite, &sa, 0)) return;
    if (!CreatePipe(&hParentOutRead, &hChildOutWrite, &sa, 0)) {
        CloseHandle(hChildInRead);
        CloseHandle(hParentInWrite);
        return;
    }

    SetHandleInformation(hParentInWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hParentOutRead, HANDLE_FLAG_INHERIT, 0);

    wchar_t exePath[MAX_PATH * 4];
    DWORD exeLen = GetModuleFileNameW(NULL, exePath, (DWORD)_countof(exePath));
    if (exeLen == 0 || exeLen >= _countof(exePath)) {
        CloseHandle(hChildInRead); CloseHandle(hParentInWrite);
        CloseHandle(hParentOutRead); CloseHandle(hChildOutWrite);
        return;
    }

    wchar_t* cmdline = NULL;
    const wchar_t* pCmd = cmdStr;
    while (*pCmd && iswspace(*pCmd)) pCmd++;
    bool isSimpleExternal = false;
    if (iswalnum(*pCmd) || *pCmd == L'.' || *pCmd == L'/' || *pCmd == L'\\') {
        if (!wcspbrk(pCmd, L"|&;()<>{}'`\"")) {
            isSimpleExternal = true;
        }
    }

    if (isSimpleExternal) {
        CommandAST* ast = self->parser->Parse(self->parser, cmdStr);
        if (ast && ast->argc > 0) {
            cmdline = BuildWin32CommandLine(ast->argv, ast->argc);
        }
        if (ast) FreeAST(ast);
    }

    if (!cmdline) {
        wchar_t* childArgv[3] = { exePath, L"-c", (wchar_t*)cmdStr };
        cmdline = BuildWin32CommandLine(childArgv, 3);
    }

    if (!cmdline) {
        CloseHandle(hChildInRead); CloseHandle(hParentInWrite);
        CloseHandle(hParentOutRead); CloseHandle(hChildOutWrite);
        return;
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdInput = hChildInRead;
    si.hStdOutput = hChildOutWrite;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    ZeroMemory(&pi, sizeof(pi));

    if (CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        self->coproc_in_write = hParentInWrite;
        self->coproc_out_read = hParentOutRead;
        self->coproc_proc = pi.hProcess;
        self->coproc_pid = pi.dwProcessId;
        self->coproc_active = true;
        CloseHandle(pi.hThread);
        CloseHandle(hChildInRead);
        CloseHandle(hChildOutWrite);
    } else {
        CloseHandle(hChildInRead); CloseHandle(hParentInWrite);
        CloseHandle(hParentOutRead); CloseHandle(hChildOutWrite);
    }
    free(cmdline);
}

/* Runs a bare-& segment in a child copy of this shell so builtins and
   redirections work, and registers it as a job so $! is set. */
static void Shell_StartBackgroundSegment(IShell* self, const wchar_t* text, size_t len) {
    while (len && iswspace(*text)) { text++; len--; }
    while (len && iswspace(text[len - 1])) len--;
    if (!len) return;

    wchar_t* seg = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    if (!seg) return;
    memcpy(seg, text, len * sizeof(wchar_t));
    seg[len] = L'\0';

    wchar_t exePath[MAX_PATH * 4];
    DWORD exeLen = GetModuleFileNameW(NULL, exePath, (DWORD)_countof(exePath));
    if (exeLen == 0 || exeLen >= _countof(exePath)) {
        free(seg);
        return;
    }

    wchar_t* childArgv[3] = { exePath, L"-c", seg };
    wchar_t* cmdline = BuildWin32CommandLine(childArgv, 3);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (cmdline && CreateProcessW(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        int jid = Shell_AddJob(pi.dwProcessId, pi.hProcess, seg);
        /* Non-interactive shells stay silent, as POSIX requires. */
        if (self->interactive) wprintf(L"[%d] %lu\n", jid, pi.dwProcessId);
    }
    free(cmdline);
    free(seg);
}

int Shell_ExecuteLine(IShell* self, const wchar_t* line) {
    if (!self) return 1;
    if (!line) return 0;
    {
        /* Deep nesting (recursive functions, nested $(...)) would otherwise overflow the stack. */
        ULONG_PTR lo = 0, hi = 0;
        GetCurrentThreadStackLimits(&lo, &hi);
        if ((lo && (ULONG_PTR)&lo - lo < 64 * 1024) || s_executeLineDepth > 256) {
            fwprintf(stderr, L"ksh: recursion too deep\n");
            self->last_status = 1;
            return 1;
        }
    }
    s_executeLineDepth++;
    size_t length;
    size_t start = 0;
    wchar_t quote = L'\0';
    bool escaped = false;
    bool shouldRun = true;
    int compoundDepth = 0;
    int parenDepth = 0;
    int braceDepth = 0;
    length = wcslen(line);
    if (self && self->verbose && s_executeLineDepth == 1) {
        fwprintf(stderr, L"%ls\n", line);
    }

    for (size_t i = 0; i < length; ++i) {
        wchar_t current = line[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (current == L'\\' && quote != L'\'') {
            if (line[i + 1] == L';' || line[i + 1] == L'&' || line[i + 1] == L'|') {
                bool is_path_bs = false;
                if (i > 0 && (line[i - 1] == L':' || line[i - 1] == L'\\' || line[i - 1] == L'/' || iswalnum(line[i - 1]))) {
                    is_path_bs = true;
                } else if (i >= 3 && _wcsnicmp(line + i - 3, L"cd ", 3) == 0) {
                    is_path_bs = true;
                }
                if (is_path_bs) {
                    continue;
                }
            }
            escaped = true;
            continue;
        }
        if (quote) {
            if (current == quote) quote = L'\0';
            continue;
        }
        if (current == L'\'' || current == L'"') {
            quote = current;
            continue;
        }

        if (current == L'(') parenDepth++;
        else if (current == L')') { if (parenDepth > 0) parenDepth--; }

        bool atWordStart = (i == 0 || iswspace(line[i - 1]) || line[i - 1] == L';' || line[i - 1] == L'\n');
        if (atWordStart) {
            if (current == L'{' && (line[i + 1] == L'\0' || iswspace(line[i + 1]))) braceDepth++;
            else if (current == L'}' && (line[i + 1] == L'\0' || iswspace(line[i + 1]) || line[i + 1] == L';')) {
                if (braceDepth > 0) braceDepth--;
            }
            if (MatchKeyword(line + i, L"if", NULL) ||
                MatchKeyword(line + i, L"for", NULL) ||
                MatchKeyword(line + i, L"select", NULL) ||
                MatchKeyword(line + i, L"while", NULL) ||
                MatchKeyword(line + i, L"until", NULL) ||
                MatchKeyword(line + i, L"case", NULL) ||
                MatchKeyword(line + i, L"[[", NULL)) {
                compoundDepth++;
            } else if (MatchKeyword(line + i, L"fi", NULL) ||
                       MatchKeyword(line + i, L"done", NULL) ||
                       MatchKeyword(line + i, L"esac", NULL) ||
                       MatchKeyword(line + i, L"]]", NULL)) {
                if (compoundDepth > 0) compoundDepth--;
            }
        }
        if (compoundDepth > 0 || parenDepth > 0 || braceDepth > 0) {
            continue;
        }

        size_t operatorLength = 0;
        wchar_t operation = L'\0';
        if (current == L';' || current == L'\n') {
            operatorLength = 1;
            operation = L';';
        } else if ((current == L'&' || current == L'|') &&
            line[i + 1] == current) {
            operatorLength = 2;
            operation = current;
        } else if (current == L'|' && line[i + 1] == L'&') {
            operatorLength = 2;
            operation = L'p';
        } else if (current == L'&' && line[i + 1] != L'>' &&
            (i == 0 || (line[i - 1] != L'>' && line[i - 1] != L'<'))) {
            /* Bare & (not &&, >&, <&, &>) runs the preceding list in the background. */
            operatorLength = 1;
            operation = L'b';
        }
        if (!operatorLength) continue;

        if (shouldRun) {
            if (operation == L'p') {
                size_t segLen = i - start;
                wchar_t* seg = (wchar_t*)malloc((segLen + 1) * sizeof(wchar_t));
                if (seg) {
                    memcpy(seg, line + start, segLen * sizeof(wchar_t));
                    seg[segLen] = L'\0';
                    Shell_StartCoprocess(self, seg);
                    free(seg);
                }
                shouldRun = true;
                i += operatorLength - 1;
                start = i + 1;
                continue;
            }
            if (operation == L'b') {
                Shell_StartBackgroundSegment(self, line + start, i - start);
                self->last_status = 0;
                shouldRun = true;
                i += operatorLength - 1;
                start = i + 1;
                continue;
            }
            int status = ExecuteLineSegment(self, line + start, i - start);
            if (!self->running || self->loopBreak > 0 || self->loopContinue > 0) {
                s_executeLineDepth--;
                if (s_executeLineDepth == 0) Shell_ProcessProcSubstCleanup(self);
                return self->last_status;
            }
            if (self->errexit && status != 0 && operation == L';') {
                self->running = false;
                s_executeLineDepth--;
                if (s_executeLineDepth == 0) Shell_ProcessProcSubstCleanup(self);
                return status;
            }
        }
        if (operation == L';' || operation == L'b') {
            shouldRun = true;
        } else if (operation == L'&') {
            shouldRun = self->last_status == 0;
        } else {
            shouldRun = self->last_status != 0;
        }
        i += operatorLength - 1;
        start = i + 1;
    }
    if (shouldRun && self->running && self->loopBreak == 0 && self->loopContinue == 0) {
        size_t remLen = length - start;
        const wchar_t* rem = line + start;
        while (remLen && iswspace(rem[remLen - 1])) remLen--;
        if (remLen >= 2 && rem[remLen - 2] == L'|' && rem[remLen - 1] == L'&') {
            size_t segLen = remLen - 2;
            wchar_t* seg = (wchar_t*)malloc((segLen + 1) * sizeof(wchar_t));
            if (seg) {
                memcpy(seg, rem, segLen * sizeof(wchar_t));
                seg[segLen] = L'\0';
                Shell_StartCoprocess(self, seg);
                free(seg);
            }
        } else {
            int status = ExecuteLineSegment(self, line + start, length - start);
            if (self->errexit && status != 0) self->running = false;
        }
    }
    s_executeLineDepth--;
    if (s_executeLineDepth == 0) {
        Shell_ProcessProcSubstCleanup(self);
        Shell_UpdateJobsNotify(self);
    }
    return self->last_status;
}

typedef struct CaptureReaderThreadArg {
    HANDLE hPipe;
    char* outputA;
    size_t totalBytes;
} CaptureReaderThreadArg;

static DWORD WINAPI CaptureReaderThread(LPVOID lpParam) {
    CaptureReaderThreadArg* arg = (CaptureReaderThreadArg*)lpParam;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (ReadFile(arg->hPipe, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
        if (arg->totalBytes >= 64 * 1024 * 1024) break;
        char* newOutput = (char*)realloc(arg->outputA, arg->totalBytes + bytesRead + 1);
        if (!newOutput) break;
        arg->outputA = newOutput;
        memcpy(arg->outputA + arg->totalBytes, buffer, bytesRead);
        arg->totalBytes += bytesRead;
        arg->outputA[arg->totalBytes] = '\0';
    }
    return 0;
}

wchar_t* Shell_CaptureCommandOutput(struct IShell* shell, const wchar_t* command) {
    if (!shell || !command || !*command) return SafeStrDup(L"");

    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 1048576)) {
        return SafeStrDup(L"");
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    fflush(stdout);
    int savedStdOutFd = _dup(_fileno(stdout));
    HANDLE savedStdOutHandle = GetStdHandle(STD_OUTPUT_HANDLE);

    HANDLE hWriteForFd = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), hWritePipe,
                         GetCurrentProcess(), &hWriteForFd,
                         0, TRUE, DUPLICATE_SAME_ACCESS)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        if (savedStdOutFd >= 0) _close(savedStdOutFd);
        return SafeStrDup(L"");
    }

    SetStdHandle(STD_OUTPUT_HANDLE, hWritePipe);
    int writeFd = _open_osfhandle((intptr_t)hWriteForFd, 0);
    if (writeFd >= 0) {
        _dup2(writeFd, _fileno(stdout));
        _close(writeFd);
        _setmode(_fileno(stdout), _O_U8TEXT);
    }

    CaptureReaderThreadArg readerArg = { hReadPipe, NULL, 0 };
    HANDLE hReaderThread = CreateThread(NULL, 0, CaptureReaderThread, &readerArg, 0, NULL);

    shell->ExecuteLine(shell, command);
    fflush(stdout);

    if (savedStdOutFd >= 0) {
        _dup2(savedStdOutFd, _fileno(stdout));
        _setmode(_fileno(stdout), _O_U8TEXT);
        _close(savedStdOutFd);
    }
    CloseHandle(hWritePipe);
    SetStdHandle(STD_OUTPUT_HANDLE, savedStdOutHandle);

    if (hReaderThread) {
        WaitForSingleObject(hReaderThread, INFINITE);
        CloseHandle(hReaderThread);
    }
    CloseHandle(hReadPipe);

    char* outputA = readerArg.outputA;
    size_t totalBytes = readerArg.totalBytes;

    if (!outputA || totalBytes == 0) {
        free(outputA);
        return SafeStrDup(L"");
    }

    int wideCount = MultiByteToWideChar(CP_UTF8, 0, outputA, (int)totalBytes, NULL, 0);
    wchar_t* wideStr = NULL;
    if (wideCount > 0) {
        wideStr = (wchar_t*)malloc((wideCount + 1) * sizeof(wchar_t));
        if (wideStr) {
            MultiByteToWideChar(CP_UTF8, 0, outputA, (int)totalBytes, wideStr, wideCount);
            wideStr[wideCount] = L'\0';
        }
    } else {
        wideCount = MultiByteToWideChar(CP_ACP, 0, outputA, (int)totalBytes, NULL, 0);
        if (wideCount > 0) {
            wideStr = (wchar_t*)malloc((wideCount + 1) * sizeof(wchar_t));
            if (wideStr) {
                MultiByteToWideChar(CP_ACP, 0, outputA, (int)totalBytes, wideStr, wideCount);
                wideStr[wideCount] = L'\0';
            }
        }
    }
    free(outputA);

    if (!wideStr) return SafeStrDup(L"");

    size_t len = wcslen(wideStr);
    while (len > 0 && (wideStr[len - 1] == L'\n' || wideStr[len - 1] == L'\r')) {
        wideStr[--len] = L'\0';
    }

    return wideStr;
}



bool IsBatchFileName(const wchar_t* path) {
    if (!path) return false;
    size_t length = wcslen(path);
    if (length < 4) return false;
    const wchar_t* extension = path + length - 4;
    return _wcsicmp(extension, L".bat") == 0 ||
        _wcsicmp(extension, L".cmd") == 0;
}



int Shell_RunScript(IShell* self, const wchar_t* filename) {
    if (IsBatchFileName(filename)) {
        int argumentCount = self->env->positionalCount > 1 ?
            self->env->positionalCount - 1 : 0;
        return Shell_RunBatch(self, filename, argumentCount,
            argumentCount ? &self->env->positional[1] : NULL);
    }
    FILE* fp = NULL;
    if ((_wfopen_s(&fp, filename, L"r, ccs=UTF-8") != 0 || !fp) &&
        (_wfopen_s(&fp, filename, L"r") != 0 || !fp)) {
        wprintf(L"ksh: cannot open script: %ls\n", filename);
        return 1;
    }

    wchar_t line[MAX_LINE_LEN * 4];
    int status = 0;
    bool first_line = true;
    size_t lineNo = 0;
    /* Heredoc temp files may be used by compound commands spanning later lines, so delete at the end. */
    ScriptHeredocs heredocs = { NULL, 0 };

    while (fgetws(line, sizeof(line) / sizeof(wchar_t), fp)) {
        lineNo++;
        /* Strip line breaks */
        size_t slen = wcslen(line);
        while (slen > 0 && (line[slen - 1] == L'\n' || line[slen - 1] == L'\r')) {
            line[--slen] = L'\0';
        }

        /* Check POSIX/Ksh Shebang on first line */
        if (first_line) {
            first_line = false;
            if (line[0] == 0xFEFF) {
                memmove(line, line + 1, (wcslen(line + 1) + 1) * sizeof(wchar_t));
            }
            if (wcsncmp(line, L"#!", 2) == 0) {
                const wchar_t* interp = line + 2;
                while (*interp && iswspace(*interp)) interp++;
                if (wcsstr(interp, L"/usr/bin/env ") || wcsstr(interp, L"env ")) {
                    const wchar_t* cmdName = wcsstr(interp, L"env ") + 4;
                    while (*cmdName && iswspace(*cmdName)) cmdName++;
                    if (_wcsnicmp(cmdName, L"sh", 2) != 0 &&
                        _wcsnicmp(cmdName, L"bash", 4) != 0 &&
                        _wcsnicmp(cmdName, L"ksh", 3) != 0) {
                        wchar_t execBuf[MAX_LINE_LEN];
                        swprintf_s(execBuf, _countof(execBuf), L"%ls \"%ls\"", cmdName, filename);
                        fclose(fp);
                        return self->ExecuteLine(self, execBuf);
                    }
                } else if (!wcsstr(interp, L"sh") && !wcsstr(interp, L"ksh") && !wcsstr(interp, L"bash")) {
                    const wchar_t* baseInterp = wcsrchr(interp, L'/');
                    if (!baseInterp) baseInterp = wcsrchr(interp, L'\\');
                    baseInterp = baseInterp ? baseInterp + 1 : interp;
                    wchar_t interpName[128];
                    wcsncpy_s(interpName, _countof(interpName), baseInterp, _TRUNCATE);
                    wchar_t* space = wcschr(interpName, L' ');
                    if (space) *space = L'\0';
                    if (*interpName) {
                        wchar_t execBuf[MAX_LINE_LEN];
                        swprintf_s(execBuf, _countof(execBuf), L"%ls \"%ls\"", interpName, filename);
                        fclose(fp);
                        return self->ExecuteLine(self, execBuf);
                    }
                }
                continue;
            }
        }

        const wchar_t* pTrim = line;
        while (*pTrim && iswspace(*pTrim)) pTrim++;
        if (!*pTrim || *pTrim == L'#') continue;

        wchar_t lineNoText[32];
        swprintf_s(lineNoText, _countof(lineNoText), L"%zu", lineNo);
        self->env->Set(self->env, L"LINENO", lineNoText);
        /* Check for here-document << DELIM */
        Script_ProcessHeredocs(line, sizeof(line) / sizeof(wchar_t), fp, &heredocs);

        /* Check for multiline compound command */
        int scriptCompoundDepth = 0;
        int scriptParenDepth = 0;
        wchar_t sQuote = L'\0';
        size_t curLen = wcslen(line);
        for (size_t i = 0; i < curLen; ++i) {
            if (sQuote) {
                if (line[i] == sQuote) sQuote = L'\0';
                continue;
            }
            if (line[i] == L'\'' || line[i] == L'"') {
                sQuote = line[i];
                continue;
            }
            if (line[i] == L'(') scriptParenDepth++;
            else if (line[i] == L')') { if (scriptParenDepth > 0) scriptParenDepth--; }
            bool atStart = (i == 0 || iswspace(line[i - 1]) || line[i - 1] == L';');
            if (atStart) {
                if (MatchKeyword(line + i, L"if", NULL) ||
                    MatchKeyword(line + i, L"for", NULL) ||
                    MatchKeyword(line + i, L"select", NULL) ||
                    MatchKeyword(line + i, L"while", NULL) ||
                    MatchKeyword(line + i, L"until", NULL) ||
                    MatchKeyword(line + i, L"case", NULL)) scriptCompoundDepth++;
                else if (MatchKeyword(line + i, L"fi", NULL) ||
                         MatchKeyword(line + i, L"done", NULL) ||
                         MatchKeyword(line + i, L"esac", NULL)) {
                    if (scriptCompoundDepth > 0) scriptCompoundDepth--;
                }
            }
        }

        while (scriptCompoundDepth > 0 || scriptParenDepth > 0) {
            wchar_t nextLine[MAX_LINE_LEN];
            if (!fgetws(nextLine, MAX_LINE_LEN, fp)) break;
            size_t nlen = wcslen(nextLine);
            while (nlen > 0 && (nextLine[nlen - 1] == L'\n' || nextLine[nlen - 1] == L'\r')) {
                nextLine[--nlen] = L'\0';
            }
            Script_ProcessHeredocs(nextLine, _countof(nextLine), fp, &heredocs);
            nlen = wcslen(nextLine);
            size_t existLen = wcslen(line);
            if (existLen + nlen + 4 < sizeof(line) / sizeof(wchar_t)) {
                wcscat_s(line, sizeof(line) / sizeof(wchar_t), L"\n");
                wcscat_s(line, sizeof(line) / sizeof(wchar_t), nextLine);
            }
            curLen = wcslen(nextLine);
            for (size_t i = 0; i < curLen; ++i) {
                if (sQuote) {
                    if (nextLine[i] == sQuote) sQuote = L'\0';
                    continue;
                }
                if (nextLine[i] == L'\'' || nextLine[i] == L'"') {
                    sQuote = nextLine[i];
                    continue;
                }
                if (nextLine[i] == L'(') scriptParenDepth++;
                else if (nextLine[i] == L')') { if (scriptParenDepth > 0) scriptParenDepth--; }
                bool atStart = (i == 0 || iswspace(nextLine[i - 1]) || nextLine[i - 1] == L';');
                if (atStart) {
                    if (MatchKeyword(nextLine + i, L"if", NULL) ||
                        MatchKeyword(nextLine + i, L"for", NULL) ||
                        MatchKeyword(nextLine + i, L"while", NULL) ||
                        MatchKeyword(nextLine + i, L"until", NULL) ||
                        MatchKeyword(nextLine + i, L"case", NULL)) scriptCompoundDepth++;
                    else if (MatchKeyword(nextLine + i, L"fi", NULL) ||
                             MatchKeyword(nextLine + i, L"done", NULL) ||
                             MatchKeyword(nextLine + i, L"esac", NULL)) {
                        if (scriptCompoundDepth > 0) scriptCompoundDepth--;
                    }
                }
            }
        }

        wchar_t functionName[MAX_LINE_LEN];
        size_t bodyStart = 0;
        if (Shell_ParseFunctionHeader(line, functionName,
                _countof(functionName), &bodyStart)) {
            wchar_t* body = NULL;
            size_t endIndex = 0;
            if (!Shell_CollectFunctionBody(fp, line, bodyStart, &body, &endIndex, Script_FunctionHeredocs()) ||
                !Shell_SetFunction(self, functionName, body)) {
                free(body);
                fwprintf(stderr, L"ksh: invalid or incomplete function definition: %ls\n",
                    functionName);
                status = 2;
                break;
            }
            free(body);
            const wchar_t* rest = line + endIndex;
            while (*rest && (iswspace(*rest) || *rest == L';')) rest++;
            if (*rest && *rest != L'#') {
                status = self->ExecuteLine(self, rest);
                if (!self->running || self->returnRequested) {
                    if (self->returnRequested) status = self->returnStatus;
                    break;
                }
            }
            continue;
        }

        status = self->ExecuteLine(self, line);
        if (!self->running || self->returnRequested) {
            if (self->returnRequested) status = self->returnStatus;
            break;
        }
    }

    fclose(fp);
    for (size_t k = 0; k < heredocs.count; k++) {
        Ksh_HeredocUnregister(heredocs.paths[k]);
        _wremove(heredocs.paths[k]);
    }
    free(heredocs.paths);
    return status;
}

static wchar_t* Ini_Trim(wchar_t* str) {
    if (!str) return NULL;
    while (*str && iswspace(*str)) str++;
    if (!*str) return str;
    wchar_t* end = str + wcslen(str) - 1;
    while (end > str && iswspace(*end)) {
        *end = L'\0';
        end--;
    }
    return str;
}

static void Ini_StripQuotes(wchar_t* str) {
    if (!str) return;
    size_t len = wcslen(str);
    if (len >= 2) {
        if ((str[0] == L'"' && str[len - 1] == L'"') ||
            (str[0] == L'\'' && str[len - 1] == L'\'')) {
            str[len - 1] = L'\0';
            memmove(str, str + 1, len * sizeof(wchar_t));
        }
    }
}

static void Shell_ParseIniLine(IShell* shell, wchar_t* line, wchar_t* currentSection, size_t sectionSize) {
    wchar_t* trimmed = Ini_Trim(line);
    if (!*trimmed) return;

    /* Skip full-line comments (; for INI, # for shell) */
    if (trimmed[0] == L';' || trimmed[0] == L'#') return;

    /* INI section header: [section] */
    if (trimmed[0] == L'[') {
        wchar_t* endBracket = wcschr(trimmed, L']');
        if (endBracket) {
            *endBracket = L'\0';
            wchar_t* secName = Ini_Trim(trimmed + 1);
            wcsncpy_s(currentSection, sectionSize, secName, _TRUNCATE);
            _wcslwr_s(currentSection, sectionSize);
            return;
        }
    }

    /* Common shell builtins that should execute directly */
    if (_wcsnicmp(trimmed, L"alias ", 6) == 0 ||
        _wcsnicmp(trimmed, L"set ", 4) == 0 ||
        _wcsnicmp(trimmed, L"typeset ", 8) == 0 ||
        _wcsnicmp(trimmed, L"unalias ", 8) == 0 ||
        _wcsnicmp(trimmed, L"source ", 7) == 0 ||
        _wcsnicmp(trimmed, L". ", 2) == 0) {
        shell->ExecuteLine(shell, trimmed);
        return;
    }

    /* Check for key = value or key: value */
    wchar_t* sep = wcschr(trimmed, L'=');
    if (!sep && (wcscmp(currentSection, L"prompt") == 0 || wcscmp(currentSection, L"env") == 0 ||
                 wcscmp(currentSection, L"aliases") == 0 || wcscmp(currentSection, L"alias") == 0)) {
        sep = wcschr(trimmed, L':');
    }

    if (sep) {
        *sep = L'\0';
        wchar_t* rawKey = Ini_Trim(trimmed);
        wchar_t* rawVal = sep + 1;
        while (*rawVal && (*rawVal == L' ' || *rawVal == L'\t')) rawVal++;

        /* Strip inline comments for non-quoted values */
        size_t vlen = wcslen(rawVal);
        if (vlen > 0 && rawVal[0] != L'"' && rawVal[0] != L'\'') {
            wchar_t* semi = wcschr(rawVal, L';');
            if (semi) *semi = L'\0';
            vlen = wcslen(rawVal);
            while (vlen > 0 && (rawVal[vlen - 1] == L'\r' || rawVal[vlen - 1] == L'\n')) {
                rawVal[--vlen] = L'\0';
            }
        } else if (vlen >= 2 && (rawVal[0] == L'"' || rawVal[0] == L'\'')) {
            wchar_t q = rawVal[0];
            wchar_t* endQ = wcschr(rawVal + 1, q);
            if (endQ) {
                *endQ = L'\0';
                rawVal++;
            }
        }

        /* Check if key starts with "export " */
        if (_wcsnicmp(rawKey, L"export ", 7) == 0) {
            rawKey = Ini_Trim(rawKey + 7);
        }

        /* Prompt customization: prompt, ps1, or [prompt] format/value/string */
        if (_wcsicmp(rawKey, L"prompt") == 0 || _wcsicmp(rawKey, L"ps1") == 0 ||
            (wcscmp(currentSection, L"prompt") == 0 &&
             (_wcsicmp(rawKey, L"format") == 0 || _wcsicmp(rawKey, L"value") == 0 ||
              _wcsicmp(rawKey, L"string") == 0))) {
            if (Shell_IsAdmin() && (wcscmp(rawVal, L"$ ") == 0 || wcscmp(rawVal, L"$") == 0)) {
                shell->env->Set(shell->env, L"PS1", L"# ");
            } else {
                shell->env->Set(shell->env, L"PS1", rawVal);
            }
            return;
        }

        /* [aliases] or [alias] section */
        if (wcscmp(currentSection, L"aliases") == 0 || wcscmp(currentSection, L"alias") == 0) {
            Ini_StripQuotes(rawVal);
            Shell_SetAlias(shell, rawKey, rawVal);
            return;
        }

        /* [env] or [environment] section */
        if (wcscmp(currentSection, L"env") == 0 || wcscmp(currentSection, L"environment") == 0) {
            Ini_StripQuotes(rawVal);
            shell->env->Set(shell->env, rawKey, rawVal);
            return;
        }

        /* Restore separator for shell command fallback if not recognized as INI key */
        *sep = L'=';
    }

    /* Fallback: execute line as shell command (e.g. alias, export, set, source, etc.) */
    shell->ExecuteLine(shell, trimmed);
}

static void Shell_LoadIniFile(IShell* shell, const wchar_t* filename) {
    if (!shell || !filename) return;
    FILE* fp = NULL;
    if ((_wfopen_s(&fp, filename, L"r, ccs=UTF-8") != 0 || !fp) &&
        (_wfopen_s(&fp, filename, L"r") != 0 || !fp)) {
        return;
    }

    wchar_t line[MAX_LINE_LEN * 2];
    wchar_t currentSection[128] = {0};

    bool firstLine = true;
    while (fgetws(line, (int)_countof(line), fp)) {
        if (firstLine) {
            firstLine = false;
            if (line[0] == 0xFEFF) {
                memmove(line, line + 1, (wcslen(line + 1) + 1) * sizeof(wchar_t));
            }
        }
        Shell_ParseIniLine(shell, line, currentSection, _countof(currentSection));
    }

    fclose(fp);
}

static bool TryLoadIniFile(IShell* shell, const wchar_t* path) {
    if (!path || !*path) return false;
    DWORD attr = GetFileAttributesW(path);
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        Shell_LoadIniFile(shell, path);
        return true;
    }
    return false;
}

void Shell_LoadKshIni(IShell* shell) {
    if (!shell) return;
    wchar_t* inheritedEnv = NULL;
    const wchar_t* envFile = shell->env->Get(shell->env, L"ENV");
    if (!envFile) envFile = inheritedEnv = GetEnvironmentValue(L"ENV");
    if (envFile && *envFile) {
        wchar_t* expanded = shell->env->Expand(shell->env, envFile);
        bool loaded = false;
        if (expanded && *expanded) {
            loaded = TryLoadIniFile(shell, expanded);
        }
        free(expanded);
        free(inheritedEnv);
        if (loaded) return;
    }
    free(inheritedEnv);

    /* 1. Check user profile / home directory: %USERPROFILE%\ksh.ini or %HOME%\ksh.ini */
    wchar_t userIni[MAX_PATH] = {0};
    wchar_t home[MAX_PATH] = {0};
    DWORD len = GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
    if (!len) len = GetEnvironmentVariableW(L"HOME", home, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        swprintf_s(userIni, MAX_PATH, L"%ls\\ksh.ini", home);
        if (TryLoadIniFile(shell, userIni)) return;

        /* Auto-generate starter template in user's home directory if missing */
        if (GetFileAttributesW(userIni) == INVALID_FILE_ATTRIBUTES) {
            FILE* fp = _wfsopen(userIni, L"wt, ccs=UTF-8", _SH_DENYNO);
            if (fp) {
                fputws(
                    L"; ==============================================================================\n"
                    L"; ntksh Configuration File (ksh.ini)\n"
                    L"; Automatically loaded on startup from %USERPROFILE%\\ksh.ini\n"
                    L"; ==============================================================================\n\n"
                    L"[prompt]\n"
                    L"; Shell prompt customization. Default is \"$ \" (or \"# \" for Administrator).\n"
                    L"; Supported escape tokens:\n"
                    L";   \\u - Username\n"
                    L";   \\h - Hostname\n"
                    L";   \\w - Full current working directory\n"
                    L";   \\W - Basename of current working directory\n"
                    L";   \\$ - '$' (or '#' if running as Administrator)\n",
                    fp
                );
                fputws(
                    Shell_IsAdmin() ? L"prompt = \"# \"\n\n" : L"prompt = \"$ \"\n\n",
                    fp
                );
                fputws(
                    L"[aliases]\n"
                    L"; Common command aliases\n"
                    L"ll = ls -la\n"
                    L"la = ls -A\n"
                    L"l = ls -CF\n"
                    L"cls = clear\n\n"
                    L"[env]\n"
                    L"; Environment variables exported on startup\n"
                    L"; EDITOR = notepad\n",
                    fp
                );
                fclose(fp);
                (void)TryLoadIniFile(shell, userIni);
                return;
            }
        }
    }

    /* 2. Check ksh.exe application directory: <exe_dir>\ksh.ini */
    wchar_t exePath[MAX_PATH];
    DWORD elen = GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (elen > 0 && elen < MAX_PATH) {
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) {
            *(lastSlash + 1) = L'\0';
            wcscat_s(exePath, MAX_PATH, L"ksh.ini");
            if (_wcsicmp(exePath, userIni) != 0 && TryLoadIniFile(shell, exePath)) return;
        }
    }

    /* 3. Check current directory: .\ksh.ini */
    TryLoadIniFile(shell, L"ksh.ini");
}

void Shell_LoadKshrc(IShell* shell) {
    Shell_LoadKshIni(shell);
}



void Shell_Destroy(IShell* self) {
    if (!self) return;
    Module_Cleanup();
    if (!self->execReplaced) Shell_FireTrap(self, L"EXIT");
    Shell_FreeTraps();
    Shell_ProcessProcSubstCleanup(self);
    for (int n = 3; n < KSH_FD_COUNT; n++) {
        if (self->fdTable[n]) {
            CloseHandle(self->fdTable[n]);
            self->fdTable[n] = NULL;
        }
    }
    if (self->coproc_active) {
        if (self->coproc_in_write) CloseHandle(self->coproc_in_write);
        if (self->coproc_out_read) CloseHandle(self->coproc_out_read);
        if (self->coproc_proc) CloseHandle(self->coproc_proc);
        self->coproc_active = false;
    }
    ShellFunction* function = self->functions;
    while (function) {
        ShellFunction* next = function->next;
        free(function->name);
        free(function->body);
        free(function);
        function = next;
    }
    ShellTypeDef* typeDef = self->types;
    while (typeDef) {
        ShellTypeDef* nextType = typeDef->next;
        ShellTypeMember* m = typeDef->members;
        while (m) {
            ShellTypeMember* nextM = m->next;
            if (m->functionBody) free(m->functionBody);
            free(m);
            m = nextM;
        }
        free(typeDef);
        typeDef = nextType;
    }
    ShellAlias* alias = self->aliases;
    while (alias) {
        ShellAlias* next = alias->next;
        free(alias->name);
        free(alias->value);
        free(alias);
        alias = next;
    }
    DirectoryStackEntry* directory = self->directoryStack;
    while (directory) {
        DirectoryStackEntry* next = directory->next;
        free(directory->path);
        free(directory);
        directory = next;
    }
    if (self->executor) self->executor->Destroy(self->executor);
    if (self->parser) self->parser->Destroy(self->parser);
    if (self->env) self->env->Destroy(self->env);
    History_Cleanup();
    free(self);
}

IShell* CreateShell(void) {
    IShell* shell = (IShell*)calloc(1, sizeof(IShell));
    if (!shell) return NULL;
    shell->env = CreateEnvironment();
    if (!shell->env) {
        free(shell);
        return NULL;
    }
    shell->env->shell = shell;
    shell->parser = CreateParser(shell->env);
    if (!shell->parser) {
        shell->env->Destroy(shell->env);
        free(shell);
        return NULL;
    }
    shell->executor = CreateExecutor(shell);
    if (!shell->executor) {
        shell->parser->Destroy(shell->parser);
        shell->env->Destroy(shell->env);
        free(shell);
        return NULL;
    }
    shell->running = true;
    shell->last_status = 0;
    shell->fileMask = 0022;
    const ShellLimit defaultLimits[SHELL_LIMIT_COUNT] = {
        {L'c', L"core file size", ULLONG_MAX, ULLONG_MAX},
        {L'd', L"data segment size", ULLONG_MAX, ULLONG_MAX},
        {L'f', L"file size", ULLONG_MAX, ULLONG_MAX},
        {L'i', L"pending signals", ULLONG_MAX, ULLONG_MAX},
        {L'l', L"locked memory", ULLONG_MAX, ULLONG_MAX},
        {L'm', L"resident set size", ULLONG_MAX, ULLONG_MAX},
        {L'n', L"open files", 512, 512},
        {L'p', L"pipe size", 512, 512},
        {L'q', L"POSIX message queues", ULLONG_MAX, ULLONG_MAX},
        {L'r', L"real-time priority", ULLONG_MAX, ULLONG_MAX},
        {L's', L"stack size", ULLONG_MAX, ULLONG_MAX},
        {L't', L"CPU time", ULLONG_MAX, ULLONG_MAX},
        {L'u', L"user processes", ULLONG_MAX, ULLONG_MAX},
        {L'v', L"virtual memory", ULLONG_MAX, ULLONG_MAX},
        {L'x', L"file locks", ULLONG_MAX, ULLONG_MAX},
        {L'T', L"threads", ULLONG_MAX, ULLONG_MAX}
    };
    memcpy(shell->limits, defaultLimits, sizeof(defaultLimits));

    for (int fdIdx = 3; fdIdx < KSH_FD_COUNT; fdIdx++) {
        wchar_t fdVar[32], fdVal[64];
        swprintf_s(fdVar, _countof(fdVar), L"__KSH_FD_%d", fdIdx);
        if (GetEnvironmentVariableW(fdVar, fdVal, _countof(fdVal)) > 0) {
            unsigned long long hVal = wcstoull(fdVal, NULL, 16);
            if (hVal) shell->fdTable[fdIdx] = (HANDLE)(uintptr_t)hVal;
        }
    }

    shell->RunREPL = Shell_RunREPL;
    shell->RunScript = Shell_RunScript;
    shell->ExecuteLine = Shell_ExecuteLine;
    shell->Destroy = Shell_Destroy;

    const wchar_t* editor = shell->env->Get(shell->env, L"VISUAL");
    if (!editor) editor = shell->env->Get(shell->env, L"EDITOR");
    if (editor && wcsstr(editor, L"vi")) {
        shell->editMode = EDIT_MODE_VI;
    } else {
        shell->editMode = EDIT_MODE_EMACS;
    }

    /* Initialize environment */
    if (!shell->env->Set(shell->env, L"SHELL", L"ksh") ||
        !shell->env->Set(shell->env, L"KSH_VERSION", NTKSH_VERSION) ||
        !shell->env->Set(shell->env, L".sh.version", NTKSH_VERSION) ||
        !shell->env->Set(shell->env, L"PS1", Shell_IsAdmin() ? L"# " : L"$ ")) {
        shell->Destroy(shell);
        return NULL;
    }

    const wchar_t* existingHome = shell->env->Get(shell->env, L"HOME");
    if (!existingHome) {
        wchar_t userProfile[MAX_PATH];
        DWORD ulen = GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH);
        if (ulen > 0 && ulen < MAX_PATH) {
            for (wchar_t* p = userProfile; *p; p++) {
                if (*p == L'\\') *p = L'/';
            }
            shell->env->Set(shell->env, L"HOME", userProfile);
        }
    }

    wchar_t initialCwd[KSH_PATH_MAX];
    if (GetCurrentDirectoryW(_countof(initialCwd), initialCwd)) {
        shell->env->Set(shell->env, L"PWD", initialCwd);
    }

    Shell_InstallSignalHandler();
    Module_Init(shell);
    g_active_shell = shell;

    return shell;
}

/* ========================================================================= */
/*                         Built-in Implementations                          */
/* ========================================================================= */





bool IsValidIdentifier(const wchar_t* name) {
    if (!name || !(iswalpha(*name) || *name == L'_' || *name == L'.')) return false;
    for (name++; *name; name++) {
        if (!(iswalnum(*name) || *name == L'_' || *name == L'.')) return false;
    }
    return true;
}

void PrintPosixPath(const wchar_t* path) {
    for (const wchar_t* p = path; *p; ++p)
        putwchar(*p == L'\\' ? L'/' : *p);
}

bool SameDirectory(const wchar_t* left, const wchar_t* right) {
    HANDLE leftHandle = CreateFileW(left, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (leftHandle == INVALID_HANDLE_VALUE) return false;
    HANDLE rightHandle = CreateFileW(right, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (rightHandle == INVALID_HANDLE_VALUE) {
        CloseHandle(leftHandle);
        return false;
    }
    BY_HANDLE_FILE_INFORMATION leftInfo;
    BY_HANDLE_FILE_INFORMATION rightInfo;
    bool same = GetFileInformationByHandle(leftHandle, &leftInfo) &&
        GetFileInformationByHandle(rightHandle, &rightInfo) &&
        leftInfo.dwVolumeSerialNumber == rightInfo.dwVolumeSerialNumber &&
        leftInfo.nFileIndexHigh == rightInfo.nFileIndexHigh &&
        leftInfo.nFileIndexLow == rightInfo.nFileIndexLow;
    CloseHandle(leftHandle);
    CloseHandle(rightHandle);
    return same;
}

