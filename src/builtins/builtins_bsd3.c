#include <ksh/common.h>
#include <iphlpapi.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>
#include <ksh/help.h>

/* ========================================================================= */
/*                          BSD Core Utilities - Part 3                     */
/*  sockstat, nc (netcat), mtree, look, daemon, lam, vmstat, pbcopy, pbpaste */
/* ========================================================================= */

/* ------------------------------------------------------------------------- */
/* 1. SOCKSTAT (FreeBSD Sockets Status)                                      */
/* ------------------------------------------------------------------------- */

static void GetProcessInfoByPid(DWORD pid, wchar_t* out_proc, size_t proc_sz, wchar_t* out_user, size_t user_sz) {
    wcsncpy(out_proc, L"?", proc_sz);
    wcsncpy(out_user, L"?", user_sz);

    if (pid == 0) {
        wcsncpy(out_proc, L"System Idle", proc_sz);
        wcsncpy(out_user, L"SYSTEM", user_sz);
        return;
    }
    if (pid == 4) {
        wcsncpy(out_proc, L"System", proc_sz);
        wcsncpy(out_user, L"SYSTEM", user_sz);
        return;
    }

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return;

    wchar_t fullPath[MAX_PATH];
    DWORD pathLen = MAX_PATH;
    if (QueryFullProcessImageNameW(hProc, 0, fullPath, &pathLen)) {
        const wchar_t* base = wcsrchr(fullPath, L'\\');
        base = base ? base + 1 : fullPath;
        wcsncpy(out_proc, base, proc_sz);
    }

    HANDLE hToken = NULL;
    if (OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) {
        DWORD len = 0;
        GetTokenInformation(hToken, TokenUser, NULL, 0, &len);
        if (len > 0) {
            TOKEN_USER* pTu = (TOKEN_USER*)malloc(len);
            if (pTu && GetTokenInformation(hToken, TokenUser, pTu, len, &len)) {
                wchar_t name[128] = {0};
                wchar_t dom[128] = {0};
                DWORD nameLen = 128, domLen = 128;
                SID_NAME_USE use;
                if (LookupAccountSidW(NULL, pTu->User.Sid, name, &nameLen, dom, &domLen, &use)) {
                    if (domLen > 0 && wcscmp(dom, L"BUILTIN") != 0) {
                        swprintf(out_user, user_sz, L"%ls\\%ls", dom, name);
                    } else {
                        wcsncpy(out_user, name, user_sz);
                    }
                }
                free(pTu);
            }
        }
        CloseHandle(hToken);
    }
    CloseHandle(hProc);
}

static void* FetchNetTable(bool tcp, ULONG af, int cls) {
    DWORD sz = 32768;
    for (int retry = 0; retry < 5; retry++) {
        void* t = malloc(sz);
        if (!t) return NULL;
        DWORD rc = tcp ? GetExtendedTcpTable(t, &sz, TRUE, af, (TCP_TABLE_CLASS)cls, 0)
                       : GetExtendedUdpTable(t, &sz, TRUE, af, (UDP_TABLE_CLASS)cls, 0);
        if (rc == NO_ERROR) return t;
        free(t);
        if (rc != ERROR_INSUFFICIENT_BUFFER) break;
        sz += 16384;
    }
    return NULL;
}

static bool MatchPort(DWORD port, const wchar_t* port_filter) {
    if (!port_filter || !*port_filter) return true;
    wchar_t buf[256];
    wcsncpy(buf, port_filter, 255);
    buf[255] = L'\0';
    wchar_t* ctx = NULL;
    wchar_t* tok = wcstok(buf, L",", &ctx);
    while (tok) {
        DWORD p = (DWORD)wcstoul(tok, NULL, 10);
        if (p == port) return true;
        tok = wcstok(NULL, L",", &ctx);
    }
    return false;
}

