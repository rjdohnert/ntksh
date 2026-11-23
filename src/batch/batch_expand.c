#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

bool Batch_Append(wchar_t** output, size_t* length,
    size_t* capacity, const wchar_t* value, size_t valueLength) {
    if (valueLength > SIZE_MAX - *length - 1) return false;
    size_t needed = *length + valueLength + 1;
    if (needed > *capacity) {
        size_t nextCapacity = *capacity ? *capacity : 256;
        while (nextCapacity < needed) {
            if (nextCapacity > SIZE_MAX / 2) return false;
            nextCapacity *= 2;
        }
        wchar_t* resized = (wchar_t*)realloc(*output,
            nextCapacity * sizeof(wchar_t));
        if (!resized) return false;
        *output = resized;
        *capacity = nextCapacity;
    }
    memcpy(*output + *length, value, valueLength * sizeof(wchar_t));
    *length += valueLength;
    (*output)[*length] = L'\0';
    return true;
}

BatchLoopVariable* Batch_FindLoopVariable(BatchContext* context,
    wchar_t name) {
    for (BatchLoopVariable* variable = context->loopVariables; variable;
        variable = variable->next) {
        if (towupper(variable->name) == towupper(name)) return variable;
    }
    return NULL;
}

const wchar_t* Batch_GetVariable(IShell* shell,
    BatchContext* context, const wchar_t* name, wchar_t* scratch,
    size_t scratchCapacity) {
    if (_wcsicmp(name, L"ERRORLEVEL") == 0) {
        _snwprintf_s(scratch, scratchCapacity, _TRUNCATE, L"%d",
            context ? context->errorLevel : shell->last_status);
        return scratch;
    }
    if (_wcsicmp(name, L"CD") == 0) {
        DWORD length = GetCurrentDirectoryW((DWORD)scratchCapacity, scratch);
        return length && length < scratchCapacity ? scratch : L"";
    }
    if (_wcsicmp(name, L"RANDOM") == 0) {
        _snwprintf_s(scratch, scratchCapacity, _TRUNCATE, L"%u",
            (unsigned int)(rand() % 32768));
        return scratch;
    }
    if (_wcsicmp(name, L"DATE") == 0 || _wcsicmp(name, L"TIME") == 0) {
        SYSTEMTIME now;
        GetLocalTime(&now);
        if (_wcsicmp(name, L"DATE") == 0)
            _snwprintf_s(scratch, scratchCapacity, _TRUNCATE,
                L"%02u/%02u/%04u", now.wMonth, now.wDay, now.wYear);
        else
            _snwprintf_s(scratch, scratchCapacity, _TRUNCATE,
                L"%02u:%02u:%02u.%02u", now.wHour, now.wMinute,
                now.wSecond, now.wMilliseconds / 10);
        return scratch;
    }
    const wchar_t* value = shell->env->Get(shell->env, name);
    if (value) return value;
    return NULL;
}

