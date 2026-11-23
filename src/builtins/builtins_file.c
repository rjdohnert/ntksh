#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

/* ========================================================================= */
/*       Category: ls, cp, mv, rm, ln, xargs, base64, checksums, ps, pgrep    */
/* ========================================================================= */

typedef struct {
    wchar_t name[KSH_PATH_MAX];
    wchar_t full_path[KSH_PATH_MAX];
    DWORD attr;
    uint64_t size;
    FILETIME mtime;
    FILETIME atime;
    FILETIME ctime;
    uint64_t inode;
    uint32_t nlink;
} LsItem;

static int CompareLsItems(void* ctx, const void* a, const void* b) {
    const LsItem* ia = (const LsItem*)a;
    const LsItem* ib = (const LsItem*)b;
    int flags = *(int*)ctx; // bit 0: mtime, bit 1: size, bit 2: reverse, bit 3: ctime, bit 4: atime, bit 5: group_dirs
    int cmp = 0;

    if (flags & 32) { // group directories first
        bool da = (ia->attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool db = (ib->attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (da != db) return da ? -1 : 1;
    }

    if (flags & 1) { // mtime
        cmp = CompareFileTime(&ia->mtime, &ib->mtime);
        cmp = -cmp; // newest first
    } else if (flags & 8) { // ctime
        cmp = CompareFileTime(&ia->ctime, &ib->ctime);
        cmp = -cmp;
    } else if (flags & 16) { // atime
        cmp = CompareFileTime(&ia->atime, &ib->atime);
        cmp = -cmp;
    } else if (flags & 2) { // size
        if (ia->size < ib->size) cmp = 1;
        else if (ia->size > ib->size) cmp = -1;
        else cmp = 0;
    } else {
        cmp = _wcsicmp(ia->name, ib->name);
    }
    if (flags & 4) cmp = -cmp;
    return cmp;
}

static void FormatLsSize(uint64_t sz, bool human, wchar_t* out, size_t out_len) {
    if (!human) {
        swprintf_s(out, out_len, L"%llu", sz);
        return;
    }
    static const wchar_t* units[] = { L"B", L"K", L"M", L"G", L"T" };
    double dsz = (double)sz;
    int u = 0;
    while (dsz >= 1024.0 && u < 4) {
        dsz /= 1024.0;
        u++;
    }
    if (u == 0) swprintf_s(out, out_len, L"%lluB", sz);
    else swprintf_s(out, out_len, L"%.1f%ls", dsz, units[u]);
}

static const wchar_t* kLsMonths[12] = {
    L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
    L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"
};

typedef struct {
    bool opt_a, opt_A, opt_l, opt_h, opt_k;
    bool opt_r, opt_t, opt_S, opt_1, opt_C, opt_x, opt_m;
    bool opt_F, opt_p, opt_d, opt_R, opt_c, opt_u, opt_U;
    bool opt_f, opt_g, opt_o, opt_n, opt_i, opt_s, opt_T, opt_G;
    bool group_dirs;
    int sort_flags;
    int term_width;
    bool is_tty;
} LsOpts;

static void LsPrintSingle(const LsOpts* opts, const LsItem* item, const wchar_t* indicator, const wchar_t* target_path) {
    (void)target_path;
    const wchar_t* dot = wcsrchr(item->name, L'.');
    if (opts->opt_l) {
        wchar_t perm[11] = L"-rw-r--r--";
        if (item->attr & FILE_ATTRIBUTE_DIRECTORY) {
            perm[0] = L'd';
            perm[3] = L'x'; perm[6] = L'x'; perm[9] = L'x';
        } else if (item->attr & FILE_ATTRIBUTE_REPARSE_POINT) {
            perm[0] = L'l';
            perm[3] = L'x'; perm[6] = L'x'; perm[9] = L'x';
        }

        if (dot && (_wcsicmp(dot, L".exe") == 0 || _wcsicmp(dot, L".cmd") == 0 || _wcsicmp(dot, L".bat") == 0 || _wcsicmp(dot, L".ksh") == 0 || _wcsicmp(dot, L".ps1") == 0)) {
            perm[3] = L'x'; perm[6] = L'x'; perm[9] = L'x';
        }
        if (item->attr & FILE_ATTRIBUTE_READONLY) {
            perm[2] = L'-'; perm[5] = L'-'; perm[8] = L'-';
        }

        const FILETIME* ft = &item->mtime;
        if (opts->opt_c) ft = &item->ctime;
        else if (opts->opt_u) ft = &item->atime;

        SYSTEMTIME st;
        FileTimeToSystemTime(ft, &st);

        wchar_t time_buf[64];
        if (opts->opt_T) {
            swprintf_s(time_buf, _countof(time_buf), L"%ls %2d %02d:%02d:%02d %4d",
                       kLsMonths[(st.wMonth >= 1 && st.wMonth <= 12) ? st.wMonth - 1 : 0],
                       st.wDay, st.wHour, st.wMinute, st.wSecond, st.wYear);
        } else {
            SYSTEMTIME cur_st;
            GetLocalTime(&cur_st);
            if (cur_st.wYear == st.wYear) {
                swprintf_s(time_buf, _countof(time_buf), L"%ls %2d %02d:%02d",
                           kLsMonths[(st.wMonth >= 1 && st.wMonth <= 12) ? st.wMonth - 1 : 0],
                           st.wDay, st.wHour, st.wMinute);
            } else {
                swprintf_s(time_buf, _countof(time_buf), L"%ls %2d  %4d",
                           kLsMonths[(st.wMonth >= 1 && st.wMonth <= 12) ? st.wMonth - 1 : 0],
                           st.wDay, st.wYear);
            }
        }

        wchar_t sbuf[32];
        FormatLsSize(item->size, opts->opt_h, sbuf, _countof(sbuf));

        if (opts->opt_i) wprintf(L"%8llu ", (unsigned long long)item->inode);
        if (opts->opt_s) {
            uint64_t blks = opts->opt_k ? ((item->size + 1023) / 1024) : ((item->size + 511) / 512);
            wprintf(L"%4llu ", (unsigned long long)blks);
        }

        wprintf(L"%ls %2u ", perm, item->nlink ? item->nlink : 1);
        if (!opts->opt_g) wprintf(L"%-8ls ", opts->opt_n ? L"0" : L"owner");
        if (!opts->opt_o) wprintf(L"%-8ls ", opts->opt_n ? L"0" : L"group");
        wprintf(L"%8ls %ls ", sbuf, time_buf);

        if (opts->opt_G) {
            if (item->attr & FILE_ATTRIBUTE_DIRECTORY) wprintf(L"\033[1;34m%ls\033[0m%ls\n", item->name, indicator);
            else if (perm[3] == L'x') wprintf(L"\033[1;32m%ls\033[0m%ls\n", item->name, indicator);
            else if (item->attr & FILE_ATTRIBUTE_REPARSE_POINT) wprintf(L"\033[1;36m%ls\033[0m%ls\n", item->name, indicator);
            else wprintf(L"%ls%ls\n", item->name, indicator);
        } else {
            wprintf(L"%ls%ls\n", item->name, indicator);
        }
    } else {
        if (opts->opt_i) wprintf(L"%8llu ", (unsigned long long)item->inode);
        if (opts->opt_s) {
            uint64_t blks = opts->opt_k ? ((item->size + 1023) / 1024) : ((item->size + 511) / 512);
            wprintf(L"%4llu ", (unsigned long long)blks);
        }
        if (opts->opt_G) {
            if (item->attr & FILE_ATTRIBUTE_DIRECTORY) wprintf(L"\033[1;34m%ls\033[0m%ls", item->name, indicator);
            else if (dot && (_wcsicmp(dot, L".exe") == 0 || _wcsicmp(dot, L".cmd") == 0 || _wcsicmp(dot, L".bat") == 0 || _wcsicmp(dot, L".ksh") == 0))
                wprintf(L"\033[1;32m%ls\033[0m%ls", item->name, indicator);
            else if (item->attr & FILE_ATTRIBUTE_REPARSE_POINT) wprintf(L"\033[1;36m%ls\033[0m%ls", item->name, indicator);
            else wprintf(L"%ls%ls", item->name, indicator);
        } else {
            wprintf(L"%ls%ls", item->name, indicator);
        }
    }
}

static void LsFormatDirectory(const LsOpts* opts, const wchar_t* target, bool show_header);

static void LsProcessDirItems(const LsOpts* opts, const wchar_t* target, LsItem* items, size_t count) {
    if (!opts->opt_f) {
        int sf = opts->sort_flags;
        qsort_s(items, count, sizeof(LsItem), CompareLsItems, &sf);
    }

    if (opts->opt_l) {
        uint64_t total_blks = 0;
        for (size_t i = 0; i < count; i++) {
            total_blks += opts->opt_k ? ((items[i].size + 1023) / 1024) : ((items[i].size + 511) / 512);
        }
        wprintf(L"total %llu\n", (unsigned long long)total_blks);
        for (size_t i = 0; i < count; i++) {
            wchar_t ind[2] = L"";
            if (opts->opt_F) {
                if (items[i].attr & FILE_ATTRIBUTE_DIRECTORY) ind[0] = L'/';
                else if (items[i].attr & FILE_ATTRIBUTE_REPARSE_POINT) ind[0] = L'@';
                else {
                    const wchar_t* dot = wcsrchr(items[i].name, L'.');
                    if (dot && (_wcsicmp(dot, L".exe") == 0 || _wcsicmp(dot, L".cmd") == 0 || _wcsicmp(dot, L".bat") == 0 || _wcsicmp(dot, L".ksh") == 0 || _wcsicmp(dot, L".ps1") == 0)) {
                        ind[0] = L'*';
                    }
                }
            } else if (opts->opt_p && (items[i].attr & FILE_ATTRIBUTE_DIRECTORY)) {
                ind[0] = L'/';
            }
            LsPrintSingle(opts, &items[i], ind, target);
        }
    } else if (opts->opt_m) {
        for (size_t i = 0; i < count; i++) {
            wchar_t ind[2] = L"";
            if (opts->opt_F) {
                if (items[i].attr & FILE_ATTRIBUTE_DIRECTORY) ind[0] = L'/';
                else if (items[i].attr & FILE_ATTRIBUTE_REPARSE_POINT) ind[0] = L'@';
                else {
                    const wchar_t* dot = wcsrchr(items[i].name, L'.');
                    if (dot && (_wcsicmp(dot, L".exe") == 0 || _wcsicmp(dot, L".cmd") == 0 || _wcsicmp(dot, L".bat") == 0 || _wcsicmp(dot, L".ksh") == 0)) ind[0] = L'*';
                }
            } else if (opts->opt_p && (items[i].attr & FILE_ATTRIBUTE_DIRECTORY)) ind[0] = L'/';
            LsPrintSingle(opts, &items[i], ind, target);
            if (i + 1 < count) wprintf(L", ");
        }
        if (count > 0) wprintf(L"\n");
    } else if (opts->opt_1 || (!opts->opt_C && !opts->opt_x && !opts->is_tty)) {
        for (size_t i = 0; i < count; i++) {
            wchar_t ind[2] = L"";
            if (opts->opt_F) {
                if (items[i].attr & FILE_ATTRIBUTE_DIRECTORY) ind[0] = L'/';
                else if (items[i].attr & FILE_ATTRIBUTE_REPARSE_POINT) ind[0] = L'@';
                else {
                    const wchar_t* dot = wcsrchr(items[i].name, L'.');
                    if (dot && (_wcsicmp(dot, L".exe") == 0 || _wcsicmp(dot, L".cmd") == 0 || _wcsicmp(dot, L".bat") == 0 || _wcsicmp(dot, L".ksh") == 0)) ind[0] = L'*';
                }
            } else if (opts->opt_p && (items[i].attr & FILE_ATTRIBUTE_DIRECTORY)) ind[0] = L'/';
            LsPrintSingle(opts, &items[i], ind, target);
            wprintf(L"\n");
        }
    } else {
        /* Multi-column layout (default for terminal) */
        size_t max_len = 0;
        for (size_t i = 0; i < count; i++) {
            size_t len = wcslen(items[i].name) + (opts->opt_F || opts->opt_p ? 1 : 0);
            if (opts->opt_i) len += 9;
            if (opts->opt_s) len += 5;
            if (len > max_len) max_len = len;
        }
        size_t col_w = max_len + 2;
        if (col_w > (size_t)opts->term_width) col_w = (size_t)opts->term_width;
        size_t cols = (size_t)opts->term_width / col_w;
        if (cols < 1) cols = 1;
        size_t rows = (count + cols - 1) / cols;

        if (opts->opt_x) {
            for (size_t i = 0; i < count; i++) {
                wchar_t ind[2] = L"";
                if (opts->opt_F) {
                    if (items[i].attr & FILE_ATTRIBUTE_DIRECTORY) ind[0] = L'/';
                    else if (items[i].attr & FILE_ATTRIBUTE_REPARSE_POINT) ind[0] = L'@';
                    else {
                        const wchar_t* dot = wcsrchr(items[i].name, L'.');
                        if (dot && (_wcsicmp(dot, L".exe") == 0 || _wcsicmp(dot, L".cmd") == 0 || _wcsicmp(dot, L".bat") == 0 || _wcsicmp(dot, L".ksh") == 0)) ind[0] = L'*';
                    }
                } else if (opts->opt_p && (items[i].attr & FILE_ATTRIBUTE_DIRECTORY)) ind[0] = L'/';
                LsPrintSingle(opts, &items[i], ind, target);
                if ((i + 1) % cols == 0 || i + 1 == count) wprintf(L"\n");
                else {
                    size_t cur_len = wcslen(items[i].name) + (ind[0] ? 1 : 0);
                    for (size_t p = cur_len; p < col_w; p++) fputwc(L' ', stdout);
                }
            }
        } else {
            for (size_t r = 0; r < rows; r++) {
                for (size_t c = 0; c < cols; c++) {
                    size_t idx = c * rows + r;
                    if (idx < count) {
                        wchar_t ind[2] = L"";
                        if (opts->opt_F) {
                            if (items[idx].attr & FILE_ATTRIBUTE_DIRECTORY) ind[0] = L'/';
                            else if (items[idx].attr & FILE_ATTRIBUTE_REPARSE_POINT) ind[0] = L'@';
                            else {
                                const wchar_t* dot = wcsrchr(items[idx].name, L'.');
                                if (dot && (_wcsicmp(dot, L".exe") == 0 || _wcsicmp(dot, L".cmd") == 0 || _wcsicmp(dot, L".bat") == 0 || _wcsicmp(dot, L".ksh") == 0)) ind[0] = L'*';
                            }
                        } else if (opts->opt_p && (items[idx].attr & FILE_ATTRIBUTE_DIRECTORY)) ind[0] = L'/';
                        LsPrintSingle(opts, &items[idx], ind, target);
                        if (c + 1 < cols && (c + 1) * rows + r < count) {
                            size_t cur_len = wcslen(items[idx].name) + (ind[0] ? 1 : 0);
                            for (size_t p = cur_len; p < col_w; p++) fputwc(L' ', stdout);
                        } else {
                            wprintf(L"\n");
                        }
                    }
                }
            }
        }
    }

    if (opts->opt_R) {
        for (size_t i = 0; i < count; i++) {
            if ((items[i].attr & FILE_ATTRIBUTE_DIRECTORY) && !(items[i].attr & FILE_ATTRIBUTE_REPARSE_POINT) &&
                wcscmp(items[i].name, L".") != 0 && wcscmp(items[i].name, L"..") != 0) {
                wchar_t sub_path[KSH_PATH_MAX];
                swprintf_s(sub_path, _countof(sub_path), L"%ls\\%ls", target, items[i].name);
                wprintf(L"\n%ls:\n", sub_path);
                LsFormatDirectory(opts, sub_path, false);
            }
        }
    }
}

static void LsFormatDirectory(const LsOpts* opts, const wchar_t* target, bool show_header) {
    if (show_header) wprintf(L"%ls:\n", target);

    wchar_t search_mask[KSH_PATH_MAX];
    swprintf_s(search_mask, _countof(search_mask), L"%ls\\*", target);
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search_mask, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"ls: %ls: Permission denied\n", target);
        return;
    }

    size_t cap = 128, count = 0;
    LsItem* items = (LsItem*)malloc(cap * sizeof(LsItem));
    if (!items) { FindClose(hFind); return; }

    do {
        if (!opts->opt_a && !opts->opt_A && (fd.cFileName[0] == L'.' || (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))) continue;
        if (opts->opt_A && (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)) continue;

        if (count >= cap) {
            cap *= 2;
            LsItem* grown = (LsItem*)realloc(items, cap * sizeof(LsItem));
            if (!grown) break;
            items = grown;
        }
        wcsncpy_s(items[count].name, _countof(items[count].name), fd.cFileName, _TRUNCATE);
        swprintf_s(items[count].full_path, _countof(items[count].full_path), L"%ls\\%ls", target, fd.cFileName);
        items[count].attr = fd.dwFileAttributes;
        items[count].size = (((uint64_t)fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        items[count].mtime = fd.ftLastWriteTime;
        items[count].atime = fd.ftLastAccessTime;
        items[count].ctime = fd.ftCreationTime;
        items[count].inode = 0;
        items[count].nlink = 1;
        count++;
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    LsProcessDirItems(opts, target, items, count);
    free(items);
}

bool Builtin_Ls(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: ls [OPTION]... [FILE]...\n"
                L"List information about the FILEs (the current directory by default).\n\n"
                L"  -a, --all         Do not ignore entries starting with .\n"
                L"  -A, --almost-all  Do not list implied . and ..\n"
                L"  -l                Use a long listing format\n"
                L"  -h, --human-readable  With -l/-s, print human readable sizes (e.g., 1K 234M 2G)\n"
                L"  -k                Like --block-size=1K\n"
                L"  -r, --reverse     Reverse order while sorting\n"
                L"  -t                Sort by modification time, newest first\n"
                L"  -S                Sort by file size, largest first\n"
                L"  -c                Use status change time (ctime) for sorting/printing\n"
                L"  -u                Use access time (atime) for sorting/printing\n"
                L"  -U                Use creation time (birth time) for sorting/printing\n"
                L"  -f                Do not sort; enables -a, disables -l, -s\n"
                L"  -1                List one file per line\n"
                L"  -C                List entries by columns (default for terminal)\n"
                L"  -x                List entries across rows instead of down columns\n"
                L"  -m                Fill width with a comma separated list of entries\n"
                L"  -F                Append indicator (one of */=>@|) to entries\n"
                L"  -p                Append / indicator to directories\n"
                L"  -d                List directories themselves, not their contents\n"
                L"  -R, --recursive   List subdirectories recursively\n"
                L"  -g                In -l, do not print owner\n"
                L"  -o                In -l, do not print group\n"
                L"  -n                Like -l, but list numeric user and group IDs\n"
                L"  -i                Print inode number of each file\n"
                L"  -s                Print allocated size of each file in blocks\n"
                L"  -T                Display complete time information in -l\n"
                L"  -G, --color       Colorize output\n"
                L"  --group-directories-first  Group directories before files\n");
        *code = 0;
        return true;
    }

    LsOpts opts;
    memset(&opts, 0, sizeof(opts));
    opts.term_width = 80;
    opts.is_tty = _isatty(_fileno(stdout));

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        if (w > 20) opts.term_width = w;
    }

    int file_start = -1;
    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (wcscmp(arg, L"--") == 0) {
            file_start = i + 1;
            break;
        }
        if (arg[0] == L'-' && arg[1] != L'\0' && file_start == -1) {
            if (wcscmp(arg, L"-V") == 0 || wcscmp(arg, L"--version") == 0) {
                wprintf(L"ls (BSD-compatible) 1.0 (ntksh)\n");
                *code = 0;
                return true;
            }
            if (wcscmp(arg, L"--all") == 0) { opts.opt_a = true; continue; }
            if (wcscmp(arg, L"--almost-all") == 0) { opts.opt_A = true; continue; }
            if (wcscmp(arg, L"--human-readable") == 0) { opts.opt_h = true; continue; }
            if (wcscmp(arg, L"--reverse") == 0) { opts.opt_r = true; continue; }
            if (wcscmp(arg, L"--recursive") == 0) { opts.opt_R = true; continue; }
            if (wcscmp(arg, L"--color") == 0) { opts.opt_G = true; continue; }
            if (wcscmp(arg, L"--group-directories-first") == 0) { opts.group_dirs = true; continue; }

            for (int j = 1; arg[j]; j++) {
                switch (arg[j]) {
                case L'a': opts.opt_a = true; break;
                case L'A': opts.opt_A = true; break;
                case L'l': opts.opt_l = true; break;
                case L'h': opts.opt_h = true; break;
                case L'k': opts.opt_k = true; break;
                case L'r': opts.opt_r = true; break;
                case L't': opts.opt_t = true; break;
                case L'S': opts.opt_S = true; break;
                case L'1': opts.opt_1 = true; break;
                case L'C': opts.opt_C = true; break;
                case L'x': opts.opt_x = true; break;
                case L'm': opts.opt_m = true; break;
                case L'F': opts.opt_F = true; break;
                case L'p': opts.opt_p = true; break;
                case L'd': opts.opt_d = true; break;
                case L'R': opts.opt_R = true; break;
                case L'c': opts.opt_c = true; break;
                case L'u': opts.opt_u = true; break;
                case L'U': opts.opt_U = true; break;
                case L'f': opts.opt_f = true; opts.opt_a = true; break;
                case L'g': opts.opt_g = true; break;
                case L'o': opts.opt_o = true; break;
                case L'n': opts.opt_n = true; opts.opt_l = true; break;
                case L'i': opts.opt_i = true; break;
                case L's': opts.opt_s = true; break;
                case L'T': opts.opt_T = true; break;
                case L'G': opts.opt_G = true; break;
                default: break;
                }
            }
        } else {
            if (file_start == -1) file_start = i;
        }
    }

    if (opts.opt_t) opts.sort_flags |= 1;
    if (opts.opt_S) opts.sort_flags |= 2;
    if (opts.opt_r) opts.sort_flags |= 4;
    if (opts.opt_c) opts.sort_flags |= 8;
    if (opts.opt_u) opts.sort_flags |= 16;
    if (opts.group_dirs) opts.sort_flags |= 32;

    int files_count = (file_start != -1 && file_start < cmd->argc) ? (cmd->argc - file_start) : 0;
    const wchar_t* dummy_dir = L".";
    const wchar_t* const* files = (files_count > 0) ? (const wchar_t* const*)(cmd->argv + file_start) : &dummy_dir;
    if (files_count <= 0) files_count = 1;

    for (int fi = 0; fi < files_count; fi++) {
        const wchar_t* target = files[fi];
        DWORD attr = GetFileAttributesW(target);
        if (attr == INVALID_FILE_ATTRIBUTES) {
            fwprintf(stderr, L"ls: %ls: No such file or directory\n", target);
            continue;
        }

        bool is_dir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!is_dir || opts.opt_d) {
            LsItem item;
            wcsncpy_s(item.name, _countof(item.name), target, _TRUNCATE);
            wcsncpy_s(item.full_path, _countof(item.full_path), target, _TRUNCATE);
            item.attr = attr;
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if (GetFileAttributesExW(target, GetFileExInfoStandard, &fad)) {
                item.size = (((uint64_t)fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
                item.mtime = fad.ftLastWriteTime;
                item.atime = fad.ftLastAccessTime;
                item.ctime = fad.ftCreationTime;
            } else {
                item.size = 0;
                memset(&item.mtime, 0, sizeof(item.mtime));
                memset(&item.atime, 0, sizeof(item.atime));
                memset(&item.ctime, 0, sizeof(item.ctime));
            }
            item.inode = 0;
            item.nlink = 1;
            wchar_t ind[2] = L"";
            if (opts.opt_F) {
                if (item.attr & FILE_ATTRIBUTE_DIRECTORY) ind[0] = L'/';
                else if (item.attr & FILE_ATTRIBUTE_REPARSE_POINT) ind[0] = L'@';
                else {
                    const wchar_t* dot = wcsrchr(item.name, L'.');
                    if (dot && (_wcsicmp(dot, L".exe") == 0 || _wcsicmp(dot, L".cmd") == 0 || _wcsicmp(dot, L".bat") == 0 || _wcsicmp(dot, L".ksh") == 0)) ind[0] = L'*';
                }
            } else if (opts.opt_p && (item.attr & FILE_ATTRIBUTE_DIRECTORY)) ind[0] = L'/';
            LsPrintSingle(&opts, &item, ind, target);
            if (!opts.opt_l) wprintf(L"\n");
        } else {
            LsFormatDirectory(&opts, target, files_count > 1);
        }
    }

    *code = 0;
    return true;
}