bool Builtin_Sockstat(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: sockstat [-46clqU] [-p ports] [-P protocols]\n"
                L"Display open sockets, owning processes, and connection states.\n\n"
                L"  -4            Show only IPv4 sockets\n"
                L"  -6            Show only IPv6 sockets\n"
                L"  -c            Show connected sockets\n"
                L"  -l            Show listening sockets\n"
                L"  -p ports      Filter by comma-separated port numbers\n"
                L"  -P protocols  Filter by comma-separated protocols (tcp, udp)\n"
                L"  -q            Quiet mode; suppress header\n"
                L"  -U            Display numeric user/process identifiers\n");
        *code = 0;
        return true;
    }

    bool opt_4 = false, opt_6 = false, opt_c = false, opt_l = false, opt_q = false, opt_U = false;
    const wchar_t* port_filter = NULL;
    const wchar_t* proto_filter = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0') {
            for (size_t j = 1; j < wcslen(arg); j++) {
                switch (arg[j]) {
                    case L'4': opt_4 = true; break;
                    case L'6': opt_6 = true; break;
                    case L'c': opt_c = true; break;
                    case L'l': opt_l = true; break;
                    case L'q': opt_q = true; break;
                    case L'U': opt_U = true; break;
                    case L'p':
                        if (j + 1 < wcslen(arg)) port_filter = &arg[j + 1];
                        else if (i + 1 < cmd->argc) port_filter = cmd->argv[++i];
                        goto next_arg;
                    case L'P':
                        if (j + 1 < wcslen(arg)) proto_filter = &arg[j + 1];
                        else if (i + 1 < cmd->argc) proto_filter = cmd->argv[++i];
                        goto next_arg;
                    default: break;
                }
            }
        }
    next_arg:;
    }

    if (!opt_4 && !opt_6) { opt_4 = true; opt_6 = true; }
    if (!opt_c && !opt_l) { opt_c = true; opt_l = true; }

    bool show_tcp = true, show_udp = true;
    if (proto_filter) {
        show_tcp = (wcsstr(proto_filter, L"tcp") != NULL || wcsstr(proto_filter, L"TCP") != NULL);
        show_udp = (wcsstr(proto_filter, L"udp") != NULL || wcsstr(proto_filter, L"UDP") != NULL);
    }

    if (!opt_q) {
        wprintf(L"%-12ls %-18ls %6ls %-5ls %-23ls %-23ls\n",
                L"USER", L"COMMAND", L"PID", L"PROTO", L"LOCAL ADDRESS", L"FOREIGN ADDRESS");
    }

    // 1. IPv4 TCP
    if (opt_4 && show_tcp) {
        PMIB_TCPTABLE_OWNER_PID tbl = (PMIB_TCPTABLE_OWNER_PID)FetchNetTable(true, AF_INET, TCP_TABLE_OWNER_PID_ALL);
        if (tbl) {
            for (DWORD i = 0; i < tbl->dwNumEntries; i++) {
                MIB_TCPROW_OWNER_PID* r = &tbl->table[i];
                bool is_listen = (r->dwState == MIB_TCP_STATE_LISTEN);
                if (is_listen && !opt_l) continue;
                if (!is_listen && !opt_c) continue;

                DWORD locPort = ntohs((u_short)r->dwLocalPort);
                DWORD remPort = ntohs((u_short)r->dwRemotePort);
                if (port_filter && !MatchPort(locPort, port_filter) && !MatchPort(remPort, port_filter))
                    continue;

                wchar_t user[64], proc[64];
                if (opt_U) {
                    swprintf(user, 64, L"%u", r->dwOwningPid);
                    swprintf(proc, 64, L"%u", r->dwOwningPid);
                } else {
                    GetProcessInfoByPid(r->dwOwningPid, proc, 64, user, 64);
                }

                struct in_addr inLoc, inRem;
                inLoc.S_un.S_addr = r->dwLocalAddr;
                inRem.S_un.S_addr = r->dwRemoteAddr;

                char sLoc[INET_ADDRSTRLEN] = "*", sRem[INET_ADDRSTRLEN] = "*";
                inet_ntop(AF_INET, &inLoc, sLoc, sizeof(sLoc));
                if (!is_listen && r->dwRemoteAddr != 0) inet_ntop(AF_INET, &inRem, sRem, sizeof(sRem));

                wchar_t locStr[64], remStr[64];
                swprintf(locStr, 64, L"%hs:%u", sLoc, locPort);
                if (is_listen) swprintf(remStr, 64, L"*:*");
                else swprintf(remStr, 64, L"%hs:%u", sRem, remPort);

                wprintf(L"%-12ls %-18ls %6u %-5ls %-23ls %-23ls\n",
                        user, proc, r->dwOwningPid, L"tcp4", locStr, remStr);
            }
            free(tbl);
        }
    }

    // 2. IPv6 TCP
    if (opt_6 && show_tcp) {
        PMIB_TCP6TABLE_OWNER_PID tbl = (PMIB_TCP6TABLE_OWNER_PID)FetchNetTable(true, AF_INET6, TCP_TABLE_OWNER_PID_ALL);
        if (tbl) {
            for (DWORD i = 0; i < tbl->dwNumEntries; i++) {
                MIB_TCP6ROW_OWNER_PID* r = &tbl->table[i];
                bool is_listen = (r->dwState == MIB_TCP_STATE_LISTEN);
                if (is_listen && !opt_l) continue;
                if (!is_listen && !opt_c) continue;

                DWORD locPort = ntohs((u_short)r->dwLocalPort);
                DWORD remPort = ntohs((u_short)r->dwRemotePort);
                if (port_filter && !MatchPort(locPort, port_filter) && !MatchPort(remPort, port_filter))
                    continue;

                wchar_t user[64], proc[64];
                if (opt_U) {
                    swprintf(user, 64, L"%u", r->dwOwningPid);
                    swprintf(proc, 64, L"%u", r->dwOwningPid);
                } else {
                    GetProcessInfoByPid(r->dwOwningPid, proc, 64, user, 64);
                }

                char sLoc[INET6_ADDRSTRLEN] = "*", sRem[INET6_ADDRSTRLEN] = "*";
                inet_ntop(AF_INET6, &r->ucLocalAddr, sLoc, sizeof(sLoc));
                if (!is_listen) inet_ntop(AF_INET6, &r->ucRemoteAddr, sRem, sizeof(sRem));

                wchar_t locStr[64], remStr[64];
                swprintf(locStr, 64, L"[%hs]:%u", sLoc, locPort);
                if (is_listen) swprintf(remStr, 64, L"*:*");
                else swprintf(remStr, 64, L"[%hs]:%u", sRem, remPort);

                wprintf(L"%-12ls %-18ls %6u %-5ls %-23ls %-23ls\n",
                        user, proc, r->dwOwningPid, L"tcp6", locStr, remStr);
            }
            free(tbl);
        }
    }

    // 3. IPv4 UDP
    if (opt_4 && show_udp && opt_l) {
        PMIB_UDPTABLE_OWNER_PID tbl = (PMIB_UDPTABLE_OWNER_PID)FetchNetTable(false, AF_INET, UDP_TABLE_OWNER_PID);
        if (tbl) {
            for (DWORD i = 0; i < tbl->dwNumEntries; i++) {
                MIB_UDPROW_OWNER_PID* r = &tbl->table[i];
                DWORD locPort = ntohs((u_short)r->dwLocalPort);
                if (port_filter && !MatchPort(locPort, port_filter)) continue;

                wchar_t user[64], proc[64];
                if (opt_U) {
                    swprintf(user, 64, L"%u", r->dwOwningPid);
                    swprintf(proc, 64, L"%u", r->dwOwningPid);
                } else {
                    GetProcessInfoByPid(r->dwOwningPid, proc, 64, user, 64);
                }

                struct in_addr inLoc;
                inLoc.S_un.S_addr = r->dwLocalAddr;
                char sLoc[INET_ADDRSTRLEN] = "*";
                inet_ntop(AF_INET, &inLoc, sLoc, sizeof(sLoc));

                wchar_t locStr[64];
                swprintf(locStr, 64, L"%hs:%u", sLoc, locPort);

                wprintf(L"%-12ls %-18ls %6u %-5ls %-23ls %-23ls\n",
                        user, proc, r->dwOwningPid, L"udp4", locStr, L"*:*");
            }
            free(tbl);
        }
    }

    // 4. IPv6 UDP
    if (opt_6 && show_udp && opt_l) {
        PMIB_UDP6TABLE_OWNER_PID tbl = (PMIB_UDP6TABLE_OWNER_PID)FetchNetTable(false, AF_INET6, UDP_TABLE_OWNER_PID);
        if (tbl) {
            for (DWORD i = 0; i < tbl->dwNumEntries; i++) {
                MIB_UDP6ROW_OWNER_PID* r = &tbl->table[i];
                DWORD locPort = ntohs((u_short)r->dwLocalPort);
                if (port_filter && !MatchPort(locPort, port_filter)) continue;

                wchar_t user[64], proc[64];
                if (opt_U) {
                    swprintf(user, 64, L"%u", r->dwOwningPid);
                    swprintf(proc, 64, L"%u", r->dwOwningPid);
                } else {
                    GetProcessInfoByPid(r->dwOwningPid, proc, 64, user, 64);
                }

                char sLoc[INET6_ADDRSTRLEN] = "*";
                inet_ntop(AF_INET6, &r->ucLocalAddr, sLoc, sizeof(sLoc));

                wchar_t locStr[64];
                swprintf(locStr, 64, L"[%hs]:%u", sLoc, locPort);

                wprintf(L"%-12ls %-18ls %6u %-5ls %-23ls %-23ls\n",
                        user, proc, r->dwOwningPid, L"udp6", locStr, L"*:*");
            }
            free(tbl);
        }
    }

    *code = 0;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 2. NC / NETCAT (OpenBSD Netcat for Windows NT)                           */
