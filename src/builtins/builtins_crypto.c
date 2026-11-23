#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool Builtin_Base64(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: base64 [OPTION]... [FILE]\n"
                L"Base64 encode or decode FILE, or standard input, to standard output.\n\n"
                L"  -d, --decode  Decode data\n"
                L"  -w COLS       Wrap encoded lines after COLS character (default 76, 0 disables)\n"
                L"  -i            When decoding, ignore non-alphabet characters\n");
        *code = 0;
        return true;
    }

    bool decode = false;
    int wrap = 76;
    const wchar_t* filename = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (wcscmp(arg, L"-d") == 0 || wcscmp(arg, L"-D") == 0 || wcscmp(arg, L"--decode") == 0) decode = true;
        else if (wcscmp(arg, L"-w") == 0 && i + 1 < cmd->argc) wrap = (int)wcstol(cmd->argv[++i], NULL, 10);
        else if (arg[0] != L'-' && !filename) filename = arg;
    }

    FILE* fp = stdin;
    bool is_stdin = (!filename || wcscmp(filename, L"-") == 0);
    if (!is_stdin) {
        fp = _wfsopen(filename, L"rb", _SH_DENYNO);
        if (!fp) {
            fwprintf(stderr, L"base64: %ls: No such file or directory\n", filename);
            *code = 1;
            return true;
        }
    }

    if (decode) {
        int b64_rev[256];
        memset(b64_rev, -1, sizeof(b64_rev));
        for (int i = 0; i < 64; i++) b64_rev[(unsigned char)b64_table[i]] = i;
        b64_rev['='] = 0;

        int ch;
        int buf[4];
        int count = 0;
        while ((ch = (is_stdin ? getwchar() : fgetc(fp))) != (is_stdin ? WEOF : EOF)) {
            if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t') continue;
            if (ch < 0 || ch >= 256 || b64_rev[(unsigned char)ch] == -1) continue;
            buf[count++] = ch;
            if (count == 4) {
                int b0 = b64_rev[(unsigned char)buf[0]];
                int b1 = b64_rev[(unsigned char)buf[1]];
                int b2 = b64_rev[(unsigned char)buf[2]];
                int b3 = b64_rev[(unsigned char)buf[3]];

                putwchar((wchar_t)((b0 << 2) | (b1 >> 4)));
                if (buf[2] != '=') putwchar((wchar_t)(((b1 & 0xF) << 4) | (b2 >> 2)));
                if (buf[3] != '=') putwchar((wchar_t)(((b2 & 0x3) << 6) | b3));
                count = 0;
            }
        }
    } else {
        unsigned char in[3];
        size_t n;
        int col = 0;
        if (is_stdin) {
            wint_t wch;
            while ((wch = getwchar()) != WEOF) {
                in[0] = (unsigned char)wch;
                n = 1;
                wch = getwchar();
                if (wch != WEOF) {
                    in[1] = (unsigned char)wch;
                    n = 2;
                    wch = getwchar();
                    if (wch != WEOF) {
                        in[2] = (unsigned char)wch;
                        n = 3;
                    }
                }
                putwchar((wchar_t)b64_table[in[0] >> 2]);
                putwchar((wchar_t)b64_table[((in[0] & 0x3) << 4) | (n > 1 ? (in[1] >> 4) : 0)]);
                putwchar(n > 1 ? (wchar_t)b64_table[((in[1] & 0xF) << 2) | (n > 2 ? (in[2] >> 6) : 0)] : L'=');
                putwchar(n > 2 ? (wchar_t)b64_table[in[2] & 0x3F] : L'=');
                col += 4;
                if (wrap > 0 && col >= wrap) {
                    putwchar(L'\n');
                    col = 0;
                }
            }
        } else {
            while ((n = fread(in, 1, 3, fp)) > 0) {
                putwchar((wchar_t)b64_table[in[0] >> 2]);
                putwchar((wchar_t)b64_table[((in[0] & 0x3) << 4) | (n > 1 ? (in[1] >> 4) : 0)]);
                putwchar(n > 1 ? (wchar_t)b64_table[((in[1] & 0xF) << 2) | (n > 2 ? (in[2] >> 6) : 0)] : L'=');
                putwchar(n > 2 ? (wchar_t)b64_table[in[2] & 0x3F] : L'=');
                col += 4;
                if (wrap > 0 && col >= wrap) {
                    putwchar(L'\n');
                    col = 0;
                }
            }
        }
        if (col > 0) putwchar(L'\n');
    }

    if (!is_stdin && fp) fclose(fp);
    *code = 0;
    return true;
}

