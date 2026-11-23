#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

static void Batch_FreeLocalFrame(BatchLocalFrame* frame) {
    BatchVariableChange* change = frame->changes;
    while (change) {
        BatchVariableChange* next = change->next;
        free(change->name);
        free(change->value);
        free(change);
        change = next;
    }
    free(frame);
}

bool Batch_RecordVariableChange(IShell* shell, BatchContext* context,
    const wchar_t* name) {
    if (!context) return true;
    BatchLocalFrame* frame = context->localFrame;
    if (!frame) return true;
    for (BatchVariableChange* item = frame->changes; item; item = item->next)
        if (_wcsicmp(item->name, name) == 0) return true;
    const wchar_t* current = shell->env->Get(shell->env, name);
    wchar_t* inherited = current ? NULL : GetEnvironmentValue(name);
    if (!current) current = inherited;
    BatchVariableChange* change =
        (BatchVariableChange*)calloc(1, sizeof(BatchVariableChange));
    if (!change) {
        free(inherited);
        return false;
    }
    change->name = SafeStrDup(name);
    change->value = current ? SafeStrDup(current) : NULL;
    change->existed = current != NULL;
    free(inherited);
    if (!change->name || (change->existed && !change->value)) {
        free(change->name);
        free(change->value);
        free(change);
        return false;
    }
    change->next = frame->changes;
    frame->changes = change;
    return true;
}

bool Batch_SetVariable(IShell* shell, BatchContext* context,
    const wchar_t* name, const wchar_t* value) {
    if (!Batch_RecordVariableChange(shell, context, name)) return false;
    return shell->env->Set(shell->env, name, value);
}

bool Batch_EndLocal(IShell* shell, BatchContext* context) {
    BatchLocalFrame* frame = context->localFrame;
    if (!frame) return false;
    for (BatchVariableChange* change = frame->changes; change;
        change = change->next) {
        bool success = change->existed ?
            shell->env->Set(shell->env, change->name, change->value) :
            shell->env->Unset(shell->env, change->name);
        if (!success) {
            fwprintf(stderr, L"ksh: endlocal: cannot restore %ls\n",
                change->name);
            return false;
        }
    }
    context->localFrame = frame->parent;
    context->delayedExpansion = frame->delayedExpansion;
    Batch_FreeLocalFrame(frame);
    return true;
}

bool Batch_PushLine(BatchFileLines* lines, const wchar_t* text) {
    if (lines->count == lines->capacity) {
        size_t capacity = lines->capacity ? lines->capacity * 2 : 32;
        wchar_t** resized = (wchar_t**)realloc(lines->items,
            capacity * sizeof(wchar_t*));
        if (!resized) return false;
        lines->items = resized;
        lines->capacity = capacity;
    }
    lines->items[lines->count] = SafeStrDup(text);
    if (!lines->items[lines->count]) return false;
    lines->count++;
    return true;
}

void Batch_FreeLines(BatchFileLines* lines) {
    for (size_t i = 0; i < lines->count; ++i) free(lines->items[i]);
    free(lines->items);
}

const wchar_t* Batch_SkipSpaces(const wchar_t* text) {
    while (iswspace(*text)) text++;
    return text;
}

bool Batch_ReadWord(const wchar_t** cursor, wchar_t* word,
    size_t capacity) {
    const wchar_t* p = Batch_SkipSpaces(*cursor);
    if (!*p) {
        *cursor = p;
        return false;
    }
    size_t length = 0;
    wchar_t quote = L'\0';
    while (*p) {
        if (!quote && iswspace(*p)) break;
        if (*p == L'"') {
            quote = quote ? L'\0' : L'"';
            p++;
            continue;
        }
        if (*p == L'^' && p[1]) p++;
        if (length + 1 >= capacity) return false;
        word[length++] = *p++;
    }
    word[length] = L'\0';
    *cursor = p;
    return true;
}

bool Batch_IsCommand(const wchar_t* line, const wchar_t* name,
    const wchar_t** rest) {
    line = Batch_SkipSpaces(line);
    size_t length = wcslen(name);
    if (_wcsnicmp(line, name, length) != 0) return false;
    wchar_t after = line[length];
    if (after && !iswspace(after) && after != L'/') {
        if (_wcsicmp(name, L"echo") == 0) {
            if (after != L'.' && after != L':') return false;
        } else {
            return false;
        }
    }
    if (rest) *rest = Batch_SkipSpaces(line + length);
    return true;
}

/*
 * Decodes batch text the way CMD does: a UTF-16 or UTF-8 BOM wins, otherwise
 * strict UTF-8 is tried before falling back to the console (or OEM) code page.
 */
wchar_t* Batch_DecodeText(const char* data, size_t size,
    bool allowBom) {
    const unsigned char* bytes = (const unsigned char*)data;
    if (size > INT_MAX) return NULL;
    if (allowBom && size >= 2 &&
        ((bytes[0] == 0xFF && bytes[1] == 0xFE) ||
            (bytes[0] == 0xFE && bytes[1] == 0xFF))) {
        bool bigEndian = bytes[0] == 0xFE;
        size_t count = (size - 2) / 2;
        wchar_t* text = (wchar_t*)malloc((count + 1) * sizeof(wchar_t));
        if (!text) return NULL;
        for (size_t i = 0; i < count; ++i) {
            unsigned char first = bytes[2 + i * 2];
            unsigned char second = bytes[3 + i * 2];
            text[i] = bigEndian ? (wchar_t)((first << 8) | second) :
                (wchar_t)((second << 8) | first);
        }
        text[count] = L'\0';
        return text;
    }
    UINT codePage = CP_UTF8;
    if (allowBom && size >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB &&
        bytes[2] == 0xBF) {
        data += 3;
        size -= 3;
    } else if (size && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            data, (int)size, NULL, 0)) {
        codePage = GetConsoleOutputCP();
        if (!codePage || codePage == CP_UTF8) codePage = GetOEMCP();
    }
    int count = size ?
        MultiByteToWideChar(codePage, 0, data, (int)size, NULL, 0) : 0;
    if (size && count <= 0) return NULL;
    wchar_t* text = (wchar_t*)malloc(((size_t)count + 1) * sizeof(wchar_t));
    if (!text) return NULL;
    if (count &&
        MultiByteToWideChar(codePage, 0, data, (int)size, text, count) != count) {
        free(text);
        return NULL;
    }
    text[count] = L'\0';
    return text;
}

bool Batch_ReadTextFile(const wchar_t* path, wchar_t** text) {
    *text = NULL;
    HANDLE file = CreateFileW(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        size.QuadPart > 256LL * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }
    size_t expected = (size_t)size.QuadPart;
    char* data = (char*)malloc(expected + 1);
    if (!data) {
        CloseHandle(file);
        return false;
    }
    size_t total = 0;
    bool ok = true;
    while (total < expected) {
        DWORD read = 0;
        if (!ReadFile(file, data + total, (DWORD)(expected - total), &read,
                NULL)) {
            ok = false;
            break;
        }
        if (!read) break;
        total += read;
    }
    CloseHandle(file);
    if (ok) *text = Batch_DecodeText(data, total, true);
    free(data);
    return *text != NULL;
}

static bool Batch_AppendQuotedArgument(wchar_t** output, size_t* length,
    size_t* capacity, const wchar_t* argument) {
    if (*length && !Batch_Append(output, length, capacity, L" ", 1))
        return false;
    if (!Batch_Append(output, length, capacity, L"\"", 1)) return false;
    size_t backslashes = 0;
    for (const wchar_t* p = argument;; ++p) {
        if (*p == L'\\') {
            backslashes++;
            continue;
        }
        size_t repeat = !*p ? backslashes * 2 :
            *p == L'"' ? backslashes * 2 + 1 : backslashes;
        for (size_t i = 0; i < repeat; ++i) {
            if (!Batch_Append(output, length, capacity, L"\\", 1))
                return false;
        }
        backslashes = 0;
        if (!*p) break;
        if (!Batch_Append(output, length, capacity, p, 1)) return false;
    }
    return Batch_Append(output, length, capacity, L"\"", 1);
}

static HANDLE Batch_DuplicateInheritable(HANDLE source, bool output) {
    HANDLE duplicate = NULL;
    if (source && source != INVALID_HANDLE_VALUE &&
        DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(),
            &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS))
        return duplicate;
    SECURITY_ATTRIBUTES attributes = { sizeof(attributes), NULL, TRUE };
    duplicate = CreateFileW(L"NUL", output ? GENERIC_WRITE : GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes, OPEN_EXISTING, 0,
        NULL);
    return duplicate == INVALID_HANDLE_VALUE ? NULL : duplicate;
}

/*
 * Starts "ksh --batch-stage" for a pipe stage or FOR /F command, mirroring
 * the child cmd.exe that CMD uses. Only the three standard handles are
 * inherited so pipe readers reliably see end-of-file.
 */
HANDLE Batch_SpawnStage(BatchContext* context, const wchar_t* text,
    HANDLE input, HANDLE output, HANDLE error) {
    wchar_t executable[MAX_PATH * 2];
    DWORD executableLength = GetModuleFileNameW(NULL, executable,
        _countof(executable));
    if (!executableLength || executableLength >= _countof(executable))
        return NULL;
    wchar_t* commandLine = NULL;
    size_t length = 0;
    size_t capacity = 0;
    bool ok = Batch_AppendQuotedArgument(&commandLine, &length, &capacity,
            executable) &&
        Batch_Append(&commandLine, &length, &capacity, L" --batch-stage",
            14) &&
        Batch_AppendQuotedArgument(&commandLine, &length, &capacity,
            context->batchFile ? context->batchFile : L"") &&
        Batch_AppendQuotedArgument(&commandLine, &length, &capacity, text);
    for (int i = context->shift; ok && i < context->argumentCount; ++i)
        ok = Batch_AppendQuotedArgument(&commandLine, &length, &capacity,
            context->arguments[i] ? context->arguments[i] : L"");
    HANDLE handles[3] = {
        Batch_DuplicateInheritable(input, false),
        Batch_DuplicateInheritable(output, true),
        Batch_DuplicateInheritable(error, true)
    };
    HANDLE process = NULL;
    if (ok && handles[0] && handles[1] && handles[2]) {
        SIZE_T attributeSize = 0;
        InitializeProcThreadAttributeList(NULL, 1, 0, &attributeSize);
        LPPROC_THREAD_ATTRIBUTE_LIST attributeList =
            (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attributeSize);
        if (attributeList &&
            InitializeProcThreadAttributeList(attributeList, 1, 0,
                &attributeSize)) {
            if (UpdateProcThreadAttribute(attributeList, 0,
                    PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                    sizeof(handles), NULL, NULL)) {
                STARTUPINFOEXW startup;
                PROCESS_INFORMATION information;
                memset(&startup, 0, sizeof(startup));
                startup.StartupInfo.cb = sizeof(startup);
                startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
                startup.StartupInfo.hStdInput = handles[0];
                startup.StartupInfo.hStdOutput = handles[1];
                startup.StartupInfo.hStdError = handles[2];
                startup.lpAttributeList = attributeList;
                if (CreateProcessW(NULL, commandLine, NULL, NULL, TRUE,
                        EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                        &startup.StartupInfo, &information)) {
                    CloseHandle(information.hThread);
                    process = information.hProcess;
                }
            }
            DeleteProcThreadAttributeList(attributeList);
        }
        free(attributeList);
    }
    for (int i = 0; i < 3; ++i) {
        if (handles[i]) CloseHandle(handles[i]);
    }
    free(commandLine);
    if (!process)
        fwprintf(stderr, L"ksh: batch: cannot start child shell\n");
    return process;
}

static size_t Batch_LabelLength(const wchar_t* label) {
    size_t length = 0;
    while (label[length] && !iswspace(label[length])) length++;
    return length;
}

/* Like CMD, searches forward from the current line and then wraps around. */
int Batch_FindLabel(const BatchFileLines* lines, const wchar_t* label,
    size_t startLine) {
    while (*label == L':' || iswspace(*label)) label++;
    size_t labelLength = Batch_LabelLength(label);
    if (!labelLength || !lines->count) return -1;
    if (startLine >= lines->count) startLine = 0;
    for (size_t offset = 0; offset < lines->count; ++offset) {
        size_t i = (startLine + offset) % lines->count;
        const wchar_t* line = Batch_SkipSpaces(lines->items[i]);
        if (*line != L':') continue;
        line++;
        if (Batch_LabelLength(line) == labelLength &&
            _wcsnicmp(line, label, labelLength) == 0)
            return (int)i;
    }
    return -1;
}