bool Batch_ApplyModifiers(IShell* shell, const wchar_t* value,
    const wchar_t* modifiers, const wchar_t* searchVariable,
    wchar_t* output, size_t outputCapacity) {
    wchar_t path[MAX_PATH * 2];
    size_t valueLength = wcslen(value);
    if (valueLength >= _countof(path)) return false;
    if (valueLength >= 2 && value[0] == L'"' &&
        value[valueLength - 1] == L'"') {
        memcpy(path, value + 1, (valueLength - 2) * sizeof(wchar_t));
        path[valueLength - 2] = L'\0';
    } else {
        wcscpy_s(path, _countof(path), value);
    }
    if (!path[0]) {
        output[0] = L'\0';
        return true;
    }
    if (searchVariable) {
        const wchar_t* searchPath = shell->env->Get(shell->env,
            searchVariable);
        wchar_t* inherited = searchPath ? NULL :
            GetEnvironmentValue(searchVariable);
        if (!searchPath) searchPath = inherited;
        wchar_t foundPath[MAX_PATH * 2];
        DWORD length = searchPath && *searchPath ?
            SearchPathW(searchPath, path, NULL, _countof(foundPath),
                foundPath, NULL) : 0;
        free(inherited);
        if (!length || length >= _countof(foundPath)) {
            output[0] = L'\0';
            return true;
        }
        wcscpy_s(path, _countof(path), foundPath);
    }
    if (wcschr(modifiers, L'f')) {
        wchar_t fullPath[MAX_PATH * 2];
        DWORD length = GetFullPathNameW(path, _countof(fullPath), fullPath,
            NULL);
        if (!length || length >= _countof(fullPath)) return false;
        wcscpy_s(path, _countof(path), fullPath);
    }
    if (wcschr(modifiers, L's')) {
        wchar_t shortPath[MAX_PATH * 2];
        DWORD length = GetShortPathNameW(path, shortPath,
            (DWORD)_countof(shortPath));
        if (length && length < _countof(shortPath))
            wcscpy_s(path, _countof(path), shortPath);
    }
    wchar_t drive[_MAX_DRIVE], directory[_MAX_DIR];
    wchar_t name[_MAX_FNAME], extension[_MAX_EXT];
    if (_wsplitpath_s(path, drive, _countof(drive), directory,
            _countof(directory), name, _countof(name), extension,
            _countof(extension)) != 0)
        return false;
    output[0] = L'\0';
    size_t used = 0;
    if (wcschr(modifiers, L'd')) {
        size_t length = wcslen(drive);
        if (length >= outputCapacity) return false;
        wcscpy_s(output, outputCapacity, drive);
        used = length;
    }
    if (wcschr(modifiers, L'p')) {
        size_t length = wcslen(directory);
        if (length >= outputCapacity - used) return false;
        wcscpy_s(output + used, outputCapacity - used, directory);
        used += length;
    }
    if (wcschr(modifiers, L'n')) {
        size_t length = wcslen(name);
        if (length >= outputCapacity - used) return false;
        wcscpy_s(output + used, outputCapacity - used, name);
        used += length;
    }
    if (wcschr(modifiers, L'x')) {
        size_t length = wcslen(extension);
        if (length >= outputCapacity - used) return false;
        wcscpy_s(output + used, outputCapacity - used, extension);
        used += length;
    }
    WIN32_FILE_ATTRIBUTE_DATA data;
    bool hasMetadata = wcschr(modifiers, L'a') || wcschr(modifiers, L't') ||
        wcschr(modifiers, L'z');
    bool hasFileData = hasMetadata &&
        GetFileAttributesExW(path, GetFileExInfoStandard, &data);
    if (wcschr(modifiers, L'a')) {
        static const struct {
            DWORD flag;
            wchar_t code;
        } attributeCodes[] = {
            { FILE_ATTRIBUTE_DIRECTORY, L'd' },
            { FILE_ATTRIBUTE_READONLY, L'r' },
            { FILE_ATTRIBUTE_HIDDEN, L'h' },
            { FILE_ATTRIBUTE_SYSTEM, L's' },
            { FILE_ATTRIBUTE_ARCHIVE, L'a' },
            { FILE_ATTRIBUTE_REPARSE_POINT, L'l' },
            { FILE_ATTRIBUTE_COMPRESSED, L'c' },
            { FILE_ATTRIBUTE_ENCRYPTED, L'e' },
            { FILE_ATTRIBUTE_OFFLINE, L'o' },
            { FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, L'i' },
            { FILE_ATTRIBUTE_TEMPORARY, L't' }
        };
        for (size_t i = 0; i < _countof(attributeCodes); ++i) {
            if (used + 1 >= outputCapacity) return false;
            output[used++] = hasFileData &&
                (data.dwFileAttributes & attributeCodes[i].flag) ?
                attributeCodes[i].code : L'-';
        }
        output[used] = L'\0';
    }
    if (wcschr(modifiers, L't') && hasFileData) {
        FILETIME localTime;
        SYSTEMTIME modified;
        wchar_t date[128], time[128];
        if (FileTimeToLocalFileTime(&data.ftLastWriteTime, &localTime) &&
            FileTimeToSystemTime(&localTime, &modified) &&
            GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &modified,
                NULL, date, _countof(date)) &&
            GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &modified, NULL, time,
                _countof(time))) {
            int written = swprintf_s(output + used, outputCapacity - used,
                L"%ls %ls", date, time);
            if (written < 0) return false;
            used += (size_t)written;
        }
    }
    if (wcschr(modifiers, L'z') && hasFileData) {
        unsigned long long size =
            ((unsigned long long)data.nFileSizeHigh << 32) |
            data.nFileSizeLow;
        int written = swprintf_s(output + used, outputCapacity - used,
            L"%llu", size);
        if (written < 0) return false;
        used += (size_t)written;
    }
    if (!used) {
        if (wcslen(path) >= outputCapacity) return false;
        wcscpy_s(output, outputCapacity, path);
    }
    return true;
}

