#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

void FreeAST(CommandAST* cmd) {
    while (cmd) {
        CommandAST* next = cmd->next_in_pipeline;
        for (int i = 0; i < cmd->argc; i++) {
            free(cmd->argv[i]);
        }
        if (cmd->in_redirect) {
            if (cmd->is_temp_in_redirect) {
                _wremove(cmd->in_redirect);
            }
            free(cmd->in_redirect);
        }
        if (cmd->out_redirect) free(cmd->out_redirect);
        if (cmd->err_redirect) free(cmd->err_redirect);
        for (int fd = 0; fd < KSH_FD_COUNT; fd++) {
            free(cmd->fds[fd].path);
        }
        free(cmd);
        cmd = next;
    }
}



static bool ParseRedirectionFilename(const wchar_t* line, size_t* position,
    wchar_t* path, size_t capacity) {
    size_t used = 0;
    wchar_t quote = L'\0';
    size_t i = *position;
    while (line[i] == L' ' || line[i] == L'\t') i++;
    while (line[i]) {
        wchar_t current = line[i];
        if (!quote && (iswspace(current) || current == L'|' ||
                current == L'<' || current == L'>'))
            break;
        if (current == L'\\' && quote != L'\'') {
            wchar_t next = line[i + 1];
            if (!next) return false;
            if (quote == L'"' && next != L'"' && next != L'\\' &&
                next != L'$' && next != L'`') {
                if (used + 1 >= capacity) return false;
                path[used++] = current;
                i++;
                continue;
            }
            if (used + 1 >= capacity) return false;
            path[used++] = next;
            i += 2;
            continue;
        }
        if (current == L'\'' && quote != L'"') {
            quote = quote == L'\'' ? L'\0' : L'\'';
            i++;
            continue;
        }
        if (current == L'"' && quote != L'\'') {
            quote = quote == L'"' ? L'\0' : L'"';
            i++;
            continue;
        }
        if (used + 1 >= capacity) return false;
        path[used++] = current;
        i++;
    }
    if (quote || !used) return false;
    path[used] = L'\0';
    if (path[0] == L'/' && iswalpha(path[1]) && (path[2] == L'/' || path[2] == L'\\')) {
        wchar_t drive = (wchar_t)towupper(path[1]);
        path[0] = drive;
        path[1] = L':';
        path[2] = L'\\';
        for (wchar_t* p = path + 3; *p; ++p) {
            if (*p == L'/') *p = L'\\';
        }
    }
    *position = i;
    return true;
}

static void TranslateDrivePathInPlace(wchar_t* token, size_t cap) {
    (void)cap;
    if (!token) return;
    if (token[0] == L'/' && iswalpha(token[1]) &&
        (token[2] == L'/' || token[2] == L'\\')) {
        token[0] = (wchar_t)towupper(token[1]);
        token[1] = L':';
        token[2] = L'\\';
        for (wchar_t* p = token + 3; *p; ++p) {
            if (*p == L'/') *p = L'\\';
        }
    }
}

static bool HasWildcardPattern(const wchar_t* str) {
    if (!str) return false;
    for (const wchar_t* p = str; *p; p++) {
        if (*p == L'*' || *p == L'?') return true;
        if (*p == L'[' && wcschr(p + 1, L']')) return true;
        if ((*p == L'@' || *p == L'!' || *p == L'+' || *p == L'?' || *p == L'*') && p[1] == L'(') return true;
    }
    return false;
}

static int GlobSortCompare(const void* a, const void* b) {
    return _wcsicmp(*(const wchar_t**)a, *(const wchar_t**)b);
}

static wchar_t** s_heredocPaths;
static size_t s_heredocCount;

void Ksh_HeredocRegister(const wchar_t* path) {
    wchar_t** grown = (wchar_t**)realloc(s_heredocPaths, (s_heredocCount + 1) * sizeof(wchar_t*));
    if (!grown) return;
    s_heredocPaths = grown;
    if ((s_heredocPaths[s_heredocCount] = SafeStrDup(path)) != NULL) s_heredocCount++;
}