bool Builtin_Checksum(ALG_ID alg, const wchar_t* alg_name, CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: %ls [OPTION]... [FILE]...\n"
                L"Print or check %ls checksums.\n\n"
                L"  -q, --quiet   Don't print OK for each successfully verified file\n"
                L"  -r            Reverse the format of the digest\n", alg_name, alg_name);
        *code = 0;
        return true;
    }

    int file_start = cmd->argc;
    bool quiet = false;
    for (int i = 1; i < cmd->argc; i++) {
        if (wcscmp(cmd->argv[i], L"-q") == 0 || wcscmp(cmd->argv[i], L"--quiet") == 0) quiet = true;
        else if (wcscmp(cmd->argv[i], L"-b") == 0 || wcscmp(cmd->argv[i], L"-t") == 0) continue;
        else { file_start = i; break; }
    }

    int files_count = cmd->argc - file_start;
    const wchar_t* dummy_stdin = L"-";
    const wchar_t* const* files = (files_count > 0) ? (const wchar_t* const*)(cmd->argv + file_start) : &dummy_stdin;
    if (files_count <= 0) files_count = 1;
    (void)quiet;

    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        fwprintf(stderr, L"%ls: cryptographic context initialization failed\n", alg_name);
        *code = 1;
        return true;
    }

    int ret = 0;
    for (int fi = 0; fi < files_count; fi++) {
        const wchar_t* fname = files[fi];
        bool is_stdin = (wcscmp(fname, L"-") == 0);
        FILE* fp = is_stdin ? stdin : _wfsopen(fname, L"rb", _SH_DENYNO);
        if (!fp) {
            fwprintf(stderr, L"%ls: %ls: No such file or directory\n", alg_name, fname);
            ret = 1;
            continue;
        }

        HCRYPTHASH hHash = 0;
        if (!CryptCreateHash(hProv, alg, 0, 0, &hHash)) {
            if (!is_stdin) fclose(fp);
            ret = 1;
            continue;
        }

        BYTE buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
            CryptHashData(hHash, buf, (DWORD)n, 0);
        }

        BYTE hash_bytes[64];
        DWORD hash_len = sizeof(hash_bytes);
        if (CryptGetHashParam(hHash, HP_HASHVAL, hash_bytes, &hash_len, 0)) {
            for (DWORD b = 0; b < hash_len; b++) {
                wprintf(L"%02x", hash_bytes[b]);
            }
            if (!is_stdin) wprintf(L"  %ls\n", fname);
            else wprintf(L"  -\n");
        } else {
            ret = 1;
        }

        CryptDestroyHash(hHash);
        if (!is_stdin && fp) fclose(fp);
    }

    CryptReleaseContext(hProv, 0);
    *code = ret;
    return true;
}

bool Builtin_Dos2Unix(bool to_unix, CommandAST* cmd, int* code) {
    const wchar_t* name = to_unix ? L"dos2unix" : L"unix2dos";
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: %ls [options] [file ...]\n"
                L"Convert text file line breaks between CRLF and LF format.\n\n"
                L"  -q, --quiet   Quiet mode; suppress all warnings\n", name);
        *code = 0;
        return true;
    }

    int file_start = 1;
    for (int i = 1; i < cmd->argc; i++) {
        if (wcscmp(cmd->argv[i], L"-q") == 0 || wcscmp(cmd->argv[i], L"--quiet") == 0) { }
        else { file_start = i; break; }
    }

    int files_count = cmd->argc - file_start;
    if (files_count <= 0) {
        /* Stream filter mode */
        wint_t ch;
        wint_t prev = WEOF;
        while ((ch = getwchar()) != WEOF) {
            if (to_unix) {
                if (ch == L'\r') continue;
                putwchar(ch);
            } else {
                if (ch == L'\n' && prev != L'\r') putwchar(L'\r');
                putwchar(ch);
            }
            prev = ch;
        }
        *code = 0;
        return true;
    }

    int ret = 0;
    for (int fi = file_start; fi < cmd->argc; fi++) {
        const wchar_t* path = cmd->argv[fi];
        FILE* in = _wfsopen(path, L"rb", _SH_DENYNO);
        if (!in) {
            fwprintf(stderr, L"%ls: %ls: No such file or directory\n", name, path);
            ret = 1;
            continue;
        }
        wchar_t tmp_path[MAX_PATH];
        swprintf_s(tmp_path, MAX_PATH, L"%ls.tmp", path);
        FILE* out = _wfsopen(tmp_path, L"wb", _SH_DENYNO);
        if (!out) {
            fclose(in);
            ret = 1;
            continue;
        }

        int ch;
        int prev = EOF;
        while ((ch = fgetc(in)) != EOF) {
            if (to_unix) {
                if (ch == '\r') continue;
                fputc(ch, out);
            } else {
                if (ch == '\n' && prev != '\r') fputc('\r', out);
                fputc(ch, out);
            }
            prev = ch;
        }
        fclose(in);
        fclose(out);

        DeleteFileW(path);
        MoveFileW(tmp_path, path);
    }

    *code = ret;
    return true;
}