/* ------------------------------------------------------------------------- */

bool Builtin_Nc(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: nc [-46klnuvz] [-w timeout] [-p port] [destination] [port]\n"
                L"OpenBSD Netcat: arbitrary TCP and UDP connections and listeners.\n\n"
                L"  -4            Use IPv4 only\n"
                L"  -6            Use IPv6 only\n"
                L"  -k            Keep listening after client disconnects (listen mode)\n"
                L"  -l            Listen mode, for inbound connections\n"
                L"  -n            Do not resolve hostnames via DNS\n"
                L"  -p port       Specify source port or listening port\n"
                L"  -u            Use UDP instead of default TCP\n"
                L"  -v            Verbose output\n"
                L"  -w timeout    Timeout in seconds for connection and reads\n"
                L"  -z            Zero-I/O mode (port scanning)\n");
        *code = 0;
        return true;
    }

    bool opt_4 = false, opt_6 = false, opt_k = false, opt_l = false, opt_n = false;
    bool opt_u = false, opt_v = false, opt_z = false;
    int opt_w = 0;
    const wchar_t* opt_p = NULL;
    const wchar_t* target_host = NULL;
    const wchar_t* target_port = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0') {
            for (size_t j = 1; j < wcslen(arg); j++) {
                switch (arg[j]) {
                    case L'4': opt_4 = true; break;
                    case L'6': opt_6 = true; break;
                    case L'k': opt_k = true; break;
                    case L'l': opt_l = true; break;
                    case L'n': opt_n = true; break;
                    case L'u': opt_u = true; break;
                    case L'v': opt_v = true; break;
                    case L'z': opt_z = true; break;
                    case L'p':
                        if (j + 1 < wcslen(arg)) opt_p = &arg[j + 1];
                        else if (i + 1 < cmd->argc) opt_p = cmd->argv[++i];
                        goto next_nc_arg;
                    case L'w':
                        if (j + 1 < wcslen(arg)) opt_w = (int)wcstol(&arg[j + 1], NULL, 10);
                        else if (i + 1 < cmd->argc) opt_w = (int)wcstol(cmd->argv[++i], NULL, 10);
                        goto next_nc_arg;
                    default: break;
                }
            }
        } else {
            if (!target_host && !opt_l) target_host = arg;
            else if (!target_port) target_port = arg;
            else if (!target_host && opt_l) target_host = arg;
        }
    next_nc_arg:;
    }

    if (opt_l && !target_port && opt_p) target_port = opt_p;
    if (!target_port && !opt_l) {
        fwprintf(stderr, L"nc: missing destination or port\n");
        *code = 1;
        return true;
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fwprintf(stderr, L"nc: WSAStartup failed\n");
        *code = 1;
        return true;
    }

    char hostA[256] = "0.0.0.0";
    char portA[32] = "0";
    if (target_host) WideCharToMultiByte(CP_UTF8, 0, target_host, -1, hostA, sizeof(hostA), NULL, NULL);
    if (target_port) WideCharToMultiByte(CP_UTF8, 0, target_port, -1, portA, sizeof(portA), NULL, NULL);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = opt_4 ? AF_INET : (opt_6 ? AF_INET6 : AF_UNSPEC);
    hints.ai_socktype = opt_u ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = opt_u ? IPPROTO_UDP : IPPROTO_TCP;
    if (opt_l) hints.ai_flags = AI_PASSIVE;
    if (opt_n) hints.ai_flags |= AI_NUMERICHOST;

    int gai = getaddrinfo(opt_l && !target_host ? NULL : hostA, portA, &hints, &res);
    if (gai != 0 || !res) {
        if (opt_v) fwprintf(stderr, L"nc: getaddrinfo (%hs:%hs) failed: %d\n", hostA, portA, gai);
        WSACleanup();
        *code = 1;
        return true;
    }

    // Server mode
    if (opt_l) {
        SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (s == INVALID_SOCKET) {
            freeaddrinfo(res);
            WSACleanup();
            *code = 1;
            return true;
        }

        BOOL reuse = TRUE;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

        if (bind(s, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
            if (opt_v) fwprintf(stderr, L"nc: bind failed: %d\n", WSAGetLastError());
            closesocket(s);
            freeaddrinfo(res);
            WSACleanup();
            *code = 1;
            return true;
        }

        if (opt_u) {
            if (opt_v) fwprintf(stderr, L"Listening on UDP %hs:%hs\n", hostA, portA);
            char buf[8192];
            struct sockaddr_storage peer;
            int peerLen = sizeof(peer);
            int n = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr*)&peer, &peerLen);
            if (n > 0) {
                fwrite(buf, 1, n, stdout);
                fflush(stdout);
            }
            closesocket(s);
        } else {
            listen(s, 1);
            if (opt_v) fwprintf(stderr, L"Listening on %hs %hs\n", hostA, portA);
            do {
                struct sockaddr_storage peer;
                int peerLen = sizeof(peer);
                SOCKET c = accept(s, (struct sockaddr*)&peer, &peerLen);
                if (c == INVALID_SOCKET) break;
                if (opt_v) fwprintf(stderr, L"Connection received\n");

                HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
                char buf[4096];
                bool running = true;
                while (running) {
                    fd_set rfds;
                    FD_ZERO(&rfds);
                    FD_SET(c, &rfds);
                    struct timeval tv = {0, 10000}; // 10ms
                    int sel = select(0, &rfds, NULL, NULL, &tv);
                    if (sel > 0 && FD_ISSET(c, &rfds)) {
                        int r = recv(c, buf, sizeof(buf), 0);
                        if (r <= 0) break;
                        fwrite(buf, 1, r, stdout);
                        fflush(stdout);
                    }

                    DWORD avail = 0;
                    if (PeekNamedPipe(hIn, NULL, 0, NULL, &avail, NULL) && avail > 0) {
                        DWORD bytesRead = 0;
                        if (ReadFile(hIn, buf, min(avail, (DWORD)sizeof(buf)), &bytesRead, NULL) && bytesRead > 0) {
                            send(c, buf, (int)bytesRead, 0);
                        }
                    }
                    Sleep(1);
                }
                closesocket(c);
            } while (opt_k);
            closesocket(s);
        }
    } else {
        // Client mode
        SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (s == INVALID_SOCKET) {
            freeaddrinfo(res);
            WSACleanup();
            *code = 1;
            return true;
        }

        if (opt_w > 0) {
            DWORD timeout_ms = opt_w * 1000;
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
        }

        if (connect(s, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
            if (opt_v) fwprintf(stderr, L"nc: connect to %hs port %hs failed: %d\n", hostA, portA, WSAGetLastError());
            closesocket(s);
            freeaddrinfo(res);
            WSACleanup();
            *code = 1;
            return true;
        }

        if (opt_v) fwprintf(stderr, L"Connection to %hs %hs port [%hs/*] succeeded!\n",
                            hostA, portA, opt_u ? "udp" : "tcp");

        if (opt_z) {
            closesocket(s);
            freeaddrinfo(res);
            WSACleanup();
            *code = 0;
            return true;
        }

        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        char buf[4096];
        bool eof_stdin = false;
        while (true) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(s, &rfds);
            struct timeval tv = {0, 10000};
            int sel = select(0, &rfds, NULL, NULL, &tv);
            if (sel > 0 && FD_ISSET(s, &rfds)) {
                int r = recv(s, buf, sizeof(buf), 0);
                if (r <= 0) break;
                fwrite(buf, 1, r, stdout);
                fflush(stdout);
            }

            if (!eof_stdin) {
                DWORD avail = 0;
                if (PeekNamedPipe(hIn, NULL, 0, NULL, &avail, NULL)) {
                    if (avail > 0) {
                        DWORD bytesRead = 0;
                        if (ReadFile(hIn, buf, min(avail, (DWORD)sizeof(buf)), &bytesRead, NULL) && bytesRead > 0) {
                            send(s, buf, (int)bytesRead, 0);
                        }
                    }
                } else {
                    // Try direct synchronous read if not a pipe
                    DWORD bytesRead = 0;
                    if (ReadFile(hIn, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0) {
                        send(s, buf, (int)bytesRead, 0);
                    } else {
                        eof_stdin = true;
                        shutdown(s, SD_SEND);
                    }
                }
            }
            Sleep(1);
        }
        closesocket(s);
    }

    freeaddrinfo(res);
    WSACleanup();
    *code = 0;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 3. MTREE (BSD Directory Hierarchy Specification & Verification)          */
/* ------------------------------------------------------------------------- */

static bool ComputeFileSha256(const wchar_t* path, wchar_t* out_hex, size_t hex_cap) {
    FILE* fp = _wfsopen(path, L"rb", _SH_DENYNO);
    if (!fp) return false;

    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        fclose(fp);
        return false;
    }

    HCRYPTHASH hHash = 0;
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        fclose(fp);
        return false;
    }

    BYTE buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        CryptHashData(hHash, buf, (DWORD)n, 0);
    }
    fclose(fp);

    BYTE hash[32];
    DWORD hashLen = sizeof(hash);
    bool ok = CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    if (ok && hex_cap >= 65) {
        for (DWORD i = 0; i < hashLen; i++) {
            swprintf(out_hex + (i * 2), hex_cap - (i * 2), L"%02x", hash[i]);
        }
        out_hex[64] = L'\0';
        return true;
    }
    return false;
}