static bool CopyRecursive(const wchar_t* src, const wchar_t* dst, bool force, bool preserve) {
    DWORD attr = GetFileAttributesW(src);
    if (attr == INVALID_FILE_ATTRIBUTES) return false;

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
            return true;
        }
        CreateDirectoryW(dst, NULL);
        wchar_t smask[KSH_PATH_MAX];
        _snwprintf_s(smask, _countof(smask), _TRUNCATE, L"%ls\\*", src);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(smask, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                wchar_t sub_src[KSH_PATH_MAX], sub_dst[KSH_PATH_MAX];
                _snwprintf_s(sub_src, _countof(sub_src), _TRUNCATE, L"%ls\\%ls", src, fd.cFileName);
                _snwprintf_s(sub_dst, _countof(sub_dst), _TRUNCATE, L"%ls\\%ls", dst, fd.cFileName);
                CopyRecursive(sub_src, sub_dst, force, preserve);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        return true;
    } else {
        return CopyFileW(src, dst, !force) != 0;
    }
}

bool Builtin_Cp(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: cp [OPTION]... SOURCE... DEST\n"
                L"Copy SOURCE to DEST, or multiple SOURCE(s) to DIRECTORY.\n\n"
                L"  -r, -R, -a   Copy directories recursively\n"
                L"  -f           Force: remove existing destination files\n"
                L"  -p           Preserve specified attributes and timestamps\n"
                L"  -v           Explain what is being done\n"
                L"  -n           Do not overwrite an existing file\n");
        *code = 0;
        return true;
    }

    bool recursive = false, force = false, preserve = false, verbose = false, noclobber = false;
    int file_start = -1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && file_start == -1) {
            for (int j = 1; arg[j]; j++) {
                if (arg[j] == L'r' || arg[j] == L'R' || arg[j] == L'a') recursive = true;
                else if (arg[j] == L'f') force = true;
                else if (arg[j] == L'p') preserve = true;
                else if (arg[j] == L'v') verbose = true;
                else if (arg[j] == L'n') noclobber = true;
            }
        } else {
            if (file_start == -1) file_start = i;
        }
    }

    int files_count = (file_start != -1) ? (cmd->argc - file_start) : 0;
    if (files_count < 2) {
        fwprintf(stderr, L"cp: missing destination file operand after '%ls'\n", (files_count == 1) ? cmd->argv[file_start] : L"");
        *code = 1;
        return true;
    }

    const wchar_t* dest = cmd->argv[cmd->argc - 1];
    DWORD dest_attr = GetFileAttributesW(dest);
    bool dest_is_dir = (dest_attr != INVALID_FILE_ATTRIBUTES && (dest_attr & FILE_ATTRIBUTE_DIRECTORY));

    int ret = 0;
    for (int i = file_start; i < cmd->argc - 1; i++) {
        const wchar_t* src = cmd->argv[i];
        wchar_t target_path[KSH_PATH_MAX];
        if (dest_is_dir) {
            const wchar_t* bname = wcsrchr(src, L'\\');
            const wchar_t* bname2 = wcsrchr(src, L'/');
            if (bname2 && (!bname || bname2 > bname)) bname = bname2;
            bname = bname ? bname + 1 : src;
            _snwprintf_s(target_path, _countof(target_path), _TRUNCATE, L"%ls\\%ls", dest, bname);
        } else {
            wcsncpy_s(target_path, _countof(target_path), dest, _TRUNCATE);
        }

        if (noclobber && GetFileAttributesW(target_path) != INVALID_FILE_ATTRIBUTES) {
            continue;
        }

        bool ok = false;
        if (recursive) {
            ok = CopyRecursive(src, target_path, force, preserve);
        } else {
            ok = CopyFileW(src, target_path, !force) != 0;
        }

        if (ok) {
            if (verbose) wprintf(L"'%ls' -> '%ls'\n", src, target_path);
        } else {
            fwprintf(stderr, L"cp: cannot copy '%ls' to '%ls'\n", src, target_path);
            ret = 1;
        }
    }

    *code = ret;
    return true;
}