bool Builtin_Ps(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: ps [-efal] [-u user] [-p pid]\n"
                L"Report a snapshot of the current processes.\n\n"
                L"  -e, -A        Select all processes\n"
                L"  -f            Do full format listing\n"
                L"  -l            Long format\n"
                L"  -p PID        Select by process ID\n");
        *code = 0;
        return true;
    }

    bool full = false;
    DWORD target_pid = 0;
    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (wcscmp(arg, L"-f") == 0 || wcscmp(arg, L"-ef") == 0) full = true;
        else if (wcscmp(arg, L"-p") == 0 && i + 1 < cmd->argc) target_pid = (DWORD)wcstoul(cmd->argv[++i], NULL, 10);
    }

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        *code = 1;
        return true;
    }

    if (full) {
        wprintf(L"  UID        PID       PPID  C STIME TTY          TIME CMD\n");
    } else {
        wprintf(L"  PID TTY          TIME CMD\n");
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (target_pid != 0 && pe.th32ProcessID != target_pid) continue;
            if (full) {
                wprintf(L" 1000 %10lu %10lu  0 00:00 con      00:00:00 %ls\n",
                        pe.th32ProcessID, pe.th32ParentProcessID, pe.szExeFile);
            } else {
                wprintf(L"%5lu con      00:00:00 %ls\n", pe.th32ProcessID, pe.szExeFile);
            }
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);
    *code = 0;
    return true;
}

bool Builtin_Pgrep(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: pgrep [options] <pattern>\n"
                L"Look up processes based on name and other attributes.\n\n"
                L"  -l            List the process name as well as the process ID\n"
                L"  -c            Count of matching processes\n"
                L"  -i            Ignore case distinctions\n");
        *code = 0;
        return true;
    }

    bool list_names = false;
    bool count_only = false;
    bool ignore_case = true;
    const wchar_t* pattern = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0') {
            for (int j = 1; arg[j]; j++) {
                if (arg[j] == L'l') list_names = true;
                else if (arg[j] == L'c') count_only = true;
                else if (arg[j] == L'i') ignore_case = true;
            }
        } else if (!pattern) {
            pattern = arg;
        }
    }

    if (!pattern) {
        fwprintf(stderr, L"pgrep: no matching criteria specified\n");
        *code = 2;
        return true;
    }

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        *code = 2;
        return true;
    }

    int match_count = 0;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            bool matched = ignore_case ? (StrContainsW(pe.szExeFile, pattern, true)) : (wcsstr(pe.szExeFile, pattern) != NULL);
            if (matched) {
                match_count++;
                if (!count_only) {
                    if (list_names) wprintf(L"%lu %ls\n", pe.th32ProcessID, pe.szExeFile);
                    else wprintf(L"%lu\n", pe.th32ProcessID);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);

    if (count_only) wprintf(L"%d\n", match_count);
    *code = (match_count > 0) ? 0 : 1;
    return true;
}

bool Builtin_Pkill(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: pkill [options] <pattern>\n"
                L"Signal or kill processes based on name.\n\n"
                L"  -i            Ignore case distinctions\n");
        *code = 0;
        return true;
    }

    bool ignore_case = true;
    const wchar_t* pattern = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0') {
            for (int j = 1; arg[j]; j++) {
                if (arg[j] == L'i') ignore_case = true;
            }
        } else if (!pattern) {
            pattern = arg;
        }
    }

    if (!pattern) {
        fwprintf(stderr, L"pkill: no matching criteria specified\n");
        *code = 2;
        return true;
    }

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        *code = 2;
        return true;
    }

    int kill_count = 0;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            bool matched = ignore_case ? (StrContainsW(pe.szExeFile, pattern, true)) : (wcsstr(pe.szExeFile, pattern) != NULL);
            if (matched) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    if (TerminateProcess(hProc, 1)) kill_count++;
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);

    *code = (kill_count > 0) ? 0 : 1;
    return true;
}