static void MtreeDumpDir(const wchar_t* base_dir, const wchar_t* rel_dir, bool opt_sha) {
    wchar_t searchPath[MAX_PATH * 2];
    if (rel_dir && *rel_dir) swprintf(searchPath, MAX_PATH * 2, L"%ls\\%ls\\*", base_dir, rel_dir);
    else swprintf(searchPath, MAX_PATH * 2, L"%ls\\*", base_dir);

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

        wchar_t curRel[MAX_PATH * 2];
        if (rel_dir && *rel_dir) swprintf(curRel, MAX_PATH * 2, L"%ls/%ls", rel_dir, fd.cFileName);
        else swprintf(curRel, MAX_PATH * 2, L"%ls", fd.cFileName);

        wchar_t fullPath[MAX_PATH * 2];
        swprintf(fullPath, MAX_PATH * 2, L"%ls\\%ls", base_dir, curRel);
        for (size_t k = 0; fullPath[k]; k++) if (fullPath[k] == L'/') fullPath[k] = L'\\';

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            wprintf(L"%ls type=dir\n", curRel);
            MtreeDumpDir(base_dir, curRel, opt_sha);
        } else {
            uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            uint64_t ft = ((uint64_t)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
            time_t unixTime = (time_t)((ft - 116444736000000000ULL) / 10000000ULL);

            wchar_t shaHex[65] = {0};
            if (opt_sha) ComputeFileSha256(fullPath, shaHex, sizeof(shaHex) / sizeof(shaHex[0]));

            if (opt_sha && shaHex[0]) {
                wprintf(L"%ls type=file size=%llu time=%lld sha256digest=%ls\n", curRel, sz, (long long)unixTime, shaHex);
            } else {
                wprintf(L"%ls type=file size=%llu time=%lld\n", curRel, sz, (long long)unixTime);
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

bool Builtin_Mtree(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: mtree [-c | -f specfile] [-p path] [-K keywords] [-qn]\n"
                L"Map or verify directory hierarchy and file integrity specifications.\n\n"
                L"  -c            Create specification from directory\n"
                L"  -f specfile   Verify directory against specfile\n"
                L"  -p path       Root directory path (default: .)\n"
                L"  -K keywords   Keywords to compute/verify (type, size, time, sha256)\n"
                L"  -q            Quiet mode; only report discrepancies\n"
                L"  -n            Suppress header and comments\n");
        *code = 0;
        return true;
    }

    bool opt_c = false, opt_q = false, opt_n = false;
    (void)opt_q;
    const wchar_t* spec_file = NULL;
    const wchar_t* root_path = L".";
    bool opt_sha = true;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (wcscmp(arg, L"-c") == 0) opt_c = true;
        else if (wcscmp(arg, L"-q") == 0) opt_q = true;
        else if (wcscmp(arg, L"-n") == 0) opt_n = true;
        else if (wcscmp(arg, L"-f") == 0 && i + 1 < cmd->argc) spec_file = cmd->argv[++i];
        else if (wcscmp(arg, L"-p") == 0 && i + 1 < cmd->argc) root_path = cmd->argv[++i];
        else if (wcscmp(arg, L"-K") == 0 && i + 1 < cmd->argc) {
            const wchar_t* kw = cmd->argv[++i];
            opt_sha = (wcsstr(kw, L"sha256") != NULL || wcsstr(kw, L"all") != NULL);
        }
    }

    if (!opt_c && !spec_file) opt_c = true;

    if (opt_c) {
        if (!opt_n) {
            wprintf(L"# . mtTree 2.0 specification\n");
            wprintf(L"# Generated for %ls\n", root_path);
        }
        MtreeDumpDir(root_path, L"", opt_sha);
        *code = 0;
        return true;
    }

    // Verify mode
    FILE* fp = _wfsopen(spec_file, L"rt", _SH_DENYNO);
    if (!fp) {
        fwprintf(stderr, L"mtree: %ls: No such file or directory\n", spec_file);
        *code = 1;
        return true;
    }

    int mismatches = 0;
    wchar_t line[4096];
    while (fgetws(line, 4096, fp)) {
        size_t l = wcslen(line);
        while (l > 0 && (line[l - 1] == L'\r' || line[l - 1] == L'\n')) line[--l] = 0;
        if (l == 0 || line[0] == L'#') continue;

        wchar_t path[MAX_PATH] = {0};
        wchar_t type[32] = {0};
        uint64_t exp_size = (uint64_t)-1;
        wchar_t exp_sha[65] = {0};

        wchar_t* ctx = NULL;
        wchar_t* tok = wcstok(line, L" \t", &ctx);
        if (!tok) continue;
        wcsncpy_s(path, _countof(path), tok, _TRUNCATE);

        while ((tok = wcstok(NULL, L" \t", &ctx)) != NULL) {
            if (wcsncmp(tok, L"type=", 5) == 0) wcsncpy_s(type, _countof(type), tok + 5, _TRUNCATE);
            else if (wcsncmp(tok, L"size=", 5) == 0) exp_size = _wcstoui64(tok + 5, NULL, 10);
            else if (wcsncmp(tok, L"sha256digest=", 13) == 0) wcsncpy_s(exp_sha, _countof(exp_sha), tok + 13, _TRUNCATE);
            else if (wcsncmp(tok, L"sha256=", 7) == 0) wcsncpy_s(exp_sha, _countof(exp_sha), tok + 7, _TRUNCATE);
        }

        wchar_t fullPath[MAX_PATH * 2];
        swprintf_s(fullPath, _countof(fullPath), L"%ls\\%ls", root_path, path);
        for (size_t k = 0; fullPath[k]; k++) if (fullPath[k] == L'/') fullPath[k] = L'\\';

        DWORD attrs = GetFileAttributesW(fullPath);
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            fwprintf(stderr, L"%ls: missing\n", path);
            mismatches++;
            continue;
        }

        bool is_dir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (wcscmp(type, L"dir") == 0 && !is_dir) {
            fwprintf(stderr, L"%ls: type mismatch (expected dir, found file)\n", path);
            mismatches++;
        } else if (wcscmp(type, L"file") == 0 && is_dir) {
            fwprintf(stderr, L"%ls: type mismatch (expected file, found dir)\n", path);
            mismatches++;
        }

        if (!is_dir) {
            if (exp_size != (uint64_t)-1) {
                WIN32_FILE_ATTRIBUTE_DATA fad;
                if (GetFileAttributesExW(fullPath, GetFileExInfoStandard, &fad)) {
                    uint64_t sz = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
                    if (sz != exp_size) {
                        fwprintf(stderr, L"%ls: size (expected %llu, found %llu)\n", path, exp_size, sz);
                        mismatches++;
                    }
                }
            }

            if (exp_sha[0]) {
                wchar_t act_sha[65] = {0};
                if (ComputeFileSha256(fullPath, act_sha, 65)) {
                    if (_wcsicmp(act_sha, exp_sha) != 0) {
                        fwprintf(stderr, L"%ls: sha256digest (expected %ls, found %ls)\n", path, exp_sha, act_sha);
                        mismatches++;
                    }
                }
            }
        }
    }
    fclose(fp);

    *code = (mismatches > 0 ? 1 : 0);
    return true;
}