void Ksh_HeredocUnregister(const wchar_t* path) {
    for (size_t k = 0; k < s_heredocCount; k++) {
        if (_wcsicmp(s_heredocPaths[k], path) == 0) {
            free(s_heredocPaths[k]);
            s_heredocPaths[k] = s_heredocPaths[--s_heredocCount];
            return;
        }
    }
}

static bool HeredocIsRegistered(const wchar_t* path) {
    for (size_t k = 0; k < s_heredocCount; k++)
        if (_wcsicmp(s_heredocPaths[k], path) == 0) return true;
    return false;
}

/* Expands a registered here-document body into a new temp file; returns its path or NULL. */
static wchar_t* HeredocExpandToTemp(IEnvironment* env, const wchar_t* path) {
    FILE* in = NULL;
    char* raw = NULL;
    wchar_t* text = NULL;
    wchar_t* expanded = NULL;
    wchar_t* result = NULL;
    long size;
    if (_wfopen_s(&in, path, L"rb") != 0 || !in) return NULL;
    if (fseek(in, 0, SEEK_END) == 0 && (size = ftell(in)) >= 0 && fseek(in, 0, SEEK_SET) == 0 &&
        (raw = (char*)malloc((size_t)size + 1)) != NULL && fread(raw, 1, (size_t)size, in) == (size_t)size) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, raw, (int)size, NULL, 0);
        text = (wchar_t*)malloc(((size_t)wlen + 1) * sizeof(wchar_t));
        if (text) {
            MultiByteToWideChar(CP_UTF8, 0, raw, (int)size, text, wlen);
            text[wlen] = L'\0';
            env->heredocMode = true;
            expanded = env->Expand(env, text);
            env->heredocMode = false;
        }
    }
    fclose(in);
    if (expanded) {
        wchar_t tempFile[MAX_PATH];
        FILE* out = NULL;
        int u8len = WideCharToMultiByte(CP_UTF8, 0, expanded, -1, NULL, 0, NULL, NULL);
        char* u8 = u8len > 0 ? (char*)malloc((size_t)u8len) : NULL;
        if (u8 && Ksh_CreateTempFile(L"khx", tempFile, MAX_PATH) &&
            _wfopen_s(&out, tempFile, L"wb") == 0 && out) {
            WideCharToMultiByte(CP_UTF8, 0, expanded, -1, u8, u8len, NULL, NULL);
            fwrite(u8, 1, (size_t)u8len - 1, out);
            fclose(out);
            result = SafeStrDup(tempFile);
        }
        free(u8);
    }
    free(expanded);
    free(text);
    free(raw);
    return result;
}

static bool CommandHasRedirect(const CommandAST* cmd) {
    if (cmd->out_redirect || cmd->err_redirect || cmd->in_redirect) return true;
    for (int fd = 0; fd < KSH_FD_COUNT; fd++) {
        if (cmd->fds[fd].kind != FD_REDIRECT_NONE) return true;
    }
    return false;
}

static void SetFdRedirect(CommandAST* cmd, int fd, FdRedirectKind kind,
    wchar_t* path, int dupFrom) {
    if (fd < 0 || fd >= KSH_FD_COUNT) {
        free(path);
        return;
    }
    free(cmd->fds[fd].path);
    cmd->fds[fd].kind = kind;
    cmd->fds[fd].path = path;
    cmd->fds[fd].dupFrom = dupFrom;
}