/* ========================================================================= */
/*                   Category C: Extended Utilities & Cryptography           */
/* ========================================================================= */

static const unsigned int cksum_crctab[256] = {
    0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,
    0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61, 0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
    0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
    0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd,
    0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039, 0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5,
    0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
    0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
    0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1, 0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d,
    0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
    0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca,
    0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde, 0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02,
    0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
    0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692,
    0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6, 0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a,
    0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
    0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34, 0xdc3abded, 0xd8fba05a,
    0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637, 0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
    0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
    0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b,
    0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff, 0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623,
    0xf12f560e, 0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
    0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
    0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7, 0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b,
    0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
    0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c,
    0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8, 0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24,
    0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
    0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654,
    0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0, 0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c,
    0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
    0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c,
    0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668, 0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

bool Builtin_Cksum(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: cksum [FILE]...\n"
                L"Print CRC checksum and byte counts of each FILE.\n");
        *code = 0;
        return true;
    }

    int file_count = cmd->argc - 1;
    const wchar_t* dummy_stdin = L"-";
    const wchar_t* const* files = (file_count > 0) ? (const wchar_t* const*)(cmd->argv + 1) : &dummy_stdin;
    if (file_count <= 0) file_count = 1;

    for (int fi = 0; fi < file_count; fi++) {
        const wchar_t* fname = files[fi];
        bool is_stdin = (wcscmp(fname, L"-") == 0);
        FILE* fp = is_stdin ? stdin : _wfsopen(fname, L"rb", _SH_DENYNO);
        if (!fp) {
            fwprintf(stderr, L"cksum: %ls: No such file or directory\n", fname);
            continue;
        }

        unsigned int crc = 0;
        unsigned __int64 total_len = 0;
        unsigned char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
            total_len += n;
            for (size_t i = 0; i < n; i++) {
                crc = (crc << 8) ^ cksum_crctab[((crc >> 24) ^ buf[i]) & 0xFF];
            }
        }
        if (!is_stdin && fp) fclose(fp);

        /* Include length in CRC */
        for (unsigned __int64 l = total_len; l > 0; l >>= 8) {
            crc = (crc << 8) ^ cksum_crctab[((crc >> 24) ^ (l & 0xFF)) & 0xFF];
        }
        crc = ~crc;

        if (is_stdin) wprintf(L"%u %llu\n", crc, total_len);
        else wprintf(L"%u %llu %ls\n", crc, total_len, fname);
    }
    *code = 0;
    return true;
}

bool Builtin_Nproc(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: nproc [OPTION]...\n"
                L"Print the number of processing units available to the current process.\n\n"
                L"  --all              print the number of installed processors\n"
                L"  --ignore=N         if possible, subtract N from that number\n");
        *code = 0;
        return true;
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int count = (int)si.dwNumberOfProcessors;
    for (int i = 1; i < cmd->argc; i++) {
        if (wcsncmp(cmd->argv[i], L"--ignore=", 9) == 0) {
            int ig = (int)wcstol(cmd->argv[i] + 9, NULL, 10);
            count -= ig;
            if (count < 1) count = 1;
        }
    }
    wprintf(L"%d\n", count);
    *code = 0;
    return true;
}

static bool IsLeapYear(int y) {
    return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}

static int DaysInMonth(int m, int y) {
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && IsLeapYear(y)) return 29;
    if (m >= 1 && m <= 12) return days[m - 1];
    return 30;
}