/* ------------------------------------------------------------------------- */
/* 4. LOOK (BSD Binary Search on Sorted Files)                               */
/* ------------------------------------------------------------------------- */

static int LookCompare(const wchar_t* line, const wchar_t* key, bool fold, bool dict, wchar_t term) {
    while (*key) {
        if (term && *line == term) return 0;
        if (dict) {
            while (*line && !iswalnum(*line) && !iswspace(*line)) line++;
            while (*key && !iswalnum(*key) && !iswspace(*key)) key++;
        }
        if (!*key) break;
        if (!*line) return -1;

        wchar_t c1 = fold ? towlower(*line) : *line;
        wchar_t c2 = fold ? towlower(*key) : *key;
        if (c1 != c2) return (int)c1 - (int)c2;
        line++;
        key++;
    }
    return 0; // Key matches prefix
}

bool Builtin_Look(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: look [-df] [-t term] string [file]\n"
                L"Display lines beginning with a given string in sorted file.\n\n"
                L"  -d            Dictionary order; only alphanumeric and spaces compared\n"
                L"  -f            Fold case; case-insensitive comparison\n"
                L"  -t term       Termination character; compare up to term\n");
        *code = 0;
        return true;
    }

    bool opt_d = false, opt_f = false;
    wchar_t opt_t = 0;
    const wchar_t* string_key = NULL;
    const wchar_t* filename = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != 0 && !string_key) {
            for (size_t j = 1; j < wcslen(arg); j++) {
                if (arg[j] == L'd') opt_d = true;
                else if (arg[j] == L'f') opt_f = true;
                else if (arg[j] == L't') {
                    if (j + 1 < wcslen(arg)) opt_t = arg[j + 1];
                    else if (i + 1 < cmd->argc) opt_t = cmd->argv[++i][0];
                    goto next_look_arg;
                }
            }
        } else {
            if (!string_key) string_key = arg;
            else if (!filename) filename = arg;
        }
    next_look_arg:;
    }

    if (!string_key) {
        fwprintf(stderr, L"look: missing search string\n");
        *code = 2;
        return true;
    }

    FILE* fp = NULL;
    if (filename) {
        fp = _wfsopen(filename, L"rt, ccs=UTF-8", _SH_DENYNO);
        if (!fp) fp = _wfsopen(filename, L"rt", _SH_DENYNO);
        if (!fp) {
            fwprintf(stderr, L"look: %ls: No such file or directory\n", filename);
            *code = 2;
            return true;
        }
    } else {
        fp = stdin;
    }

    // Read and print matching lines
    wchar_t line[4096];
    int matched = 0;
    while (fgetws(line, 4096, fp)) {
        size_t l = wcslen(line);
        while (l > 0 && (line[l - 1] == L'\r' || line[l - 1] == L'\n')) line[--l] = 0;

        if (LookCompare(line, string_key, opt_f, opt_d, opt_t) == 0) {
            wprintf(L"%ls\n", line);
            matched++;
        }
    }

    if (filename && fp) fclose(fp);
    *code = (matched > 0 ? 0 : 1);
    return true;
}