bool Builtin_Mv(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: mv [OPTION]... SOURCE... DEST\n"
                L"Rename SOURCE to DEST, or move SOURCE(s) to DIRECTORY.\n\n"
                L"  -f           Do not prompt before overwriting\n"
                L"  -n           Do not overwrite an existing file\n"
                L"  -v           Explain what is being done\n");
        *code = 0;
        return true;
    }

    bool force = true, noclobber = false, verbose = false;
    int file_start = -1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && file_start == -1) {
            for (int j = 1; arg[j]; j++) {
                if (arg[j] == L'f') { force = true; noclobber = false; }
                else if (arg[j] == L'n') { noclobber = true; force = false; }
                else if (arg[j] == L'v') verbose = true;
            }
        } else {
            if (file_start == -1) file_start = i;
        }
    }

    int files_count = (file_start != -1) ? (cmd->argc - file_start) : 0;
    if (files_count < 2) {
        fwprintf(stderr, L"mv: missing destination file operand\n");
        *code = 1;
        return true;
    }

    const wchar_t* dest = cmd->argv[cmd->argc - 1];
    DWORD dest_attr = GetFileAttributesW(dest);
    bool dest_is_dir = (dest_attr != INVALID_FILE_ATTRIBUTES && (dest_attr & FILE_ATTRIBUTE_DIRECTORY));

    int ret = 0;
    for (int i = file_start; i < cmd->argc - 1; i++) {
        const wchar_t* src = cmd->argv[i];
        wchar_t target_path[KSH_PATH_MAX];
        if (dest_is_dir) {
            const wchar_t* bname = wcsrchr(src, L'\\');
            const wchar_t* bname2 = wcsrchr(src, L'/');
            if (bname2 && (!bname || bname2 > bname)) bname = bname2;
            bname = bname ? bname + 1 : src;
            _snwprintf_s(target_path, _countof(target_path), _TRUNCATE, L"%ls\\%ls", dest, bname);
        } else {
            wcsncpy_s(target_path, _countof(target_path), dest, _TRUNCATE);
        }

        if (noclobber && GetFileAttributesW(target_path) != INVALID_FILE_ATTRIBUTES) {
            continue;
        }

        DWORD flags = MOVEFILE_COPY_ALLOWED;
        if (force) flags |= MOVEFILE_REPLACE_EXISTING;

        if (MoveFileExW(src, target_path, flags)) {
            if (verbose) wprintf(L"renamed '%ls' -> '%ls'\n", src, target_path);
        } else {
            fwprintf(stderr, L"mv: cannot move '%ls' to '%ls'\n", src, target_path);
            ret = 1;
        }
    }

    *code = ret;
    return true;
}