void Batch_SetLoopValue(BatchContext* context,
    BatchLoopVariable* variable, const wchar_t* value) {
    wchar_t* replacement = SafeStrDup(value);
    if (!replacement) return;
    free(variable->value);
    variable->value = replacement;
    context->loopVariables = variable;
}

static const wchar_t* Batch_SkipBlanks(const wchar_t* text) {
    while (*text == L' ' || *text == L'\t' || *text == L'\r') text++;
    return text;
}

static const wchar_t* Batch_SkipCommandPrefix(const wchar_t* text) {
    text = Batch_SkipBlanks(text);
    while (*text == L'@') text = Batch_SkipBlanks(text + 1);
    return text;
}

static bool Batch_IsCommentStatement(const wchar_t* text) {
    text = Batch_SkipCommandPrefix(text);
    if (*text == L':') return true;
    return _wcsnicmp(text, L"rem", 3) == 0 &&
        (!text[3] || iswspace(text[3]));
}

/* Returns the index of the ')' closing the block opened at text[open]. */
size_t Batch_FindBlockEnd(const wchar_t* text, size_t open) {
    int depth = 0;
    bool quoted = false;
    bool lineStart = false;
    for (size_t i = open; text[i]; ++i) {
        if (lineStart) {
            lineStart = false;
            if (Batch_IsCommentStatement(text + i)) {
                while (text[i] && text[i] != L'\n') i++;
                if (!text[i]) break;
                lineStart = true;
                continue;
            }
        }
        wchar_t c = text[i];
        if (c == L'\n') {
            quoted = false;
            lineStart = true;
            continue;
        }
        if (c == L'"') {
            quoted = !quoted;
            continue;
        }
        if (quoted) continue;
        if (c == L'^' && text[i + 1]) {
            i++;
            continue;
        }
        if (c == L'(') {
            depth++;
        } else if (c == L')' && depth > 0 && --depth == 0) {
            return i;
        }
    }
    return SIZE_MAX;
}

bool Batch_FlowStopped(IShell* shell, BatchContext* context) {
    return !shell->running || context->flow != BATCH_FLOW_NONE;
}

int Batch_ExecuteStatements(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, const wchar_t* text);
int Batch_RunLines(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, size_t firstLine);



static bool Batch_CombineDestination(const wchar_t* destination,
    const wchar_t* source, bool directory, wchar_t* output,
    size_t outputCapacity) {
    if (!directory) {
        return wcslen(destination) < outputCapacity &&
            wcscpy_s(output, outputCapacity, destination) == 0;
    }
    const wchar_t* leaf = wcsrchr(source, L'\\');
    const wchar_t* forwardSlash = wcsrchr(source, L'/');
    if (forwardSlash && (!leaf || forwardSlash > leaf)) leaf = forwardSlash;
    leaf = leaf ? leaf + 1 : source;
    return swprintf_s(output, outputCapacity, L"%ls\\%ls", destination,
        leaf) >= 0;
}

static bool Batch_ConfirmOverwrite(const wchar_t* path, bool* overwriteAll) {
    wprintf(L"Overwrite %ls (Yes/No/All)? ", path);
    fflush(stdout);
    wint_t response = towupper(fgetwc(stdin));
    wint_t character;
    while ((character = fgetwc(stdin)) != WEOF && character != L'\n') { }
    *overwriteAll = response == L'A';
    return response == L'Y' || *overwriteAll;
}

static bool Batch_CopyAsciiFile(const wchar_t* source,
    const wchar_t* destination, bool overwrite) {
    HANDLE input = CreateFileW(source, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (input == INVALID_HANDLE_VALUE) return false;
    HANDLE output = CreateFileW(destination, GENERIC_WRITE, 0, NULL,
        overwrite ? CREATE_ALWAYS : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE) {
        CloseHandle(input);
        return false;
    }
    bool success = true;
    BYTE buffer[65536];
    bool finished = false;
    while (!finished) {
        DWORD bytesRead = 0;
        if (!ReadFile(input, buffer, sizeof(buffer), &bytesRead, NULL)) {
            success = false;
            break;
        }
        if (!bytesRead) break;
        BYTE* marker = (BYTE*)memchr(buffer, 0x1a, bytesRead);
        DWORD bytesToWrite = marker ? (DWORD)(marker - buffer) : bytesRead;
        DWORD offset = 0;
        while (offset < bytesToWrite) {
            DWORD bytesWritten = 0;
            if (!WriteFile(output, buffer + offset, bytesToWrite - offset,
                    &bytesWritten, NULL) || !bytesWritten) {
                success = false;
                break;
            }
            offset += bytesWritten;
        }
        if (!success || marker) finished = true;
    }
    if (success && !FlushFileBuffers(output)) success = false;
    CloseHandle(input);
    CloseHandle(output);
    if (!success && !overwrite) DeleteFileW(destination);
    return success;
}

static bool Batch_VerifyCopy(const wchar_t* source,
    const wchar_t* destination, bool ascii) {
    HANDLE sourceFile = CreateFileW(source, GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (sourceFile == INVALID_HANDLE_VALUE) return false;
    HANDLE destinationFile = CreateFileW(destination, GENERIC_READ,
        FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN,
        NULL);
    if (destinationFile == INVALID_HANDLE_VALUE) {
        CloseHandle(sourceFile);
        return false;
    }
    LARGE_INTEGER sourceSize, destinationSize;
    bool equal = GetFileSizeEx(sourceFile, &sourceSize) &&
        GetFileSizeEx(destinationFile, &destinationSize);
    BYTE sourceBuffer[65536], destinationBuffer[65536];
    while (equal) {
        DWORD sourceBytes = 0, destinationBytes = 0;
        if (!ReadFile(sourceFile, sourceBuffer, sizeof(sourceBuffer),
                &sourceBytes, NULL) ||
            !ReadFile(destinationFile, destinationBuffer,
                sizeof(destinationBuffer), &destinationBytes, NULL)) {
            equal = false;
            break;
        }
        if (!sourceBytes || !destinationBytes) {
            equal = sourceBytes == 0 && destinationBytes == 0;
            break;
        }
        BYTE* marker = ascii ?
            (BYTE*)memchr(sourceBuffer, 0x1a, sourceBytes) : NULL;
        DWORD expectedBytes = marker ?
            (DWORD)(marker - sourceBuffer) : sourceBytes;
        if (destinationBytes != expectedBytes ||
            memcmp(sourceBuffer, destinationBuffer, expectedBytes) != 0) {
            equal = false;
            break;
        }
        if (marker) {
            DWORD extraBytes = 0;
            BYTE extra;
            equal = !ReadFile(destinationFile, &extra, 1, &extraBytes, NULL) ||
                extraBytes == 0;
            break;
        }
    }
    if (equal && !ascii)
        equal = sourceSize.QuadPart == destinationSize.QuadPart;
    CloseHandle(destinationFile);
    CloseHandle(sourceFile);
    return equal;
}

int Batch_CopyMove(IShell* shell, const wchar_t* rest, bool move) {
    bool overwrite = false;
    bool forcePrompt = true;
    bool verify = shell->batchVerify;
    bool restartable = false;
    bool ascii = false;
    bool modeSpecified = false;
    bool useShortNames = false;
    if (!move) {
        const wchar_t* copyCommand = shell->env->Get(shell->env, L"COPYCMD");
        wchar_t* inherited = copyCommand ? NULL :
            GetEnvironmentValue(L"COPYCMD");
        if (!copyCommand) copyCommand = inherited;
        if (copyCommand && _wcsicmp(Batch_SkipSpaces(copyCommand), L"/y") == 0) {
            overwrite = true;
            forcePrompt = false;
        } else if (copyCommand &&
            _wcsicmp(Batch_SkipSpaces(copyCommand), L"/-y") == 0) {
            overwrite = false;
            forcePrompt = true;
        }
        free(inherited);
    }
    wchar_t source[MAX_LINE_LEN] = { 0 };
    wchar_t destination[MAX_LINE_LEN] = { 0 };
    const wchar_t* cursor = rest;
    wchar_t word[MAX_LINE_LEN];
    int operandCount = 0;
    while (Batch_ReadWord(&cursor, word, _countof(word))) {
        if (word[0] == L'/' || word[0] == L'-') {
            if (_wcsicmp(word, L"/y") == 0 ||
                _wcsicmp(word, L"-y") == 0) {
                overwrite = true;
                forcePrompt = false;
                continue;
            }
            if (_wcsicmp(word, L"/-y") == 0) {
                overwrite = false;
                forcePrompt = true;
                continue;
            }
            if (!move && _wcsicmp(word, L"/v") == 0) {
                verify = true;
                continue;
            }
            if (!move && _wcsicmp(word, L"/z") == 0) {
                restartable = true;
                continue;
            }
            if (!move && _wcsicmp(word, L"/n") == 0) {
                useShortNames = true;
                continue;
            }
            if (!move && _wcsicmp(word, L"/a") == 0) {
                if (modeSpecified && !ascii) {
                    fwprintf(stderr, L"ksh: batch copy: /A and /B conflict\n");
                    return 2;
                }
                ascii = true;
                modeSpecified = true;
                continue;
            }
            if (!move && _wcsicmp(word, L"/b") == 0) {
                if (modeSpecified && ascii) {
                    fwprintf(stderr, L"ksh: batch copy: /A and /B conflict\n");
                    return 2;
                }
                ascii = false;
                modeSpecified = true;
                continue;
            }
            if (_wcsicmp(word, L"/?") == 0) {
                if (move)
                    wprintf(L"MOVE [/Y | /-Y] source destination\n");
                else
                    wprintf(L"COPY [/A | /B] [/V] [/N] [/Y | /-Y] [/Z] source [destination]\n");
                return 0;
            }
            fwprintf(stderr, L"ksh: batch %ls: invalid option %ls\n",
                move ? L"move" : L"copy", word);
            return 2;
        }
        if (operandCount == 0) wcscpy_s(source, _countof(source), word);
        else if (operandCount == 1)
            wcscpy_s(destination, _countof(destination), word);
        operandCount++;
    }
    if (operandCount < (move ? 2 : 1) || operandCount > 2) {
        fwprintf(stderr, L"ksh: batch %ls: expected %ls\n",
            move ? L"move" : L"copy",
            move ? L"source and destination" :
                L"source and optional destination");
        return 2;
    }
    if (operandCount == 1) destination[0] = L'\0';
    DWORD destinationAttributes = *destination ?
        GetFileAttributesW(destination) : INVALID_FILE_ATTRIBUTES;
    bool destinationDirectory = destinationAttributes !=
        INVALID_FILE_ATTRIBUTES &&
        (destinationAttributes & FILE_ATTRIBUTE_DIRECTORY);
    bool wildcard = wcspbrk(source, L"*?") != NULL;
    DWORD copied = 0;
    bool failed = false;
    if (!move && _wcsicmp(source, L"con") == 0) {
        if (!*destination) {
            fwprintf(stderr, L"ksh: batch copy: destination required\n");
            return 1;
        }
        FILE* outFp = NULL;
        if (_wfopen_s(&outFp, destination, L"wb") != 0 || !outFp) {
            fwprintf(stderr, L"ksh: batch copy: cannot open destination: %ls\n", destination);
            return 1;
        }
        char buf[4096];
        while (fgets(buf, sizeof(buf), stdin)) {
            char* cz = strchr(buf, 0x1A); /* Ctrl+Z */
            if (cz) {
                *cz = '\0';
                fputs(buf, outFp);
                break;
            }
            fputs(buf, outFp);
        }
        fclose(outFp);
        wprintf(L"        1 file(s) copied.\n");
        return 0;
    }
    if (!move && _wcsicmp(destination, L"con") == 0) {
        FILE* inFp = NULL;
        if (_wfopen_s(&inFp, source, L"rb") != 0 || !inFp) {
            fwprintf(stderr, L"ksh: batch copy: source not found: %ls\n", source);
            return 1;
        }
        char buf[4096];
        size_t nr;
        while ((nr = fread(buf, 1, sizeof(buf), inFp)) > 0) {
            fwrite(buf, 1, nr, stdout);
        }
        fclose(inFp);
        wprintf(L"        1 file(s) copied.\n");
        return 0;
    }
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(source, &data);
    if (search == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"ksh: batch %ls: source not found: %ls\n",
            move ? L"move" : L"copy", source);
        return 1;
    }
    const wchar_t* separator = wcsrchr(source, L'\\');
    const wchar_t* slash = wcsrchr(source, L'/');
    if (slash && (!separator || slash > separator)) separator = slash;
    size_t prefixLength = wildcard && separator ?
        (size_t)(separator - source) + 1 : 0;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wchar_t sourcePath[MAX_PATH * 2];
        if (wildcard) {
            if (prefixLength + wcslen(data.cFileName) >=
                _countof(sourcePath)) {
                failed = true;
                continue;
            }
            if (prefixLength)
                memcpy(sourcePath, source, prefixLength * sizeof(wchar_t));
            wcscpy_s(sourcePath + prefixLength,
                _countof(sourcePath) - prefixLength, data.cFileName);
        } else {
            wcscpy_s(sourcePath, _countof(sourcePath), source);
        }
        wchar_t destinationPath[MAX_PATH * 2];
        bool intoDirectory = destinationDirectory || wildcard;
        if (!*destination) {
            if (!move) {
                fwprintf(stderr,
                    L"ksh: batch copy: destination required\n");
                failed = true;
                break;
            }
            if (!GetCurrentDirectoryW(_countof(destination), destination)) {
                failed = true;
                break;
            }
            destinationAttributes = GetFileAttributesW(destination);
            destinationDirectory = destinationAttributes !=
                INVALID_FILE_ATTRIBUTES &&
                (destinationAttributes & FILE_ATTRIBUTE_DIRECTORY);
            intoDirectory = true;
        }
        if (!Batch_CombineDestination(destination, sourcePath, intoDirectory,
                destinationPath, _countof(destinationPath))) {
            failed = true;
            continue;
        }
        if (useShortNames) {
            wchar_t shortPath[MAX_PATH * 2];
            DWORD shortLength = GetShortPathNameW(sourcePath, shortPath,
                (DWORD)_countof(shortPath));
            if (shortLength && shortLength < _countof(shortPath))
                wcscpy_s(sourcePath, _countof(sourcePath), shortPath);
        }
        bool replaceDestination = overwrite;
        DWORD fileAttributes = GetFileAttributesW(destinationPath);
        if (fileAttributes != INVALID_FILE_ATTRIBUTES &&
            !replaceDestination) {
            if (!forcePrompt) {
                failed = true;
                continue;
            }
            bool overwriteAll = false;
            if (!Batch_ConfirmOverwrite(destinationPath, &overwriteAll))
                continue;
            replaceDestination = true;
            if (overwriteAll) overwrite = true;
        }
        bool success;
        if (move) {
            success = MoveFileExW(sourcePath, destinationPath,
                MOVEFILE_COPY_ALLOWED |
                    (replaceDestination ? MOVEFILE_REPLACE_EXISTING : 0)) != FALSE;
        } else if (ascii) {
            success = Batch_CopyAsciiFile(sourcePath, destinationPath,
                replaceDestination);
        } else {
            success = restartable ?
                CopyFileExW(sourcePath, destinationPath, NULL, NULL, NULL,
                    COPY_FILE_RESTARTABLE |
                        (replaceDestination ? 0 : COPY_FILE_FAIL_IF_EXISTS)) :
                CopyFileW(sourcePath, destinationPath,
                    replaceDestination ? FALSE : TRUE) != FALSE;
        }
        if (success && verify &&
            !Batch_VerifyCopy(sourcePath, destinationPath, ascii)) {
            fwprintf(stderr, L"ksh: batch copy: verification failed: %ls\n",
                destinationPath);
            success = false;
        }
        if (success) copied++;
        else failed = true;
    } while (FindNextFileW(search, &data));
    FindClose(search);
    if (!copied || failed) return 1;
    wprintf(L"%lu file(s) %ls.\n", copied,
        move ? L"moved" : L"copied");
    return 0;
}