/*
 * CMD expands a batch line in two phases: %VAR%/argument expansion happens
 * once when the line (or parenthesized block) is read, while FOR variables,
 * delayed !VAR! expansion and caret escapes are resolved per command.
 */


static const wchar_t* Batch_FindInsensitive(const wchar_t* text,
    const wchar_t* needle) {
    size_t needleLength = wcslen(needle);
    if (!needleLength) return NULL;
    for (; *text; ++text) {
        if (_wcsnicmp(text, needle, needleLength) == 0) return text;
    }
    return NULL;
}

/* Appends NAME, NAME:~start[,length] or NAME:[*]search=replace. */
static bool Batch_AppendVariable(IShell* shell, BatchContext* context,
    const wchar_t* spec, size_t specLength, wchar_t** output,
    size_t* length, size_t* capacity) {
    wchar_t name[MAX_LINE_LEN];
    if (specLength >= _countof(name)) return false;
    memcpy(name, spec, specLength * sizeof(wchar_t));
    name[specLength] = L'\0';
    wchar_t* modifier = wcschr(name, L':');
    if (modifier) *modifier++ = L'\0';
    wchar_t scratch[MAX_PATH * 2];
    wchar_t* inherited = NULL;
    const wchar_t* value = *name ? Batch_GetVariable(shell, context, name,
        scratch, _countof(scratch)) : NULL;
    if (!value && *name) value = inherited = GetEnvironmentValue(name);
    if (!value) value = L"";
    size_t valueLength = wcslen(value);
    bool ok = true;
    if (!modifier) {
        ok = Batch_Append(output, length, capacity, value, valueLength);
    } else if (*modifier == L'~') {
        long long total = (long long)valueLength;
        wchar_t* end = NULL;
        long long start = wcstoll(modifier + 1, &end, 10);
        long long stop = total;
        if (start < 0) start += total;
        if (start < 0) start = 0;
        if (start > total) start = total;
        if (*end == L',') {
            long long count = wcstoll(end + 1, NULL, 10);
            stop = count < 0 ? total + count : start + count;
        }
        if (stop > total) stop = total;
        if (stop < start) stop = start;
        ok = Batch_Append(output, length, capacity, value + start,
            (size_t)(stop - start));
    } else {
        wchar_t* equal = wcschr(modifier, L'=');
        const wchar_t* search = modifier;
        bool firstOnly = *search == L'*';
        if (firstOnly) search++;
        if (!equal || search == equal) {
            ok = Batch_Append(output, length, capacity, value, valueLength);
        } else {
            *equal = L'\0';
            const wchar_t* replacement = equal + 1;
            size_t searchLength = wcslen(search);
            const wchar_t* cursor = value;
            const wchar_t* match;
            while (ok && (match = Batch_FindInsensitive(cursor, search))) {
                ok = (firstOnly || Batch_Append(output, length, capacity,
                        cursor, (size_t)(match - cursor))) &&
                    Batch_Append(output, length, capacity, replacement,
                        wcslen(replacement));
                cursor = match + searchLength;
                if (firstOnly) break;
            }
            ok = ok && Batch_Append(output, length, capacity, cursor,
                wcslen(cursor));
        }
    }
    free(inherited);
    return ok;
}