/* ------------------------------------------------------------------------- */
/* 5. DAEMON (FreeBSD Process Detachment & Supervision)                      */
/* ------------------------------------------------------------------------- */

bool Builtin_Daemon(IShell* shell, CommandAST* cmd, int* code) {
    (void)shell;
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: daemon [-fr] [-p pidfile] [-P sup_pidfile] [-o outfile] [-m mask] [-u user] command [args...]\n"
                L"Run commands detached from the controlling terminal with supervision.\n\n"
                L"  -f            Run in foreground\n"
                L"  -r            Supervise and automatically restart process on exit\n"
                L"  -p pidfile    Write child process ID to pidfile\n"
                L"  -P sup_pidfile Write supervisor process ID to sup_pidfile\n"
                L"  -o outfile    Redirect stdout/stderr to outfile\n"
                L"  -m mask       Output redirection mask (1=stdout, 2=stderr, 3=both)\n"
                L"  -u user       Run process under specified username\n");
        *code = 0;
        return true;
    }

    bool opt_f = false, opt_r = false;
    const wchar_t* opt_p = NULL;
    const wchar_t* opt_P = NULL;
    const wchar_t* opt_o = NULL;
    int opt_m = 3;
    int cmd_start = -1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != 0 && cmd_start == -1) {
            if (wcscmp(arg, L"-f") == 0) opt_f = true;
            else if (wcscmp(arg, L"-r") == 0) opt_r = true;
            else if (wcscmp(arg, L"-p") == 0 && i + 1 < cmd->argc) opt_p = cmd->argv[++i];
            else if (wcscmp(arg, L"-P") == 0 && i + 1 < cmd->argc) opt_P = cmd->argv[++i];
            else if (wcscmp(arg, L"-o") == 0 && i + 1 < cmd->argc) opt_o = cmd->argv[++i];
            else if (wcscmp(arg, L"-m") == 0 && i + 1 < cmd->argc) opt_m = (int)wcstol(cmd->argv[++i], NULL, 10);
            else if (wcscmp(arg, L"-u") == 0 && i + 1 < cmd->argc) i++; // Accept user
        } else {
            cmd_start = i;
            break;
        }
    }

    if (cmd_start == -1) {
        fwprintf(stderr, L"daemon: missing command\n");
        *code = 1;
        return true;
    }

    if (opt_P) {
        FILE* fpP = _wfsopen(opt_P, L"wt", _SH_DENYNO);
        if (fpP) {
            fwprintf(fpP, L"%u\n", GetCurrentProcessId());
            fclose(fpP);
        }
    }

    wchar_t* cmdline = BuildWin32CommandLine(cmd->argv + cmd_start, cmd->argc - cmd_start);
    if (!cmdline) {
        *code = 1;
        return true;
    }

    HANDLE hOut = INVALID_HANDLE_VALUE;
    if (opt_o) {
        SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
        hOut = CreateFileW(opt_o, FILE_APPEND_DATA | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (hOut != INVALID_HANDLE_VALUE) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        if (opt_m & 1) si.hStdOutput = hOut;
        if (opt_m & 2) si.hStdError = hOut;
    }

    DWORD creationFlags = (opt_f ? 0 : (CREATE_NO_WINDOW | DETACHED_PROCESS));

    do {
        PROCESS_INFORMATION pi;
        memset(&pi, 0, sizeof(pi));
        wchar_t* bufCmd = SafeStrDup(cmdline);
        if (!bufCmd) break;

        if (!CreateProcessW(NULL, bufCmd, NULL, NULL, TRUE, creationFlags, NULL, NULL, &si, &pi)) {
            fwprintf(stderr, L"daemon: failed to spawn '%ls' (error %d)\n", cmd->argv[cmd_start], GetLastError());
            free(bufCmd);
            if (hOut != INVALID_HANDLE_VALUE) CloseHandle(hOut);
            free(cmdline);
            *code = 1;
            return true;
        }
        free(bufCmd);

        if (opt_p) {
            FILE* fp = _wfsopen(opt_p, L"wt", _SH_DENYNO);
            if (fp) {
                fwprintf(fp, L"%u\n", pi.dwProcessId);
                fclose(fp);
            }
        }

        if (opt_f || opt_r) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD exitCode = 0;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            if (!opt_r) {
                *code = (int)exitCode;
                break;
            }
            Sleep(1000); // Backoff before restarting
        } else {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            *code = 0;
            break;
        }
    } while (opt_r);

    free(cmdline);
    if (hOut != INVALID_HANDLE_VALUE) CloseHandle(hOut);
    return true;
}