static CommandAST* Parser_Parse(IParser* self, const wchar_t* raw_line) {
    if (!raw_line) return NULL;

    /* Batch / Ksh comment checks */
    const wchar_t* p = raw_line;
    while (*p == L' ' || *p == L'\t') p++;
    if (*p == L'#' || *p == L'\0' || wcsncmp(p, L"::", 2) == 0 || _wcsnicmp(p, L"REM ", 4) == 0) {
        return NULL; /* Comments / Blank lines */
    }

    self->env->escapeExpansions = true;
    wchar_t* expanded = self->env->Expand(self->env, raw_line);
    self->env->escapeExpansions = false;
    if (!expanded) return NULL;
    if (self->env->expansionError) {
        free(expanded);
        return NULL;
    }

    CommandAST* head = (CommandAST*)calloc(1, sizeof(CommandAST));
    if (!head) {
        free(expanded);
        return NULL;
    }
    CommandAST* curr = head;

    wchar_t token[MAX_LINE_LEN] = {0};
    size_t tk_idx = 0;
    bool token_started = false;
    bool token_was_quoted = false;
    bool in_single_quote = false;
    bool in_double_quote = false;
    int paren_depth = 0;

    size_t len = wcslen(expanded);
    for (size_t i = 0; i <= len; i++) {
        wchar_t c = expanded[i];

        if (c == L'\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            token_started = true;
            token_was_quoted = true;
            continue;
        }
        if (c == L'\"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            token_started = true;
            token_was_quoted = true;
            continue;
        }

        if (!in_single_quote && !in_double_quote) {
            bool in_test = (curr->argc > 0 && wcscmp(curr->argv[0], L"[[") == 0 &&
                           (curr->argc == 1 || wcscmp(curr->argv[curr->argc - 1], L"]]") != 0));
            if (c == L'(') paren_depth++;
            else if (c == L')' && paren_depth > 0) paren_depth--;

            if (in_test) {
                if (c == L'|' && expanded[i + 1] == L'|') {
                    if (token_started) {
                        token[tk_idx] = L'\0';
                        if (curr->argc >= MAX_ARGS - 1 || !(curr->argv[curr->argc++] = SafeStrDup(token))) goto parse_error;
                        tk_idx = 0; token_started = false; token_was_quoted = false;
                    }
                    if (curr->argc >= MAX_ARGS - 1 || !(curr->argv[curr->argc++] = SafeStrDup(L"||"))) goto parse_error;
                    i++;
                    continue;
                }
                if (c == L'&' && expanded[i + 1] == L'&') {
                    if (token_started) {
                        token[tk_idx] = L'\0';
                        if (curr->argc >= MAX_ARGS - 1 || !(curr->argv[curr->argc++] = SafeStrDup(token))) goto parse_error;
                        tk_idx = 0; token_started = false; token_was_quoted = false;
                    }
                    if (curr->argc >= MAX_ARGS - 1 || !(curr->argv[curr->argc++] = SafeStrDup(L"&&"))) goto parse_error;
                    i++;
                    continue;
                }
                if (c == L'<' || c == L'>') {
                    if (token_started) {
                        token[tk_idx] = L'\0';
                        if (curr->argc >= MAX_ARGS - 1 || !(curr->argv[curr->argc++] = SafeStrDup(token))) goto parse_error;
                        tk_idx = 0; token_started = false; token_was_quoted = false;
                    }
                    wchar_t opStr[2] = { c, L'\0' };
                    if (curr->argc >= MAX_ARGS - 1 || !(curr->argv[curr->argc++] = SafeStrDup(opStr))) goto parse_error;
                    continue;
                }
                if (c == L'|') {
                    if (tk_idx + 1 >= sizeof(token) / sizeof(token[0])) goto parse_error;
                    token[tk_idx++] = c;
                    token_started = true;
                    continue;
                }
            } else {
                if (c == L'|' && paren_depth > 0) {
                    if (tk_idx + 1 >= sizeof(token) / sizeof(token[0])) goto parse_error;
                    token[tk_idx++] = c;
                    token_started = true;
                    continue;
                }
            }

            if (c == L'|' || c == L'>' || c == L'<' || c == L' ' || c == L'\t' || c == L'\0') {
                /* A lone unquoted digit directly before < or > names the descriptor. */
                int fdPrefix = -1;
                bool bothStreams = false;
                if ((c == L'<' || c == L'>') && token_started && !token_was_quoted &&
                    tk_idx == 1 && token[0] >= L'0' && token[0] <= L'9') {
                    fdPrefix = token[0] - L'0';
                    token_started = false;
                    tk_idx = 0;
                }
                if (c == L'>' && token_started) {
                    token[tk_idx] = L'\0';
                    if (wcscmp(token, L"&") == 0) {
                        bothStreams = true;
                        token_started = false;
                        tk_idx = 0;
                    }
                }

                if (token_started) {
                    token[tk_idx] = L'\0';
                    TranslateDrivePathInPlace(token, _countof(token));
                    bool globbingOff = in_test || (self->env->shell && self->env->shell->noglob);
                    bool hasWildcard = (!globbingOff && !token_was_quoted && HasWildcardPattern(token));
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
                                qsort(matches, matchCount, sizeof(wchar_t*), GlobSortCompare);
                                for (int m = 0; m < matchCount; m++) {
                                    if (curr->argc < MAX_ARGS - 1) {
                                        curr->argv[curr->argc++] = matches[m];
                                        expandedGlob = true;
                                    } else {
                                        free(matches[m]);
                                    }
                                }
                            }
                        }
                    }
                    if (!expandedGlob) {
                        if (curr->argc >= MAX_ARGS - 1 ||
                            !(curr->argv[curr->argc] = SafeStrDup(token)))
                            goto parse_error;
                        curr->argc++;
                    }
                    tk_idx = 0;
                    token_started = false;
                    token_was_quoted = false;
                }

                if (c == L'>') {
                    int fd = fdPrefix >= 0 ? fdPrefix : 1;
                    bool is_append = false;
                    if (expanded[i + 1] == L'>') {
                        is_append = true;
                        i++;
                    } else if (!bothStreams && expanded[i + 1] == L'&' &&
                        ((expanded[i + 2] >= L'0' && expanded[i + 2] <= L'9') || expanded[i + 2] == L'-' || expanded[i + 2] == L'p')) {
                        wchar_t target = expanded[i + 2];
                        if (target == L'-') {
                            SetFdRedirect(curr, fd, FD_REDIRECT_CLOSE, NULL, -1);
                        } else if (target == L'p') {
                            SetFdRedirect(curr, fd, FD_REDIRECT_DUP, NULL, -2);
                        } else if ((fd == 1 || fd == 2) && (target == L'1' || target == L'2')) {
                            if (target == L'1') {
                                curr->err_to_out = true;
                                curr->err_to_out_before_out = (curr->out_redirect == NULL);
                            } else {
                                curr->out_to_err = true;
                                curr->out_to_err_before_err = (curr->err_redirect == NULL);
                            }
                        } else {
                            SetFdRedirect(curr, fd, FD_REDIRECT_DUP, NULL, target - L'0');
                        }
                        i += 2;
                        continue;
                    } else if (expanded[i + 1] == L'&') {
                        bothStreams = true;
                        i++;
                    }
                    wchar_t fbuf[MAX_LINE_LEN];
                    size_t filenamePosition = i + 1;
                    if (!ParseRedirectionFilename(expanded, &filenamePosition,
                            fbuf, MAX_LINE_LEN))
                        goto parse_error;

                    if (wcscmp(fbuf, L"&1") == 0) {
                        curr->err_to_out = true;
                        curr->err_to_out_before_out = (curr->out_redirect == NULL);
                    } else if (wcscmp(fbuf, L"&2") == 0) {
                        curr->out_to_err = true;
                        curr->out_to_err_before_err = (curr->err_redirect == NULL);
                    } else {
                        wchar_t* redirect = SafeStrDup(fbuf);
                        if (!redirect) goto parse_error;
                        if (!bothStreams && (fd == 0 || fd >= 3)) {
                            SetFdRedirect(curr, fd,
                                is_append ? FD_REDIRECT_APPEND : FD_REDIRECT_WRITE,
                                redirect, -1);
                        } else if (bothStreams) {
                            free(curr->out_redirect);
                            curr->out_redirect = redirect;
                            curr->append_out = is_append;
                            free(curr->err_redirect);
                            curr->err_redirect = SafeStrDup(fbuf);
                            curr->append_err = is_append;
                        } else if (fd == 2) {
                            free(curr->err_redirect);
                            curr->err_redirect = redirect;
                            curr->append_err = is_append;
                        } else {
                            free(curr->out_redirect);
                            curr->out_redirect = redirect;
                            curr->append_out = is_append;
                        }
                    }
                    i = filenamePosition - 1;
                    continue;
                } else if (c == L'<') {
                    int fd = fdPrefix >= 0 ? fdPrefix : 0;
                    if (expanded[i + 1] == L'&' &&
                        ((expanded[i + 2] >= L'0' && expanded[i + 2] <= L'9') || expanded[i + 2] == L'-' || expanded[i + 2] == L'p')) {
                        if (expanded[i + 2] == L'-') {
                            SetFdRedirect(curr, fd, FD_REDIRECT_CLOSE, NULL, -1);
                        } else if (expanded[i + 2] == L'p') {
                            SetFdRedirect(curr, fd, FD_REDIRECT_DUP, NULL, -2);
                        } else {
                            SetFdRedirect(curr, fd, FD_REDIRECT_DUP, NULL, expanded[i + 2] - L'0');
                        }
                        i += 2;
                        continue;
                    }
                    if (expanded[i + 1] == L'>') {
                        i += 1;
                    }
                    if (fdPrefix > 0) {
                        if (expanded[i + 1] == L'<') goto parse_error;
                        wchar_t fbuf[MAX_LINE_LEN];
                        size_t filenamePosition = i + 1;
                        if (!ParseRedirectionFilename(expanded, &filenamePosition, fbuf, MAX_LINE_LEN))
                            goto parse_error;
                        wchar_t* redirect = SafeStrDup(fbuf);
                        if (!redirect) goto parse_error;
                        SetFdRedirect(curr, fd, FD_REDIRECT_READ, redirect, -1);
                        i = filenamePosition - 1;
                        continue;
                    }
                    if (expanded[i + 1] == L'<' && expanded[i + 2] == L'<') {
                        i += 2;
                        wchar_t fbuf[MAX_LINE_LEN];
                        size_t filenamePosition = i + 1;
                        if (!ParseRedirectionFilename(expanded, &filenamePosition, fbuf, MAX_LINE_LEN))
                            goto parse_error;
                        wchar_t tempPath[MAX_PATH], tempFile[MAX_PATH];
                        GetTempPathW(MAX_PATH, tempPath);
                        GetTempFileNameW(tempPath, L"ksh", 0, tempFile);
                        FILE* tf = NULL;
                        if (_wfopen_s(&tf, tempFile, L"wb") == 0 && tf) {
                            char u8[MAX_LINE_LEN * 4];
                            int u8len = WideCharToMultiByte(CP_UTF8, 0, fbuf, -1, u8, sizeof(u8), NULL, NULL);
                            if (u8len > 1) fwrite(u8, 1, u8len - 1, tf);
                            fwrite("\n", 1, 1, tf);
                            fclose(tf);
                            free(curr->in_redirect);
                            curr->in_redirect = SafeStrDup(tempFile);
                            curr->is_temp_in_redirect = true;
                        }
                        i = filenamePosition - 1;
                        continue;
                    } else if (expanded[i + 1] == L'<') {
                        i += 1;
                        wchar_t fbuf[MAX_LINE_LEN];
                        size_t filenamePosition = i + 1;
                        if (!ParseRedirectionFilename(expanded, &filenamePosition, fbuf, MAX_LINE_LEN))
                            goto parse_error;
                        wchar_t tempPath[MAX_PATH], tempFile[MAX_PATH];
                        GetTempPathW(MAX_PATH, tempPath);
                        GetTempFileNameW(tempPath, L"ksh", 0, tempFile);
                        FILE* tf = NULL;
                        if (_wfopen_s(&tf, tempFile, L"w, ccs=UTF-8") == 0 && tf) {
                            fclose(tf);
                            free(curr->in_redirect);
                            curr->in_redirect = SafeStrDup(tempFile);
                            curr->is_temp_in_redirect = true;
                        }
                        i = filenamePosition - 1;
                        continue;
                    }
                    wchar_t fbuf[MAX_LINE_LEN];
                    size_t filenamePosition = i + 1;
                    if (!ParseRedirectionFilename(expanded, &filenamePosition,
                            fbuf, MAX_LINE_LEN))
                        goto parse_error;
                    wchar_t* redirect = NULL;
                    bool redirectIsTemp = false;
                    if (HeredocIsRegistered(fbuf)) {
                        redirect = HeredocExpandToTemp(self->env, fbuf);
                        redirectIsTemp = redirect != NULL;
                    }
                    if (!redirect) redirect = SafeStrDup(fbuf);
                    if (!redirect) goto parse_error;
                    free(curr->in_redirect);
                    curr->in_redirect = redirect;
                    curr->is_temp_in_redirect = redirectIsTemp;
                    i = filenamePosition - 1;
                    continue;
                } else if (c == L'|') {
                    CommandAST* next_cmd = (CommandAST*)calloc(1, sizeof(CommandAST));
                    if (!next_cmd) goto parse_error;
                    curr->next_in_pipeline = next_cmd;
                    curr = next_cmd;
                    continue;
                }
                continue;
            }
        }
        if (c == L'\\' && !in_single_quote && expanded[i + 1]) {
            wchar_t next = expanded[i + 1];
            if (in_double_quote) {
                if (next == L'"' || next == L'\\' || next == L'$' || next == L'`') {
                    c = next;
                    i++;
                }
            } else {
                if (iswspace(next) || next == L'"' || next == L'\'' ||
                    next == L'\\' || next == L'$' || next == L'`' ||
                    next == L'|' || next == L'&' || next == L';' ||
                    next == L'<' || next == L'>' || next == L'(' || next == L')' ||
                    next == L'*' || next == L'?' || next == L'#') {
                    c = next;
                    i++;
                }
            }
        }
        if (tk_idx + 1 >= sizeof(token) / sizeof(token[0]))
            goto parse_error;
        token[tk_idx++] = c;
        token_started = true;
    }

    if (in_single_quote || in_double_quote) goto parse_error;
    free(expanded);
    for (CommandAST* command = head; command;
        command = command->next_in_pipeline) {
        if (command->argc == 0 && !CommandHasRedirect(command)) {
            fwprintf(stderr, L"ksh: empty command in pipeline\n");
            FreeAST(head);
            return NULL;
        }
    }
    if (head->argc == 0 && !CommandHasRedirect(head)) {
        FreeAST(head);
        return NULL;
    }
    return head;

parse_error:
    fwprintf(stderr, L"ksh: invalid or oversized command line\n");
    free(expanded);
    FreeAST(head);
    return NULL;
}

static void Parser_Destroy(IParser* self) {
    if (s_heredocPaths) {
        for (size_t k = 0; k < s_heredocCount; k++) {
            if (s_heredocPaths[k]) {
                _wremove(s_heredocPaths[k]);
                free(s_heredocPaths[k]);
            }
        }
        free(s_heredocPaths);
        s_heredocPaths = NULL;
        s_heredocCount = 0;
    }
    free(self);
}

IParser* CreateParser(IEnvironment* env) {
    IParser* p = (IParser*)malloc(sizeof(IParser));
    if (!p) return NULL;
    p->env = env;
    p->Parse = Parser_Parse;
    p->Destroy = Parser_Destroy;
    return p;
}