static int DayOfWeek(int d, int m, int y) {
    /* Zeller / Sakamoto algorithm (0 = Sunday) */
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static void PrintMonthCal(int m, int y) {
    static const wchar_t* month_names[] = {
        L"January", L"February", L"March", L"April", L"May", L"June",
        L"July", L"August", L"September", L"October", L"November", L"December"
    };
    wprintf(L"     %ls %d\n", month_names[m - 1], y);
    wprintf(L"Su Mo Tu We Th Fr Sa\n");
    int first_day = DayOfWeek(1, m, y);
    for (int i = 0; i < first_day; i++) wprintf(L"   ");
    int total_days = DaysInMonth(m, y);
    int col = first_day;
    for (int day = 1; day <= total_days; day++) {
        wprintf(L"%2d ", day);
        col++;
        if (col == 7) { putwchar(L'\n'); col = 0; }
    }
    if (col != 0) putwchar(L'\n');
}

bool Builtin_Cal(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: cal [options] [[[day] month] year]\n"
                L"Display a calendar.\n\n"
                L"  -3            Display previous, current and next month\n"
                L"  -y, --year    Display a calendar for the entire current year\n");
        *code = 0;
        return true;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    int month = st.wMonth;
    int year = st.wYear;
    bool show3 = false, show_year = false;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* a = cmd->argv[i];
        if (wcscmp(a, L"-3") == 0) show3 = true;
        else if (wcscmp(a, L"-y") == 0 || wcscmp(a, L"--year") == 0) show_year = true;
        else if (a[0] != L'-') {
            int val = (int)wcstol(a, NULL, 10);
            if (val > 12) year = val;
            else month = val;
            if (i + 1 < cmd->argc && cmd->argv[i + 1][0] != L'-') {
                year = (int)wcstol(cmd->argv[++i], NULL, 10);
            }
        }
    }

    if (show_year) {
        wprintf(L"                               %d\n\n", year);
        for (int m = 1; m <= 12; m++) {
            PrintMonthCal(m, year);
            putwchar(L'\n');
        }
    } else if (show3) {
        int prev_m = (month == 1) ? 12 : month - 1;
        int prev_y = (month == 1) ? year - 1 : year;
        int next_m = (month == 12) ? 1 : month + 1;
        int next_y = (month == 12) ? year + 1 : year;
        PrintMonthCal(prev_m, prev_y); putwchar(L'\n');
        PrintMonthCal(month, year); putwchar(L'\n');
        PrintMonthCal(next_m, next_y);
    } else {
        PrintMonthCal(month, year);
    }
    *code = 0;
    return true;
}

static void FactorPrintSingle(unsigned __int64 n) {
    wprintf(L"%llu:", n);
    if (n <= 1) { putwchar(L'\n'); return; }
    while (n % 2 == 0) {
        wprintf(L" 2");
        n /= 2;
    }
    while (n % 3 == 0) {
        wprintf(L" 3");
        n /= 3;
    }
    for (unsigned __int64 d = 5; d * d <= n; d += 6) {
        while (n % d == 0) {
            wprintf(L" %llu", d);
            n /= d;
        }
        while (n % (d + 2) == 0) {
            wprintf(L" %llu", d + 2);
            n /= (d + 2);
        }
    }
    if (n > 1) wprintf(L" %llu", n);
    putwchar(L'\n');
}

bool Builtin_Factor(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: factor [NUMBER]...\n"
                L"Print the prime factors of each specified integer NUMBER.\n");
        *code = 0;
        return true;
    }

    if (cmd->argc > 1) {
        for (int i = 1; i < cmd->argc; i++) {
            if (cmd->argv[i][0] == L'-') continue;
            unsigned __int64 val = wcstoull(cmd->argv[i], NULL, 10);
            FactorPrintSingle(val);
        }
    } else {
        wchar_t buf[256];
        while (fgetws(buf, _countof(buf), stdin)) {
            wchar_t* context = NULL;
            wchar_t* token = wcstok_s(buf, L" \t\r\n", &context);
            while (token) {
                unsigned __int64 val = wcstoull(token, NULL, 10);
                FactorPrintSingle(val);
                token = wcstok_s(NULL, L" \t\r\n", &context);
            }
        }
    }
    *code = 0;
    return true;
}



/* ========================================================================= */
/*                   Category F: Archival, Compression & Tools               */
/* ========================================================================= */