/* ------------------------------------------------------------------------- */
/* 6. LAM (FreeBSD / macOS Line Laminator)                                   */
/* ------------------------------------------------------------------------- */

bool Builtin_Lam(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: lam [-t term] [-s delim] [-p min[.max]] file...\n"
                L"Laminate lines of specified files side by side.\n\n"
                L"  -t term       Line termination string (default: newline)\n"
                L"  -s delim      Delimiter string between file columns\n"
                L"  -p min[.max]  Pad column width with minimum spaces and optional maximum truncate\n"
                L"  -             Use standard input for file\n");
        *code = 0;
        return true;
    }

    const wchar_t* opt_t = L"\n";
    const wchar_t* opt_s = L"";
    int min_pad = 0, max_pad = 0;
    FILE* files[64];
    int file_count = 0;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (wcscmp(arg, L"-t") == 0 && i + 1 < cmd->argc) opt_t = cmd->argv[++i];
        else if (wcscmp(arg, L"-s") == 0 && i + 1 < cmd->argc) opt_s = cmd->argv[++i];
        else if (wcscmp(arg, L"-p") == 0 && i + 1 < cmd->argc) {
            const wchar_t* p = cmd->argv[++i];
            min_pad = (int)wcstol(p, NULL, 10);
            const wchar_t* dot = wcschr(p, L'.');
            if (dot) max_pad = (int)wcstol(dot + 1, NULL, 10);
        } else {
            if (file_count < 64) {
                if (wcscmp(arg, L"-") == 0) files[file_count++] = stdin;
                else {
                    FILE* fp = _wfsopen(arg, L"rt, ccs=UTF-8", _SH_DENYNO);
                    if (!fp) fp = _wfsopen(arg, L"rt", _SH_DENYNO);
                    if (!fp) {
                        fwprintf(stderr, L"lam: %ls: No such file or directory\n", arg);
                        *code = 1;
                        return true;
                    }
                    files[file_count++] = fp;
                }
            }
        }
    }

    if (file_count == 0) {
        files[file_count++] = stdin;
    }

    wchar_t lineBuf[64][2048];
    bool active = true;
    while (active) {
        active = false;
        for (int i = 0; i < file_count; i++) {
            lineBuf[i][0] = L'\0';
            if (files[i] && fgetws(lineBuf[i], 2048, files[i])) {
                size_t l = wcslen(lineBuf[i]);
                while (l > 0 && (lineBuf[i][l - 1] == L'\r' || lineBuf[i][l - 1] == L'\n')) lineBuf[i][--l] = 0;
                active = true;
            }
        }
        if (!active) break;

        for (int i = 0; i < file_count; i++) {
            if (i > 0 && opt_s[0]) wprintf(L"%ls", opt_s);
            wchar_t* s = lineBuf[i];
            if (max_pad > 0 && (int)wcslen(s) > max_pad) s[max_pad] = 0;
            wprintf(L"%ls", s);
            if (min_pad > 0) {
                int pad = min_pad - (int)wcslen(s);
                while (pad-- > 0) putwchar(L' ');
            }
        }
        wprintf(L"%ls", opt_t);
    }

    for (int i = 0; i < file_count; i++) {
        if (files[i] && files[i] != stdin) fclose(files[i]);
    }
    *code = 0;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 7. VMSTAT (BSD Virtual Memory & System Statistics)                       */
/* ------------------------------------------------------------------------- */