static int Batch_TypeFiles(const wchar_t* rest) {
    bool pauseBetweenPages = false;
    unsigned int tabWidth = 8;
    const wchar_t* cursor = rest;
    wchar_t** paths = calloc(MAX_ARGS, sizeof(*paths));
    if (!paths) {
        fwprintf(stderr, L"ksh: batch type: out of memory\n");
        return 1;
    }
    int pathCount = 0;
    int status = 0;
    wchar_t word[MAX_LINE_LEN];
    while (Batch_ReadWord(&cursor, word, _countof(word))) {
        if (_wcsicmp(word, L"/p") == 0) {
            pauseBetweenPages = true;
        } else if (_wcsnicmp(word, L"/t:", 3) == 0) {
            wchar_t* end = NULL;
            errno = 0;
            unsigned long parsed = wcstoul(word + 3, &end, 10);
            if (errno || end == word + 3 || *end || parsed == 0 ||
                parsed > 256) {
                fwprintf(stderr, L"ksh: batch type: invalid tab width\n");
                status = 2;
                goto cleanup;
            }
            tabWidth = (unsigned int)parsed;
        } else if (_wcsicmp(word, L"/?") == 0) {
            wprintf(L"TYPE [/P] [/T:n] [drive:][path]filename\n");
            goto cleanup;
        } else if (word[0] == L'/' || word[0] == L'-') {
            fwprintf(stderr, L"ksh: batch type: invalid option %ls\n", word);
            status = 2;
            goto cleanup;
        } else {
            if (pathCount == MAX_ARGS) {
                fwprintf(stderr, L"ksh: batch type: too many files\n");
                status = 2;
                goto cleanup;
            }
            size_t pathLength = wcslen(word) + 1;
            paths[pathCount] = malloc(pathLength * sizeof(*word));
            if (!paths[pathCount]) {
                fwprintf(stderr, L"ksh: batch type: out of memory\n");
                status = 1;
                goto cleanup;
            }
            wcscpy_s(paths[pathCount], pathLength, word);
            pathCount++;
        }
    }
    if (!pathCount) {
        status = 2;
        goto cleanup;
    }

    HANDLE outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD consoleMode;
    CONSOLE_SCREEN_BUFFER_INFO screenInfo;
    bool consoleOutput = outputHandle != INVALID_HANDLE_VALUE &&
        GetConsoleMode(outputHandle, &consoleMode) &&
        GetConsoleScreenBufferInfo(outputHandle, &screenInfo);
    DWORD pageHeight = consoleOutput ?
        (DWORD)(screenInfo.srWindow.Bottom - screenInfo.srWindow.Top + 1) : 0;
    DWORD rowCount = 0;
    for (int fileIndex = 0; fileIndex < pathCount; ++fileIndex) {
        bool isCon = (_wcsicmp(paths[fileIndex], L"con") == 0);
        FILE* file = isCon ? stdin : NULL;
        if (!isCon && (_wfopen_s(&file, paths[fileIndex], L"r, ccs=UTF-8") != 0 ||
            !file)) {
            fwprintf(stderr, L"ksh: batch type: cannot open %ls\n",
                paths[fileIndex]);
            status = 1;
            continue;
        }
        unsigned int column = 0;
        wint_t character;
        while ((character = fgetwc(file)) != WEOF) {
            if (character == 0x1A) break; /* Ctrl+Z */
            if (character == L'\t') {
                unsigned int spaces = tabWidth - column % tabWidth;
                while (spaces--) {
                    if (fputwc(L' ', stdout) == WEOF) {
                        status = 1;
                        break;
                    }
                    column++;
                }
            } else {
                if (fputwc((wchar_t)character, stdout) == WEOF) {
                    status = 1;
                    break;
                }
                if (character == L'\n') {
                    column = 0;
                    if (pauseBetweenPages && consoleOutput &&
                        ++rowCount >= pageHeight - 1) {
                        fputws(L"Press any key to continue . . . ", stdout);
                        fflush(stdout);
                        (void)_getwch();
                        fputws(L"\r                                \r",
                            stdout);
                        rowCount = 0;
                    }
                } else if (character != L'\r') {
                    column++;
                }
            }
            if (status) break;
        }
        if (ferror(file)) status = 1;
        if (!isCon) fclose(file);
        if (status) break;
    }

cleanup:
    for (int i = 0; i < pathCount; ++i) free(paths[i]);
    free(paths);
    return status;
}

bool Batch_DeleteTree(const wchar_t* directory) {
    wchar_t pattern[MAX_PATH * 2];
    if (swprintf_s(pattern, _countof(pattern), L"%ls\\*", directory) < 0)
        return false;
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(pattern, &data);
    if (search == INVALID_HANDLE_VALUE) return false;
    bool success = true;
    do {
        if (wcscmp(data.cFileName, L".") == 0 ||
            wcscmp(data.cFileName, L"..") == 0)
            continue;
        wchar_t path[MAX_PATH * 2];
        if (swprintf_s(path, _countof(path), L"%ls\\%ls", directory,
                data.cFileName) < 0) {
            success = false;
            continue;
        }
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                if (!RemoveDirectoryW(path)) success = false;
            } else if (!Batch_DeleteTree(path)) {
                success = false;
            }
        } else {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) &&
                !SetFileAttributesW(path,
                    data.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY))
                success = false;
            if (!DeleteFileW(path)) success = false;
        }
    } while (FindNextFileW(search, &data));
    FindClose(search);
    return RemoveDirectoryW(directory) != FALSE && success;
}

bool Batch_CreateDirectoryPath(const wchar_t* path) {
    wchar_t fullPath[MAX_PATH];
    DWORD fullLength = GetFullPathNameW(path, _countof(fullPath), fullPath,
        NULL);
    if (!fullLength || fullLength >= _countof(fullPath)) return false;
    size_t length = wcslen(fullPath);
    while (length > 3 &&
        (fullPath[length - 1] == L'\\' || fullPath[length - 1] == L'/'))
        fullPath[--length] = L'\0';

    size_t rootLength = 0;
    if (fullPath[0] == L'\\' && fullPath[1] == L'\\') {
        const wchar_t* serverEnd = wcschr(fullPath + 2, L'\\');
        if (!serverEnd) return false;
        const wchar_t* shareEnd = wcschr(serverEnd + 1, L'\\');
        rootLength = shareEnd ? (size_t)(shareEnd - fullPath) + 1 : length;
    } else if (fullPath[1] == L':') {
        if (fullPath[2] != L'\\' && fullPath[2] != L'/') return false;
        rootLength = 3;
    } else if (fullPath[0] == L'\\' || fullPath[0] == L'/') {
        rootLength = 1;
    } else {
        return false;
    }

    bool createdFinalDirectory = false;
    for (size_t end = rootLength; end <= length; ++end) {
        if (end != length && fullPath[end] != L'\\' &&
            fullPath[end] != L'/')
            continue;
        if (end == rootLength) continue;
        wchar_t separator = fullPath[end];
        fullPath[end] = L'\0';
        bool isFinal = end == length;
        if (!CreateDirectoryW(fullPath, NULL)) {
            DWORD error = GetLastError();
            DWORD attributes = error == ERROR_ALREADY_EXISTS ?
                GetFileAttributesW(fullPath) : INVALID_FILE_ATTRIBUTES;
            if (isFinal || attributes == INVALID_FILE_ATTRIBUTES ||
                !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                fullPath[end] = separator;
                return false;
            }
        } else if (isFinal) {
            createdFinalDirectory = true;
        }
        fullPath[end] = separator;
    }
    return createdFinalDirectory;
}

bool Batch_IsProtectedDeleteTarget(const wchar_t* directory) {
    wchar_t fullPath[MAX_PATH];
    wchar_t volumeRoot[MAX_PATH];
    DWORD fullLength = GetFullPathNameW(directory, _countof(fullPath),
        fullPath, NULL);
    if (!fullLength || fullLength >= _countof(fullPath) ||
        !GetVolumePathNameW(fullPath, volumeRoot, _countof(volumeRoot)))
        return true;

    size_t fullLengthNormalized = wcslen(fullPath);
    while (fullLengthNormalized > 3 &&
        (fullPath[fullLengthNormalized - 1] == L'\\' ||
            fullPath[fullLengthNormalized - 1] == L'/'))
        fullPath[--fullLengthNormalized] = L'\0';
    size_t volumeLength = wcslen(volumeRoot);
    while (volumeLength > 3 &&
        (volumeRoot[volumeLength - 1] == L'\\' ||
            volumeRoot[volumeLength - 1] == L'/'))
        volumeRoot[--volumeLength] = L'\0';
    if (_wcsicmp(fullPath, volumeRoot) == 0) return true;

    wchar_t currentDirectory[MAX_PATH];
    DWORD currentLength = GetCurrentDirectoryW(_countof(currentDirectory),
        currentDirectory);
    if (!currentLength || currentLength >= _countof(currentDirectory) ||
        !GetFullPathNameW(currentDirectory, _countof(currentDirectory),
            currentDirectory, NULL))
        return true;
    size_t currentPathLength = wcslen(currentDirectory);
    while (currentPathLength > 3 &&
        (currentDirectory[currentPathLength - 1] == L'\\' ||
            currentDirectory[currentPathLength - 1] == L'/'))
        currentDirectory[--currentPathLength] = L'\0';
    if (_wcsicmp(fullPath, currentDirectory) == 0) return true;
    if (currentPathLength > fullLengthNormalized &&
        _wcsnicmp(currentDirectory, fullPath, fullLengthNormalized) == 0 &&
        (fullPath[fullLengthNormalized - 1] == L'\\' ||
            fullPath[fullLengthNormalized - 1] == L'/' ||
            currentDirectory[fullLengthNormalized] == L'\\' ||
            currentDirectory[fullLengthNormalized] == L'/'))
        return true;
    return false;
}

