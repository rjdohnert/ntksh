#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

static const wchar_t* const g_interactiveBuiltins[] = {
    L"alias", L"apply", L"arp", L"assoc", L"attrib", L"autoload", L"awk", L"banner", L"base64", L"basename",
    L"bg", L"break", L"builtin", L"caesar", L"cal", L"cat", L"cd", L"chdir", L"chcp", L"chflags",
    L"chmod", L"chown", L"choice", L"cksum", L"clip", L"cls", L"cmp", L"col", L"color", L"colrm",
    L"column", L"comm", L"command", L"continue", L"copy", L"cp", L"csplit", L"curl", L"cut",
    L"daemon", L"date", L"dd", L"del", L"devinfo", L"df", L"diff", L"dir", L"dirname",
    L"dirs", L"disown", L"dos2unix", L"doskey", L"du", L"echo", L"egrep", L"env", L"erase",
    L"eval", L"exec", L"exit", L"expand", L"export", L"expr", L"factor", L"false", L"fc",
    L"fg", L"fgrep", L"file", L"find", L"findstr", L"float", L"fmt", L"fold", L"free",
    L"fstat", L"ftype", L"functions", L"getent", L"getopts", L"goto", L"grep", L"gunzip", L"gzip",
    L"hash", L"hd", L"head", L"help", L"hexdump", L"hist", L"history", L"host", L"hostname", L"id",
    L"if", L"ifconfig", L"install", L"integer", L"iostat", L"jobs", L"join", L"jot", L"kill",
    L"lam", L"leave", L"let", L"link", L"ln", L"local", L"lockf", L"logname", L"look", L"ls", L"man",
    L"md", L"md5", L"md5sum", L"mkdir", L"mklink", L"mktemp", L"mode", L"module", L"more", L"move",
    L"mtree", L"mv", L"nameref", L"nc", L"netcat", L"netstat", L"nice", L"nl", L"nohup",
    L"nproc", L"od", L"paste", L"patch", L"path", L"pause", L"pax", L"pbcopy", L"pbpaste",
    L"pgrep", L"ping", L"pkill", L"popd", L"pr", L"print", L"printenv", L"printf", L"prompt", L"ps",
    L"pushd", L"pwd", L"r", L"rd", L"read", L"readlink", L"readonly", L"realpath", L"rem",
    L"ren", L"rename", L"renice", L"replace", L"return", L"rev", L"rm", L"rmdir", L"rot13", L"rs", L"script",
    L"sed", L"seq", L"set", L"setenv", L"setlocal", L"setvar", L"sha1", L"sha1sum",
    L"sha256", L"sha256sum", L"sha384", L"sha384sum", L"sha512", L"sha512sum", L"shift",
    L"shred", L"shuf", L"shutdown", L"sleep", L"sockstat", L"sort", L"source", L"split", L"srm", L"start",
    L"stat", L"strings", L"stty", L"subst", L"sum", L"suspend", L"sync", L"sysctl", L"systeminfo", L"tac",
    L"tail", L"tar", L"taskkill", L"tasklist", L"tee", L"test", L"time", L"timeout",
    L"times", L"title", L"touch", L"tr", L"traceroute", L"tracert", L"trap", L"tree", L"true", L"truncate", L"tsort",
    L"tty", L"type", L"typeset", L"ul", L"ulimit", L"umask", L"unalias", L"uname", L"unexpand",
    L"uniq", L"unlink", L"unvis", L"unix2dos", L"unset", L"uptime", L"uuidgen", L"ver",
    L"verify", L"version", L"vis", L"vmstat", L"vol", L"wait", L"watch", L"wc", L"wget",
    L"what", L"whence", L"where", L"which", L"whoami", L"whois", L"xargs", L"yes", L"zcat"
};

static void ConsoleMoveLeft(HANDLE hOut, short width) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
        COORD prev = csbi.dwCursorPosition;
        if (prev.X > 0) {
            prev.X--;
        } else if (prev.Y > 0) {
            prev.X = width - 1;
            prev.Y--;
        }
        SetConsoleCursorPosition(hOut, prev);
    }
}

static void ConsoleMoveRight(HANDLE hOut, short width) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
        COORD next = csbi.dwCursorPosition;
        if (next.X + 1 < width) {
            next.X++;
        } else {
            next.X = 0;
            next.Y++;
        }
        SetConsoleCursorPosition(hOut, next);
    }
}

static void ConsoleRedrawTail(HANDLE hOut, const wchar_t* line, size_t cursor, size_t length, size_t old_length) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hOut, &csbi)) return;
    COORD saved_pos = csbi.dwCursorPosition;

    CONSOLE_CURSOR_INFO cci;
    GetConsoleCursorInfo(hOut, &cci);
    CONSOLE_CURSOR_INFO cci_hide = cci;
    cci_hide.bVisible = FALSE;
    SetConsoleCursorInfo(hOut, &cci_hide);

    wprintf(L"%ls", line + cursor);
    if (old_length > length) {
        for (size_t k = length; k < old_length; k++) putwchar(L' ');
    }
    fflush(stdout);

    SetConsoleCursorPosition(hOut, saved_pos);
    SetConsoleCursorInfo(hOut, &cci);
    fflush(stdout);
}