bool Builtin_Vmstat(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: vmstat [-sw] [interval [count]]\n"
                L"Display virtual memory, paging, CPU, and performance counters.\n\n"
                L"  -s            Display summary memory statistics and system counters\n"
                L"  -w            Wide mode display\n"
                L"  interval      Update interval in seconds\n"
                L"  count         Number of times to report\n");
        *code = 0;
        return true;
    }

    bool opt_s = false, opt_w = false;
    int interval = 0, count = 1;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (wcscmp(arg, L"-s") == 0) opt_s = true;
        else if (wcscmp(arg, L"-w") == 0) opt_w = true;
        else if (interval == 0) interval = (int)wcstol(arg, NULL, 10);
        else count = (int)wcstol(arg, NULL, 10);
    }

    if (opt_s) {
        MEMORYSTATUSEX ms;
        memset(&ms, 0, sizeof(ms));
        ms.dwLength = sizeof(ms);
        GlobalMemoryStatusEx(&ms);
        PERFORMANCE_INFORMATION pi;
        memset(&pi, 0, sizeof(pi));
        pi.cb = sizeof(pi);
        GetPerformanceInfo(&pi, sizeof(pi));

        wprintf(L"%12llu K total physical memory\n", ms.ullTotalPhys / 1024);
        wprintf(L"%12llu K available physical memory\n", ms.ullAvailPhys / 1024);
        wprintf(L"%12llu K total page file\n", ms.ullTotalPageFile / 1024);
        wprintf(L"%12llu K available page file\n", ms.ullAvailPageFile / 1024);
        wprintf(L"%12llu K current commit total\n", (uint64_t)pi.CommitTotal * pi.PageSize / 1024);
        wprintf(L"%12llu K current commit limit\n", (uint64_t)pi.CommitLimit * pi.PageSize / 1024);
        wprintf(L"%12u processes\n", (unsigned int)pi.ProcessCount);
        wprintf(L"%12u threads\n", (unsigned int)pi.ThreadCount);
        wprintf(L"%12u system handles\n", (unsigned int)pi.HandleCount);
        wprintf(L"%12llu K kernel paged pool\n", (uint64_t)pi.KernelPaged * pi.PageSize / 1024);
        wprintf(L"%12llu K kernel non-paged pool\n", (uint64_t)pi.KernelNonpaged * pi.PageSize / 1024);
        *code = 0;
        return true;
    }

    (void)opt_w;
    wprintf(L" procs       memory               page                    cpu\n");
    wprintf(L" r  b   avm        fre        flt    re  pi  po    us  sy  id\n");

    FILETIME prev_idle, prev_kernel, prev_user;
    GetSystemTimes(&prev_idle, &prev_kernel, &prev_user);

    int iter = 0;
    while (true) {
        if (iter > 0 && interval > 0) Sleep(interval * 1000);

        MEMORYSTATUSEX ms;
        memset(&ms, 0, sizeof(ms));
        ms.dwLength = sizeof(ms);
        GlobalMemoryStatusEx(&ms);
        PERFORMANCE_INFORMATION pi;
        memset(&pi, 0, sizeof(pi));
        pi.cb = sizeof(pi);
        GetPerformanceInfo(&pi, sizeof(pi));

        FILETIME cur_idle, cur_kernel, cur_user;
        GetSystemTimes(&cur_idle, &cur_kernel, &cur_user);

        uint64_t idle = (((uint64_t)cur_idle.dwHighDateTime << 32) | cur_idle.dwLowDateTime) -
                        (((uint64_t)prev_idle.dwHighDateTime << 32) | prev_idle.dwLowDateTime);
        uint64_t kernel = (((uint64_t)cur_kernel.dwHighDateTime << 32) | cur_kernel.dwLowDateTime) -
                          (((uint64_t)prev_kernel.dwHighDateTime << 32) | prev_kernel.dwLowDateTime);
        uint64_t user = (((uint64_t)cur_user.dwHighDateTime << 32) | cur_user.dwLowDateTime) -
                        (((uint64_t)prev_user.dwHighDateTime << 32) | prev_user.dwLowDateTime);

        prev_idle = cur_idle;
        prev_kernel = cur_kernel;
        prev_user = cur_user;

        uint64_t total = kernel + user;
        int cpu_us = total > 0 ? (int)(user * 100 / total) : 0;
        int cpu_sy = total > 0 && kernel >= idle ? (int)((kernel - idle) * 100 / total) : 0;
        int cpu_id = total > 0 ? (int)(idle * 100 / total) : 100;
        if (cpu_id > 100) cpu_id = 100;

        uint64_t avm_m = ms.ullTotalPhys / (1024 * 1024);
        uint64_t fre_m = ms.ullAvailPhys / (1024 * 1024);

        wprintf(L" 1  0 %5lluM %9lluM     %5u     0   0   0   %3d %3d %3d\n",
                avm_m, fre_m, (unsigned int)(pi.HandleCount / 100), cpu_us, cpu_sy, cpu_id);

        iter++;
        if (count > 0 && iter >= count) break;
    }

    *code = 0;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 8. PBCOPY (macOS / BSD Clipboard Copy)                                    */
/* ------------------------------------------------------------------------- */

static void GetClipFallbackPath(wchar_t* path, size_t cap) {
    wchar_t temp[MAX_PATH];
    if (GetTempPathW(MAX_PATH, temp)) {
        swprintf(path, cap, L"%lsntksh_clipboard.txt", temp);
    } else {
        wcsncpy(path, L"ntksh_clipboard.txt", cap);
    }
}

bool Builtin_Pbcopy(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: pbcopy [-pboard {general|ruler|find|font}]\n"
                L"Read standard input and copy data to the Windows clipboard.\n");
        *code = 0;
        return true;
    }

    size_t cap = 8192;
    size_t len = 0;
    wchar_t* buf = (wchar_t*)malloc(cap * sizeof(wchar_t));
    if (!buf) { *code = 1; return true; }
    buf[0] = 0;

    wchar_t line_buf[4096];
    while (fgetws(line_buf, 4096, stdin)) {
        size_t l = wcslen(line_buf);
        if (len + l + 1 >= cap) {
            size_t newcap = (cap + l + 4096) * 2;
            wchar_t* newbuf = (wchar_t*)realloc(buf, newcap * sizeof(wchar_t));
            if (!newbuf) break;
            buf = newbuf;
            cap = newcap;
        }
        if (len + l + 1 < cap) {
            wmemcpy(buf + len, line_buf, l + 1);
            len += l;
        }
    }
    buf[len] = L'\0';

    wchar_t clipPath[MAX_PATH];
    GetClipFallbackPath(clipPath, MAX_PATH);
    FILE* fpClip = _wfsopen(clipPath, L"wt, ccs=UTF-8", _SH_DENYNO);
    if (fpClip) {
        fputws(buf, fpClip);
        fclose(fpClip);
    }

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
        if (hMem) {
            void* pMem = GlobalLock(hMem);
            if (pMem) {
                memcpy(pMem, buf, (len + 1) * sizeof(wchar_t));
                GlobalUnlock(hMem);
                if (!SetClipboardData(CF_UNICODETEXT, hMem)) {
                    GlobalFree(hMem);
                }
            } else {
                GlobalFree(hMem);
            }
        }
        CloseClipboard();
    }

    free(buf);
    *code = 0;
    return true;
}

/* ------------------------------------------------------------------------- */
/* 9. PBPASTE (macOS / BSD Clipboard Paste)                                  */
/* ------------------------------------------------------------------------- */

bool Builtin_Pbpaste(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: pbpaste [-pboard {general|ruler|find|font}] [-Prefer {txt|rtf|ps}]\n"
                L"Print the contents of the Windows clipboard to standard output.\n");
        *code = 0;
        return true;
    }

    bool pasted = false;
    if (OpenClipboard(NULL)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            const wchar_t* pText = (const wchar_t*)GlobalLock(hData);
            if (pText) {
                if (*pText) {
                    fputws(pText, stdout);
                    fflush(stdout);
                }
                pasted = true;
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
    }

    if (!pasted) {
        wchar_t clipPath[MAX_PATH];
        GetClipFallbackPath(clipPath, MAX_PATH);
        FILE* fp = _wfsopen(clipPath, L"rt, ccs=UTF-8", _SH_DENYNO);
        if (fp) {
            wchar_t line[4096];
            while (fgetws(line, 4096, fp)) {
                fputws(line, stdout);
            }
            fclose(fp);
        }
    }

    *code = 0;
    return true;
}