bool Batch_ConfirmRecursiveDelete(const wchar_t* directory) {
    wprintf(L"%ls, Are you sure (Y/N)? ", directory);
    fflush(stdout);
    wint_t response = towupper(fgetwc(stdin));
    wint_t character;
    while ((character = fgetwc(stdin)) != WEOF && character != L'\n') { }
    return response == L'Y';
}

static bool Batch_ParseDeleteAttributes(const wchar_t* option,
    DWORD* requiredAttributes, DWORD* excludedAttributes) {
    if (_wcsnicmp(option, L"/a", 2) != 0 ||
        (option[2] && option[2] != L':'))
        return false;
    const wchar_t* attributes = option[2] == L':' ? option + 3 : L"";
    bool exclude = false;
    for (const wchar_t* current = attributes; *current; ++current) {
        DWORD attribute;
        if (*current == L'-') {
            exclude = true;
            continue;
        }
        switch (towlower(*current)) {
        case L'a': attribute = FILE_ATTRIBUTE_ARCHIVE; break;
        case L'c': attribute = FILE_ATTRIBUTE_COMPRESSED; break;
        case L'd': attribute = FILE_ATTRIBUTE_DIRECTORY; break;
        case L'e': attribute = FILE_ATTRIBUTE_ENCRYPTED; break;
        case L'h': attribute = FILE_ATTRIBUTE_HIDDEN; break;
        case L'i': attribute = FILE_ATTRIBUTE_NOT_CONTENT_INDEXED; break;
        case L'l': attribute = FILE_ATTRIBUTE_REPARSE_POINT; break;
        case L'o': attribute = FILE_ATTRIBUTE_OFFLINE; break;
        case L'r': attribute = FILE_ATTRIBUTE_READONLY; break;
        case L's': attribute = FILE_ATTRIBUTE_SYSTEM; break;
        case L't': attribute = FILE_ATTRIBUTE_TEMPORARY; break;
        default: return false;
        }
        if (exclude) *excludedAttributes |= attribute;
        else *requiredAttributes |= attribute;
    }
    return true;
}

static bool Batch_MatchesDeleteAttributes(DWORD attributes,
    DWORD requiredAttributes, DWORD excludedAttributes) {
    return (attributes & requiredAttributes) == requiredAttributes &&
        (attributes & excludedAttributes) == 0;
}

static bool Batch_DeletePatternInDirectory(const wchar_t* directory,
    const wchar_t* pattern, bool force, bool prompt, bool recursive,
    DWORD requiredAttributes, DWORD excludedAttributes, unsigned int depth,
    unsigned long* deleted) {
    if (depth > 64) return false;
    wchar_t query[MAX_PATH * 2];
    if (swprintf_s(query, _countof(query), L"%ls\\%ls", directory,
            pattern) < 0)
        return false;
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(query, &data);
    bool success = true;
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (!Batch_MatchesDeleteAttributes(data.dwFileAttributes,
                    requiredAttributes, excludedAttributes))
                continue;
            wchar_t path[MAX_PATH * 2];
            if (swprintf_s(path, _countof(path), L"%ls\\%ls", directory,
                    data.cFileName) < 0) {
                success = false;
                continue;
            }
            if (prompt) {
                wprintf(L"%ls, Delete (Y/N)? ", path);
                wint_t response = towupper(getwchar());
                while (getwchar() != L'\n' && !feof(stdin)) { }
                if (response != L'Y') continue;
            }
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) && force)
                (void)SetFileAttributesW(path,
                    data.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            if (DeleteFileW(path)) (*deleted)++;
            else success = false;
        } while (FindNextFileW(search, &data));
        FindClose(search);
    }
    if (!recursive) return success;
    wchar_t children[MAX_PATH * 2];
    if (swprintf_s(children, _countof(children), L"%ls\\*", directory) < 0)
        return false;
    search = FindFirstFileW(children, &data);
    if (search == INVALID_HANDLE_VALUE) return success;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            wcscmp(data.cFileName, L".") == 0 ||
            wcscmp(data.cFileName, L"..") == 0)
            continue;
        wchar_t child[MAX_PATH * 2];
        if (swprintf_s(child, _countof(child), L"%ls\\%ls", directory,
                data.cFileName) < 0 ||
            !Batch_DeletePatternInDirectory(child, pattern, force, prompt,
                recursive, requiredAttributes, excludedAttributes,
                depth + 1, deleted))
            success = false;
    } while (FindNextFileW(search, &data));
    FindClose(search);
    return success;
}

int Batch_DeleteFiles(const wchar_t* rest) {
    bool force = false;
    bool prompt = false;
    bool recursive = false;
    DWORD requiredAttributes = 0;
    DWORD excludedAttributes = 0;
    const wchar_t* cursor = rest;
    wchar_t (*paths)[MAX_LINE_LEN] =
        (wchar_t(*)[MAX_LINE_LEN])calloc(MAX_ARGS, sizeof(*paths));
    if (!paths) return 1;
    int pathCount = 0;
    wchar_t word[MAX_LINE_LEN];
    while (Batch_ReadWord(&cursor, word, _countof(word))) {
        if (word[0] == L'/' || word[0] == L'-') {
            if (_wcsicmp(word, L"/f") == 0) force = true;
            else if (_wcsicmp(word, L"/p") == 0) prompt = true;
            else if (_wcsicmp(word, L"/s") == 0) recursive = true;
            else if (_wcsicmp(word, L"/q") == 0) { }
            else if (_wcsnicmp(word, L"/a", 2) == 0) {
                if (!Batch_ParseDeleteAttributes(word, &requiredAttributes,
                        &excludedAttributes)) {
                    fwprintf(stderr,
                        L"ksh: batch del: invalid attribute qualifier %ls\n",
                        word);
                    free(paths);
                    return 2;
                }
            }
            else if (_wcsicmp(word, L"/?") == 0) {
                wprintf(L"DEL [/P] [/F] [/S] [/Q] files\n");
                free(paths);
                return 0;
            } else {
                fwprintf(stderr, L"ksh: batch del: invalid option %ls\n",
                    word);
                free(paths);
                return 2;
            }
            continue;
        }
        if (pathCount == MAX_ARGS) {
            free(paths);
            return 2;
        }
        wcscpy_s(paths[pathCount++], MAX_LINE_LEN, word);
    }
    if (!pathCount) {
        free(paths);
        return 2;
    }
    bool success = true;
    unsigned long deleted = 0;
    for (int i = 0; i < pathCount; ++i) {
        wchar_t* separator = wcsrchr(paths[i], L'\\');
        wchar_t* slash = wcsrchr(paths[i], L'/');
        if (slash && (!separator || slash > separator)) separator = slash;
        wchar_t directory[MAX_PATH * 2];
        wchar_t pattern[MAX_LINE_LEN];
        if (separator) {
            size_t prefixLength = (size_t)(separator - paths[i]);
            if (prefixLength >= _countof(directory) ||
                wcslen(separator + 1) >= _countof(pattern)) {
                free(paths);
                return 2;
            }
            memcpy(directory, paths[i], prefixLength * sizeof(wchar_t));
            directory[prefixLength] = L'\0';
            wcscpy_s(pattern, _countof(pattern), separator + 1);
        } else {
            if (!GetCurrentDirectoryW(_countof(directory), directory)) {
                free(paths);
                return 1;
            }
            wcscpy_s(pattern, _countof(pattern), paths[i]);
        }
        if (!Batch_DeletePatternInDirectory(directory, pattern, force, prompt,
                recursive, requiredAttributes, excludedAttributes, 0,
                &deleted))
            success = false;
    }
    if (deleted) wprintf(L"%lu file(s) deleted.\n", deleted);
    free(paths);
    return success && deleted ? 0 : 1;
}

int Batch_ExecuteCodePage(const wchar_t* rest) {
    rest = Batch_SkipSpaces(rest);
    if (!*rest) {
        UINT codePage = GetConsoleOutputCP();
        if (!codePage) return 1;
        wprintf(L"Active code page: %u\n", codePage);
        return ferror(stdout) ? 1 : 0;
    }
    wchar_t* end = NULL;
    unsigned long codePage = wcstoul(rest, &end, 10);
    if (end == rest || *Batch_SkipSpaces(end) ||
        codePage > UINT_MAX || !SetConsoleCP((UINT)codePage) ||
        !SetConsoleOutputCP((UINT)codePage)) {
        fwprintf(stderr, L"ksh: batch chcp: invalid or unavailable code page\n");
        return 1;
    }
    wprintf(L"Active code page: %lu\n", codePage);
    return ferror(stdout) ? 1 : 0;
}

int Batch_ExecuteColor(IShell* shell, const wchar_t* rest) {
    rest = Batch_SkipSpaces(rest);
    if (_wcsicmp(rest, L"/?") == 0) {
        wprintf(L"COLOR [attr]\n"
            L"  attr: two hexadecimal digits; first is background, second foreground.\n");
        return 0;
    }
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!console || console == INVALID_HANDLE_VALUE ||
        !GetConsoleScreenBufferInfo(console, &info)) {
        fwprintf(stderr, L"ksh: batch color: stdout is not a console\n");
        return 1;
    }
    if (!shell->capturedConsoleAttributes) {
        shell->defaultConsoleAttributes = info.wAttributes;
        shell->capturedConsoleAttributes = true;
    }
    if (!*rest)
        return SetConsoleTextAttribute(console,
            shell->defaultConsoleAttributes) ? 0 : 1;
    if (wcslen(rest) != 2 || !iswxdigit(rest[0]) || !iswxdigit(rest[1])) {
        fwprintf(stderr, L"ksh: batch color: expected two hexadecimal digits\n");
        return 1;
    }
    unsigned int background = (unsigned int)wcstoul(
        (wchar_t[]){rest[0], L'\0'}, NULL, 16);
    unsigned int foreground = (unsigned int)wcstoul(
        (wchar_t[]){rest[1], L'\0'}, NULL, 16);
    if (background == foreground) {
        fwprintf(stderr, L"ksh: batch color: foreground and background colors match\n");
        return 1;
    }
    WORD attributes = (WORD)((background << 4) | foreground);
    return SetConsoleTextAttribute(console, attributes) ? 0 : 1;
}

int Batch_ExecuteTitle(const wchar_t* rest) {
    rest = Batch_SkipSpaces(rest);
    if (_wcsicmp(rest, L"/?") == 0) {
        wprintf(L"TITLE [string]\n");
        return 0;
    }
    if (!SetConsoleTitleW(rest)) {
        fwprintf(stderr, L"ksh: batch title: cannot set console title\n");
        return 1;
    }
    return 0;
}