static bool DeleteRecursive(const wchar_t* path, bool force, bool verbose) {
    DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return force;

    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            if (RemoveDirectoryW(path)) {
                if (verbose) wprintf(L"removed directory '%ls'\n", path);
                return true;
            }
        } else {
            if (DeleteFileW(path)) {
                if (verbose) wprintf(L"removed '%ls'\n", path);
                return true;
            }
        }
        return false;
    }

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        wchar_t smask[KSH_PATH_MAX];
        _snwprintf_s(smask, _countof(smask), _TRUNCATE, L"%ls\\*", path);
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(smask, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
                wchar_t sub[KSH_PATH_MAX];
                _snwprintf_s(sub, _countof(sub), _TRUNCATE, L"%ls\\%ls", path, fd.cFileName);
                DeleteRecursive(sub, force, verbose);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (RemoveDirectoryW(path)) {
            if (verbose) wprintf(L"removed directory '%ls'\n", path);
            return true;
        }
        return false;
    } else {
        if (attr & FILE_ATTRIBUTE_READONLY) {
            SetFileAttributesW(path, attr & ~FILE_ATTRIBUTE_READONLY);
        }
        if (DeleteFileW(path)) {
            if (verbose) wprintf(L"removed '%ls'\n", path);
            return true;
        }
        return false;
    }
}

bool Builtin_Rm(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: rm [OPTION]... [FILE]...\n"
                L"Remove (unlink) the FILE(s).\n\n"
                L"  -f           Ignore nonexistent files and arguments, never prompt\n"
                L"  -r, -R       Remove directories and their contents recursively\n"
                L"  -d           Remove empty directories\n"
                L"  -v           Explain what is being done\n");
        *code = 0;
        return true;
    }

    bool force = false, recursive = false, verbose = false, empty_dirs = false;
    int file_start = -1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && file_start == -1) {
            for (int j = 1; arg[j]; j++) {
                if (arg[j] == L'f') force = true;
                else if (arg[j] == L'r' || arg[j] == L'R') recursive = true;
                else if (arg[j] == L'v') verbose = true;
                else if (arg[j] == L'd') empty_dirs = true;
            }
        } else {
            if (file_start == -1) file_start = i;
        }
    }

    if (file_start == -1) {
        if (!force) fwprintf(stderr, L"rm: missing operand\n");
        *code = force ? 0 : 1;
        return true;
    }

    int ret = 0;
    for (int i = file_start; i < cmd->argc; i++) {
        const wchar_t* path = cmd->argv[i];
        DWORD attr = GetFileAttributesW(path);
        if (attr == INVALID_FILE_ATTRIBUTES) {
            if (!force) {
                fwprintf(stderr, L"rm: cannot remove '%ls': No such file or directory\n", path);
                ret = 1;
            }
            continue;
        }

        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            if (recursive) {
                if (!DeleteRecursive(path, force, verbose)) ret = 1;
            } else if (empty_dirs) {
                if (RemoveDirectoryW(path)) {
                    if (verbose) wprintf(L"removed directory '%ls'\n", path);
                } else {
                    fwprintf(stderr, L"rm: cannot remove '%ls': Directory not empty\n", path);
                    ret = 1;
                }
            } else {
                fwprintf(stderr, L"rm: cannot remove '%ls': Is a directory\n", path);
                ret = 1;
            }
        } else {
            if (attr & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(path, attr & ~FILE_ATTRIBUTE_READONLY);
            if (DeleteFileW(path)) {
                if (verbose) wprintf(L"removed '%ls'\n", path);
            } else {
                if (!force) {
                    fwprintf(stderr, L"rm: cannot remove '%ls'\n", path);
                    ret = 1;
                }
            }
        }
    }

    *code = ret;
    return true;
}

bool Builtin_Ln(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: ln [OPTION]... TARGET [LINK_NAME]\n"
                L"Create links to TARGET with the name LINK_NAME.\n\n"
                L"  -s           Make symbolic links instead of hard links\n"
                L"  -f           Remove existing destination files\n"
                L"  -v           Print name of each created link\n");
        *code = 0;
        return true;
    }

    bool symlink = false, force = false, verbose = false;
    int file_start = -1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && file_start == -1) {
            for (int j = 1; arg[j]; j++) {
                if (arg[j] == L's') symlink = true;
                else if (arg[j] == L'f') force = true;
                else if (arg[j] == L'v') verbose = true;
            }
        } else {
            if (file_start == -1) file_start = i;
        }
    }

    int files_count = (file_start != -1) ? (cmd->argc - file_start) : 0;
    if (files_count < 1) {
        fwprintf(stderr, L"ln: missing file operand\n");
        *code = 1;
        return true;
    }

    const wchar_t* target = cmd->argv[file_start];
    wchar_t link_name[KSH_PATH_MAX];
    if (files_count >= 2) {
        wcsncpy_s(link_name, _countof(link_name), cmd->argv[file_start + 1], _TRUNCATE);
    } else {
        const wchar_t* bname = wcsrchr(target, L'\\');
        const wchar_t* bname2 = wcsrchr(target, L'/');
        if (bname2 && (!bname || bname2 > bname)) bname = bname2;
        wcsncpy_s(link_name, _countof(link_name), bname ? bname + 1 : target, _TRUNCATE);
    }

    if (force) {
        DeleteFileW(link_name);
        RemoveDirectoryW(link_name);
    }

    bool ok = false;
    if (symlink) {
        DWORD attr = GetFileAttributesW(target);
        DWORD flags = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
        flags |= 0x2; // SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
        ok = CreateSymbolicLinkW(link_name, target, flags) != 0;
        if (!ok) {
            // try without unprivileged flag
            ok = CreateSymbolicLinkW(link_name, target, flags & ~0x2) != 0;
        }
    } else {
        ok = CreateHardLinkW(link_name, target, NULL) != 0;
    }

    if (ok) {
        if (verbose) wprintf(L"'%ls' -> '%ls'\n", link_name, target);
        *code = 0;
    } else {
        fwprintf(stderr, L"ln: failed to create %ls link '%ls' -> '%ls'\n", symlink ? L"symbolic" : L"hard", link_name, target);
        *code = 1;
    }
    return true;
}

bool Builtin_Xargs(IShell* shell, CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: xargs [OPTION]... [COMMAND [INITIAL-ARGS]...]\n"
                L"Build and execute command lines from standard input.\n\n"
                L"  -n MAX_ARGS   Use at most MAX_ARGS arguments per command line\n"
                L"  -I REPLACE    Replace occurrences of REPLACE with input\n"
                L"  -0            Input items are terminated by a null character\n"
                L"  -d DELIM      Input items are terminated by the specified character\n"
                L"  -r            Do not run command if standard input is completely empty\n"
                L"  -t            Print command line on standard error before executing\n");
        *code = 0;
        return true;
    }

    int max_args = -1;
    const wchar_t* replace_str = NULL;
    bool null_delim = false;
    wchar_t delim = 0;
    bool no_empty = false;
    bool verbose = false;
    int cmd_start = -1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && cmd_start == -1) {
            if (wcscmp(arg, L"-0") == 0) null_delim = true;
            else if (wcscmp(arg, L"-r") == 0 || wcscmp(arg, L"--no-run-if-empty") == 0) no_empty = true;
            else if (wcscmp(arg, L"-t") == 0) verbose = true;
            else if (wcscmp(arg, L"-n") == 0 && i + 1 < cmd->argc) max_args = (int)wcstol(cmd->argv[++i], NULL, 10);
            else if (wcscmp(arg, L"-I") == 0 && i + 1 < cmd->argc) {
                replace_str = cmd->argv[++i];
                if (!*replace_str) { fwprintf(stderr, L"xargs: -I requires a non-empty replace string\n"); *code = 1; return true; }
            }
            else if (wcscmp(arg, L"-d") == 0 && i + 1 < cmd->argc) delim = cmd->argv[++i][0];
        } else {
            if (cmd_start == -1) cmd_start = i;
        }
    }

    int base_argc = (cmd_start != -1) ? (cmd->argc - cmd_start) : 1;
    wchar_t default_cmd[] = L"echo";

    size_t cap = 128, count = 0;
    wchar_t** items = (wchar_t**)malloc(cap * sizeof(wchar_t*));
    if (!items) {
        *code = 1;
        return true;
    }

    wchar_t token[4096];
    int tlen = 0;
    wint_t wch;
    while ((wch = fgetwc(stdin)) != WEOF) {
        bool is_sep = false;
        if (null_delim) is_sep = (wch == L'\0');
        else if (delim) is_sep = (wch == (wint_t)delim || wch == L'\n' || wch == L'\r');
        else is_sep = iswspace((wchar_t)wch);

        if (is_sep) {
            if (tlen > 0) {
                token[tlen] = L'\0';
                if (count >= cap) {
                    size_t new_cap = cap * 2;
                    wchar_t** new_items = (wchar_t**)realloc(items, new_cap * sizeof(wchar_t*));
                    if (!new_items) break;
                    items = new_items;
                    cap = new_cap;
                }
                items[count++] = _wcsdup(token);
                tlen = 0;
            }
        } else {
            if (tlen < 4095) token[tlen++] = (wchar_t)wch;
        }
    }
    if (tlen > 0) {
        token[tlen] = L'\0';
        if (count >= cap) {
            size_t new_cap = cap * 2;
            wchar_t** new_items = (wchar_t**)realloc(items, new_cap * sizeof(wchar_t*));
            if (new_items) {
                items = new_items;
                cap = new_cap;
                items[count++] = _wcsdup(token);
            }
        } else {
            items[count++] = _wcsdup(token);
        }
    }

    if (count == 0 && no_empty) {
        free(items);
        *code = 0;
        return true;
    }

    int ret = 0;
    if (replace_str) {
        size_t rlen = wcslen(replace_str);
        for (size_t i = 0; i < count; i++) {
            CommandAST xcmd;
            memset(&xcmd, 0, sizeof(xcmd));
            bool allocated[MAX_ARGS] = { false };
            for (int a = 0; a < base_argc && xcmd.argc < MAX_ARGS - 1; a++) {
                const wchar_t* orig = (cmd_start != -1) ? cmd->argv[cmd_start + a] : default_cmd;
                const wchar_t* found = wcsstr(orig, replace_str);
                if (found) {
                    wchar_t repBuf[MAX_LINE_LEN];
                    size_t rb = 0;
                    bool fits = true;
                    const wchar_t* cur = orig;
                    size_t ilen = wcslen(items[i]);
                    repBuf[0] = L'\0';
                    while (fits && (found = wcsstr(cur, replace_str)) != NULL) {
                        size_t seg = (size_t)(found - cur);
                        if (rb + seg + ilen >= _countof(repBuf)) { fits = false; break; }
                        wmemcpy(repBuf + rb, cur, seg); rb += seg;
                        wmemcpy(repBuf + rb, items[i], ilen); rb += ilen;
                        cur = found + rlen;
                    }
                    if (fits && rb + wcslen(cur) < _countof(repBuf)) {
                        wcscpy_s(repBuf + rb, _countof(repBuf) - rb, cur);
                    } else {
                        fits = false;
                    }
                    if (!fits) {
                        fwprintf(stderr, L"xargs: replacement too long\n");
                        ret = 1;
                        repBuf[0] = L'\0';
                    }
                    xcmd.argv[xcmd.argc] = SafeStrDup(repBuf);
                    allocated[xcmd.argc] = true;
                    xcmd.argc++;
                } else {
                    xcmd.argv[xcmd.argc++] = (wchar_t*)orig;
                }
            }
            if (verbose) {
                wchar_t* cl = BuildWin32CommandLine(xcmd.argv, xcmd.argc);
                if (cl) { fwprintf(stderr, L"%ls\n", cl); free(cl); }
            }
            int st = shell->executor->Execute(shell->executor, &xcmd);
            if (st != 0) ret = st;
            for (int a = 0; a < xcmd.argc; a++) {
                if (allocated[a]) free(xcmd.argv[a]);
            }
        }
    } else {
        int batch_size = (max_args > 0) ? max_args : (int)count;
        if (batch_size < 1) batch_size = 1;

        size_t idx = 0;
        while (idx < count || (count == 0 && !no_empty)) {
            CommandAST xcmd;
            memset(&xcmd, 0, sizeof(xcmd));
            for (int a = 0; a < base_argc && xcmd.argc < MAX_ARGS - 1; a++) {
                xcmd.argv[xcmd.argc++] = (cmd_start != -1) ? cmd->argv[cmd_start + a] : default_cmd;
            }
            int added = 0;
            while (idx < count && added < batch_size && xcmd.argc < MAX_ARGS - 1) {
                xcmd.argv[xcmd.argc++] = items[idx++];
                added++;
            }
            if (added == 0 && idx < count) {
                fwprintf(stderr, L"xargs: argument list too long\n");
                ret = 1;
                break;
            }
            if (verbose) {
                wchar_t* cl = BuildWin32CommandLine(xcmd.argv, xcmd.argc);
                if (cl) { fwprintf(stderr, L"%ls\n", cl); free(cl); }
            }
            int st = shell->executor->Execute(shell->executor, &xcmd);
            if (st != 0) ret = st;
            if (count == 0) break;
        }
    }

    for (size_t i = 0; i < count; i++) free(items[i]);
    free(items);

    *code = ret;
    return true;
}