static const wchar_t* Batch_GetArgument(BatchContext* context,
    unsigned int index, bool resolveScript) {
    if (index == 0) {
        /* %~f0 and friends name the batch file even inside CALL :label. */
        if (resolveScript && context->batchFile) return context->batchFile;
        return context->scriptPath ? context->scriptPath : L"";
    }
    int argument = context->shift + (int)index - 1;
    if (argument >= 0 && argument < context->argumentCount &&
        context->arguments[argument])
        return context->arguments[argument];
    return L"";
}

/* Expands %%X, %%~modsX or %%~mods$VAR:X; text points just past the %%. */
static bool Batch_AppendLoopReference(IShell* shell, BatchContext* context,
    const wchar_t* text, size_t* consumed, wchar_t** output,
    size_t* length, size_t* capacity, bool* failed) {
    *failed = false;
    if (!text[0]) return false;
    if (text[0] != L'~') {
        BatchLoopVariable* variable = Batch_FindLoopVariable(context, text[0]);
        if (!variable) return false;
        const wchar_t* value = variable->value ? variable->value : L"";
        *consumed = 1;
        *failed = !Batch_Append(output, length, capacity, value,
            wcslen(value));
        return true;
    }
    size_t run = 0;
    while (run < 15 && text[1 + run] &&
        wcschr(L"fdpnxstaz", towlower(text[1 + run])))
        run++;
    /* Prefer the longest modifier run that still ends in a loop variable. */
    for (size_t k = run + 1; k-- > 0;) {
        wchar_t modifiers[16];
        for (size_t m = 0; m < k; ++m)
            modifiers[m] = (wchar_t)towlower(text[1 + m]);
        modifiers[k] = L'\0';
        const wchar_t* searchVariable = NULL;
        wchar_t searchName[256];
        size_t nameIndex = 1 + k;
        if (text[nameIndex] == L'$') {
            const wchar_t* colon = wcschr(text + nameIndex + 1, L':');
            size_t nameLength = colon ?
                (size_t)(colon - (text + nameIndex + 1)) : 0;
            if (!nameLength || nameLength >= _countof(searchName)) continue;
            memcpy(searchName, text + nameIndex + 1,
                nameLength * sizeof(wchar_t));
            searchName[nameLength] = L'\0';
            searchVariable = searchName;
            nameIndex = (size_t)(colon - text) + 1;
        }
        BatchLoopVariable* variable = text[nameIndex] ?
            Batch_FindLoopVariable(context, text[nameIndex]) : NULL;
        if (!variable) continue;
        wchar_t formatted[MAX_PATH * 2];
        *consumed = nameIndex + 1;
        *failed = !Batch_ApplyModifiers(shell,
                variable->value ? variable->value : L"", modifiers,
                searchVariable, formatted, _countof(formatted)) ||
            !Batch_Append(output, length, capacity, formatted,
                wcslen(formatted));
        return true;
    }
    return false;
}