bool Builtin_Install(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: install [OPTION]... SOURCE... DEST\n"
                L"  or:  install [OPTION]... -d DIRECTORY...\n"
                L"Copy SOURCE to DEST or create DIRECTORY.\n\n"
                L"  -d, --directory        treat all arguments as directory names\n"
                L"  -m, --mode=MODE        set permission mode (ignored on Windows)\n"
                L"  -v, --verbose          print the name of each directory created or file copied\n"
                L"  -t, --target-directory=DIR copy all SOURCE arguments into DIR\n");
        *code = 0;
        return true;
    }

    bool is_dir_mode = false, verbose = false;
    const wchar_t* target_dir = NULL;
    int src_start = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* a = cmd->argv[i];
        if (wcscmp(a, L"-d") == 0 || wcscmp(a, L"--directory") == 0) is_dir_mode = true;
        else if (wcscmp(a, L"-v") == 0 || wcscmp(a, L"--verbose") == 0) verbose = true;
        else if ((wcscmp(a, L"-m") == 0 || wcscmp(a, L"--mode") == 0) && i + 1 < cmd->argc) { i++; }
        else if ((wcscmp(a, L"-t") == 0 || wcscmp(a, L"--target-directory") == 0) && i + 1 < cmd->argc) {
            target_dir = cmd->argv[++i];
        } else if (a[0] != L'-') { src_start = i; break; }
    }

    if (src_start >= cmd->argc) {
        fwprintf(stderr, L"install: missing file operand\n");
        *code = 1;
        return true;
    }

    if (is_dir_mode) {
        for (int i = src_start; i < cmd->argc; i++) {
            const wchar_t* d = cmd->argv[i];
            /* Recursive directory creation */
            wchar_t tmp[MAX_PATH];
            wcscpy_s(tmp, _countof(tmp), d);
            for (wchar_t* p = tmp; *p; p++) {
                if (*p == L'/' || *p == L'\\') {
                    wchar_t ch = *p;
                    *p = L'\0';
                    if (wcslen(tmp) > 2) CreateDirectoryW(tmp, NULL);
                    *p = ch;
                }
            }
            CreateDirectoryW(tmp, NULL);
            if (verbose) wprintf(L"install: creating directory '%ls'\n", d);
        }
        *code = 0;
        return true;
    }

    if (target_dir) {
        for (int i = src_start; i < cmd->argc; i++) {
            const wchar_t* src = cmd->argv[i];
            const wchar_t* bname = wcsrchr(src, L'/');
            if (!bname) bname = wcsrchr(src, L'\\');
            bname = bname ? (bname + 1) : src;
            wchar_t dst_path[MAX_PATH];
            swprintf_s(dst_path, _countof(dst_path), L"%ls\\%ls", target_dir, bname);
            CopyFileW(src, dst_path, FALSE);
            if (verbose) wprintf(L"'%ls' -> '%ls'\n", src, dst_path);
        }
    } else {
        int count = cmd->argc - src_start;
        if (count < 2) {
            fwprintf(stderr, L"install: missing destination file operand\n");
            *code = 1;
            return true;
        }
        const wchar_t* dst = cmd->argv[cmd->argc - 1];
        DWORD attr = GetFileAttributesW(dst);
        bool dst_is_dir = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));

        for (int i = src_start; i < cmd->argc - 1; i++) {
            const wchar_t* src = cmd->argv[i];
            wchar_t dst_path[MAX_PATH];
            if (dst_is_dir) {
                const wchar_t* bname = wcsrchr(src, L'/');
                if (!bname) bname = wcsrchr(src, L'\\');
                bname = bname ? (bname + 1) : src;
                swprintf_s(dst_path, _countof(dst_path), L"%ls\\%ls", dst, bname);
            } else {
                wcscpy_s(dst_path, _countof(dst_path), dst);
            }
            CopyFileW(src, dst_path, FALSE);
            if (verbose) wprintf(L"'%ls' -> '%ls'\n", src, dst_path);
        }
    }
    *code = 0;
    return true;
}

bool Builtin_Uuidgen(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: uuidgen [options]\n"
                L"Create a new UUID value.\n\n"
                L"  -r, --random     generate random-based UUID (default)\n"
                L"  -t, --time       generate time-based UUID\n");
        *code = 0;
        return true;
    }

    HCRYPTPROV hProv = 0;
    unsigned char bytes[16] = {0};
    if (CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        CryptGenRandom(hProv, sizeof(bytes), bytes);
        CryptReleaseContext(hProv, 0);
    } else {
        for (int i = 0; i < 16; i++) bytes[i] = (unsigned char)(rand() & 0xFF);
    }

    /* RFC 4122 v4 format */
    bytes[6] = (bytes[6] & 0x0F) | 0x40; /* version 4 */
    bytes[8] = (bytes[8] & 0x3F) | 0x80; /* variant 1 */

    wprintf(L"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
            bytes[0], bytes[1], bytes[2], bytes[3],
            bytes[4], bytes[5],
            bytes[6], bytes[7],
            bytes[8], bytes[9],
            bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    *code = 0;
    return true;
}