/* ========================================================================= */
/*                   Category A: System, Disk & Binary Utilities             */
/* ========================================================================= */

static unsigned __int64 DuRecurse(const wchar_t* root, int depth, int max_depth,
                                   bool human, bool summary_only, bool all_files, bool in_mb) {
    unsigned __int64 total_bytes = 0;
    wchar_t pattern[MAX_PATH];
    swprintf_s(pattern, MAX_PATH, L"%ls\\*", root);
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            wchar_t sub_path[MAX_PATH];
            swprintf_s(sub_path, MAX_PATH, L"%ls\\%ls", root, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                unsigned __int64 dir_sz = DuRecurse(sub_path, depth + 1, max_depth, human, summary_only, all_files, in_mb);
                total_bytes += dir_sz;
                if (!summary_only && (max_depth < 0 || depth + 1 <= max_depth)) {
                    if (human) {
                        wchar_t hbuf[32]; FormatBytesHuman(dir_sz, hbuf, _countof(hbuf));
                        wprintf(L"%s\t%ls\n", hbuf, sub_path);
                    } else if (in_mb) {
                        wprintf(L"%llu\t%ls\n", (dir_sz + 1048575) / 1048576, sub_path);
                    } else {
                        wprintf(L"%llu\t%ls\n", (dir_sz + 1023) / 1024, sub_path);
                    }
                }
            } else {
                ULARGE_INTEGER sz; sz.LowPart = fd.nFileSizeLow; sz.HighPart = fd.nFileSizeHigh;
                total_bytes += sz.QuadPart;
                if (all_files && !summary_only && (max_depth < 0 || depth <= max_depth)) {
                    if (human) {
                        wchar_t hbuf[32]; FormatBytesHuman(sz.QuadPart, hbuf, _countof(hbuf));
                        wprintf(L"%s\t%ls\n", hbuf, sub_path);
                    } else if (in_mb) {
                        wprintf(L"%llu\t%ls\n", (sz.QuadPart + 1048575) / 1048576, sub_path);
                    } else {
                        wprintf(L"%llu\t%ls\n", (sz.QuadPart + 1023) / 1024, sub_path);
                    }
                }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
    return total_bytes;
}

bool Builtin_Du(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: du [OPTION]... [FILE]...\n"
                L"Summarize device usage of the set of FILEs, recursively for directories.\n\n"
                L"  -a, --all             write counts for all files, not just directories\n"
                L"  -c, --total           produce a grand total\n"
                L"  -d, --max-depth=N     print total for a directory only if depth <= N\n"
                L"  -h, --human-readable  print sizes in human readable format (e.g., 1K 234M 2G)\n"
                L"  -k                    like --block-size=1K (default)\n"
                L"  -m                    like --block-size=1M\n"
                L"  -s, --summarize       display only a total for each argument\n");
        *code = 0;
        return true;
    }

    bool human = false, total = false, all_files = false, summary = false, in_mb = false;
    int max_depth = -1;
    const wchar_t* targets[64];
    int target_count = 0;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* a = cmd->argv[i];
        if (wcscmp(a, L"-h") == 0 || wcscmp(a, L"--human-readable") == 0) human = true;
        else if (wcscmp(a, L"-c") == 0 || wcscmp(a, L"--total") == 0) total = true;
        else if (wcscmp(a, L"-a") == 0 || wcscmp(a, L"--all") == 0) all_files = true;
        else if (wcscmp(a, L"-s") == 0 || wcscmp(a, L"--summarize") == 0) summary = true;
        else if (wcscmp(a, L"-k") == 0) { in_mb = false; }
        else if (wcscmp(a, L"-m") == 0) { in_mb = true; }
        else if ((wcscmp(a, L"-d") == 0 || wcscmp(a, L"--max-depth") == 0) && i + 1 < cmd->argc) {
            max_depth = (int)wcstol(cmd->argv[++i], NULL, 10);
        } else if (a[0] != L'-' && target_count < 64) {
            targets[target_count++] = a;
        }
    }
    const wchar_t* def_target = L".";
    if (target_count == 0) { targets[0] = def_target; target_count = 1; }

    unsigned __int64 grand_total = 0;
    for (int i = 0; i < target_count; i++) {
        const wchar_t* path = targets[i];
        DWORD attr = GetFileAttributesW(path);
        if (attr == INVALID_FILE_ATTRIBUTES) {
            fwprintf(stderr, L"du: cannot access '%ls': No such file or directory\n", path);
            continue;
        }
        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            unsigned __int64 sz = DuRecurse(path, 0, max_depth, human, summary, all_files, in_mb);
            grand_total += sz;
            if (human) {
                wchar_t hbuf[32]; FormatBytesHuman(sz, hbuf, _countof(hbuf));
                wprintf(L"%s\t%ls\n", hbuf, path);
            } else if (in_mb) {
                wprintf(L"%llu\t%ls\n", (sz + 1048575) / 1048576, path);
            } else {
                wprintf(L"%llu\t%ls\n", (sz + 1023) / 1024, path);
            }
        } else {
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if (GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) {
                ULARGE_INTEGER sz; sz.LowPart = fad.nFileSizeLow; sz.HighPart = fad.nFileSizeHigh;
                grand_total += sz.QuadPart;
                if (human) {
                    wchar_t hbuf[32]; FormatBytesHuman(sz.QuadPart, hbuf, _countof(hbuf));
                    wprintf(L"%s\t%ls\n", hbuf, path);
                } else if (in_mb) {
                    wprintf(L"%llu\t%ls\n", (sz.QuadPart + 1048575) / 1048576, path);
                } else {
                    wprintf(L"%llu\t%ls\n", (sz.QuadPart + 1023) / 1024, path);
                }
            }
        }
    }
    if (total) {
        if (human) {
            wchar_t hbuf[32]; FormatBytesHuman(grand_total, hbuf, _countof(hbuf));
            wprintf(L"%s\ttotal\n", hbuf);
        } else if (in_mb) {
            wprintf(L"%llu\ttotal\n", (grand_total + 1048575) / 1048576);
        } else {
            wprintf(L"%llu\ttotal\n", (grand_total + 1023) / 1024);
        }
    }
    *code = 0;
    return true;
}