wchar_t* Batch_Expand(IShell* shell, BatchContext* context,
    const wchar_t* input, int mode) {
    size_t capacity = 0;
    size_t length = 0;
    wchar_t* output = NULL;
    if (!Batch_Append(&output, &length, &capacity, L"", 0)) return NULL;
    bool quoted = false;
    for (size_t i = 0; input[i];) {
        wchar_t current = input[i];
        if (mode == BATCH_EXPAND_LOOP) {
            /* Only FOR variables; carets, quotes and %% stay for a child. */
            if (current == L'%' && input[i + 1] == L'%') {
                size_t consumed = 0;
                bool failed = false;
                if (Batch_AppendLoopReference(shell, context, input + i + 2,
                        &consumed, &output, &length, &capacity, &failed)) {
                    if (failed) goto fail;
                    i += 2 + consumed;
                    continue;
                }
                if (!Batch_Append(&output, &length, &capacity, input + i, 2))
                    goto fail;
                i += 2;
                continue;
            }
            if (!Batch_Append(&output, &length, &capacity, input + i, 1))
                goto fail;
            i++;
            continue;
        }
        if (mode == BATCH_EXPAND_FINAL) {
            if (current == L'"') {
                quoted = !quoted;
            } else if (current == L'\n') {
                quoted = false;
            } else if (current == L'^' && !quoted) {
                if (input[i + 1] && !Batch_Append(&output, &length,
                        &capacity, input + i + 1, 1))
                    goto fail;
                i += input[i + 1] ? 2 : 1;
                continue;
            } else if (current == L'!' && context->delayedExpansion) {
                const wchar_t* close = wcschr(input + i + 1, L'!');
                if (close) {
                    size_t nameLength = (size_t)(close - (input + i + 1));
                    if (nameLength && !Batch_AppendVariable(shell, context,
                            input + i + 1, nameLength, &output, &length,
                            &capacity))
                        goto fail;
                    i = (size_t)(close - input) + 1;
                    continue;
                }
                i++;
                continue;
            } else if (current == L'%' && input[i + 1] == L'%') {
                size_t consumed = 0;
                bool failed = false;
                if (Batch_AppendLoopReference(shell, context, input + i + 2,
                        &consumed, &output, &length, &capacity, &failed)) {
                    if (failed) goto fail;
                    i += 2 + consumed;
                    continue;
                }
                if (!Batch_Append(&output, &length, &capacity, L"%", 1))
                    goto fail;
                i += 2;
                continue;
            }
            if (!Batch_Append(&output, &length, &capacity, input + i, 1))
                goto fail;
            i++;
            continue;
        }

        if (current != L'%') {
            if (!Batch_Append(&output, &length, &capacity, input + i, 1))
                goto fail;
            i++;
            continue;
        }
        if (input[i + 1] == L'%') {
            /* Keep %% so FOR variables survive until the final phase. */
            if (!Batch_Append(&output, &length, &capacity, input + i, 2))
                goto fail;
            i += 2;
            continue;
        }
        if (input[i + 1] == L'~') {
            size_t candidate = i + 2;
            wchar_t modifiers[16];
            size_t modifierLength = 0;
            while (input[candidate] &&
                wcschr(L"fdpnxstaz", towlower(input[candidate])) &&
                modifierLength + 1 < _countof(modifiers))
                modifiers[modifierLength++] =
                    (wchar_t)towlower(input[candidate++]);
            modifiers[modifierLength] = L'\0';
            if (input[candidate] >= L'0' && input[candidate] <= L'9') {
                const wchar_t* value = Batch_GetArgument(context,
                    (unsigned int)(input[candidate] - L'0'), true);
                wchar_t formatted[MAX_PATH * 2];
                if (!Batch_ApplyModifiers(shell, value, modifiers, NULL,
                        formatted, _countof(formatted)) ||
                    !Batch_Append(&output, &length, &capacity, formatted,
                        wcslen(formatted)))
                    goto fail;
                i = candidate + 1;
                continue;
            }
        }
        if (input[i + 1] >= L'0' && input[i + 1] <= L'9') {
            const wchar_t* value = Batch_GetArgument(context,
                (unsigned int)(input[i + 1] - L'0'), false);
            if (!Batch_Append(&output, &length, &capacity, value,
                    wcslen(value)))
                goto fail;
            i += 2;
            continue;
        }
        if (input[i + 1] == L'*') {
            for (int argument = 0; argument < context->argumentCount;
                ++argument) {
                if (!context->arguments[argument]) continue;
                if (argument &&
                    !Batch_Append(&output, &length, &capacity, L" ", 1))
                    goto fail;
                if (!Batch_Append(&output, &length, &capacity,
                        context->arguments[argument],
                        wcslen(context->arguments[argument])))
                    goto fail;
            }
            i += 2;
            continue;
        }
        const wchar_t* close = wcschr(input + i + 1, L'%');
        if (close && close > input + i + 1) {
            if (!Batch_AppendVariable(shell, context, input + i + 1,
                    (size_t)(close - (input + i + 1)), &output, &length,
                    &capacity))
                goto fail;
            i = (size_t)(close - input) + 1;
            continue;
        }
        if (!Batch_Append(&output, &length, &capacity, L"%", 1))
            goto fail;
        i++;
    }
    return output;
fail:
    free(output);
    return NULL;
}