int Batch_ExecuteVolume(const wchar_t* rest) {
    wchar_t path[MAX_PATH];
    rest = Batch_SkipSpaces(rest);
    if (!*rest) {
        if (!GetCurrentDirectoryW(_countof(path), path)) return 1;
    } else {
        wchar_t operand[MAX_PATH];
        if (!Batch_ReadWord(&rest, operand, _countof(operand)) ||
            *Batch_SkipSpaces(rest)) {
            fwprintf(stderr, L"ksh: batch vol: expected at most one drive operand\n");
            return 2;
        }
        if (_wcsicmp(operand, L"/?") == 0) {
            wprintf(L"VOL [drive:]\n");
            return 0;
        }
        if (wcslen(operand) == 2 && operand[1] == L':')
            swprintf_s(path, _countof(path), L"%ls\\", operand);
        else
            wcscpy_s(path, _countof(path), operand);
    }
    wchar_t volumeRoot[MAX_PATH];
    wchar_t label[MAX_PATH];
    DWORD serial, maximumComponent, flags;
    if (!GetVolumePathNameW(path, volumeRoot, _countof(volumeRoot)) ||
        !GetVolumeInformationW(volumeRoot, label, _countof(label), &serial,
            &maximumComponent, &flags, NULL, 0)) {
        fwprintf(stderr, L"ksh: batch vol: cannot query volume information\n");
        return 1;
    }
    wchar_t drive = volumeRoot[0];
    if (label[0])
        wprintf(L"Volume in drive %lc is %ls\n", drive, label);
    else
        wprintf(L"Volume in drive %lc has no label.\n", drive);
    wprintf(L"Volume Serial Number is %04X-%04X\n",
        (unsigned int)(serial >> 16), (unsigned int)(serial & 0xffff));
    return ferror(stdout) ? 1 : 0;
}

int Batch_ExecuteBreak(const wchar_t* rest) {
    rest = Batch_SkipSpaces(rest);
    if (_wcsicmp(rest, L"/?") == 0) {
        wprintf(L"BREAK [ON | OFF]\n");
        return 0;
    }
    if (*rest && _wcsicmp(rest, L"on") != 0 &&
        _wcsicmp(rest, L"off") != 0) {
        fwprintf(stderr, L"ksh: batch break: usage: BREAK [ON | OFF]\n");
        return 2;
    }
    return 0;
}

static bool Batch_IsBatExtension(const BatchContext* context) {
    const wchar_t* path = context->batchFile;
    size_t length = path ? wcslen(path) : 0;
    return length >= 4 && _wcsicmp(path + length - 4, L".bat") == 0;
}

static bool Batch_WordEquals(const wchar_t* text, const wchar_t* word) {
    size_t length = wcslen(word);
    return _wcsnicmp(text, word, length) == 0 &&
        !*Batch_SkipSpaces(text + length);
}

static int Batch_CallLabel(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, wchar_t** arguments, int argumentCount) {
    int labelLine = Batch_FindLabel(lines, arguments[0],
        context->currentLine);
    if (labelLine < 0) {
        fwprintf(stderr, L"ksh: batch call: label not found: %ls\n",
            arguments[0]);
        return 1;
    }
    wchar_t* oldArguments[MAX_ARGS];
    memcpy(oldArguments, context->arguments, sizeof(oldArguments));
    int oldCount = context->argumentCount;
    int oldShift = context->shift;
    wchar_t* oldPath = context->scriptPath;
    size_t oldLine = context->currentLine;
    BatchLocalFrame* oldFloor = context->frameFloor;
    context->frameFloor = context->localFrame;
    context->argumentCount = argumentCount - 1;
    context->shift = 0;
    context->scriptPath = arguments[0];
    for (int i = 0; i < context->argumentCount; ++i)
        context->arguments[i] = arguments[i + 1];
    int status = Batch_RunLines(shell, context, lines, (size_t)labelLine + 1);
    /* CMD implicitly ends SETLOCAL scopes opened by a called subroutine. */
    while (context->localFrame &&
        context->localFrame != context->frameFloor) {
        if (!Batch_EndLocal(shell, context)) break;
    }
    context->frameFloor = oldFloor;
    context->scriptPath = oldPath;
    context->argumentCount = oldCount;
    context->shift = oldShift;
    context->currentLine = oldLine;
    memcpy(context->arguments, oldArguments, sizeof(oldArguments));
    return status;
}

static int Batch_ExecuteCall(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, const wchar_t* rest) {
    if (!*rest) return 0;
    /* CALL performs a second round of %-expansion. */
    wchar_t* target = Batch_Expand(shell, context, rest,
        BATCH_EXPAND_PERCENT);
    if (!target) return 1;
    const wchar_t* start = Batch_SkipSpaces(target);
    bool label = start[0] == L':' && start[1];
    wchar_t first[MAX_LINE_LEN] = { 0 };
    const wchar_t* firstCursor = start;
    if (!label) Batch_ReadWord(&firstCursor, first, _countof(first));
    int status;
    if (label || IsBatchFileName(first)) {
        wchar_t* arguments[MAX_ARGS] = { 0 };
        int argumentCount = 0;
        wchar_t word[MAX_LINE_LEN];
        const wchar_t* cursor = start;
        while (argumentCount < MAX_ARGS &&
            Batch_ReadDelimitedToken(&cursor, word, _countof(word))) {
            arguments[argumentCount] = SafeStrDup(word);
            if (!arguments[argumentCount]) break;
            argumentCount++;
        }
        if (!argumentCount) status = 1;
        else if (label)
            status = Batch_CallLabel(shell, context, lines, arguments,
                argumentCount);
        else
            status = Shell_RunBatch(shell, first, argumentCount - 1,
                argumentCount > 1 ? &arguments[1] : NULL);
        for (int i = 0; i < argumentCount; ++i) free(arguments[i]);
    } else {
        status = Batch_ExecuteStatements(shell, context, lines, target);
    }
    free(target);
    return status;
}