bool Builtin_Df(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: df [OPTION]... [FILE]...\n"
                L"Show information about the file system on which each FILE resides.\n\n"
                L"  -h, --human-readable  print sizes in powers of 1024 (e.g., 1023M)\n"
                L"  -k                    like --block-size=1K\n"
                L"  -m                    like --block-size=1M\n"
                L"  -T, --print-type      print file system type\n");
        *code = 0;
        return true;
    }

    bool human = false, show_type = false, in_mb = false;
    for (int i = 1; i < cmd->argc; i++) {
        if (wcscmp(cmd->argv[i], L"-h") == 0 || wcscmp(cmd->argv[i], L"--human-readable") == 0) human = true;
        else if (wcscmp(cmd->argv[i], L"-T") == 0 || wcscmp(cmd->argv[i], L"--print-type") == 0) show_type = true;
        else if (wcscmp(cmd->argv[i], L"-k") == 0) in_mb = false;
        else if (wcscmp(cmd->argv[i], L"-m") == 0) in_mb = true;
    }

    if (show_type)
        wprintf(L"%-16ls %-8ls %10ls %10ls %10ls %5ls %ls\n", L"Filesystem", L"Type", L"1K-blocks", L"Used", L"Available", L"Use%", L"Mounted on");
    else
        wprintf(L"%-16ls %10ls %10ls %10ls %5ls %ls\n", L"Filesystem", L"1K-blocks", L"Used", L"Available", L"Use%", L"Mounted on");

    DWORD drives = GetLogicalDrives();
    for (int d = 0; d < 26; d++) {
        if (!(drives & (1 << d))) continue;
        wchar_t root[4] = { (wchar_t)(L'A' + d), L':', L'\\', L'\0' };
        UINT drive_type = GetDriveTypeW(root);
        if (drive_type == DRIVE_NO_ROOT_DIR) continue;

        ULARGE_INTEGER free_bytes, total_bytes, total_free;
        if (!GetDiskFreeSpaceExW(root, &free_bytes, &total_bytes, &total_free)) continue;

        unsigned __int64 total = total_bytes.QuadPart;
        unsigned __int64 avail = free_bytes.QuadPart;
        unsigned __int64 used = (total >= avail) ? (total - avail) : 0;
        int pct = (total > 0) ? (int)((used * 100) / total) : 0;

        wchar_t fs_name[MAX_PATH] = L"NTFS";
        if (show_type) {
            GetVolumeInformationW(root, NULL, 0, NULL, NULL, NULL, fs_name, _countof(fs_name));
        }

        wchar_t s_total[32], s_used[32], s_avail[32];
        if (human) {
            FormatBytesHuman(total, s_total, _countof(s_total));
            FormatBytesHuman(used, s_used, _countof(s_used));
            FormatBytesHuman(avail, s_avail, _countof(s_avail));
        } else if (in_mb) {
            swprintf_s(s_total, _countof(s_total), L"%llu", total / (1024 * 1024));
            swprintf_s(s_used, _countof(s_used), L"%llu", used / (1024 * 1024));
            swprintf_s(s_avail, _countof(s_avail), L"%llu", avail / (1024 * 1024));
        } else {
            swprintf_s(s_total, _countof(s_total), L"%llu", total / 1024);
            swprintf_s(s_used, _countof(s_used), L"%llu", used / 1024);
            swprintf_s(s_avail, _countof(s_avail), L"%llu", avail / 1024);
        }

        wchar_t drive_name[8]; swprintf_s(drive_name, _countof(drive_name), L"%c:", L'A' + d);
        if (show_type) {
            wprintf(L"%-16ls %-8ls %10ls %10ls %10ls %4d%% %ls\n", drive_name, fs_name, s_total, s_used, s_avail, pct, root);
        } else {
            wprintf(L"%-16ls %10ls %10ls %10ls %4d%% %ls\n", drive_name, s_total, s_used, s_avail, pct, root);
        }
    }
    *code = 0;
    return true;
}

static void FormatFileTimeStr(const FILETIME* ft, wchar_t* out, size_t out_sz) {
    SYSTEMTIME st, lt;
    FileTimeToSystemTime(ft, &st);
    SystemTimeToTzSpecificLocalTime(NULL, &st, &lt);
    swprintf_s(out, out_sz, L"%04d-%02d-%02d %02d:%02d:%02d",
        lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);
}

bool Builtin_Stat(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: stat [OPTION]... FILE...\n"
                L"Display file or file system status.\n\n"
                L"  -L, --dereference     follow links\n"
                L"  -t, --terse           print the information in terse form\n"
                L"  -c, --format=FORMAT   use the specified FORMAT instead of the default\n");
        *code = 0;
        return true;
    }

    bool terse = false;
    const wchar_t* fmt = NULL;
    int first_file = 1;

    for (int i = 1; i < cmd->argc; i++) {
        if (wcscmp(cmd->argv[i], L"-t") == 0 || wcscmp(cmd->argv[i], L"--terse") == 0) terse = true;
        else if ((wcscmp(cmd->argv[i], L"-c") == 0 || wcscmp(cmd->argv[i], L"--format") == 0) && i + 1 < cmd->argc) {
            fmt = cmd->argv[++i];
        } else if (cmd->argv[i][0] != L'-') {
            first_file = i;
            break;
        }
    }

    if (first_file >= cmd->argc) {
        fwprintf(stderr, L"stat: missing operand\n");
        *code = 1;
        return true;
    }
    (void)fmt;

    int ret = 0;
    for (int fi = first_file; fi < cmd->argc; fi++) {
        const wchar_t* path = cmd->argv[fi];
        HANDLE hFile = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            fwprintf(stderr, L"stat: cannot statx '%ls': No such file or directory\n", path);
            ret = 1;
            continue;
        }

        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(hFile, &info)) {
            CloseHandle(hFile);
            ret = 1;
            continue;
        }
        CloseHandle(hFile);

        ULARGE_INTEGER sz; sz.LowPart = info.nFileSizeLow; sz.HighPart = info.nFileSizeHigh;
        bool is_dir = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const wchar_t* type_str = is_dir ? L"directory" : L"regular file";
        wchar_t atime[64], mtime[64], ctime[64];
        FormatFileTimeStr(&info.ftLastAccessTime, atime, _countof(atime));
        FormatFileTimeStr(&info.ftLastWriteTime, mtime, _countof(mtime));
        FormatFileTimeStr(&info.ftCreationTime, ctime, _countof(ctime));

        if (terse) {
            wprintf(L"%ls %llu %u %x %u %u %llx %u %u %ls %ls %ls\n",
                    path, sz.QuadPart, (unsigned int)((sz.QuadPart + 511) / 512), info.dwFileAttributes,
                    0, 0, ((unsigned __int64)info.nFileIndexHigh << 32) | info.nFileIndexLow,
                    info.nNumberOfLinks, 0, atime, mtime, ctime);
        } else {
            wprintf(L"  File: %ls\n", path);
            wprintf(L"  Size: %-14llu Blocks: %-10llu IO Block: 4096   %ls\n",
                    sz.QuadPart, (sz.QuadPart + 511) / 512, type_str);
            wprintf(L"Device: %xh/%ud Inode: %llu  Links: %u\n",
                    info.dwVolumeSerialNumber, info.dwVolumeSerialNumber,
                    ((unsigned __int64)info.nFileIndexHigh << 32) | info.nFileIndexLow, info.nNumberOfLinks);
            wprintf(L"Access: (%04o/%ls%ls%ls)\n",
                    is_dir ? 0755 : 0644,
                    (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) ? L"r-x" : L"rwx",
                    L"r-x", L"r-x");
            wprintf(L"Access: %ls\n", atime);
            wprintf(L"Modify: %ls\n", mtime);
            wprintf(L"Birth:  %ls\n", ctime);
        }
    }
    *code = ret;
    return true;
}

bool Builtin_Strings(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: strings [option(s)] [file(s)]\n"
                L" Display printable strings in [file(s)] (stdin by default)\n\n"
                L"  -a                    Scan the entire file (default)\n"
                L"  -n <number>           Locate & print any sequence at least <number>\n"
                L"                        characters long (default 4).\n"
                L"  -t {o,d,x}            Print the location of the string in base 8, 10 or 16\n");
        *code = 0;
        return true;
    }

    int min_len = 4;
    wchar_t radix = 0;
    int file_idx = 1;

    for (int i = 1; i < cmd->argc; i++) {
        if (wcscmp(cmd->argv[i], L"-n") == 0 && i + 1 < cmd->argc) {
            min_len = (int)wcstol(cmd->argv[++i], NULL, 10);
            if (min_len <= 0) min_len = 4;
        } else if (wcscmp(cmd->argv[i], L"-t") == 0 && i + 1 < cmd->argc) {
            radix = cmd->argv[++i][0];
        } else if (wcscmp(cmd->argv[i], L"-a") == 0) { }
        else if (cmd->argv[i][0] != L'-') {
            file_idx = i;
            break;
        }
    }

    int file_count = cmd->argc - file_idx;
    const wchar_t* dummy_stdin = L"-";
    const wchar_t* const* files = (file_count > 0) ? (const wchar_t* const*)(cmd->argv + file_idx) : &dummy_stdin;
    if (file_count <= 0) file_count = 1;

    int ret = 0;
    for (int fi = 0; fi < file_count; fi++) {
        const wchar_t* fname = files[fi];
        bool is_stdin = (wcscmp(fname, L"-") == 0);
        FILE* fp = is_stdin ? stdin : _wfsopen(fname, L"rb", _SH_DENYNO);
        if (!fp) {
            fwprintf(stderr, L"strings: '%ls': No such file\n", fname);
            ret = 1;
            continue;
        }

        char buf[1024];
        int count = 0;
        unsigned __int64 offset = 0;
        unsigned __int64 start_offset = 0;
        int ch;
        while ((ch = (is_stdin ? getwchar() : fgetc(fp))) != (is_stdin ? WEOF : EOF)) {
            unsigned char c = (unsigned char)ch;
            if ((c >= 32 && c <= 126) || c == '\t') {
                if (count == 0) start_offset = offset;
                if (count < (int)sizeof(buf) - 1) buf[count++] = (char)c;
            } else {
                if (count >= min_len) {
                    buf[count] = '\0';
                    if (radix == L'o') wprintf(L"%7llo %hs\n", start_offset, buf);
                    else if (radix == L'x') wprintf(L"%7llx %hs\n", start_offset, buf);
                    else if (radix == L'd') wprintf(L"%7llu %hs\n", start_offset, buf);
                    else wprintf(L"%hs\n", buf);
                }
                count = 0;
            }
            offset++;
        }
        if (count >= min_len) {
            buf[count] = '\0';
            if (radix == L'o') wprintf(L"%7llo %hs\n", start_offset, buf);
            else if (radix == L'x') wprintf(L"%7llx %hs\n", start_offset, buf);
            else if (radix == L'd') wprintf(L"%7llu %hs\n", start_offset, buf);
            else wprintf(L"%hs\n", buf);
        }

        if (!is_stdin && fp) fclose(fp);
    }
    *code = ret;
    return true;
}