static void ConsoleFullRedraw(HANDLE hOut, COORD start_pos, const wchar_t* line, size_t length, size_t old_length) {
    CONSOLE_CURSOR_INFO cci;
    GetConsoleCursorInfo(hOut, &cci);
    CONSOLE_CURSOR_INFO cci_hide = cci;
    cci_hide.bVisible = FALSE;
    SetConsoleCursorInfo(hOut, &cci_hide);

    SetConsoleCursorPosition(hOut, start_pos);
    wprintf(L"%ls", line);
    if (old_length > length) {
        for (size_t k = length; k < old_length; k++) putwchar(L' ');
        SetConsoleCursorPosition(hOut, start_pos);
        wprintf(L"%ls", line);
    }
    fflush(stdout);

    SetConsoleCursorInfo(hOut, &cci);
}

bool Shell_IsAdmin(void) {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

static wchar_t s_kill_ring[MAX_LINE_LEN] = { 0 };

static size_t ViWordForward(const wchar_t* line, size_t length, size_t cursor) {
    if (cursor >= length) return length;
    while (cursor < length && !iswspace(line[cursor])) cursor++;
    while (cursor < length && iswspace(line[cursor])) cursor++;
    return cursor;
}

static size_t ViWordBackward(const wchar_t* line, size_t cursor) {
    if (cursor == 0) return 0;
    cursor--;
    while (cursor > 0 && iswspace(line[cursor])) cursor--;
    while (cursor > 0 && !iswspace(line[cursor - 1])) cursor--;
    return cursor;
}

static size_t ViWordEnd(const wchar_t* line, size_t length, size_t cursor) {
    if (cursor + 1 >= length) return length > 0 ? length - 1 : 0;
    cursor++;
    while (cursor < length && iswspace(line[cursor])) cursor++;
    while (cursor + 1 < length && !iswspace(line[cursor + 1])) cursor++;
    return cursor;
}

static void PrintPrompt(IShell* self) {
    wchar_t promptChar = Shell_IsAdmin() ? L'#' : L'$';
    if (self && self->env) {
        wchar_t cwd[KSH_PATH_MAX];
        if (GetCurrentDirectoryW(_countof(cwd), cwd)) {
            self->env->Set(self->env, L"PWD", cwd);
        }
        const wchar_t* ps1 = self->env->Get(self->env, L"PS1");
        if (ps1 && *ps1) {
            wchar_t* expandedPs1 = self->env->Expand(self->env, ps1);
            if (expandedPs1) {
                wprintf(L"%ls", expandedPs1);
                free(expandedPs1);
                fflush(stdout);
                return;
            }
        }
    }
    wprintf(L"%lc ", promptChar);
    fflush(stdout);
}

static bool Shell_ReadLineInteractive(IShell* self, wchar_t* line, size_t maxLen) {
    if (!_isatty(_fileno(stdin))) {
        if (!fgetws(line, (int)maxLen, stdin)) return false;
        size_t l = wcslen(line);
        while (l > 0 && (line[l - 1] == L'\n' || line[l - 1] == L'\r')) line[--l] = L'\0';
        return true;
    }

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD start_pos = { 0, 0 };
    short width = 80;
    bool is_console = (GetConsoleScreenBufferInfo(hOut, &csbi) != 0);
    if (is_console) {
        start_pos = csbi.dwCursorPosition;
        if (csbi.dwSize.X > 0) width = csbi.dwSize.X;
    }

    size_t length = 0;
    size_t cursor = 0;
    int history_pos = g_history_count;
    wchar_t saved_current[MAX_LINE_LEN] = {0};
    line[0] = L'\0';

    bool vi_mode = (self && self->editMode == EDIT_MODE_VI);
    bool vi_insert = true;
    wchar_t vi_last_line[MAX_LINE_LEN] = {0};

    while (1) {
        wint_t c = _getwch();
        if (c == 0 || c == 0xE0) {
            wint_t ext = _getwch();
            if (ext == 72) { /* Up Arrow */
                if (history_pos > 0) {
                    if (history_pos == g_history_count) {
                        wcscpy_s(saved_current, MAX_LINE_LEN, line);
                    }
                    history_pos--;
                    size_t old_length = length;
                    wcscpy_s(line, maxLen, g_history[history_pos]);
                    length = wcslen(line);
                    cursor = length;
                    if (is_console) {
                        ConsoleFullRedraw(hOut, start_pos, line, length, old_length);
                    } else {
                        wprintf(L"%ls", line);
                        fflush(stdout);
                    }
                }
            } else if (ext == 80) { /* Down Arrow */
                if (history_pos < g_history_count) {
                    history_pos++;
                    size_t old_length = length;
                    if (history_pos == g_history_count) {
                        wcscpy_s(line, maxLen, saved_current);
                    } else {
                        wcscpy_s(line, maxLen, g_history[history_pos]);
                    }
                    length = wcslen(line);
                    cursor = length;
                    if (is_console) {
                        ConsoleFullRedraw(hOut, start_pos, line, length, old_length);
                    } else {
                        wprintf(L"%ls", line);
                        fflush(stdout);
                    }
                }
            } else if (ext == 75) { /* Left Arrow */
                if (cursor > 0) {
                    cursor--;
                    if (is_console) ConsoleMoveLeft(hOut, width);
                    else { putwchar(L'\b'); fflush(stdout); }
                }
            } else if (ext == 77) { /* Right Arrow */
                if (cursor < length) {
                    cursor++;
                    if (is_console) ConsoleMoveRight(hOut, width);
                    else { putwchar(line[cursor - 1]); fflush(stdout); }
                }
            } else if (ext == 71) { /* Home */
                while (cursor > 0) {
                    cursor--;
                    if (is_console) ConsoleMoveLeft(hOut, width);
                    else putwchar(L'\b');
                }
                fflush(stdout);
            } else if (ext == 79) { /* End */
                while (cursor < length) {
                    cursor++;
                    if (is_console) ConsoleMoveRight(hOut, width);
                    else putwchar(line[cursor - 1]);
                }
                fflush(stdout);
            } else if (ext == 83) { /* Delete */
                if (cursor < length) {
                    size_t old_length = length;
                    memmove(&line[cursor], &line[cursor + 1], (length - cursor) * sizeof(wchar_t));
                    length--;
                    if (is_console) {
                        ConsoleRedrawTail(hOut, line, cursor, length, old_length);
                    } else {
                        wprintf(L"%ls \b", line + cursor);
                        for (size_t i = length; i > cursor; --i) putwchar(L'\b');
                        fflush(stdout);
                    }
                }
            }
            continue;
        }

        if (c == L'\r' || c == L'\n') {
            while (cursor < length) {
                cursor++;
                if (is_console) ConsoleMoveRight(hOut, width);
                else putwchar(line[cursor - 1]);
            }
            putwchar(L'\n');
            fflush(stdout);
            line[length] = L'\0';
            return true;
        }

        if (c == 3) { /* Ctrl+C */
            line[0] = L'\0';
            putwchar(L'\n');
            fflush(stdout);
            return true;
        }

        if (c == 4 || c == 26) { /* Ctrl+D or Ctrl+Z */
            if (length == 0) return false;
            if (c == 4 && cursor < length) {
                size_t old_length = length;
                memmove(&line[cursor], &line[cursor + 1], (length - cursor) * sizeof(wchar_t));
                length--;
                if (is_console) ConsoleRedrawTail(hOut, line, cursor, length, old_length);
            }
            continue;
        }

        /* Vi command mode */
        if (vi_mode && !vi_insert) {
            if (c == 27) continue;
            if (c == 'i') {
                wcscpy_s(vi_last_line, MAX_LINE_LEN, line);
                vi_insert = true;
                continue;
            }
            if (c == 'I') {
                while (cursor > 0) {
                    cursor--;
                    if (is_console) ConsoleMoveLeft(hOut, width);
                    else putwchar(L'\b');
                }
                fflush(stdout);
                wcscpy_s(vi_last_line, MAX_LINE_LEN, line);
                vi_insert = true;
                continue;
            }
            if (c == 'a') {
                if (cursor < length) {
                    cursor++;
                    if (is_console) ConsoleMoveRight(hOut, width);
                    else putwchar(line[cursor - 1]);
                    fflush(stdout);
                }
                wcscpy_s(vi_last_line, MAX_LINE_LEN, line);
                vi_insert = true;
                continue;
            }
            if (c == 'A') {
                while (cursor < length) {
                    cursor++;
                    if (is_console) ConsoleMoveRight(hOut, width);
                    else putwchar(line[cursor - 1]);
                }
                fflush(stdout);
                wcscpy_s(vi_last_line, MAX_LINE_LEN, line);
                vi_insert = true;
                continue;
            }
            if (c == 's') {
                wcscpy_s(vi_last_line, MAX_LINE_LEN, line);
                if (cursor < length) {
                    size_t old_len = length;
                    memmove(&line[cursor], &line[cursor + 1], (length - cursor) * sizeof(wchar_t));
                    length--;
                    if (is_console) ConsoleRedrawTail(hOut, line, cursor, length, old_len);
                }
                vi_insert = true;
                continue;
            }
            if (c == 'S') {
                wcscpy_s(vi_last_line, MAX_LINE_LEN, line);
                size_t old_len = length;
                while (cursor > 0) {
                    cursor--;
                    if (is_console) ConsoleMoveLeft(hOut, width);
                }
                length = 0;
                line[0] = L'\0';
                if (is_console) ConsoleFullRedraw(hOut, start_pos, line, length, old_len);
                vi_insert = true;
                continue;
            }
            if (c == 'C') {
                wcscpy_s(vi_last_line, MAX_LINE_LEN, line);
                size_t old_len = length;
                length = cursor;
                line[length] = L'\0';
                if (is_console) ConsoleRedrawTail(hOut, line, cursor, length, old_len);
                vi_insert = true;
                continue;
            }
            if (c == 'h' || c == 8 || c == 127) {
                if (cursor > 0) {
                    cursor--;
                    if (is_console) ConsoleMoveLeft(hOut, width);
                    else { putwchar(L'\b'); fflush(stdout); }
                }
                continue;
            }
            if (c == 'l' || c == ' ') {
                if (cursor < length) {
                    cursor++;
                    if (is_console) ConsoleMoveRight(hOut, width);
                    else { putwchar(line[cursor - 1]); fflush(stdout); }
                }
                continue;
            }
            if (c == '0' || c == '^') {
                while (cursor > 0) {
                    cursor--;
                    if (is_console) ConsoleMoveLeft(hOut, width);
                    else putwchar(L'\b');
                }
                fflush(stdout);
                continue;
            }
            if (c == '$') {
                while (cursor + 1 < length) {
                    cursor++;
                    if (is_console) ConsoleMoveRight(hOut, width);
                    else putwchar(line[cursor - 1]);
                }
                fflush(stdout);
                continue;
            }
            if (c == 'w') {
                size_t npos = ViWordForward(line, length, cursor);
                while (cursor < npos) {
                    cursor++;
                    if (is_console) ConsoleMoveRight(hOut, width);
                    else putwchar(line[cursor - 1]);
                }
                fflush(stdout);
                continue;
            }
            if (c == 'b') {
                size_t npos = ViWordBackward(line, cursor);
                while (cursor > npos) {
                    cursor--;
                    if (is_console) ConsoleMoveLeft(hOut, width);
                    else putwchar(L'\b');
                }
                fflush(stdout);
                continue;
            }
            if (c == 'e') {
                size_t npos = ViWordEnd(line, length, cursor);
                while (cursor < npos) {
                    cursor++;
                    if (is_console) ConsoleMoveRight(hOut, width);
                    else putwchar(line[cursor - 1]);
                }
                fflush(stdout);
                continue;
            }
            if (c == 'x') {
                if (cursor < length) {
                    size_t old_len = length;
                    wcscpy_s(vi_last_line, MAX_LINE_LEN, line);
                    memmove(&line[cursor], &line[cursor + 1], (length - cursor) * sizeof(wchar_t));
                    length--;
                    if (is_console) ConsoleRedrawTail(hOut, line, cursor, length, old_len);
                }
                continue;
            }
            if (c == 'X') {
                if (cursor > 0) {
                    size_t old_len = length;
                    wcscpy_s(vi_last_line, MAX_LINE_LEN, line);
                    cursor--;
                    memmove(&line[cursor], &line[cursor + 1], (length - cursor) * sizeof(wchar_t));
                    length--;
                    if (is_console) {
                        ConsoleMoveLeft(hOut, width);
                        ConsoleRedrawTail(hOut, line, cursor, length, old_len);
                    }
                }
                continue;
            }
            if (c == 'D') {
                wcscpy_s(vi_last_line, MAX_LINE_LEN, line);
                size_t old_len = length;
                length = cursor;
                line[length] = L'\0';
                if (is_console) ConsoleRedrawTail(hOut, line, cursor, length, old_len);
                continue;
            }
            if (c == 'd') {
                wint_t next_k = _getwch();
                if (next_k == 'd') {
                    wcscpy_s(vi_last_line, MAX_LINE_LEN, line);
                    size_t old_len = length;
                    while (cursor > 0) {
                        cursor--;
                        if (is_console) ConsoleMoveLeft(hOut, width);
                    }
                    length = 0;
                    line[0] = L'\0';
                    if (is_console) ConsoleFullRedraw(hOut, start_pos, line, length, old_len);
                } else if (next_k == 'w') {
                    size_t npos = ViWordForward(line, length, cursor);
                    size_t del_cnt = npos - cursor;
                    size_t old_len = length;
                    wcscpy_s(vi_last_line, MAX_LINE_LEN, line);
                    memmove(&line[cursor], &line[npos], (length - npos + 1) * sizeof(wchar_t));
                    length -= del_cnt;
                    if (is_console) ConsoleRedrawTail(hOut, line, cursor, length, old_len);
                }
                continue;
            }
            if (c == 'c') {
                wint_t next_k = _getwch();
                if (next_k == 'c') {
                    wcscpy_s(vi_last_line, MAX_LINE_LEN, line);
                    size_t old_len = length;
                    while (cursor > 0) {
                        cursor--;
                        if (is_console) ConsoleMoveLeft(hOut, width);
                    }
                    length = 0;
                    line[0] = L'\0';
                    if (is_console) ConsoleFullRedraw(hOut, start_pos, line, length, old_len);
                    vi_insert = true;
                } else if (next_k == 'w') {
                    size_t npos = ViWordForward(line, length, cursor);
                    size_t del_cnt = npos - cursor;
                    size_t old_len = length;
                    wcscpy_s(vi_last_line, MAX_LINE_LEN, line);
                    memmove(&line[cursor], &line[npos], (length - npos + 1) * sizeof(wchar_t));
                    length -= del_cnt;
                    if (is_console) ConsoleRedrawTail(hOut, line, cursor, length, old_len);
                    vi_insert = true;
                }
                continue;
            }
            if (c == 'r') {
                wint_t rep_c = _getwch();
                if (rep_c >= 32 && cursor < length) {
                    wcscpy_s(vi_last_line, MAX_LINE_LEN, line);
                    line[cursor] = (wchar_t)rep_c;
                    if (is_console) ConsoleRedrawTail(hOut, line, cursor, length, length);
                    else { putwchar((wchar_t)rep_c); putwchar(L'\b'); fflush(stdout); }
                }
                continue;
            }
            if (c == '~') {
                if (cursor < length) {
                    line[cursor] = iswupper(line[cursor]) ? towlower(line[cursor]) : towupper(line[cursor]);
                    putwchar(line[cursor]);
                    fflush(stdout);
                    cursor++;
                }
                continue;
            }
            if (c == 'u') {
                wchar_t tmp_line[MAX_LINE_LEN];
                wcscpy_s(tmp_line, MAX_LINE_LEN, line);
                size_t old_len = length;
                wcscpy_s(line, maxLen, vi_last_line);
                wcscpy_s(vi_last_line, MAX_LINE_LEN, tmp_line);
                length = wcslen(line);
                cursor = (cursor <= length) ? cursor : length;
                if (is_console) ConsoleFullRedraw(hOut, start_pos, line, length, old_len);
                continue;
            }
            if (c == 'k') {
                if (history_pos > 0) {
                    if (history_pos == g_history_count) {
                        wcscpy_s(saved_current, MAX_LINE_LEN, line);
                    }
                    history_pos--;
                    size_t old_len = length;
                    wcscpy_s(line, maxLen, g_history[history_pos]);
                    length = wcslen(line);
                    cursor = length;
                    if (is_console) ConsoleFullRedraw(hOut, start_pos, line, length, old_len);
                }
                continue;
            }
            if (c == 'j') {
                if (history_pos < g_history_count) {
                    history_pos++;
                    size_t old_len = length;
                    if (history_pos == g_history_count) {
                        wcscpy_s(line, maxLen, saved_current);
                    } else {
                        wcscpy_s(line, maxLen, g_history[history_pos]);
                    }
                    length = wcslen(line);
                    cursor = length;
                    if (is_console) ConsoleFullRedraw(hOut, start_pos, line, length, old_len);
                }
                continue;
            }
            continue;
        }

        /* Escape in Vi mode switches to Command mode */
        if (vi_mode && c == 27) {
            vi_insert = false;
            wcscpy_s(vi_last_line, MAX_LINE_LEN, line);
            if (cursor > 0) {
                cursor--;
                if (is_console) ConsoleMoveLeft(hOut, width);
                else putwchar(L'\b');
                fflush(stdout);
            }
            continue;
        }

        /* Emacs shortcuts */
        if (!vi_mode) {
            if (c == 1) { /* Ctrl+A: Home */
                while (cursor > 0) {
                    cursor--;
                    if (is_console) ConsoleMoveLeft(hOut, width);
                    else putwchar(L'\b');
                }
                fflush(stdout);
                continue;
            }
            if (c == 5) { /* Ctrl+E: End */
                while (cursor < length) {
                    cursor++;
                    if (is_console) ConsoleMoveRight(hOut, width);
                    else putwchar(line[cursor - 1]);
                }
                fflush(stdout);
                continue;
            }
            if (c == 2) { /* Ctrl+B: Left */
                if (cursor > 0) {
                    cursor--;
                    if (is_console) ConsoleMoveLeft(hOut, width);
                    else { putwchar(L'\b'); fflush(stdout); }
                }
                continue;
            }
            if (c == 6) { /* Ctrl+F: Right */
                if (cursor < length) {
                    cursor++;
                    if (is_console) ConsoleMoveRight(hOut, width);
                    else { putwchar(line[cursor - 1]); fflush(stdout); }
                }
                continue;
            }
            if (c == 16) { /* Ctrl+P: History Up */
                if (history_pos > 0) {
                    if (history_pos == g_history_count) {
                        wcscpy_s(saved_current, MAX_LINE_LEN, line);
                    }
                    history_pos--;
                    size_t old_length = length;
                    wcscpy_s(line, maxLen, g_history[history_pos]);
                    length = wcslen(line);
                    cursor = length;
                    if (is_console) ConsoleFullRedraw(hOut, start_pos, line, length, old_length);
                }
                continue;
            }
            if (c == 14) { /* Ctrl+N: History Down */
                if (history_pos < g_history_count) {
                    history_pos++;
                    size_t old_length = length;
                    if (history_pos == g_history_count) {
                        wcscpy_s(line, maxLen, saved_current);
                    } else {
                        wcscpy_s(line, maxLen, g_history[history_pos]);
                    }
                    length = wcslen(line);
                    cursor = length;
                    if (is_console) ConsoleFullRedraw(hOut, start_pos, line, length, old_length);
                }
                continue;
            }
            if (c == 11) { /* Ctrl+K: Kill to end of line */
                wcscpy_s(s_kill_ring, MAX_LINE_LEN, line + cursor);
                size_t old_length = length;
                length = cursor;
                line[length] = L'\0';
                if (is_console) ConsoleRedrawTail(hOut, line, cursor, length, old_length);
                continue;
            }
            if (c == 21) { /* Ctrl+U: Kill line to cursor */
                wcsncpy_s(s_kill_ring, MAX_LINE_LEN, line, cursor);
                size_t old_length = length;
                memmove(line, line + cursor, (length - cursor + 1) * sizeof(wchar_t));
                length -= cursor;
                cursor = 0;
                if (is_console) ConsoleFullRedraw(hOut, start_pos, line, length, old_length);
                continue;
            }
            if (c == 25) { /* Ctrl+Y: Yank */
                size_t klen = wcslen(s_kill_ring);
                if (klen > 0 && length + klen < maxLen) {
                    size_t old_length = length;
                    memmove(&line[cursor + klen], &line[cursor], (length - cursor + 1) * sizeof(wchar_t));
                    memcpy(&line[cursor], s_kill_ring, klen * sizeof(wchar_t));
                    cursor += klen;
                    length += klen;
                    if (is_console) {
                        wprintf(L"%.*ls", (int)klen, s_kill_ring);
                        fflush(stdout);
                        ConsoleRedrawTail(hOut, line, cursor, length, old_length);
                    }
                }
                continue;
            }
            if (c == 23) { /* Ctrl+W: Kill word backward */
                if (cursor > 0) {
                    size_t npos = ViWordBackward(line, cursor);
                    size_t del_cnt = cursor - npos;
                    size_t old_length = length;
                    wcsncpy_s(s_kill_ring, MAX_LINE_LEN, line + npos, del_cnt);
                    cursor = npos;
                    memmove(&line[cursor], &line[cursor + del_cnt], (length - (cursor + del_cnt) + 1) * sizeof(wchar_t));
                    length -= del_cnt;
                    if (is_console) {
                        for (size_t k = 0; k < del_cnt; k++) ConsoleMoveLeft(hOut, width);
                        ConsoleRedrawTail(hOut, line, cursor, length, old_length);
                    }
                }
                continue;
            }
            if (c == 12) { /* Ctrl+L: Clear screen */
                if (is_console) {
                    COORD top_left = { 0, 0 };
                    DWORD written = 0;
                    DWORD buf_size = (DWORD)csbi.dwSize.X * (DWORD)csbi.dwSize.Y;
                    FillConsoleOutputCharacterW(hOut, L' ', buf_size, top_left, &written);
                    FillConsoleOutputAttribute(hOut, csbi.wAttributes, buf_size, top_left, &written);
                    SetConsoleCursorPosition(hOut, top_left);
                    PrintPrompt(self);
                    GetConsoleScreenBufferInfo(hOut, &csbi);
                    start_pos = csbi.dwCursorPosition;
                    wprintf(L"%ls", line);
                    fflush(stdout);
                    for (size_t k = length; k > cursor; k--) ConsoleMoveLeft(hOut, width);
                }
                continue;
            }
        }

        if (c == 8 || c == 127) { /* Backspace */
            if (cursor > 0) {
                if (cursor == length && is_console) {
                    CONSOLE_SCREEN_BUFFER_INFO cur_csbi;
                    if (GetConsoleScreenBufferInfo(hOut, &cur_csbi)) {
                        COORD prev = cur_csbi.dwCursorPosition;
                        if (prev.X > 0) {
                            prev.X--;
                        } else if (prev.Y > 0) {
                            prev.X = width - 1;
                            prev.Y--;
                        }
                        DWORD written = 0;
                        FillConsoleOutputCharacterW(hOut, L' ', 1, prev, &written);
                        SetConsoleCursorPosition(hOut, prev);
                    }
                    cursor--;
                    length--;
                    line[length] = L'\0';
                } else {
                    size_t old_length = length;
                    cursor--;
                    memmove(&line[cursor], &line[cursor + 1], (length - cursor) * sizeof(wchar_t));
                    length--;
                    if (is_console) {
                        ConsoleMoveLeft(hOut, width);
                        ConsoleRedrawTail(hOut, line, cursor, length, old_length);
                    } else {
                        putwchar(L'\b');
                        wprintf(L"%ls \b", line + cursor);
                        for (size_t i = length; i > cursor; --i) putwchar(L'\b');
                        fflush(stdout);
                    }
                }
            }
            continue;
        }

        if (c == L'\t') { /* Tab Completion */
            size_t token_start = cursor;
            while (token_start > 0 && !iswspace(line[token_start - 1]) &&
                   line[token_start - 1] != L'|' && line[token_start - 1] != L'&' &&
                   line[token_start - 1] != L';' && line[token_start - 1] != L'(') {
                token_start--;
            }
            size_t prefix_len = cursor - token_start;
            wchar_t prefix[MAX_LINE_LEN];
            if (prefix_len < _countof(prefix)) {
                wcsncpy_s(prefix, _countof(prefix), line + token_start, prefix_len);
                prefix[prefix_len] = L'\0';

                bool is_cmd = (token_start == 0);
                if (!is_cmd) {
                    size_t prev = token_start;
                    while (prev > 0 && iswspace(line[prev - 1])) prev--;
                    if (prev == 0 || line[prev - 1] == L'|' || line[prev - 1] == L'&' ||
                        line[prev - 1] == L';' || line[prev - 1] == L'(') {
                        is_cmd = true;
                    }
                }

                wchar_t* matches[256];
                int match_count = 0;

                /* 1. If in command position and no slash, complete builtins/aliases/functions */
                if (is_cmd && !wcschr(prefix, L'/') && !wcschr(prefix, L'\\')) {
                    for (size_t b = 0; b < _countof(g_interactiveBuiltins) && match_count < 256; b++) {
                        if (_wcsnicmp(g_interactiveBuiltins[b], prefix, prefix_len) == 0) {
                            matches[match_count++] = SafeStrDup(g_interactiveBuiltins[b]);
                        }
                    }
                    const wchar_t* dynNames[64];
                    size_t dynCount = Module_GetDynamicBuiltinNames(dynNames, 64);
                    for (size_t d = 0; d < dynCount && match_count < 256; d++) {
                        if (_wcsnicmp(dynNames[d], prefix, prefix_len) == 0) {
                            matches[match_count++] = SafeStrDup(dynNames[d]);
                        }
                    }
                    if (self) {
                        for (ShellAlias* al = self->aliases; al && match_count < 256; al = al->next) {
                            if (_wcsnicmp(al->name, prefix, prefix_len) == 0) {
                                matches[match_count++] = SafeStrDup(al->name);
                            }
                        }
                        for (ShellFunction* fn = self->functions; fn && match_count < 256; fn = fn->next) {
                            if (_wcsnicmp(fn->name, prefix, prefix_len) == 0) {
                                matches[match_count++] = SafeStrDup(fn->name);
                            }
                        }
                    }
                }

                /* 2. File / Directory completions */
                if (!is_cmd || match_count == 0 || wcschr(prefix, L'/') || wcschr(prefix, L'\\')) {
                    wchar_t search_dir[KSH_PATH_MAX] = L".";
                    const wchar_t* file_prefix = prefix;
                    const wchar_t* last_slash = wcsrchr(prefix, L'\\');
                    const wchar_t* last_slash2 = wcsrchr(prefix, L'/');
                    if (last_slash2 && (!last_slash || last_slash2 > last_slash)) last_slash = last_slash2;
                    if (last_slash) {
                        size_t dlen = (size_t)(last_slash - prefix);
                        wcsncpy_s(search_dir, _countof(search_dir), prefix, dlen ? dlen : 1);
                        if (dlen == 0) wcscpy_s(search_dir, _countof(search_dir), L"/");
                        file_prefix = last_slash + 1;
                    }
                    wchar_t pat[KSH_PATH_MAX];
                    _snwprintf_s(pat, _countof(pat), _TRUNCATE, L"%ls\\%ls*", search_dir, file_prefix);
                    WIN32_FIND_DATAW wfd;
                    HANDLE hFind = FindFirstFileW(pat, &wfd);
                    if (hFind != INVALID_HANDLE_VALUE) {
                        do {
                            if (wcscmp(wfd.cFileName, L".") == 0 || wcscmp(wfd.cFileName, L"..") == 0) continue;
                            bool is_dir = (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                            wchar_t candidate[KSH_PATH_MAX];
                            if (last_slash) {
                                _snwprintf_s(candidate, _countof(candidate), _TRUNCATE, L"%.*ls%ls%ls",
                                    (int)(last_slash - prefix + 1), prefix, wfd.cFileName, is_dir ? L"\\" : L"");
                            } else {
                                _snwprintf_s(candidate, _countof(candidate), _TRUNCATE, L"%ls%ls",
                                    wfd.cFileName, is_dir ? L"\\" : L"");
                            }
                            if (match_count < 256) matches[match_count++] = SafeStrDup(candidate);
                        } while (FindNextFileW(hFind, &wfd));
                        FindClose(hFind);
                    }
                }

                if (match_count == 1) {
                    const wchar_t* m = matches[0];
                    size_t mlen = wcslen(m);
                    bool add_space = (m[mlen - 1] != L'\\' && m[mlen - 1] != L'/');
                    size_t needed = mlen - prefix_len + (add_space ? 1 : 0);
                    if (length + needed < maxLen) {
                        size_t old_length = length;
                        memmove(&line[cursor + needed], &line[cursor], (length - cursor + 1) * sizeof(wchar_t));
                        memcpy(&line[token_start], m, mlen * sizeof(wchar_t));
                        if (add_space) line[token_start + mlen] = L' ';
                        length += needed;
                        cursor = token_start + mlen + (add_space ? 1 : 0);
                        if (is_console) {
                            ConsoleFullRedraw(hOut, start_pos, line, length, old_length);
                        } else {
                            wprintf(L"%ls", &line[token_start]);
                            for (size_t k = length; k > cursor; k--) putwchar(L'\b');
                            fflush(stdout);
                        }
                    }
                } else if (match_count > 1) {
                    size_t common_len = wcslen(matches[0]);
                    for (int m = 1; m < match_count; m++) {
                        size_t k = 0;
                        while (k < common_len && matches[m][k] &&
                               towlower(matches[0][k]) == towlower(matches[m][k])) {
                            k++;
                        }
                        common_len = k;
                    }
                    if (common_len > prefix_len) {
                        size_t needed = common_len - prefix_len;
                        if (length + needed < maxLen) {
                            size_t old_length = length;
                            memmove(&line[cursor + needed], &line[cursor], (length - cursor + 1) * sizeof(wchar_t));
                            memcpy(&line[token_start], matches[0], common_len * sizeof(wchar_t));
                            length += needed;
                            cursor = token_start + common_len;
                            if (is_console) {
                                ConsoleFullRedraw(hOut, start_pos, line, length, old_length);
                            } else {
                                wprintf(L"%ls", &line[token_start]);
                                for (size_t k = length; k > cursor; k--) putwchar(L'\b');
                                fflush(stdout);
                            }
                        }
                    } else {
                        putwchar(L'\n');
                        for (int m = 0; m < match_count; m++) {
                            wprintf(L"%ls  ", matches[m]);
                            if ((m + 1) % 4 == 0) putwchar(L'\n');
                        }
                        if (match_count % 4 != 0) putwchar(L'\n');
                        PrintPrompt(self);
                        if (is_console && GetConsoleScreenBufferInfo(hOut, &csbi)) {
                            start_pos = csbi.dwCursorPosition;
                        }
                        wprintf(L"%ls", line);
                        fflush(stdout);
                        if (cursor < length && is_console) {
                            for (size_t k = length; k > cursor; k--) ConsoleMoveLeft(hOut, width);
                        }
                    }
                }
                for (int m = 0; m < match_count; m++) free(matches[m]);
            }
            continue;
        }

        if (c == 3) { /* Ctrl+C */
            line[0] = L'\0';
            putwchar(L'\n');
            fflush(stdout);
            return true;
        }

        if (c == 4 || c == 26) { /* Ctrl+D or Ctrl+Z */
            if (length == 0) return false;
            continue;
        }

        if (c >= 32 && length + 1 < maxLen) {
            size_t old_length = length;
            if (cursor < length) {
                memmove(&line[cursor + 1], &line[cursor], (length - cursor + 1) * sizeof(wchar_t));
                line[cursor] = (wchar_t)c;
                cursor++;
                length++;
                if (is_console) {
                    putwchar((wchar_t)c);
                    fflush(stdout);
                    ConsoleRedrawTail(hOut, line, cursor, length, old_length);
                } else {
                    putwchar((wchar_t)c);
                    wprintf(L"%ls", line + cursor);
                    for (size_t i = length; i > cursor; --i) putwchar(L'\b');
                    fflush(stdout);
                }
            } else {
                line[length++] = (wchar_t)c;
                cursor = length;
                line[length] = L'\0';
                putwchar((wchar_t)c);
                fflush(stdout);
            }
        }
    }
}

void Shell_RunREPL(IShell* self) {
    wchar_t line[MAX_LINE_LEN];

    self->interactive = true;
    History_LoadFromFile();
    Shell_LoadKshIni(self);

    wprintf(L"Type 'help' or 'man' for documentation.\n\n");

    size_t lineNo = 0;
    while (self->running) {
        if (g_interrupted) {
            g_interrupted = 0;
            wprintf(L"\n");
        }
        Shell_DispatchPendingSignals(self);
        PrintPrompt(self);

        if (!Shell_ReadLineInteractive(self, line, MAX_LINE_LEN)) {
            break; /* EOF */
        }

        size_t slen = wcslen(line);
        while (slen > 0 && (line[slen - 1] == L'\n' || line[slen - 1] == L'\r')) {
            line[--slen] = L'\0';
        }

        lineNo++;
        wchar_t lineNoText[32];
        swprintf_s(lineNoText, _countof(lineNoText), L"%zu", lineNo);
        self->env->Set(self->env, L"LINENO", lineNoText);
        self->ExecuteLine(self, line);
    }
    self->interactive = false;
}