static bool Batch_FileExists(const wchar_t* path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
        !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool Batch_ExtensionListed(const wchar_t* extension,
    const wchar_t* extensions) {
    size_t length = wcslen(extension);
    for (const wchar_t* item = extensions; *item;) {
        const wchar_t* end = wcschr(item, L';');
        size_t itemLength = end ? (size_t)(end - item) : wcslen(item);
        if (itemLength == length && _wcsnicmp(item, extension, length) == 0)
            return true;
        if (!end) break;
        item = end + 1;
    }
    return false;
}

static bool Batch_TryExecutable(const wchar_t* base, const wchar_t* leaf,
    const wchar_t* extensions, wchar_t* result, size_t capacity) {
    const wchar_t* dot = wcsrchr(leaf, L'.');
    if (dot && Batch_ExtensionListed(dot, extensions) &&
        Batch_FileExists(base)) {
        wcscpy_s(result, capacity, base);
        return true;
    }
    for (const wchar_t* item = extensions; *item;) {
        const wchar_t* end = wcschr(item, L';');
        size_t itemLength = end ? (size_t)(end - item) : wcslen(item);
        wchar_t candidate[MAX_PATH * 2];
        if (itemLength && swprintf_s(candidate, _countof(candidate),
                L"%ls%.*ls", base, (int)itemLength, item) > 0 &&
            Batch_FileExists(candidate)) {
            wcscpy_s(result, capacity, candidate);
            return true;
        }
        if (!end) break;
        item = end + 1;
    }
    return false;
}

/* Resolves a program like CMD: current directory, then PATH, with PATHEXT. */
static bool Batch_FindExecutable(const wchar_t* name, wchar_t* result,
    size_t capacity) {
    wchar_t* pathExt = GetEnvironmentValue(L"PATHEXT");
    const wchar_t* extensions = pathExt && *pathExt ? pathExt :
        L".COM;.EXE;.BAT;.CMD";
    const wchar_t* leaf = name;
    for (const wchar_t* p = name; *p; ++p)
        if (*p == L'\\' || *p == L'/' || *p == L':') leaf = p + 1;
    bool found = false;
    if (leaf != name) {
        found = Batch_TryExecutable(name, leaf, extensions, result, capacity);
    } else {
        wchar_t base[MAX_PATH * 2];
        if (swprintf_s(base, _countof(base), L".\\%ls", name) > 0)
            found = Batch_TryExecutable(base, leaf, extensions, result,
                capacity);
        wchar_t* path = found ? NULL : GetEnvironmentValue(L"PATH");
        for (const wchar_t* cursor = path; !found && cursor && *cursor;) {
            const wchar_t* end = wcschr(cursor, L';');
            size_t length = end ? (size_t)(end - cursor) : wcslen(cursor);
            wchar_t directory[MAX_PATH * 2];
            size_t directoryLength = 0;
            for (size_t i = 0; i < length &&
                directoryLength + 1 < _countof(directory); ++i) {
                if (cursor[i] != L'"') directory[directoryLength++] = cursor[i];
            }
            directory[directoryLength] = L'\0';
            if (directoryLength) {
                bool separator = directory[directoryLength - 1] == L'\\' ||
                    directory[directoryLength - 1] == L'/';
                if (swprintf_s(base, _countof(base), L"%ls%ls%ls", directory,
                        separator ? L"" : L"\\", name) > 0)
                    found = Batch_TryExecutable(base, leaf, extensions,
                        result, capacity);
            }
            cursor = end ? end + 1 : cursor + length;
        }
        free(path);
    }
    free(pathExt);
    if (found) {
        wchar_t fullPath[MAX_PATH * 2];
        DWORD length = GetFullPathNameW(result, _countof(fullPath), fullPath,
            NULL);
        if (length && length < _countof(fullPath))
            wcscpy_s(result, capacity, fullPath);
    }
    return found;
}

/* Splits a command line using the Microsoft C runtime argv rules. */
static int Batch_SplitArguments(const wchar_t* line, wchar_t** argv,
    int maxArguments) {
    int argc = 0;
    const wchar_t* p = line;
    while (argc < maxArguments) {
        while (*p == L' ' || *p == L'\t') p++;
        if (!*p) break;
        wchar_t* argument =
            (wchar_t*)malloc((wcslen(p) + 1) * sizeof(wchar_t));
        if (!argument) break;
        size_t length = 0;
        bool quoted = false;
        while (*p && (quoted || (*p != L' ' && *p != L'\t'))) {
            if (*p == L'\\') {
                size_t slashes = 0;
                while (*p == L'\\') {
                    slashes++;
                    p++;
                }
                if (*p == L'"') {
                    for (size_t i = 0; i < slashes / 2; ++i)
                        argument[length++] = L'\\';
                    if (slashes % 2) argument[length++] = *p++;
                } else {
                    for (size_t i = 0; i < slashes; ++i)
                        argument[length++] = L'\\';
                }
                continue;
            }
            if (*p == L'"') {
                if (quoted && p[1] == L'"') {
                    argument[length++] = L'"';
                    p += 2;
                    continue;
                }
                quoted = !quoted;
                p++;
                continue;
            }
            argument[length++] = *p++;
        }
        argument[length] = L'\0';
        argv[argc++] = argument;
    }
    return argc;
}

static int Batch_RunExternal(const wchar_t* program, const wchar_t* line) {
    wchar_t* commandLine = SafeStrDup(line);
    if (!commandLine) return 1;
    STARTUPINFOW startup;
    PROCESS_INFORMATION information;
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    fflush(stdout);
    fflush(stderr);
    BOOL created = CreateProcessW(program, commandLine, NULL, NULL, TRUE, 0,
        NULL, NULL, &startup, &information);
    free(commandLine);
    if (!created) {
        fwprintf(stderr, L"ksh: batch: cannot execute %ls (error %lu)\n",
            program, GetLastError());
        return 1;
    }
    CloseHandle(information.hThread);
    WaitForSingleObject(information.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(information.hProcess, &exitCode);
    CloseHandle(information.hProcess);
    return (int)exitCode;
}

/* CMD internal commands that ksh implements as builtins. */
static bool Batch_IsInternalCommand(const wchar_t* name) {
    static const wchar_t* const names[] = {
        L"assoc", L"cls", L"date", L"dir", L"ftype", L"mklink", L"popd",
        L"pushd", L"prompt", L"start", L"time"
    };
    for (size_t i = 0; i < _countof(names); ++i)
        if (_wcsicmp(name, names[i]) == 0) return true;
    return false;
}

/*
 * Runs a command that is not handled by the batch interpreter itself. Real
 * programs receive the command line verbatim, as under CMD; ksh builtins are
 * used for CMD internals and for tools that are not installed.
 */
static int Batch_ExecuteProgram(IShell* shell, BatchContext* context,
    const wchar_t* line) {
    wchar_t* argv[MAX_ARGS];
    int argc = Batch_SplitArguments(line, argv, MAX_ARGS - 1);
    if (!argc) return 0;
    const wchar_t* name = argv[0];
    CommandAST probe;
    memset(&probe, 0, sizeof(probe));
    probe.argc = 1;
    probe.argv[0] = argv[0];
    bool internal = Batch_IsInternalCommand(name);
    wchar_t program[MAX_PATH * 2];
    int status;
    if (!internal && Batch_FindExecutable(name, program, _countof(program))) {
        if (IsBatchFileName(program)) {
            const wchar_t* tail = Batch_SkipSpaces(line);
            bool quoted = false;
            while (*tail && (quoted || !iswspace(*tail))) {
                if (*tail == L'"') quoted = !quoted;
                tail++;
            }
            wchar_t* arguments[MAX_ARGS] = { 0 };
            int argumentCount = 0;
            wchar_t word[MAX_LINE_LEN];
            while (argumentCount < MAX_ARGS - 1 &&
                Batch_ReadDelimitedToken(&tail, word, _countof(word))) {
                arguments[argumentCount] = SafeStrDup(word);
                if (!arguments[argumentCount]) break;
                argumentCount++;
            }
            status = Shell_RunBatch(shell, program, argumentCount,
                argumentCount ? arguments : NULL);
            for (int i = 0; i < argumentCount; ++i) free(arguments[i]);
            /* Without CALL, CMD transfers control and never returns. */
            context->errorLevel = status;
            context->flow = BATCH_FLOW_RETURN;
        } else {
            status = Batch_RunExternal(program, line);
        }
    } else if (internal || IsBuiltinCommand(&probe)) {
        CommandAST command;
        memset(&command, 0, sizeof(command));
        command.argc = argc;
        for (int i = 0; i < argc; ++i) command.argv[i] = argv[i];
        int code = 0;
        status = TryExecBuiltin(shell, &command, &code) ? code : 1;
    } else if (Shell_HasFunction(shell, name)) {
        status = shell->ExecuteLine(shell, line);
    } else {
        fwprintf(stderr, L"'%ls' is not recognized as an internal or external command,\noperable program or batch file.\n",
            name);
        status = 9009;
    }
    for (int i = 0; i < argc; ++i) free(argv[i]);
    return status;
}

int Batch_ExecuteExpanded(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, const wchar_t* line) {
    line = Batch_SkipSpaces(line);
    if (!*line) return context->errorLevel;
    if (_wcsnicmp(line, L"rem", 3) == 0 &&
        (!line[3] || iswspace(line[3])))
        return context->errorLevel;
    if (line[0] == L':' || (line[0] == L'/' && line[1] == L'/'))
        return context->errorLevel;
    const wchar_t* rest = NULL;
    if (_wcsnicmp(line, L"echo", 4) == 0 && line[4] &&
        wcschr(L".:(/\\[]+,;=", line[4])) {
        context->keepErrorLevel = true;
        wprintf(L"%ls\n", line + 5);
        return 0;
    }
    if (Batch_IsCommand(line, L"echo", &rest)) {
        context->keepErrorLevel = true;
        if (!*rest) {
            wprintf(L"ECHO is %ls.\n", context->echoEnabled ? L"on" : L"off");
            return 0;
        }
        if (Batch_WordEquals(rest, L"off")) {
            context->echoEnabled = false;
            return 0;
        }
        if (Batch_WordEquals(rest, L"on")) {
            context->echoEnabled = true;
            return 0;
        }
        wprintf(L"%ls\n", rest);
        return 0;
    }
    if (Batch_IsCommand(line, L"break", &rest))
        return Batch_ExecuteBreak(rest);
    if (Batch_IsCommand(line, L"chcp", &rest))
        return Batch_ExecuteCodePage(rest);
    if (Batch_IsCommand(line, L"color", &rest))
        return Batch_ExecuteColor(shell, rest);
    if (Batch_IsCommand(line, L"title", &rest)) {
        context->keepErrorLevel = true;
        return Batch_ExecuteTitle(rest);
    }
    if (Batch_IsCommand(line, L"vol", &rest))
        return Batch_ExecuteVolume(rest);
    if (Batch_IsCommand(line, L"set", &rest)) {
        int status = Batch_ExecuteSet(shell, context, rest);
        if (status == 0 && Batch_IsBatExtension(context))
            context->keepErrorLevel = true;
        return status;
    }
    if (Batch_IsCommand(line, L"setlocal", &rest)) {
        BatchLocalFrame* frame =
            (BatchLocalFrame*)calloc(1, sizeof(BatchLocalFrame));
        if (!frame) return 1;
        frame->parent = context->localFrame;
        frame->delayedExpansion = context->delayedExpansion;
        context->localFrame = frame;
        int status = 0;
        wchar_t option[MAX_LINE_LEN];
        const wchar_t* cursor = rest;
        while (Batch_ReadWord(&cursor, option, _countof(option))) {
            if (_wcsicmp(option, L"EnableDelayedExpansion") == 0)
                context->delayedExpansion = true;
            else if (_wcsicmp(option, L"DisableDelayedExpansion") == 0)
                context->delayedExpansion = false;
            else if (_wcsicmp(option, L"EnableExtensions") != 0 &&
                _wcsicmp(option, L"DisableExtensions") != 0)
                status = 1;
        }
        return status;
    }
    if (Batch_IsCommand(line, L"endlocal", &rest)) {
        context->keepErrorLevel = true;
        if (!context->localFrame ||
            context->localFrame == context->frameFloor)
            return 0;
        return Batch_EndLocal(shell, context) ? 0 : 1;
    }
    if (Batch_IsCommand(line, L"goto", &rest)) {
        context->keepErrorLevel = true;
        if (Batch_LabelLength(rest) == 4 && _wcsnicmp(rest, L":eof", 4) == 0) {
            context->flow = BATCH_FLOW_RETURN;
            return 0;
        }
        int label = Batch_FindLabel(lines, rest, context->currentLine);
        if (label < 0) {
            if (Batch_LabelLength(rest) == 3 &&
                _wcsnicmp(rest, L"eof", 3) == 0) {
                context->flow = BATCH_FLOW_RETURN;
                return 0;
            }
            /* CMD aborts the batch file when a GOTO target is missing. */
            fwprintf(stderr, L"ksh: batch goto: label not found: %ls\n", rest);
            context->errorLevel = 1;
            context->flow = BATCH_FLOW_RETURN;
            return 1;
        }
        context->flow = BATCH_FLOW_GOTO;
        context->gotoLine = (size_t)label;
        return 0;
    }
    if (Batch_IsCommand(line, L"shift", &rest)) {
        context->keepErrorLevel = true;
        if (*rest == L'/') {
            wchar_t* end = NULL;
            unsigned long index = wcstoul(rest + 1, &end, 10);
            if (end == rest + 1 || index > 8) return 1;
            if (index) {
                int position = context->shift + (int)index - 1;
                if (position < context->argumentCount) {
                    wchar_t* removed = context->arguments[position];
                    memmove(&context->arguments[position],
                        &context->arguments[position + 1],
                        (size_t)(context->argumentCount - position - 1) *
                            sizeof(wchar_t*));
                    context->arguments[context->argumentCount - 1] = removed;
                    context->argumentCount--;
                }
                return 0;
            }
        }
        if (context->shift < context->argumentCount) context->shift++;
        return 0;
    }
    if (Batch_IsCommand(line, L"exit", &rest)) {
        bool returnFromBatch = false;
        if (_wcsnicmp(rest, L"/b", 2) == 0 &&
            (!rest[2] || iswspace(rest[2]))) {
            returnFromBatch = true;
            rest = Batch_SkipSpaces(rest + 2);
        }
        int status = context->errorLevel;
        if (*rest) {
            wchar_t* end = NULL;
            long long parsed = wcstoll(rest, &end, 10);
            if (end != rest) status = (int)parsed;
        }
        context->errorLevel = status;
        if (returnFromBatch) {
            context->flow = BATCH_FLOW_RETURN;
            return status;
        }
        shell->running = false;
        return status;
    }
    if (Batch_IsCommand(line, L"call", &rest))
        return Batch_ExecuteCall(shell, context, lines, rest);
    if (Batch_IsCommand(line, L"cd", &rest) ||
        Batch_IsCommand(line, L"chdir", &rest)) {
        if (!*rest) {
            wchar_t directory[MAX_PATH];
            if (!GetCurrentDirectoryW(_countof(directory), directory))
                return 1;
            wprintf(L"%ls\n", directory);
            return 0;
        }
        if (_wcsnicmp(rest, L"/d", 2) == 0 &&
            (!rest[2] || iswspace(rest[2])))
            rest = Batch_SkipSpaces(rest + 2);
        wchar_t directory[MAX_LINE_LEN];
        Batch_StripQuotes(rest, directory, _countof(directory));
        size_t length = wcslen(directory);
        while (length && iswspace(directory[length - 1]))
            directory[--length] = L'\0';
        if (SetCurrentDirectoryW(directory)) {
            context->keepErrorLevel = true;
            return 0;
        }
        fwprintf(stderr, L"ksh: batch cd: cannot change directory to %ls\n",
            directory);
        return 1;
    }
    if (Batch_IsCommand(line, L"md", &rest) ||
        Batch_IsCommand(line, L"mkdir", &rest)) {
        wchar_t directory[MAX_LINE_LEN];
        const wchar_t* cursor = rest;
        if (!Batch_ReadWord(&cursor, directory, _countof(directory)) ||
            *Batch_SkipSpaces(cursor)) {
            fwprintf(stderr, L"ksh: batch md: expected one path operand\n");
            return 2;
        }
        if (_wcsicmp(directory, L"/?") == 0) {
            wprintf(L"MD [drive:]path\nMKDIR [drive:]path\n");
            return 0;
        }
        if (Batch_CreateDirectoryPath(directory)) return 0;
        fwprintf(stderr, L"ksh: batch md: cannot create directory %ls\n",
            directory);
        return 1;
    }
    if (Batch_IsCommand(line, L"verify", &rest)) {
        rest = Batch_SkipSpaces(rest);
        if (Batch_WordEquals(rest, L"on")) {
            shell->batchVerify = true;
            wprintf(L"VERIFY is on.\n");
            return 0;
        }
        if (Batch_WordEquals(rest, L"off")) {
            shell->batchVerify = false;
            wprintf(L"VERIFY is off.\n");
            return 0;
        }
        if (!*rest) {
            wprintf(L"VERIFY is %ls.\n",
                shell->batchVerify ? L"on" : L"off");
            return 0;
        }
        fwprintf(stderr, L"ksh: batch verify: usage: VERIFY [ON | OFF]\n");
        return 2;
    }
    if (Batch_IsCommand(line, L"rd", &rest) ||
        Batch_IsCommand(line, L"rmdir", &rest)) {
        bool recursive = false;
        bool quiet = false;
        const wchar_t* cursor = rest;
        wchar_t directory[MAX_LINE_LEN] = { 0 };
        wchar_t option[MAX_LINE_LEN];
        while (Batch_ReadWord(&cursor, option, _countof(option))) {
            if (_wcsicmp(option, L"/s") == 0) recursive = true;
            else if (_wcsicmp(option, L"/q") == 0) quiet = true;
            else if (_wcsicmp(option, L"/?") == 0) {
                wprintf(L"RD [/S] [/Q] [drive:]path\n");
                return 0;
            } else if (!*directory) {
                wcscpy_s(directory, _countof(directory), option);
            } else return 2;
        }
        if (!*directory) return 2;
        if (!recursive) return RemoveDirectoryW(directory) ? 0 : 1;
        if (Batch_IsProtectedDeleteTarget(directory)) {
            fwprintf(stderr, L"ksh: batch rd: refusing to recursively remove a volume root or the current directory\n");
            return 1;
        }
        if (!quiet && !Batch_ConfirmRecursiveDelete(directory)) return 1;
        return Batch_DeleteTree(directory) ? 0 : 1;
    }
    if (Batch_IsCommand(line, L"copy", &rest))
        return Batch_CopyMove(shell, rest, false);
    if (Batch_IsCommand(line, L"move", &rest))
        return Batch_CopyMove(shell, rest, true);
    if (Batch_IsCommand(line, L"del", &rest) ||
        Batch_IsCommand(line, L"erase", &rest))
        return Batch_DeleteFiles(rest);
    if (Batch_IsCommand(line, L"ren", &rest) ||
        Batch_IsCommand(line, L"rename", &rest)) {
        wchar_t source[MAX_LINE_LEN], destination[MAX_LINE_LEN];
        if (!Batch_ReadWord(&rest, source, _countof(source))) return 2;
        if (wcscmp(source, L"/?") == 0) {
            wprintf(L"REN [drive:][path]filename1 filename2\n");
            return 0;
        }
        if (!Batch_ReadWord(&rest, destination, _countof(destination)) ||
            *Batch_SkipSpaces(rest))
            return 2;
        return MoveFileW(source, destination) ? 0 : 1;
    }
    if (Batch_IsCommand(line, L"type", &rest))
        return Batch_TypeFiles(rest);
    if (Batch_IsCommand(line, L"path", &rest)) {
        int status;
        if (!*rest) {
            wchar_t* path = GetEnvironmentValue(L"PATH");
            wprintf(L"PATH=%ls\n", path ? path : L"");
            free(path);
            status = 0;
        } else if (_wcsicmp(rest, L";") == 0) {
            status = Batch_SetVariable(shell, context, L"PATH", L"") ? 0 : 1;
        } else {
            status = Batch_SetVariable(shell, context, L"PATH", rest) ? 0 : 1;
        }
        if (status == 0 && Batch_IsBatExtension(context))
            context->keepErrorLevel = true;
        return status;
    }
    if (Batch_IsCommand(line, L"pause", &rest)) {
        (void)rest;
        context->keepErrorLevel = true;
        fputws(L"Press any key to continue . . . ", stdout);
        fflush(stdout);
        if (_isatty(_fileno(stdin))) {
            (void)_getwch();
        } else {
            wint_t ch = fgetwc(stdin);
            if (ch == 0xFEFF) ch = fgetwc(stdin);
            if (ch == WEOF) return 1;
        }
        putwchar(L'\n');
        return 0;
    }
    if (Batch_IsCommand(line, L"ver", &rest)) {
        (void)rest;
        typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW*);
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        RtlGetVersionFn getVersion = ntdll ?
            (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion") : NULL;
        OSVERSIONINFOW version = { 0 };
        version.dwOSVersionInfoSize = sizeof(version);
        if (!getVersion || getVersion(&version) != 0) return 1;
        wprintf(L"Microsoft Windows [Version %lu.%lu.%lu]\n",
            version.dwMajorVersion, version.dwMinorVersion,
            version.dwBuildNumber);
        return 0;
    }
    return Batch_ExecuteProgram(shell, context, line);
}

/* Runs one simple command or parenthesized block after redirections. */
static int Batch_ExecuteCommand(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, const wchar_t* command) {
    const wchar_t* line = Batch_SkipCommandPrefix(command);
    line = Batch_SkipSpaces(line);
    if (*line == L'(') {
        size_t close = Batch_FindBlockEnd(line, 0);
        size_t innerLength = close == SIZE_MAX ? wcslen(line + 1) : close - 1;
        wchar_t* inner = (wchar_t*)malloc((innerLength + 1) * sizeof(wchar_t));
        if (!inner) return 1;
        memcpy(inner, line + 1, innerLength * sizeof(wchar_t));
        inner[innerLength] = L'\0';
        int status = Batch_ExecuteStatements(shell, context, lines, inner);
        free(inner);
        return status;
    }
    wchar_t* expanded = Batch_Expand(shell, context, line, BATCH_EXPAND_FINAL);
    if (!expanded) return 1;
    context->keepErrorLevel = false;
    int status = Batch_ExecuteExpanded(shell, context, lines, expanded);
    free(expanded);
    context->lastCommandStatus = status;
    if (context->flow == BATCH_FLOW_NONE &&
        !(context->keepErrorLevel && status == 0))
        context->errorLevel = status;
    context->keepErrorLevel = false;
    shell->last_status = context->errorLevel;
    shell->env->lastStatus = context->errorLevel;
    /* CMD writes unbuffered; keep pipes and child output in order. */
    fflush(stdout);
    return status;
}

typedef struct BatchRedirects {
    int savedFd[3];
    int savedMode[3];
} BatchRedirects;

static const DWORD batchStdHandleIds[3] = {
    STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE
};

static void Batch_InitRedirects(BatchRedirects* state) {
    for (int fd = 0; fd < 3; ++fd) state->savedFd[fd] = -1;
}

static bool Batch_RedirectDescriptor(BatchRedirects* state, int target,
    int source) {
    fflush(stdout);
    fflush(stderr);
    if (target == 0) fflush(stdin);
    if (state->savedFd[target] < 0) {
        int mode = _setmode(target, _O_BINARY);
        if (mode < 0) return false;
        _setmode(target, mode);
        int saved = _dup(target);
        if (saved < 0) return false;
        state->savedFd[target] = saved;
        state->savedMode[target] = mode;
    }
    if (_dup2(source, target) != 0) return false;
    _setmode(target, state->savedMode[target]);
    /* Child processes inherit the Win32 standard handles, not CRT fds. */
    SetStdHandle(batchStdHandleIds[target], (HANDLE)_get_osfhandle(target));
    return true;
}

static bool Batch_RedirectFile(BatchRedirects* state, int target,
    const wchar_t* path, int kind) {
    int flags = kind == 0 ? (_O_RDONLY | _O_BINARY) :
        (_O_WRONLY | _O_CREAT | _O_BINARY | (kind == 2 ? _O_APPEND : _O_TRUNC));
    int fd = OpenWideDescriptor(path, flags, _S_IREAD | _S_IWRITE);
    if (fd < 0) return false;
    if (kind == 2) _lseeki64(fd, 0, SEEK_END);
    bool ok = Batch_RedirectDescriptor(state, target, fd);
    _close(fd);
    return ok;
}

static void Batch_RestoreRedirects(BatchRedirects* state) {
    fflush(stdout);
    fflush(stderr);
    for (int fd = 0; fd < 3; ++fd) {
        if (state->savedFd[fd] < 0) continue;
        if (fd == 0) fflush(stdin);
        _dup2(state->savedFd[fd], fd);
        _setmode(fd, state->savedMode[fd]);
        SetStdHandle(batchStdHandleIds[fd], (HANDLE)_get_osfhandle(fd));
        _close(state->savedFd[fd]);
        state->savedFd[fd] = -1;
        if (fd == 0) clearerr(stdin);
    }
}

/* Applies [n]>file, [n]>>file, <file and n>&m, then runs the command. */
static int Batch_ExecuteRedirected(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, const wchar_t* text,
    const wchar_t* inputPath, const wchar_t* outputPath) {
    size_t length = wcslen(text);
    wchar_t* command = (wchar_t*)malloc((length + 1) * sizeof(wchar_t));
    if (!command) return 1;
    size_t commandLength = 0;
    BatchRedirects state;
    Batch_InitRedirects(&state);
    bool ok = true;
    if (inputPath && !Batch_RedirectFile(&state, 0, inputPath, 0)) ok = false;
    if (ok && outputPath && !Batch_RedirectFile(&state, 1, outputPath, 1))
        ok = false;
    const wchar_t* first = Batch_SkipCommandPrefix(text);
    bool quoted = false;
    for (size_t i = 0; ok && i < length; ++i) {
        wchar_t c = text[i];
        if (c == L'"') {
            quoted = !quoted;
        } else if (!quoted && c == L'^' && i + 1 < length) {
            command[commandLength++] = c;
            command[commandLength++] = text[++i];
            continue;
        } else if (!quoted && c == L'(' && text + i == first) {
            size_t close = Batch_FindBlockEnd(text, i);
            size_t stop = close == SIZE_MAX ? length - 1 : close;
            memcpy(command + commandLength, text + i,
                (stop - i + 1) * sizeof(wchar_t));
            commandLength += stop - i + 1;
            i = stop;
            continue;
        } else if (!quoted) {
            size_t k = i;
            int fd = -1;
            if (iswdigit(c) && (text[i + 1] == L'>' || text[i + 1] == L'<') &&
                (i == 0 || iswspace(text[i - 1]) || text[i - 1] == L')')) {
                fd = c - L'0';
                k = i + 1;
            }
            if (text[k] == L'>' || text[k] == L'<') {
                bool input = text[k] == L'<';
                if (fd < 0) fd = input ? 0 : 1;
                int kind = input ? 0 : 1;
                k++;
                if (!input && text[k] == L'>') {
                    kind = 2;
                    k++;
                }
                if (text[k] == L'&' && iswdigit(text[k + 1])) {
                    int source = text[k + 1] - L'0';
                    k += 2;
                    if (fd <= 2 && source <= 2 && fd != source)
                        ok = Batch_RedirectDescriptor(&state, fd, source);
                } else {
                    while (text[k] == L' ' || text[k] == L'\t') k++;
                    wchar_t target[MAX_LINE_LEN];
                    size_t targetLength = 0;
                    bool targetQuoted = false;
                    while (ok && text[k] && (targetQuoted ||
                            !(iswspace(text[k]) || wcschr(L"<>|&", text[k])))) {
                        if (text[k] == L'"') targetQuoted = !targetQuoted;
                        if (targetLength + 2 >= _countof(target)) {
                            ok = false;
                            break;
                        }
                        if (text[k] == L'^' && text[k + 1] && !targetQuoted)
                            target[targetLength++] = text[k++];
                        target[targetLength++] = text[k++];
                    }
                    target[targetLength] = L'\0';
                    wchar_t* expanded = ok && targetLength ?
                        Batch_Expand(shell, context, target,
                            BATCH_EXPAND_FINAL) : NULL;
                    wchar_t path[MAX_LINE_LEN];
                    if (expanded)
                        Batch_StripQuotes(expanded, path, _countof(path));
                    free(expanded);
                    if (!expanded || !*path) {
                        fwprintf(stderr, L"ksh: batch: missing redirection target\n");
                        ok = false;
                    } else if (fd <= 2 &&
                        !Batch_RedirectFile(&state, fd, path, kind)) {
                        Batch_RestoreRedirects(&state);
                        fwprintf(stderr, L"ksh: batch: cannot open %ls\n", path);
                        ok = false;
                    }
                }
                i = k - 1;
                continue;
            }
        }
        command[commandLength++] = c;
    }
    command[commandLength] = L'\0';
    int status = ok ? Batch_ExecuteCommand(shell, context, lines, command) : 1;
    Batch_RestoreRedirects(&state);
    free(command);
    if (!ok) {
        context->errorLevel = 1;
        shell->last_status = 1;
        shell->env->lastStatus = 1;
    }
    return status;
}

/*
 * Runs a pipeline like CMD: every stage executes concurrently in a child
 * shell connected by anonymous pipes, so changes made by a stage (SET, EXIT,
 * GOTO) do not leak into the script.
 */
static int Batch_ExecutePipeline(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, const wchar_t* text) {
    size_t length = wcslen(text);
    size_t stageStarts[32];
    size_t stageEnds[32];
    size_t stageCount = 0;
    size_t stageStart = 0;
    const wchar_t* stageFirst = Batch_SkipCommandPrefix(text);
    bool quoted = false;
    for (size_t i = 0;; ++i) {
        wchar_t c = text[i];
        if (c && (c != L'|' || quoted)) {
            if (c == L'"') {
                quoted = !quoted;
            } else if (!quoted && c == L'^' && text[i + 1]) {
                i++;
            } else if (!quoted && c == L'(' && text + i == stageFirst) {
                size_t close = Batch_FindBlockEnd(text, i);
                i = close == SIZE_MAX ? length - 1 : close;
            }
            continue;
        }
        if (stageCount == _countof(stageStarts)) {
            fwprintf(stderr, L"ksh: batch: pipeline too long\n");
            return 1;
        }
        stageStarts[stageCount] = stageStart;
        stageEnds[stageCount++] = i;
        if (!c) break;
        stageStart = i + 1;
        stageFirst = Batch_SkipCommandPrefix(text + stageStart);
    }
    if (stageCount == 1)
        return Batch_ExecuteRedirected(shell, context, lines, text, NULL, NULL);

    HANDLE processes[32];
    size_t started = 0;
    HANDLE previousRead = NULL;
    bool failed = false;
    fflush(stdout);
    fflush(stderr);
    for (size_t stage = 0; stage < stageCount; ++stage) {
        bool last = stage + 1 == stageCount;
        size_t stageLength = stageEnds[stage] - stageStarts[stage];
        wchar_t* stageText =
            (wchar_t*)malloc((stageLength + 1) * sizeof(wchar_t));
        if (!stageText) {
            failed = true;
            break;
        }
        memcpy(stageText, text + stageStarts[stage],
            stageLength * sizeof(wchar_t));
        stageText[stageLength] = L'\0';
        /* FOR variables belong to this script, so resolve them here. */
        wchar_t* expanded = Batch_Expand(shell, context, stageText,
            BATCH_EXPAND_LOOP);
        free(stageText);
        HANDLE readEnd = NULL;
        HANDLE writeEnd = NULL;
        if (!expanded || (!last && !CreatePipe(&readEnd, &writeEnd, NULL, 0))) {
            free(expanded);
            failed = true;
            break;
        }
        HANDLE process = Batch_SpawnStage(context, expanded,
            previousRead ? previousRead : GetStdHandle(STD_INPUT_HANDLE),
            last ? GetStdHandle(STD_OUTPUT_HANDLE) : writeEnd,
            GetStdHandle(STD_ERROR_HANDLE));
        free(expanded);
        if (previousRead) CloseHandle(previousRead);
        if (writeEnd) CloseHandle(writeEnd);
        previousRead = readEnd;
        if (!process) {
            failed = true;
            break;
        }
        processes[started++] = process;
    }
    if (previousRead) CloseHandle(previousRead);
    int status = 1;
    if (started) {
        WaitForMultipleObjects((DWORD)started, processes, TRUE, INFINITE);
        DWORD exitCode = 1;
        if (!failed && GetExitCodeProcess(processes[started - 1], &exitCode))
            status = (int)exitCode;
        for (size_t i = 0; i < started; ++i) CloseHandle(processes[i]);
    }
    context->errorLevel = status;
    context->lastCommandStatus = status;
    shell->last_status = status;
    shell->env->lastStatus = status;
    return status;
}

static int Batch_ExecuteSegment(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, const wchar_t* segment) {
    const wchar_t* line = Batch_SkipSpaces(segment);
    while (*line == L'@') line = Batch_SkipSpaces(line + 1);
    if (!*line || Batch_IsCommentStatement(line)) return context->errorLevel;
    const wchar_t* rest = NULL;
    if (Batch_IsCommand(line, L"if", &rest))
        return Batch_ExecuteIf(shell, context, lines, rest);
    if (Batch_IsCommand(line, L"for", &rest))
        return Batch_ExecuteFor(shell, context, lines, rest);
    return Batch_ExecutePipeline(shell, context, lines, line);
}

/*
 * Splits %-expanded text into statements on newlines (inside blocks), &, &&
 * and ||. IF, FOR and REM own the remainder of their line.
 */
int Batch_ExecuteStatements(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, const wchar_t* text) {
    size_t length = wcslen(text);
    int status = context->errorLevel;
    bool shouldRun = true;
    size_t position = 0;
    while (position < length) {
        const wchar_t* first = Batch_SkipCommandPrefix(text + position);
        bool compound = Batch_IsCommand(first, L"if", NULL) ||
            Batch_IsCommand(first, L"for", NULL);
        bool comment = Batch_IsCommentStatement(first);
        bool quoted = false;
        size_t end = length;
        size_t operatorLength = 0;
        wchar_t operation = L';';
        for (size_t i = position; i < length; ++i) {
            wchar_t c = text[i];
            if (c == L'\n') {
                end = i;
                operatorLength = 1;
                break;
            }
            if (comment) continue;
            if (c == L'"') {
                quoted = !quoted;
                continue;
            }
            if (quoted) continue;
            if (c == L'^' && text[i + 1]) {
                i++;
                continue;
            }
            if (c == L'(' && (compound || text + i == first)) {
                size_t close = Batch_FindBlockEnd(text, i);
                if (close == SIZE_MAX) {
                    i = length;
                    break;
                }
                i = close;
                continue;
            }
            if (compound) continue;
            if (c == L'&') {
                operation = text[i + 1] == L'&' ? L'&' : L';';
                operatorLength = operation == L'&' ? 2 : 1;
                end = i;
                break;
            }
            if (c == L'|' && text[i + 1] == L'|') {
                operation = L'|';
                operatorLength = 2;
                end = i;
                break;
            }
        }
        if (shouldRun) {
            size_t segmentLength = end - position;
            wchar_t* segment =
                (wchar_t*)malloc((segmentLength + 1) * sizeof(wchar_t));
            if (!segment) return 1;
            memcpy(segment, text + position, segmentLength * sizeof(wchar_t));
            segment[segmentLength] = L'\0';
            status = Batch_ExecuteSegment(shell, context, lines, segment);
            free(segment);
            if (Batch_FlowStopped(shell, context)) return status;
        }
        if (operation == L';') shouldRun = true;
        else if (operation == L'&') shouldRun = status == 0;
        else shouldRun = status != 0;
        position = end + operatorLength;
    }
    return status;
}

int Batch_RunLines(IShell* shell, BatchContext* context,
    const BatchFileLines* lines, size_t firstLine) {
    for (size_t lineIndex = firstLine; lineIndex < lines->count;
        ++lineIndex) {
        context->currentLine = lineIndex;
        const wchar_t* raw = lines->items[lineIndex];
        const wchar_t* start = Batch_SkipSpaces(raw);
        if (!*start || *start == L':') continue;
        wchar_t* expanded = Batch_Expand(shell, context, raw,
            BATCH_EXPAND_PERCENT);
        if (!expanded) {
            fwprintf(stderr, L"ksh: batch: cannot expand line %zu\n",
                lineIndex + 1);
            context->errorLevel = 1;
            continue;
        }
        if (context->echoEnabled && *start != L'@')
            wprintf(L"%ls\n", Batch_SkipSpaces(expanded));
        Batch_ExecuteStatements(shell, context, lines, expanded);
        free(expanded);
        if (!shell->running) break;
        if (context->flow == BATCH_FLOW_GOTO) {
            context->flow = BATCH_FLOW_NONE;
            lineIndex = context->gotoLine;
            continue;
        }
        if (context->flow == BATCH_FLOW_RETURN) {
            context->flow = BATCH_FLOW_NONE;
            break;
        }
    }
    return context->errorLevel;
}

/*
 * Tracks open parenthesized blocks across physical lines. A '(' opens a block
 * at a command position, inside IF/FOR statements, or within another block.
 */
static int Batch_UpdateBlockDepth(const wchar_t* line, int depth) {
    if (Batch_IsCommentStatement(line)) return depth;
    bool quoted = false;
    bool commandStart = true;
    bool compound = false;
    for (const wchar_t* p = line; *p; ++p) {
        wchar_t c = *p;
        if (c == L'"') {
            quoted = !quoted;
            commandStart = false;
            continue;
        }
        if (quoted) continue;
        if (c == L'^') {
            if (p[1]) p++;
            commandStart = false;
            continue;
        }
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'@') continue;
        if (c == L'(') {
            if (depth > 0 || commandStart || compound) depth++;
            commandStart = true;
            continue;
        }
        if (c == L')') {
            if (depth > 0) depth--;
            commandStart = false;
            continue;
        }
        if (c == L'&' || c == L'|') {
            commandStart = true;
            compound = false;
            continue;
        }
        if (c == L'<' || c == L'>') {
            commandStart = false;
            continue;
        }
        const wchar_t* end = p;
        while (*end && !iswspace(*end) && !wcschr(L"()\"^&|<>", *end)) end++;
        size_t wordLength = (size_t)(end - p);
        if (wordLength == 4 && _wcsnicmp(p, L"else", 4) == 0) {
            commandStart = true;
        } else {
            if (commandStart &&
                ((wordLength == 2 && _wcsnicmp(p, L"if", 2) == 0) ||
                    (wordLength == 3 && _wcsnicmp(p, L"for", 3) == 0)))
                compound = true;
            commandStart = false;
        }
        p = end - 1;
    }
    return depth;
}

/* Loads a script into logical lines, joining ^ continuations and blocks. */
bool Batch_LoadLines(const wchar_t* filename, BatchFileLines* lines) {
    wchar_t* text = NULL;
    if (!Batch_ReadTextFile(filename, &text)) return false;
    wchar_t* logicalLine = NULL;
    size_t logicalLength = 0, logicalCapacity = 0, physicalStart = 0;
    int blockDepth = 0;
    bool ok = true;
    const wchar_t* cursor = text;
    while (ok && *cursor) {
        const wchar_t* newline = wcschr(cursor, L'\n');
        size_t length = newline ? (size_t)(newline - cursor) : wcslen(cursor);
        const wchar_t* line = cursor;
        cursor = newline ? newline + 1 : cursor + length;
        while (length && line[length - 1] == L'\r') length--;
        size_t trailingCarets = 0;
        while (trailingCarets < length &&
            line[length - trailingCarets - 1] == L'^')
            trailingCarets++;
        bool continued = (trailingCarets & 1u) != 0;
        if (continued) length--;
        if (!Batch_Append(&logicalLine, &logicalLength, &logicalCapacity,
                line, length)) {
            ok = false;
            break;
        }
        if (continued) continue;
        blockDepth = Batch_UpdateBlockDepth(logicalLine + physicalStart,
            blockDepth);
        if (blockDepth > 0) {
            ok = Batch_Append(&logicalLine, &logicalLength, &logicalCapacity,
                L"\n", 1);
            physicalStart = logicalLength;
            continue;
        }
        ok = Batch_PushLine(lines, logicalLine);
        logicalLength = 0;
        physicalStart = 0;
        logicalLine[0] = L'\0';
    }
    if (ok && logicalLength) ok = Batch_PushLine(lines, logicalLine);
    free(logicalLine);
    free(text);
    if (!ok) {
        Batch_FreeLines(lines);
        memset(lines, 0, sizeof(*lines));
    }
    return ok;
}

int Shell_RunBatch(IShell* shell, const wchar_t* filename,
    int argumentCount, wchar_t* const arguments[]) {
    if (argumentCount < 0 || argumentCount >= MAX_ARGS) return 2;
    BatchFileLines lines = { 0 };
    if (!Batch_LoadLines(filename, &lines)) {
        fwprintf(stderr, L"ksh: batch: cannot read %ls\n", filename);
        return 1;
    }
    BatchContext context = { 0 };
    context.scriptPath = SafeStrDup(filename);
    context.batchFile = context.scriptPath;
    context.argumentCount = argumentCount;
    context.errorLevel = shell->last_status;
    context.echoEnabled = shell->batchContext ?
        shell->batchContext->echoEnabled : true;
    context.delayedExpansion = shell->batchContext ?
        shell->batchContext->delayedExpansion : false;
    context.parent = shell->batchContext;
    for (int i = 0; i < argumentCount; ++i)
        context.arguments[i] = SafeStrDup(arguments[i]);
    if (!context.scriptPath) {
        for (int i = 0; i < argumentCount; ++i) free(context.arguments[i]);
        Batch_FreeLines(&lines);
        return 1;
    }
    bool oldBatchMode = shell->env->batchMode;
    shell->env->batchMode = false;
    shell->batchContext = &context;
    int status = Batch_RunLines(shell, &context, &lines, 0);
    /* Like "cmd /c", a top-level script exits with its last command status. */
    if (!context.parent) status = context.lastCommandStatus;
    while (context.localFrame) {
        if (!Batch_EndLocal(shell, &context)) break;
    }
    shell->batchContext = context.parent;
    shell->env->batchMode = oldBatchMode;
    shell->last_status = status;
    shell->env->lastStatus = status;
    free(context.scriptPath);
    for (int i = 0; i < argumentCount; ++i) free(context.arguments[i]);
    Batch_FreeLines(&lines);
    return status;
}

/*
 * Entry point for "ksh --batch-stage SCRIPT TEXT [ARGS...]": runs one pipe
 * stage or FOR /F command in a fresh batch context, like "cmd /c".
 */
int Shell_RunBatchStage(IShell* shell, const wchar_t* scriptPath,
    const wchar_t* text, int argumentCount, wchar_t* const arguments[]) {
    if (argumentCount < 0 || argumentCount >= MAX_ARGS) return 2;
    BatchFileLines lines = { 0 };
    if (*scriptPath) (void)Batch_LoadLines(scriptPath, &lines);
    BatchContext context = { 0 };
    context.scriptPath = SafeStrDup(scriptPath);
    context.batchFile = context.scriptPath;
    context.argumentCount = argumentCount;
    for (int i = 0; i < argumentCount; ++i)
        context.arguments[i] = SafeStrDup(arguments[i]);
    int status = 1;
    if (context.scriptPath) {
        shell->batchContext = &context;
        shell->env->batchMode = false;
        Batch_ExecuteStatements(shell, &context, &lines, text);
        status = context.lastCommandStatus;
        while (context.localFrame) {
            if (!Batch_EndLocal(shell, &context)) break;
        }
        shell->batchContext = NULL;
    }
    free(context.scriptPath);
    for (int i = 0; i < argumentCount; ++i) free(context.arguments[i]);
    Batch_FreeLines(&lines);
    return status;
}