bool Builtin_Hexdump(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: hexdump [options] file...\n"
                L"       hd [options] file...\n\n"
                L"Options:\n"
                L"  -C, --canonical       canonical hex+ASCII display\n"
                L"  -n, --length <length> interpret only length bytes of input\n"
                L"  -s, --skip <offset>   skip offset bytes from the beginning\n"
                L"  -v, --no-squeezing    output all input data\n");
        *code = 0;
        return true;
    }

    bool canonical = (_wcsicmp(cmd->argv[0], L"hd") == 0);
    unsigned __int64 max_bytes = 0xFFFFFFFFFFFFFFFFULL;
    unsigned __int64 skip = 0;
    int file_idx = 1;

    for (int i = 1; i < cmd->argc; i++) {
        if (wcscmp(cmd->argv[i], L"-C") == 0 || wcscmp(cmd->argv[i], L"--canonical") == 0) canonical = true;
        else if ((wcscmp(cmd->argv[i], L"-n") == 0 || wcscmp(cmd->argv[i], L"--length") == 0) && i + 1 < cmd->argc) {
            max_bytes = wcstoull(cmd->argv[++i], NULL, 10);
        } else if ((wcscmp(cmd->argv[i], L"-s") == 0 || wcscmp(cmd->argv[i], L"--skip") == 0) && i + 1 < cmd->argc) {
            skip = wcstoull(cmd->argv[++i], NULL, 10);
        } else if (wcscmp(cmd->argv[i], L"-v") == 0) { }
        else if (cmd->argv[i][0] != L'-') {
            file_idx = i;
            break;
        }
    }

    int file_count = cmd->argc - file_idx;
    const wchar_t* dummy_stdin = L"-";
    const wchar_t* const* files = (file_count > 0) ? (const wchar_t* const*)(cmd->argv + file_idx) : &dummy_stdin;
    if (file_count <= 0) file_count = 1;

    unsigned __int64 total_pos = 0;
    for (int fi = 0; fi < file_count; fi++) {
        const wchar_t* fname = files[fi];
        bool is_stdin = (wcscmp(fname, L"-") == 0);
        FILE* fp = is_stdin ? stdin : _wfsopen(fname, L"rb", _SH_DENYNO);
        if (!fp) {
            fwprintf(stderr, L"hexdump: %ls: No such file or directory\n", fname);
            continue;
        }

        if (!is_stdin && skip > 0) {
            _fseeki64(fp, (long long)skip, SEEK_SET);
            total_pos += skip;
        }

        unsigned char buf[16];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
            if (max_bytes != 0xFFFFFFFFFFFFFFFFULL && n > max_bytes) n = (size_t)max_bytes;
            if (canonical) {
                wprintf(L"%08llx  ", total_pos);
                for (size_t b = 0; b < 16; b++) {
                    if (b < n) wprintf(L"%02x ", buf[b]);
                    else wprintf(L"   ");
                    if (b == 7) putwchar(L' ');
                }
                wprintf(L" |");
                for (size_t b = 0; b < n; b++) {
                    wchar_t ch = (buf[b] >= 32 && buf[b] <= 126) ? (wchar_t)buf[b] : L'.';
                    putwchar(ch);
                }
                wprintf(L"|\n");
            } else {
                wprintf(L"%07llx ", total_pos);
                for (size_t b = 0; b < n; b += 2) {
                    if (b + 1 < n) wprintf(L"%02x%02x ", buf[b + 1], buf[b]);
                    else wprintf(L"%02x ", buf[b]);
                }
                putwchar(L'\n');
            }
            total_pos += n;
            if (max_bytes != 0xFFFFFFFFFFFFFFFFULL) {
                max_bytes -= n;
                if (max_bytes == 0) break;
            }
        }
        if (canonical) wprintf(L"%08llx\n", total_pos);
        if (!is_stdin && fp) fclose(fp);
    }
    *code = 0;
    return true;
}

bool Builtin_File(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: file [OPTION...] [FILE...]\n"
                L"Determine type of FILEs.\n\n"
                L"  -b, --brief      do not prepend filenames to output lines\n"
                L"  -i, --mime       output MIME type strings\n");
        *code = 0;
        return true;
    }

    bool brief = false, mime = false;
    int first_file = 1;
    for (int i = 1; i < cmd->argc; i++) {
        if (wcscmp(cmd->argv[i], L"-b") == 0 || wcscmp(cmd->argv[i], L"--brief") == 0) brief = true;
        else if (wcscmp(cmd->argv[i], L"-i") == 0 || wcscmp(cmd->argv[i], L"--mime") == 0) mime = true;
        else if (cmd->argv[i][0] != L'-') { first_file = i; break; }
    }

    if (first_file >= cmd->argc) {
        fwprintf(stderr, L"file: missing files\n");
        *code = 1;
        return true;
    }

    int ret = 0;
    for (int fi = first_file; fi < cmd->argc; fi++) {
        const wchar_t* path = cmd->argv[fi];
        DWORD attr = GetFileAttributesW(path);
        if (attr == INVALID_FILE_ATTRIBUTES) {
            fwprintf(stderr, L"%ls: cannot open (No such file or directory)\n", path);
            ret = 1;
            continue;
        }

        if (attr & FILE_ATTRIBUTE_DIRECTORY) {
            if (!brief) wprintf(L"%ls: ", path);
            wprintf(mime ? L"inode/directory; charset=binary\n" : L"directory\n");
            continue;
        }

        FILE* fp = _wfsopen(path, L"rb", _SH_DENYNO);
        if (!fp) {
            fwprintf(stderr, L"%ls: cannot open\n", path);
            ret = 1;
            continue;
        }

        unsigned char buf[512];
        size_t n = fread(buf, 1, sizeof(buf), fp);
        fclose(fp);

        const wchar_t* desc = L"data";
        const wchar_t* mime_desc = L"application/octet-stream; charset=binary";

        if (n == 0) {
            desc = L"empty";
            mime_desc = L"application/x-empty; charset=binary";
        } else if (n >= 2 && buf[0] == 'M' && buf[1] == 'Z') {
            desc = L"PE32/PE32+ executable (GUI/console) Intel / AMD Windows";
            mime_desc = L"application/x-dosexec; charset=binary";
        } else if (n >= 4 && buf[0] == 0x7F && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') {
            desc = L"ELF executable or shared object";
            mime_desc = L"application/x-executable; charset=binary";
        } else if (n >= 4 && buf[0] == 'P' && buf[1] == 'K' && buf[2] == 0x03 && buf[3] == 0x04) {
            desc = L"Zip archive data";
            mime_desc = L"application/zip; charset=binary";
        } else if (n >= 3 && buf[0] == 0x1F && buf[1] == 0x8B) {
            desc = L"gzip compressed data";
            mime_desc = L"application/gzip; charset=binary";
        } else if (n >= 4 && memcmp(buf, "%PDF", 4) == 0) {
            desc = L"PDF document";
            mime_desc = L"application/pdf; charset=binary";
        } else if (n >= 8 && memcmp(buf, "\x89PNG\r\n\x1a\n", 8) == 0) {
            desc = L"PNG image data";
            mime_desc = L"image/png; charset=binary";
        } else if (n >= 3 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
            desc = L"JPEG image data";
            mime_desc = L"image/jpeg; charset=binary";
        } else if (n >= 3 && (memcmp(buf, "GIF87a", 6) == 0 || memcmp(buf, "GIF89a", 6) == 0)) {
            desc = L"GIF image data";
            mime_desc = L"image/gif; charset=binary";
        } else if (n >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) {
            desc = L"UTF-8 Unicode (with BOM) text";
            mime_desc = L"text/plain; charset=utf-8";
        } else if (n >= 2 && ((buf[0] == 0xFF && buf[1] == 0xFE) || (buf[0] == 0xFE && buf[1] == 0xFF))) {
            desc = L"UTF-16 Unicode text";
            mime_desc = L"text/plain; charset=utf-16";
        } else {
            /* Check if plain ASCII/UTF-8 text */
            bool is_text = true;
            bool has_crlf = false;
            for (size_t b = 0; b < n; b++) {
                if (buf[b] == 0) { is_text = false; break; }
                if (buf[b] == '\r') has_crlf = true;
            }
            if (is_text) {
                desc = has_crlf ? L"ASCII text, with CRLF line terminators" : L"ASCII text";
                mime_desc = L"text/plain; charset=us-ascii";
            }
        }

        if (!brief) wprintf(L"%ls: ", path);
        wprintf(L"%ls\n", mime ? mime_desc : desc);
    }
    *code = ret;
    return true;
}



/* ========================================================================= */
/*                   Category E: Formatting, Truncate, DD & Slicing          */
/* ========================================================================= */

bool Builtin_Column(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: column [options] [file...]\n"
                L"Columnate lists.\n\n"
                L"  -t, --table          create a table\n"
                L"  -s, --separator <sep> possible table delimiters\n"
                L"  -c, --output-width <width> output width in characters\n");
        *code = 0;
        return true;
    }

    const wchar_t* delim = NULL;
    int file_idx = -1;
    for (int i = 1; i < cmd->argc; i++) {
        if ((wcscmp(cmd->argv[i], L"-s") == 0 || wcscmp(cmd->argv[i], L"--separator") == 0) && i + 1 < cmd->argc) {
            delim = cmd->argv[++i];
        } else if (wcscmp(cmd->argv[i], L"-t") == 0 || wcscmp(cmd->argv[i], L"--table") == 0) { }
        else if (wcscmp(cmd->argv[i], L"-c") == 0 && i + 1 < cmd->argc) { i++; }
        else if (cmd->argv[i][0] != L'-') { file_idx = i; break; }
    }

    int file_count = (file_idx > 0) ? (cmd->argc - file_idx) : 0;
    const wchar_t* dummy_stdin = L"-";
    const wchar_t* const* files = (file_count > 0) ? (const wchar_t* const*)(cmd->argv + file_idx) : &dummy_stdin;
    if (file_count <= 0) file_count = 1;

    typedef struct {
        wchar_t** cols;
        int num_cols;
    } TableRow;

    TableRow* rows = NULL;
    int row_count = 0, row_cap = 64;
    rows = (TableRow*)malloc(row_cap * sizeof(TableRow));
    int col_widths[64] = {0};
    int max_cols = 0;

    for (int fi = 0; fi < file_count; fi++) {
        const wchar_t* fname = files[fi];
        bool is_stdin = (wcscmp(fname, L"-") == 0);
        FILE* fp = is_stdin ? stdin : _wfsopen(fname, L"r, ccs=UTF-8", _SH_DENYNO);
        if (!fp) continue;

        wchar_t line[4096];
        while (fgetws(line, _countof(line), fp)) {
            size_t l = wcslen(line); while (l > 0 && (line[l-1] == L'\r' || line[l-1] == L'\n')) line[--l] = L'\0';
            if (row_count >= row_cap) {
                row_cap *= 2;
                TableRow* new_rows = (TableRow*)realloc(rows, row_cap * sizeof(TableRow));
                if (!new_rows) break;
                rows = new_rows;
            }
            TableRow* r = &rows[row_count++];
            r->cols = (wchar_t**)calloc(64, sizeof(wchar_t*));
            r->num_cols = 0;
            if (!r->cols) continue;

            wchar_t* context = NULL;
            const wchar_t* sep = delim ? delim : L" \t";
            wchar_t* tok = wcstok_s(line, sep, &context);
            while (tok && r->num_cols < 64) {
                wchar_t* dtok = _wcsdup(tok);
                if (!dtok) break;
                r->cols[r->num_cols] = dtok;
                int tok_len = (int)wcslen(tok);
                if (r->num_cols < 64 && tok_len > col_widths[r->num_cols]) {
                    col_widths[r->num_cols] = tok_len;
                }
                r->num_cols++;
                tok = wcstok_s(NULL, sep, &context);
            }
            if (r->num_cols > max_cols) max_cols = r->num_cols;
        }
        if (!is_stdin && fp) fclose(fp);
    }

    for (int i = 0; i < row_count; i++) {
        for (int c = 0; c < rows[i].num_cols; c++) {
            if (c > 0) wprintf(L"  ");
            if (c < rows[i].num_cols - 1) {
                wprintf(L"%-*ls", col_widths[c], rows[i].cols[c]);
            } else {
                wprintf(L"%ls", rows[i].cols[c]);
            }
            free(rows[i].cols[c]);
        }
        putwchar(L'\n');
        free(rows[i].cols);
    }
    free(rows);
    *code = 0;
    return true;
}