static unsigned int GzipComputeCrc32(unsigned int crc, const unsigned char* buf, size_t len) {
    static unsigned int table[256];
    static int have_table = 0;
    if (!have_table) {
        for (unsigned int i = 0; i < 256; i++) {
            unsigned int c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        have_table = 1;
    }
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc = table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

bool Builtin_Gzip(CommandAST* cmd, int* code) {
    const wchar_t* cmd_name = cmd->argv[0];
    bool is_decompress = (_wcsicmp(cmd_name, L"gunzip") == 0 || _wcsicmp(cmd_name, L"zcat") == 0);
    bool to_stdout = (_wcsicmp(cmd_name, L"zcat") == 0);
    bool keep = false, force = false;

    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: %ls [OPTION]... [FILE]...\n"
                L"Compress or uncompress FILEs (by default, compress FILES in-place).\n\n"
                L"  -c, --stdout      write on standard output, keep original files unchanged\n"
                L"  -d, --decompress  decompress\n"
                L"  -f, --force       force overwrite of output file\n"
                L"  -k, --keep        keep (don't delete) input files\n"
                L"  -q, --quiet       suppress all warnings\n", cmd_name);
        *code = 0;
        return true;
    }

    int first_file = -1;
    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* a = cmd->argv[i];
        if (wcscmp(a, L"-d") == 0 || wcscmp(a, L"--decompress") == 0) is_decompress = true;
        else if (wcscmp(a, L"-c") == 0 || wcscmp(a, L"--stdout") == 0) to_stdout = true;
        else if (wcscmp(a, L"-k") == 0 || wcscmp(a, L"--keep") == 0) keep = true;
        else if (wcscmp(a, L"-f") == 0 || wcscmp(a, L"--force") == 0) force = true;
        else if (a[0] == L'-' && a[1] >= L'1' && a[1] <= L'9') { }
        else if (a[0] != L'-') { first_file = i; break; }
    }

    int file_count = (first_file > 0) ? (cmd->argc - first_file) : 0;
    const wchar_t* dummy_stdin = L"-";
    const wchar_t* const* files = (file_count > 0) ? (const wchar_t* const*)(cmd->argv + first_file) : &dummy_stdin;
    if (file_count <= 0) { file_count = 1; to_stdout = true; }
    (void)force;

    int ret = 0;
    for (int fi = 0; fi < file_count; fi++) {
        const wchar_t* path = files[fi];
        bool is_stdin = (wcscmp(path, L"-") == 0);
        FILE* in = is_stdin ? NULL : _wfsopen(path, L"rb", _SH_DENYNO);
        if (!is_stdin && !in) {
            fwprintf(stderr, L"gzip: %ls: No such file or directory\n", path);
            ret = 1;
            continue;
        }

        size_t in_cap = 65536, in_len = 0;
        unsigned char* in_data = (unsigned char*)malloc(in_cap);
        if (is_stdin) {
            wint_t wc;
            while ((wc = getwchar()) != WEOF) {
                if (in_len >= in_cap) {
                    in_cap *= 2;
                    in_data = (unsigned char*)realloc(in_data, in_cap);
                }
                in_data[in_len++] = (unsigned char)wc;
            }
        } else {
            size_t nr;
            while ((nr = fread(in_data + in_len, 1, in_cap - in_len, in)) > 0) {
                in_len += nr;
                if (in_len == in_cap) {
                    in_cap *= 2;
                    in_data = (unsigned char*)realloc(in_data, in_cap);
                }
            }
            fclose(in);
        }

        FILE* out = NULL;
        wchar_t out_path[MAX_PATH];
        bool using_stdout = to_stdout || is_stdin;
        if (!using_stdout) {
            if (wcslen(path) + 6 >= _countof(out_path)) {
                free(in_data);
                fwprintf(stderr, L"gzip: %ls: path too long\n", path);
                ret = 1;
                continue;
            }
            wcscpy_s(out_path, _countof(out_path), path);
            if (is_decompress) {
                size_t l = wcslen(out_path);
                if (l > 3 && wcscmp(out_path + l - 3, L".gz") == 0) out_path[l - 3] = L'\0';
                else wcscat_s(out_path, _countof(out_path), L".out");
            } else {
                wcscat_s(out_path, _countof(out_path), L".gz");
            }
            out = _wfsopen(out_path, L"wb", _SH_DENYNO);
            if (!out) {
                free(in_data);
                fwprintf(stderr, L"gzip: cannot open output %ls\n", out_path);
                ret = 1;
                continue;
            }
        }

        if (is_decompress) {
            /* Decompress GZIP */
            if (in_len < 10 || in_data[0] != 0x1F || in_data[1] != 0x8B || in_data[2] != 8) {
                fwprintf(stderr, L"gzip: %ls: not in gzip format\n", path);
                ret = 1;
            } else {
                size_t pos = 10;
                unsigned char flg = in_data[3];
                if (flg & 0x04) {
                    if (pos + 2 <= in_len) {
                        int xlen = in_data[pos] | (in_data[pos + 1] << 8);
                        pos += 2 + xlen;
                        if (pos > in_len) pos = in_len;
                    }
                }
                if (flg & 0x08) {
                    while (pos < in_len && in_data[pos] != 0) pos++;
                    if (pos < in_len) pos++;
                }
                if (flg & 0x10) {
                    while (pos < in_len && in_data[pos] != 0) pos++;
                    if (pos < in_len) pos++;
                }
                if (flg & 0x02) {
                    pos += 2;
                    if (pos > in_len) pos = in_len;
                }

                /* Read DEFLATE stored / uncompressed blocks */
                int bfinal = 0;
                while (!bfinal && pos < in_len) {
                    int bhdr = in_data[pos++];
                    bfinal = bhdr & 1;
                    int btype = (bhdr >> 1) & 3;
                    if (btype == 0) {
                        if (pos + 4 > in_len) break;
                        unsigned short len = in_data[pos] | (in_data[pos + 1] << 8);
                        pos += 4;
                        if (pos > in_len) break;
                        if (pos + len > in_len) len = (unsigned short)(in_len - pos);
                        if (using_stdout) {
                            for (unsigned short b = 0; b < len; b++) putwchar((wchar_t)in_data[pos + b]);
                        } else {
                            fwrite(in_data + pos, 1, len, out);
                        }
                        pos += len;
                    } else {
                        break;
                    }
                }
            }
        } else {
            /* Compress to GZIP */
            unsigned char gh[10] = { 0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0B };
            if (using_stdout) {
                for (size_t b = 0; b < 10; b++) putwchar((wchar_t)gh[b]);
            } else {
                fwrite(gh, 1, 10, out);
            }

            unsigned int crc = GzipComputeCrc32(0, in_data, in_len);
            if (in_len == 0) {
                unsigned char empty_blk[5] = { 0x01, 0x00, 0x00, 0xFF, 0xFF };
                if (using_stdout) {
                    for (size_t b = 0; b < 5; b++) putwchar((wchar_t)empty_blk[b]);
                } else {
                    fwrite(empty_blk, 1, 5, out);
                }
            } else {
                size_t pos = 0;
                while (pos < in_len) {
                    size_t chunk = in_len - pos;
                    if (chunk > 65535) chunk = 65535;
                    bool is_last = (pos + chunk == in_len);
                    unsigned char blk_hdr[5];
                    blk_hdr[0] = is_last ? 0x01 : 0x00;
                    unsigned short len16 = (unsigned short)chunk;
                    unsigned short nlen16 = (unsigned short)~len16;
                    blk_hdr[1] = (unsigned char)(len16 & 0xFF);
                    blk_hdr[2] = (unsigned char)(len16 >> 8);
                    blk_hdr[3] = (unsigned char)(nlen16 & 0xFF);
                    blk_hdr[4] = (unsigned char)(nlen16 >> 8);
                    if (using_stdout) {
                        for (size_t b = 0; b < 5; b++) putwchar((wchar_t)blk_hdr[b]);
                        for (size_t b = 0; b < chunk; b++) putwchar((wchar_t)in_data[pos + b]);
                    } else {
                        fwrite(blk_hdr, 1, 5, out);
                        fwrite(in_data + pos, 1, chunk, out);
                    }
                    pos += chunk;
                }
            }

            /* 8-byte trailer: CRC32 and ISIZE */
            unsigned char tr[8];
            tr[0] = (unsigned char)(crc & 0xFF);
            tr[1] = (unsigned char)((crc >> 8) & 0xFF);
            tr[2] = (unsigned char)((crc >> 16) & 0xFF);
            tr[3] = (unsigned char)((crc >> 24) & 0xFF);
            unsigned int isize32 = (unsigned int)(in_len & 0xFFFFFFFF);
            tr[4] = (unsigned char)(isize32 & 0xFF);
            tr[5] = (unsigned char)((isize32 >> 8) & 0xFF);
            tr[6] = (unsigned char)((isize32 >> 16) & 0xFF);
            tr[7] = (unsigned char)((isize32 >> 24) & 0xFF);
            if (using_stdout) {
                for (size_t b = 0; b < 8; b++) putwchar((wchar_t)tr[b]);
            } else {
                fwrite(tr, 1, 8, out);
            }
        }

        if (using_stdout) {
            fflush(stdout);
        } else {
            fclose(out);
            if (!keep && !is_stdin) DeleteFileW(path);
        }
        free(in_data);
    }
    *code = ret;
    return true;
}