bool Builtin_Truncate(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: truncate [OPTION]... FILE...\n"
                L"Shrink or extend the size of each FILE to the specified size.\n\n"
                L"  -c, --no-create        do not create any files\n"
                L"  -r, --reference=RFILE  base size on RFILE\n"
                L"  -s, --size=SIZE        set or adjust the file size by SIZE bytes\n");
        *code = 0;
        return true;
    }

    bool no_create = false;
    long long target_size = -1;
    const wchar_t* ref_file = NULL;
    int first_file = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* a = cmd->argv[i];
        if (wcscmp(a, L"-c") == 0 || wcscmp(a, L"--no-create") == 0) no_create = true;
        else if ((wcscmp(a, L"-r") == 0 || wcscmp(a, L"--reference") == 0) && i + 1 < cmd->argc) ref_file = cmd->argv[++i];
        else if ((wcscmp(a, L"-s") == 0 || wcscmp(a, L"--size") == 0) && i + 1 < cmd->argc) {
            const wchar_t* sstr = cmd->argv[++i];
            wchar_t* endp = NULL;
            target_size = _wcstoi64(sstr, &endp, 10);
            if (endp && *endp) {
                if (*endp == L'k' || *endp == L'K') target_size *= 1024;
                else if (*endp == L'm' || *endp == L'M') target_size *= 1024 * 1024;
                else if (*endp == L'g' || *endp == L'G') target_size *= 1024ULL * 1024 * 1024;
            }
        } else if (a[0] != L'-') { first_file = i; break; }
    }

    if (ref_file) {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(ref_file, GetFileExInfoStandard, &fad)) {
            ULARGE_INTEGER u; u.LowPart = fad.nFileSizeLow; u.HighPart = fad.nFileSizeHigh;
            target_size = (long long)u.QuadPart;
        }
    }

    if (target_size < 0 || first_file >= cmd->argc) {
        fwprintf(stderr, L"truncate: you must specify a valid size or reference file\n");
        *code = 1;
        return true;
    }

    int ret = 0;
    for (int fi = first_file; fi < cmd->argc; fi++) {
        const wchar_t* path = cmd->argv[fi];
        DWORD disp = no_create ? OPEN_EXISTING : OPEN_ALWAYS;
        HANDLE hFile = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, disp,
                                   FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            if (!no_create) {
                fwprintf(stderr, L"truncate: cannot open '%ls' for writing\n", path);
                ret = 1;
            }
            continue;
        }

        LARGE_INTEGER li;
        li.QuadPart = target_size;
        if (!SetFilePointerEx(hFile, li, NULL, FILE_BEGIN) || !SetEndOfFile(hFile)) {
            fwprintf(stderr, L"truncate: failed to set size of '%ls'\n", path);
            ret = 1;
        }
        CloseHandle(hFile);
    }
    *code = ret;
    return true;
}

bool Builtin_Dd(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: dd [OPERAND]...\n"
                L"Copy a file, converting and formatting according to the operands.\n\n"
                L"  bs=BYTES       read and write up to BYTES bytes at a time (default: 512)\n"
                L"  count=N        copy only N input blocks\n"
                L"  if=FILE        read from FILE instead of stdin\n"
                L"  of=FILE        write to FILE instead of stdout\n"
                L"  seek=N         skip N obs-sized blocks at start of output\n"
                L"  skip=N         skip N ibs-sized blocks at start of input\n"
                L"  conv=CONVS     comma-separated list of conversions: ucase, lcase, notrunc\n");
        *code = 0;
        return true;
    }

    const wchar_t* in_file = NULL;
    const wchar_t* out_file = NULL;
    unsigned __int64 bs = 512;
    unsigned __int64 count = 0xFFFFFFFFFFFFFFFFULL;
    unsigned __int64 skip = 0;
    unsigned __int64 seek = 0;
    bool conv_ucase = false, conv_lcase = false, notrunc = false;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* a = cmd->argv[i];
        if (wcsncmp(a, L"if=", 3) == 0) in_file = a + 3;
        else if (wcsncmp(a, L"of=", 3) == 0) out_file = a + 3;
        else if (wcsncmp(a, L"bs=", 3) == 0) {
            wchar_t* endp = NULL;
            bs = wcstoull(a + 3, &endp, 10);
            if (endp && *endp) {
                if (*endp == L'k' || *endp == L'K') bs *= 1024;
                else if (*endp == L'm' || *endp == L'M') bs *= 1024 * 1024;
                else if (*endp == L'g' || *endp == L'G') bs *= 1024ULL * 1024 * 1024;
            }
        } else if (wcsncmp(a, L"count=", 6) == 0) count = wcstoull(a + 6, NULL, 10);
        else if (wcsncmp(a, L"skip=", 5) == 0) skip = wcstoull(a + 5, NULL, 10);
        else if (wcsncmp(a, L"seek=", 5) == 0) seek = wcstoull(a + 5, NULL, 10);
        else if (wcsncmp(a, L"conv=", 5) == 0) {
            if (wcsstr(a + 5, L"ucase")) conv_ucase = true;
            if (wcsstr(a + 5, L"lcase")) conv_lcase = true;
            if (wcsstr(a + 5, L"notrunc")) notrunc = true;
        }
    }

    FILE* fin = (!in_file || wcscmp(in_file, L"-") == 0) ? stdin : _wfsopen(in_file, L"rb", _SH_DENYNO);
    if (!fin) {
        fwprintf(stderr, L"dd: failed to open '%ls': No such file or directory\n", in_file);
        *code = 1;
        return true;
    }

    FILE* fout = stdout;
    if (out_file && wcscmp(out_file, L"-") != 0) {
        fout = _wfsopen(out_file, notrunc ? L"r+b" : L"wb", _SH_DENYNO);
        if (!fout && notrunc) fout = _wfsopen(out_file, L"wb", _SH_DENYNO);
    }
    if (!fout) {
        if (fin != stdin) fclose(fin);
        fwprintf(stderr, L"dd: failed to open '%ls' for writing\n", out_file);
        *code = 1;
        return true;
    }

    if (skip > 0) {
        if (fin != stdin) _fseeki64(fin, (long long)(skip * bs), SEEK_SET);
        else {
            for (unsigned __int64 s = 0; s < skip * bs; s++) {
                if (getwchar() == WEOF) break;
            }
        }
    }
    if (seek > 0 && fout != stdout) {
        _fseeki64(fout, (long long)(seek * bs), SEEK_SET);
    }

    char* buf = (bs > 0 && bs <= 1024ULL * 1024 * 1024) ? (char*)malloc((size_t)bs) : NULL;
    if (!buf) {
        if (fin != stdin) fclose(fin);
        if (fout != stdout) fclose(fout);
        fwprintf(stderr, L"dd: memory allocation failed for block size %llu\n", bs);
        *code = 1;
        return true;
    }

    unsigned __int64 records_in = 0, records_out = 0, bytes_copied = 0;

    for (unsigned __int64 b = 0; b < count; b++) {
        size_t n = 0;
        if (fin == stdin) {
            for (size_t i = 0; i < (size_t)bs; i++) {
                wint_t c = getwchar();
                if (c == WEOF) break;
                buf[n++] = (char)c;
            }
        } else {
            n = fread(buf, 1, (size_t)bs, fin);
        }
        if (n == 0) break;
        records_in++;

        if (conv_ucase) {
            for (size_t i = 0; i < n; i++) if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] -= 32;
        } else if (conv_lcase) {
            for (size_t i = 0; i < n; i++) if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] += 32;
        }

        if (fout == stdout) {
            for (size_t i = 0; i < n; i++) putwchar((wchar_t)(unsigned char)buf[i]);
            records_out++;
            bytes_copied += n;
        } else {
            size_t w = fwrite(buf, 1, n, fout);
            if (w > 0) records_out++;
            bytes_copied += w;
        }
        if (n < (size_t)bs) break;
    }

    free(buf);
    if (fin != stdin) fclose(fin);
    if (fout != stdout) fclose(fout);

    fwprintf(stderr, L"%llu+0 records in\n%llu+0 records out\n%llu bytes copied\n",
             records_in, records_out, bytes_copied);
    *code = 0;
    return true;
}

bool Builtin_Colrm(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: colrm [start [stop]]\n"
                L"Filter out the specified columns.\n");
        *code = 0;
        return true;
    }

    int start_col = 1;
    int stop_col = 0x7FFFFFFF;
    if (cmd->argc > 1) start_col = (int)wcstol(cmd->argv[1], NULL, 10);
    if (cmd->argc > 2) stop_col = (int)wcstol(cmd->argv[2], NULL, 10);

    wchar_t line[4096];
    while (fgetws(line, _countof(line), stdin)) {
        int col = 1;
        for (wchar_t* p = line; *p; p++) {
            if (*p == L'\n' || *p == L'\r') { putwchar(*p); continue; }
            if (col < start_col || col > stop_col) putwchar(*p);
            col++;
        }
    }
    *code = 0;
    return true;
}

