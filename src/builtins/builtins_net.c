#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <errno.h>
#include <ksh/common.h>
#include <ksh/ast.h>
#include <ksh/ksh.h>
#include <ksh/builtins.h>

/* ========================================================================= */
/*                              Shared helpers                               */
/* ========================================================================= */

static bool net_wtok_eq(const wchar_t* p, size_t n, const wchar_t* s) {
    return wcslen(s) == n && wcsncmp(p, s, n) == 0;
}

static bool net_parse_uint(const wchar_t* s, unsigned long* out) {
    wchar_t* end = NULL;
    unsigned long v;
    if (!s || *s < L'0' || *s > L'9') return false;
    errno = 0;
    v = wcstoul(s, &end, 10);
    if (*end || errno == ERANGE) return false;
    *out = v;
    return true;
}

static void net_fmt_ether(wchar_t* out, size_t n, const unsigned char* mac, int len, int style) {
    size_t pos = 0;
    for (int i = 0; i < len && pos + 4 < n; i++) {
        if (i > 0) {
            if (style == 2) {
                if (i % 2 == 0) out[pos++] = L'.';
            } else {
                out[pos++] = style == 1 ? L'-' : L':';
            }
        }
        pos += swprintf(out + pos, n - pos, L"%02x", mac[i]);
    }
    out[pos] = 0;
}

static void net_ipv4_str(wchar_t* out, size_t n, ULONG addr_net) {
    const unsigned char* b = (const unsigned char*)&addr_net;
    swprintf(out, n, L"%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

/* ========================================================================= */
/*                                 ifconfig                                  */
/* ========================================================================= */

typedef struct {
    int inet;   /* 0 = hex mask, 1 = dotted mask, 2 = CIDR */
    int ether;  /* 0 = colon, 1 = dash, 2 = dotted */
    int inet6;  /* 0 = numeric prefixlen, 1 = CIDR */
} IfcFmt;

static bool ifc_fmt_token(IfcFmt* f, const wchar_t* t, size_t len) {
    const wchar_t* colon;
    size_t tl, vl;
    const wchar_t* v;

    if (net_wtok_eq(t, len, L"default")) { f->inet = f->ether = f->inet6 = 0; return true; }
    if (net_wtok_eq(t, len, L"cidr")) { f->inet = 2; f->inet6 = 1; return true; }

    colon = wmemchr(t, L':', len);
    if (!colon) return false;
    tl = (size_t)(colon - t);
    v = colon + 1;
    vl = len - tl - 1;

    if (net_wtok_eq(t, tl, L"addr")) {
        return net_wtok_eq(v, vl, L"default") || net_wtok_eq(v, vl, L"numeric");
    }
    if (net_wtok_eq(t, tl, L"ether")) {
        if (net_wtok_eq(v, vl, L"default") || net_wtok_eq(v, vl, L"colon")) { f->ether = 0; return true; }
        if (net_wtok_eq(v, vl, L"dash")) { f->ether = 1; return true; }
        if (net_wtok_eq(v, vl, L"dotted")) { f->ether = 2; return true; }
        return false;
    }
    if (net_wtok_eq(t, tl, L"inet")) {
        if (net_wtok_eq(v, vl, L"default") || net_wtok_eq(v, vl, L"hex")) { f->inet = 0; return true; }
        if (net_wtok_eq(v, vl, L"dotted")) { f->inet = 1; return true; }
        if (net_wtok_eq(v, vl, L"cidr")) { f->inet = 2; return true; }
        return false;
    }
    if (net_wtok_eq(t, tl, L"inet6")) {
        if (net_wtok_eq(v, vl, L"default") || net_wtok_eq(v, vl, L"numeric")) { f->inet6 = 0; return true; }
        if (net_wtok_eq(v, vl, L"cidr")) { f->inet6 = 1; return true; }
        return false;
    }
    return false;
}

static bool ifc_parse_fmt(IfcFmt* f, const wchar_t* spec) {
    const wchar_t* p = spec;
    while (*p) {
        const wchar_t* e = wcschr(p, L',');
        size_t len = e ? (size_t)(e - p) : wcslen(p);
        if (len == 0 || !ifc_fmt_token(f, p, len)) return false;
        if (!e) break;
        p = e + 1;
    }
    return true;
}

static PIP_ADAPTER_ADDRESSES ifc_get_adapters(void) {
    ULONG size = 15000;
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_PREFIX;
    for (int tries = 0; tries < 3; tries++) {
        PIP_ADAPTER_ADDRESSES p = (PIP_ADAPTER_ADDRESSES)malloc(size);
        ULONG rc;
        if (!p) return NULL;
        rc = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, p, &size);
        if (rc == NO_ERROR) return p;
        free(p);
        if (rc != ERROR_BUFFER_OVERFLOW) break;
    }
    return NULL;
}

static unsigned long ifc_flags(const IP_ADAPTER_ADDRESSES* a) {
    unsigned long f = 0;
    bool up = a->OperStatus == IfOperStatusUp;
    bool loop = a->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
    bool bcast = a->IfType == IF_TYPE_ETHERNET_CSMACD || a->IfType == IF_TYPE_IEEE80211;
    if (up) f |= 0x1;                 /* UP */
    if (bcast) f |= 0x2;              /* BROADCAST */
    if (loop) f |= 0x8;               /* LOOPBACK */
    if (up) f |= 0x40;                /* RUNNING */
    if (!loop) f |= 0x800;            /* SIMPLEX */
    if (!(a->Flags & IP_ADAPTER_NO_MULTICAST)) f |= 0x8000; /* MULTICAST */
    return f;
}

static void ifc_flag_names(unsigned long f, wchar_t* out, size_t n) {
    static const struct { unsigned long bit; const wchar_t* name; } tab[] = {
        { 0x1, L"UP" }, { 0x2, L"BROADCAST" }, { 0x8, L"LOOPBACK" },
        { 0x40, L"RUNNING" }, { 0x800, L"SIMPLEX" }, { 0x8000, L"MULTICAST" }
    };
    bool first = true;
    out[0] = 0;
    for (size_t i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        if (f & tab[i].bit) {
            if (!first) wcsncat_s(out, n, L",", _TRUNCATE);
            wcsncat_s(out, n, tab[i].name, _TRUNCATE);
            first = false;
        }
    }
}

static const wchar_t* ifc_type_name(IFTYPE t) {
    switch (t) {
    case IF_TYPE_ETHERNET_CSMACD: return L"Ethernet";
    case IF_TYPE_IEEE80211: return L"802.11 Wireless";
    case IF_TYPE_SOFTWARE_LOOPBACK: return L"Loopback";
    case IF_TYPE_PPP: return L"PPP";
    case IF_TYPE_TUNNEL: return L"Tunnel";
    default: return L"Other";
    }
}

static void ifc_print_inet(const IP_ADAPTER_UNICAST_ADDRESS_LH* u, bool bcast, const IfcFmt* f) {
    const SOCKADDR_IN* sin = (const SOCKADDR_IN*)u->Address.lpSockaddr;
    wchar_t addr[64], bstr[64];
    unsigned int p = u->OnLinkPrefixLength > 32 ? 32 : u->OnLinkPrefixLength;
    ULONG mask = p == 0 ? 0 : (ULONG)(0xFFFFFFFFul << (32 - p));
    ULONG host_addr = sin->sin_addr.S_un.S_addr;
    ULONG bcast_addr = (ntohl(host_addr) | ~mask);

    net_ipv4_str(addr, 64, host_addr);
    wprintf(L"\tinet %ls", addr);
    if (f->inet == 2) {
        wprintf(L"/%u", p);
    } else if (f->inet == 1) {
        wprintf(L" netmask %u.%u.%u.%u", (mask >> 24) & 0xff, (mask >> 16) & 0xff, (mask >> 8) & 0xff, mask & 0xff);
    } else {
        wprintf(L" netmask 0x%08lx", (unsigned long)mask);
    }
    if (bcast) {
        ULONG bn = htonl(bcast_addr);
        net_ipv4_str(bstr, 64, bn);
        wprintf(L" broadcast %ls", bstr);
    }
    wprintf(L"\n");
}

static void ifc_print_inet6(const IP_ADAPTER_UNICAST_ADDRESS_LH* u, const IfcFmt* f, bool lifetime) {
    const SOCKADDR_IN6* sin6 = (const SOCKADDR_IN6*)u->Address.lpSockaddr;
    wchar_t addr[128];
    InetNtopW(AF_INET6, (void*)&sin6->sin6_addr, addr, 128);
    wprintf(L"\tinet6 %ls", addr);
    if (sin6->sin6_scope_id) wprintf(L"%%%lu", (unsigned long)sin6->sin6_scope_id);
    if (f->inet6 == 1) {
        wprintf(L"/%u", (unsigned)u->OnLinkPrefixLength);
    } else {
        wprintf(L" prefixlen %u", (unsigned)u->OnLinkPrefixLength);
    }
    if (lifetime) {
        if (u->ValidLifetime == 0xFFFFFFFFul) wprintf(L" vltime infinite");
        else wprintf(L" vltime %lu", (unsigned long)u->ValidLifetime);
        if (u->PreferredLifetime == 0xFFFFFFFFul) wprintf(L" pltime infinite");
        else wprintf(L" pltime %lu", (unsigned long)u->PreferredLifetime);
    }
    wprintf(L"\n");
}

typedef struct {
    IfcFmt fmt;
    bool drvname, verbose, media, lifetime;
    int family;
} IfcPrintOpts;

static void ifc_print_adapter(const IP_ADAPTER_ADDRESSES* a, const IfcPrintOpts* o) {
    wchar_t flagnames[256], ether[64];
    unsigned long flags = ifc_flags(a);
    bool up = a->OperStatus == IfOperStatusUp;
    bool bcast = (flags & 0x2) != 0;
    const IP_ADAPTER_UNICAST_ADDRESS_LH* u;

    ifc_flag_names(flags, flagnames, 256);
    if (o->drvname) {
        wprintf(L"%ls [%ls]: flags=%lx<%ls> mtu %lu\n", a->FriendlyName, a->Description, flags, flagnames, (unsigned long)a->Mtu);
    } else {
        wprintf(L"%ls: flags=%lx<%ls> mtu %lu\n", a->FriendlyName, flags, flagnames, (unsigned long)a->Mtu);
    }
    if (o->verbose) {
        wprintf(L"\tindex %lu\n", (unsigned long)a->IfIndex);
        wprintf(L"\tdescription: %ls\n", a->Description);
    }
    if (o->media) {
        wprintf(L"\tmedia: %ls", ifc_type_name(a->IfType));
        if (a->TransmitLinkSpeed) wprintf(L" (%llu Mbit/s)", (unsigned long long)(a->TransmitLinkSpeed / 1000000));
        wprintf(L"\n");
    }
    if (a->PhysicalAddressLength > 0) {
        net_fmt_ether(ether, 64, a->PhysicalAddress, (int)a->PhysicalAddressLength, o->fmt.ether);
        wprintf(L"\tether %ls\n", ether);
    }
    for (u = a->FirstUnicastAddress; u; u = u->Next) {
        if (!u->Address.lpSockaddr) continue;
        if (u->Address.lpSockaddr->sa_family == AF_INET && o->family != AF_INET6) {
            ifc_print_inet(u, bcast, &o->fmt);
        } else if (u->Address.lpSockaddr->sa_family == AF_INET6 && o->family != AF_INET) {
            ifc_print_inet6(u, &o->fmt, o->lifetime);
        }
    }
    wprintf(L"\tstatus: %ls\n", up ? L"active" : L"no carrier");
}

static void ifc_usage(void) {
    wprintf(L"Usage: ifconfig [-a|-l] [-dDkLmuv] [-f type:format[,...]] [-g group|-G group] [inet|inet6] [interface]\n"
            L"       ifconfig -C\n"
            L"       ifconfig -l [-du] [-g group] [inet|inet6]\n"
            L"Display network interface configuration.\n\n"
            L"  -a            Display information about all interfaces\n"
            L"  -C            List interface cloners (none on Windows)\n"
            L"  -d            Display only interfaces that are down\n"
            L"  -D            Include the adapter description in the output\n"
            L"  -f spec       Output format: addr, ether:colon|dash|dotted, inet:hex|dotted|cidr,\n"
            L"                inet6:numeric|cidr, default, cidr\n"
            L"  -g group      Limit output to group (only 'all' matches on Windows)\n"
            L"  -G group      Exclude group from output ('all' excludes everything)\n"
            L"  -k            Keying information (accepted; no keys exist on Windows)\n"
            L"  -L            Show IPv6 address valid and preferred lifetimes\n"
            L"  -l            List interface names only\n"
            L"  -m            Show media information\n"
            L"  -n            Numeric addresses (accepted; addresses are always numeric)\n"
            L"  -u            Display only interfaces that are up\n"
            L"  -v            Verbose: include interface index and description\n"
            L"  inet, inet6   Restrict address output to the given family\n");
}

static bool ifc_name_matches(const wchar_t* fname, wchar_t* const* names, int nnames) {
    for (int i = 0; i < nnames; i++) {
        if (_wcsicmp(fname, names[i]) == 0) return true;
    }
    return false;
}

bool Builtin_Ifconfig(CommandAST* cmd, int* code) {
    IfcPrintOpts po;
    bool opt_all = false, opt_down = false, opt_up = false, opt_list = false, opt_cloners = false;
    wchar_t* names[32];
    int nnames = 0;
    int group_mode = 0;          /* 0 none, 1 include (-g), 2 exclude (-G) */
    bool group_all = false;
    PIP_ADAPTER_ADDRESSES head;
    const IP_ADAPTER_ADDRESSES* a;
    bool first_name = true;
    int matched = 0;

    memset(&po, 0, sizeof(po));
    po.fmt.inet = po.fmt.ether = po.fmt.inet6 = 0;
    po.family = AF_UNSPEC;

    if (cmd->argc > 1 && (wcscmp(cmd->argv[1], L"-h") == 0 || wcscmp(cmd->argv[1], L"--help") == 0 || wcscmp(cmd->argv[1], L"/?") == 0)) {
        ifc_usage();
        *code = 0;
        return true;
    }

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != 0) {
            for (const wchar_t* c = arg + 1; *c; c++) {
                switch (*c) {
                case 'a': opt_all = true; break;
                case 'C': opt_cloners = true; break;
                case 'd': opt_down = true; break;
                case 'D': po.drvname = true; break;
                case 'k': case 'n': break;
                case 'L': po.lifetime = true; break;
                case 'l': opt_list = true; break;
                case 'm': po.media = true; break;
                case 'u': opt_up = true; break;
                case 'v': po.verbose = true; break;
                case 'f': {
                    const wchar_t* spec;
                    if (c[1]) spec = c + 1;
                    else if (i + 1 < cmd->argc) spec = cmd->argv[++i];
                    else { fwprintf(stderr, L"ifconfig: -f requires an argument\n"); *code = 1; return true; }
                    if (!ifc_parse_fmt(&po.fmt, spec)) {
                        fwprintf(stderr, L"ifconfig: unsupported format: %ls\n", spec);
                        *code = 1;
                        return true;
                    }
                    goto next_arg;
                }
                case 'g':
                case 'G': {
                    const wchar_t* grp;
                    if (c[1]) grp = c + 1;
                    else if (i + 1 < cmd->argc) grp = cmd->argv[++i];
                    else { fwprintf(stderr, L"ifconfig: -%lc requires an argument\n", *c); *code = 1; return true; }
                    group_mode = *c == 'g' ? 1 : 2;
                    group_all = _wcsicmp(grp, L"all") == 0;
                    goto next_arg;
                }
                case 'j':
                    fwprintf(stderr, L"ifconfig: jails are not supported on Windows\n");
                    *code = 1;
                    return true;
                default:
                    fwprintf(stderr, L"ifconfig: illegal option -- %lc\n", *c);
                    ifc_usage();
                    *code = 1;
                    return true;
                }
            }
        next_arg:
            continue;
        }
        if (net_wtok_eq(arg, wcslen(arg), L"inet")) { po.family = AF_INET; continue; }
        if (net_wtok_eq(arg, wcslen(arg), L"inet6")) { po.family = AF_INET6; continue; }
        if (net_wtok_eq(arg, wcslen(arg), L"link")) { continue; }
        if (net_wtok_eq(arg, wcslen(arg), L"create") || net_wtok_eq(arg, wcslen(arg), L"destroy") ||
            net_wtok_eq(arg, wcslen(arg), L"up") || net_wtok_eq(arg, wcslen(arg), L"down") ||
            net_wtok_eq(arg, wcslen(arg), L"mtu") || net_wtok_eq(arg, wcslen(arg), L"netmask") ||
            net_wtok_eq(arg, wcslen(arg), L"address") || net_wtok_eq(arg, wcslen(arg), L"alias") ||
            net_wtok_eq(arg, wcslen(arg), L"delete") || net_wtok_eq(arg, wcslen(arg), L"description")) {
            fwprintf(stderr, L"ifconfig: changing interface parameters is not supported on Windows (use netsh)\n");
            *code = 1;
            return true;
        }
        if (nnames < 32) names[nnames++] = cmd->argv[i];
    }

    if (opt_cloners) {
        *code = 0;
        return true;
    }

    head = ifc_get_adapters();
    if (!head) {
        fwprintf(stderr, L"ifconfig: unable to query network adapters\n");
        *code = 1;
        return true;
    }

    for (a = head; a; a = a->Next) {
        bool up = a->OperStatus == IfOperStatusUp;
        bool show = true;
        if (nnames > 0 && !ifc_name_matches(a->FriendlyName, names, nnames)) show = false;
        if (group_mode == 1 && !(group_all)) show = false;
        if (group_mode == 2 && group_all) show = false;
        if (opt_down && up) show = false;
        if (opt_up && !up) show = false;
        if (!show) continue;
        matched++;

        if (opt_list) {
            if (!first_name) wprintf(L" ");
            wprintf(L"%ls", a->FriendlyName);
            first_name = false;
            continue;
        }
        ifc_print_adapter(a, &po);
    }

    if (opt_list) wprintf(L"\n");
    (void)opt_all;

    if (nnames > 0 && matched == 0) {
        fwprintf(stderr, L"ifconfig: interface %ls does not exist\n", names[0]);
        free(head);
        *code = 1;
        return true;
    }

    free(head);
    *code = 0;
    return true;
}

/* ========================================================================= */
/*                                  netstat                                  */
/* ========================================================================= */

enum {
    NS_TCP4 = 1, NS_TCP6 = 2, NS_UDP4 = 4, NS_UDP6 = 8, NS_IP = 16
};

static const char* ns_tcp_state(DWORD s) {
    switch (s) {
    case MIB_TCP_STATE_CLOSED: return "CLOSED";
    case MIB_TCP_STATE_LISTEN: return "LISTEN";
    case MIB_TCP_STATE_SYN_SENT: return "SYN_SENT";
    case MIB_TCP_STATE_SYN_RCVD: return "SYN_RCVD";
    case MIB_TCP_STATE_ESTAB: return "ESTABLISHED";
    case MIB_TCP_STATE_FIN_WAIT1: return "FIN_WAIT_1";
    case MIB_TCP_STATE_FIN_WAIT2: return "FIN_WAIT_2";
    case MIB_TCP_STATE_CLOSE_WAIT: return "CLOSE_WAIT";
    case MIB_TCP_STATE_CLOSING: return "CLOSING";
    case MIB_TCP_STATE_LAST_ACK: return "LAST_ACK";
    case MIB_TCP_STATE_TIME_WAIT: return "TIME_WAIT";
    case MIB_TCP_STATE_DELETE_TCB: return "DELETE_TCB";
    default: return "UNKNOWN";
    }
}

typedef struct { int fam; unsigned char addr[16]; wchar_t name[256]; volatile LONG done; } NsNameCache;
static NsNameCache ns_cache[1024];
static int ns_ncache;

static void ns_wsa_init(void) {
    static bool done;
    WSADATA wd;
    if (!done && WSAStartup(MAKEWORD(2, 2), &wd) == 0) done = true;
}

typedef struct { volatile LONG refs; int fam; unsigned char addr[16]; wchar_t name[256]; volatile LONG done; } NsResolveJob;

static void ns_job_release(NsResolveJob* j) {
    if (InterlockedDecrement(&j->refs) == 0) free(j);
}

static void ns_lookup(int fam, const unsigned char* addr, wchar_t* name, size_t n) {
    SOCKADDR_STORAGE ss;
    int sl;
    memset(&ss, 0, sizeof(ss));
    if (fam == AF_INET) { ((SOCKADDR_IN*)&ss)->sin_family = AF_INET; memcpy(&((SOCKADDR_IN*)&ss)->sin_addr, addr, 4); sl = sizeof(SOCKADDR_IN); }
    else { ((SOCKADDR_IN6*)&ss)->sin6_family = AF_INET6; memcpy(&((SOCKADDR_IN6*)&ss)->sin6_addr, addr, 16); sl = sizeof(SOCKADDR_IN6); }
    if (GetNameInfoW((SOCKADDR*)&ss, sl, name, (DWORD)n, NULL, 0, NI_NAMEREQD | NI_NOFQDN) != 0) name[0] = 0;
}

static DWORD WINAPI ns_resolve_thread(LPVOID p) {
    NsResolveJob* j = (NsResolveJob*)p;
    ns_lookup(j->fam, j->addr, j->name, 256);
    InterlockedExchange(&j->done, 1);
    ns_job_release(j);
    return 0;
}

static void ns_resolve_one(NsNameCache* c) {
    ns_lookup(c->fam, c->addr, c->name, 256);
    c->done = 1;
}

static NsNameCache* ns_cache_find(int fam, const void* addr) {
    size_t alen = fam == AF_INET ? 4 : 16;
    for (int i = 0; i < ns_ncache; i++)
        if (ns_cache[i].fam == fam && memcmp(ns_cache[i].addr, addr, alen) == 0) return &ns_cache[i];
    return NULL;
}

static NsNameCache* ns_queue(int fam, const void* addr) {
    static const unsigned char zero[16];
    NsNameCache* c = ns_cache_find(fam, addr);
    if (c) return c;
    if (memcmp(addr, zero, fam == AF_INET ? 4 : 16) == 0) return NULL;
    if (ns_ncache >= (int)(sizeof(ns_cache) / sizeof(ns_cache[0]))) return NULL;
    c = &ns_cache[ns_ncache++];
    memset(c, 0, sizeof(*c));
    c->fam = fam;
    memcpy(c->addr, addr, fam == AF_INET ? 4 : 16);
    return c;
}

/* Reverse lookups can each take seconds to time out, so resolve every queued address
   concurrently and give up after a deadline; unresolved addresses print numerically. */
static void ns_resolve_pending(void) {
    enum { CAP = sizeof(ns_cache) / sizeof(ns_cache[0]) };
    static HANDLE h[CAP];
    static NsResolveJob* jobs[CAP];
    static int owner[CAP];
    DWORD nh = 0;
    ULONGLONG deadline = GetTickCount64() + 3000;
    for (int i = 0; i < ns_ncache; i++) {
        NsResolveJob* j;
        if (ns_cache[i].done) continue;
        j = (NsResolveJob*)calloc(1, sizeof(*j));
        if (!j) { ns_resolve_one(&ns_cache[i]); continue; }
        j->refs = 2;
        j->fam = ns_cache[i].fam;
        memcpy(j->addr, ns_cache[i].addr, 16);
        h[nh] = CreateThread(NULL, 64 * 1024, ns_resolve_thread, j, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
        if (!h[nh]) { free(j); ns_resolve_one(&ns_cache[i]); continue; }
        jobs[nh] = j;
        owner[nh] = i;
        nh++;
    }
    for (DWORD k = 0; k < nh; k += MAXIMUM_WAIT_OBJECTS) {
        DWORD cnt = nh - k < MAXIMUM_WAIT_OBJECTS ? nh - k : MAXIMUM_WAIT_OBJECTS;
        ULONGLONG now = GetTickCount64();
        WaitForMultipleObjects(cnt, h + k, TRUE, now < deadline ? (DWORD)(deadline - now) : 0);
    }
    for (DWORD k = 0; k < nh; k++) {
        NsNameCache* c = &ns_cache[owner[k]];
        if (jobs[k]->done) wcscpy_s(c->name, sizeof(c->name) / sizeof(c->name[0]), jobs[k]->name);
        else c->name[0] = 0;
        c->done = 1;
        CloseHandle(h[k]);
        ns_job_release(jobs[k]);
    }
}

/* Reverse-resolve an address (BSD default without -n); falls back to the numeric form. */
static void ns_host(wchar_t* out, size_t n, int fam, const void* addr, bool numeric) {
    if (!numeric) {
        NsNameCache* c = ns_queue(fam, addr);
        if (c) {
            if (!c->done) ns_resolve_one(c);
            if (c->name[0]) { wcsncpy_s(out, n, c->name, _TRUNCATE); return; }
        }
    }
    InetNtopW(fam, (void*)addr, out, n);
}

static PVOID ns_fetch_table(bool tcp, ULONG af, int cls);
static PVOID ns_fetch_tcp(ULONG af, TCP_TABLE_CLASS cls);

static void ns_prefetch_sockets(unsigned mask) {
    if (mask & NS_TCP4) {
        PMIB_TCPTABLE_OWNER_PID t = (PMIB_TCPTABLE_OWNER_PID)ns_fetch_tcp(AF_INET, TCP_TABLE_OWNER_PID_ALL);
        if (t) for (DWORD i = 0; i < t->dwNumEntries; i++) { ns_queue(AF_INET, &t->table[i].dwLocalAddr); ns_queue(AF_INET, &t->table[i].dwRemoteAddr); }
        free(t);
    }
    if (mask & NS_TCP6) {
        PMIB_TCP6TABLE_OWNER_PID t = (PMIB_TCP6TABLE_OWNER_PID)ns_fetch_tcp(AF_INET6, TCP_TABLE_OWNER_PID_ALL);
        if (t) for (DWORD i = 0; i < t->dwNumEntries; i++) { ns_queue(AF_INET6, t->table[i].ucLocalAddr); ns_queue(AF_INET6, t->table[i].ucRemoteAddr); }
        free(t);
    }
    if (mask & NS_UDP4) {
        PMIB_UDPTABLE_OWNER_PID t = (PMIB_UDPTABLE_OWNER_PID)ns_fetch_table(false, AF_INET, UDP_TABLE_OWNER_PID);
        if (t) for (DWORD i = 0; i < t->dwNumEntries; i++) ns_queue(AF_INET, &t->table[i].dwLocalAddr);
        free(t);
    }
    if (mask & NS_UDP6) {
        PMIB_UDP6TABLE_OWNER_PID t = (PMIB_UDP6TABLE_OWNER_PID)ns_fetch_table(false, AF_INET6, UDP_TABLE_OWNER_PID);
        if (t) for (DWORD i = 0; i < t->dwNumEntries; i++) ns_queue(AF_INET6, t->table[i].ucLocalAddr);
        free(t);
    }
    ns_resolve_pending();
}

static void ns_join(wchar_t* out, size_t n, const wchar_t* host, unsigned short port, const char* proto, bool numeric) {
    if (port == 0) {
        swprintf(out, n, L"%ls.*", host);
        return;
    }
    if (!numeric && proto) {
        struct servent* s = getservbyport(htons(port), proto);
        if (s && s->s_name) {
            wchar_t sname[128];
            if (MultiByteToWideChar(CP_ACP, 0, s->s_name, -1, sname, 128) > 0) {
                swprintf(out, n, L"%ls.%ls", host, sname);
                out[n - 1] = 0;
                return;
            }
        }
    }
    swprintf_s(out, n, L"%ls.%u", host, (unsigned)port);
    out[n - 1] = 0;
}

static void ns_endpoint_v4(wchar_t* out, size_t n, DWORD addr, DWORD port_field, const char* proto, bool numeric) {
    wchar_t host[64];
    if (addr == 0) wcscpy_s(host, sizeof(host) / sizeof(host[0]), L"*");
    else ns_host(host, 64, AF_INET, &addr, numeric);
    ns_join(out, n, host, ntohs((unsigned short)port_field), proto, numeric);
}

static void ns_endpoint_v6(wchar_t* out, size_t n, const UCHAR* addr, DWORD port_field, const char* proto, bool numeric) {
    wchar_t host[128];
    bool zero = true;
    for (int i = 0; i < 16; i++) if (addr[i]) { zero = false; break; }
    if (zero) wcscpy_s(host, sizeof(host) / sizeof(host[0]), L"*");
    else ns_host(host, 128, AF_INET6, addr, numeric);
    ns_join(out, n, host, ntohs((unsigned short)port_field), proto, numeric);
}

static void ns_row(const char* proto, const wchar_t* local, const wchar_t* foreign, const wchar_t* state, DWORD pid, bool verbose) {
    wprintf(L"%-6hs %6d %6d %-26ls %-26ls %ls", proto, 0, 0, local, foreign, state);
    if (verbose) wprintf(L" %lu", (unsigned long)pid);
    wprintf(L"\n");
}

/* Tables can grow between the size query and the fetch, so retry on ERROR_INSUFFICIENT_BUFFER. */
static PVOID ns_fetch_table(bool tcp, ULONG af, int cls) {
    ULONG sz = 0;
    for (int tries = 0; tries < 5; tries++) {
        PVOID t;
        DWORD rc;
        if (tcp) GetExtendedTcpTable(NULL, &sz, TRUE, af, (TCP_TABLE_CLASS)cls, 0);
        else GetExtendedUdpTable(NULL, &sz, TRUE, af, (UDP_TABLE_CLASS)cls, 0);
        sz += 4096;
        t = malloc(sz);
        if (!t) return NULL;
        rc = tcp ? GetExtendedTcpTable(t, &sz, TRUE, af, (TCP_TABLE_CLASS)cls, 0)
                 : GetExtendedUdpTable(t, &sz, TRUE, af, (UDP_TABLE_CLASS)cls, 0);
        if (rc == NO_ERROR) return t;
        free(t);
        if (rc != ERROR_INSUFFICIENT_BUFFER) break;
    }
    return NULL;
}

static PVOID ns_fetch_tcp(ULONG af, TCP_TABLE_CLASS cls) {
    return ns_fetch_table(true, af, cls);
}

/* Listener table is authoritative for LISTEN; dwState in the ALL table is unreliable for listeners. */
static bool ns_tcp4_listening(const MIB_TCPTABLE_OWNER_PID* tl, const MIB_TCPROW_OWNER_PID* r) {
    if (!tl || r->dwRemoteAddr != 0 || r->dwRemotePort != 0) return false;
    for (DWORD i = 0; i < tl->dwNumEntries; i++)
        if (tl->table[i].dwLocalAddr == r->dwLocalAddr && tl->table[i].dwLocalPort == r->dwLocalPort) return true;
    return false;
}

static bool ns_tcp6_listening(const MIB_TCP6TABLE_OWNER_PID* tl, const MIB_TCP6ROW_OWNER_PID* r) {
    static const UCHAR zero[16];
    if (!tl || r->dwRemotePort != 0 || memcmp(r->ucRemoteAddr, zero, 16) != 0) return false;
    for (DWORD i = 0; i < tl->dwNumEntries; i++)
        if (tl->table[i].dwLocalPort == r->dwLocalPort && memcmp(tl->table[i].ucLocalAddr, r->ucLocalAddr, 16) == 0) return true;
    return false;
}

static DWORD ns_tcp4_state(const MIB_TCPTABLE_OWNER_PID* tl, const MIB_TCPROW_OWNER_PID* r) {
    if (ns_tcp4_listening(tl, r)) return MIB_TCP_STATE_LISTEN;
    /* A socket with a remote endpoint cannot be listening; the OS-reported LISTEN here is invalid. */
    if (r->dwState == MIB_TCP_STATE_LISTEN && (r->dwRemoteAddr != 0 || r->dwRemotePort != 0)) return 0;
    return r->dwState;
}

static DWORD ns_tcp6_state(const MIB_TCP6TABLE_OWNER_PID* tl, const MIB_TCP6ROW_OWNER_PID* r) {
    static const UCHAR zero[16];
    if (ns_tcp6_listening(tl, r)) return MIB_TCP_STATE_LISTEN;
    if (r->dwState == MIB_TCP_STATE_LISTEN && (r->dwRemotePort != 0 || memcmp(r->ucRemoteAddr, zero, 16) != 0)) return 0;
    return r->dwState;
}

static void ns_socket_list(unsigned mask, bool all, bool listen_only, bool numeric, bool verbose) {
    wchar_t local[512], foreign[512], wstate[32];

    wprintf(L"%-6ls %6ls %6ls %-26ls %-26ls %ls%ls\n", L"Proto", L"Recv-Q", L"Send-Q", L"Local Address", L"Foreign Address", L"(state)", verbose ? L" PID" : L"");
    if (!numeric) ns_prefetch_sockets(mask);

    if (mask & NS_TCP4) {
        PMIB_TCPTABLE_OWNER_PID t = (PMIB_TCPTABLE_OWNER_PID)ns_fetch_tcp(AF_INET, TCP_TABLE_OWNER_PID_ALL);
        PMIB_TCPTABLE_OWNER_PID tl = (PMIB_TCPTABLE_OWNER_PID)ns_fetch_tcp(AF_INET, TCP_TABLE_OWNER_PID_LISTENER);
        if (t) {
            for (DWORD i = 0; i < t->dwNumEntries; i++) {
                MIB_TCPROW_OWNER_PID* r = &t->table[i];
                DWORD st = ns_tcp4_state(tl, r);
                if (!all && !listen_only && st == MIB_TCP_STATE_LISTEN) continue;
                if (listen_only && st != MIB_TCP_STATE_LISTEN) continue;
                ns_endpoint_v4(local, 512, r->dwLocalAddr, r->dwLocalPort, "tcp", numeric);
                ns_endpoint_v4(foreign, 512, r->dwRemoteAddr, r->dwRemotePort, "tcp", numeric);
                swprintf(wstate, 32, L"%hs", ns_tcp_state(st));
                ns_row("tcp4", local, foreign, wstate, r->dwOwningPid, verbose);
            }
        }
        free(tl);
        free(t);
    }
    if (mask & NS_TCP6) {
        PMIB_TCP6TABLE_OWNER_PID t = (PMIB_TCP6TABLE_OWNER_PID)ns_fetch_tcp(AF_INET6, TCP_TABLE_OWNER_PID_ALL);
        PMIB_TCP6TABLE_OWNER_PID tl = (PMIB_TCP6TABLE_OWNER_PID)ns_fetch_tcp(AF_INET6, TCP_TABLE_OWNER_PID_LISTENER);
        if (t) {
            for (DWORD i = 0; i < t->dwNumEntries; i++) {
                MIB_TCP6ROW_OWNER_PID* r = &t->table[i];
                DWORD st = ns_tcp6_state(tl, r);
                if (!all && !listen_only && st == MIB_TCP_STATE_LISTEN) continue;
                if (listen_only && st != MIB_TCP_STATE_LISTEN) continue;
                ns_endpoint_v6(local, 512, r->ucLocalAddr, r->dwLocalPort, "tcp", numeric);
                ns_endpoint_v6(foreign, 512, r->ucRemoteAddr, r->dwRemotePort, "tcp", numeric);
                swprintf(wstate, 32, L"%hs", ns_tcp_state(st));
                ns_row("tcp6", local, foreign, wstate, r->dwOwningPid, verbose);
            }
        }
        free(tl);
        free(t);
    }
    if (mask & NS_UDP4) {
        PMIB_UDPTABLE_OWNER_PID t = (PMIB_UDPTABLE_OWNER_PID)ns_fetch_table(false, AF_INET, UDP_TABLE_OWNER_PID);
        if (t) {
            for (DWORD i = 0; i < t->dwNumEntries; i++) {
                MIB_UDPROW_OWNER_PID* r = &t->table[i];
                ns_endpoint_v4(local, 512, r->dwLocalAddr, r->dwLocalPort, "udp", numeric);
                ns_row("udp4", local, L"*.*", L"", r->dwOwningPid, verbose);
            }
        }
        free(t);
    }
    if (mask & NS_UDP6) {
        PMIB_UDP6TABLE_OWNER_PID t = (PMIB_UDP6TABLE_OWNER_PID)ns_fetch_table(false, AF_INET6, UDP_TABLE_OWNER_PID);
        if (t) {
            for (DWORD i = 0; i < t->dwNumEntries; i++) {
                MIB_UDP6ROW_OWNER_PID* r = &t->table[i];
                ns_endpoint_v6(local, 512, r->ucLocalAddr, r->dwLocalPort, "udp", numeric);
                ns_row("udp6", local, L"*.*", L"", r->dwOwningPid, verbose);
            }
        }
        free(t);
    }
}

static void ns_routes(int family, bool numeric) {
    PMIB_IPFORWARD_TABLE2 t = NULL;
    wchar_t dest[128], gw[128], netif[IF_MAX_STRING_SIZE + 1];
    if (GetIpForwardTable2((ADDRESS_FAMILY)family, &t) != NO_ERROR || !t) {
        fwprintf(stderr, L"netstat: unable to read routing table\n");
        return;
    }
    if (family == AF_INET) wprintf(L"Routing tables (IPv4)\n");
    else wprintf(L"Routing tables (IPv6)\n");
    if (!numeric) {
        for (ULONG i = 0; i < t->NumEntries; i++) {
            const MIB_IPFORWARD_ROW2* r = &t->Table[i];
            if (r->DestinationPrefix.PrefixLength == (family == AF_INET ? 32 : 128))
                ns_queue(family, family == AF_INET ? (const void*)&r->DestinationPrefix.Prefix.Ipv4.sin_addr : (const void*)&r->DestinationPrefix.Prefix.Ipv6.sin6_addr);
            ns_queue(family, family == AF_INET ? (const void*)&r->NextHop.Ipv4.sin_addr : (const void*)&r->NextHop.Ipv6.sin6_addr);
        }
        ns_resolve_pending();
    }
    wprintf(L"%-30ls %-30ls %-6ls %-16ls %ls\n", L"Destination", L"Gateway", L"Flags", L"Netif", L"Expire");
    for (ULONG i = 0; i < t->NumEntries; i++) {
        const MIB_IPFORWARD_ROW2* r = &t->Table[i];
        const SOCKADDR_INET* pfx = &r->DestinationPrefix.Prefix;
        const SOCKADDR_INET* nh = &r->NextHop;
        bool is_default = r->DestinationPrefix.PrefixLength == 0;
        bool nh_zero = true;
        wchar_t flags[8] = L"U";
        wchar_t alias[IF_MAX_STRING_SIZE + 1] = L"";
        const void* daddr = family == AF_INET ? (const void*)&pfx->Ipv4.sin_addr : (const void*)&pfx->Ipv6.sin6_addr;
        const void* naddr = family == AF_INET ? (const void*)&nh->Ipv4.sin_addr : (const void*)&nh->Ipv6.sin6_addr;
        size_t alen = family == AF_INET ? 4 : 16;

        for (size_t k = 0; k < alen; k++) if (((const unsigned char*)naddr)[k]) { nh_zero = false; break; }
        if (!nh_zero) wcscat_s(flags, sizeof(flags) / sizeof(flags[0]), L"G");
        if (r->DestinationPrefix.PrefixLength == (family == AF_INET ? 32 : 128)) wcscat_s(flags, sizeof(flags) / sizeof(flags[0]), L"H");
        if (r->Protocol == MIB_IPPROTO_NETMGMT) wcscat_s(flags, sizeof(flags) / sizeof(flags[0]), L"S");

        if (is_default && family == AF_INET) {
            wcscpy_s(dest, sizeof(dest) / sizeof(dest[0]), L"default");
        } else if (r->DestinationPrefix.PrefixLength == (family == AF_INET ? 32 : 128)) {
            ns_host(dest, 128, family, daddr, numeric);
        } else {
            wchar_t a[128];
            InetNtopW((ADDRESS_FAMILY)family, (void*)daddr, a, 128);
            swprintf_s(dest, sizeof(dest) / sizeof(dest[0]), L"%ls/%u", a, (unsigned)r->DestinationPrefix.PrefixLength);
        }
        if (nh_zero) swprintf_s(gw, sizeof(gw) / sizeof(gw[0]), L"link#%lu", (unsigned long)r->InterfaceIndex);
        else ns_host(gw, 128, family, naddr, numeric);
        {
            MIB_IF_ROW2 ifr;
            memset(&ifr, 0, sizeof(ifr));
            ifr.InterfaceLuid = r->InterfaceLuid;
            if (GetIfEntry2(&ifr) == NO_ERROR) wcscpy_s(alias, sizeof(alias) / sizeof(alias[0]), ifr.Alias);
            else wcscpy_s(alias, sizeof(alias) / sizeof(alias[0]), L"?");
        }
        wcscpy_s(netif, sizeof(netif) / sizeof(netif[0]), alias);
        wprintf(L"%-30ls %-30ls %-6ls %-16ls %ls\n", dest, gw, flags, netif, L"-");
    }
    FreeMibTable(t);
}

static void ns_interfaces(const wchar_t* only, bool drops, bool numeric) {
    PMIB_IF_TABLE2 t = NULL;
    (void)numeric;
    if (GetIfTable2(&t) != NO_ERROR || !t) {
        fwprintf(stderr, L"netstat: unable to read interface table\n");
        return;
    }
    wprintf(L"%-16ls %6ls %-16ls %-18ls %10ls %6ls %12ls %10ls %6ls %12ls %4ls",
        L"Name", L"Mtu", L"Network", L"Address", L"Ipkts", L"Ierrs", L"Ibytes", L"Opkts", L"Oerrs", L"Obytes", L"Coll");
    if (drops) wprintf(L" %6ls", L"Idrop");
    wprintf(L"\n");
    for (ULONG i = 0; i < t->NumEntries; i++) {
        const MIB_IF_ROW2* r = &t->Table[i];
        wchar_t name[IF_MAX_STRING_SIZE + 1];
        wchar_t net[32], mac[64];
        if (only && _wcsicmp(r->Alias, only) != 0) continue;
        wcscpy_s(name, sizeof(name) / sizeof(name[0]), r->Alias);
        for (wchar_t* p = name; *p; p++) if (*p == L' ') *p = L'_';
        swprintf_s(net, sizeof(net) / sizeof(net[0]), L"<Link#%lu>", (unsigned long)r->InterfaceIndex);
        if (r->PhysicalAddressLength) net_fmt_ether(mac, 64, r->PhysicalAddress, (int)r->PhysicalAddressLength, 0);
        else wcscpy_s(mac, sizeof(mac) / sizeof(mac[0]), L"-");
        wprintf(L"%-16ls %6lu %-16ls %-18ls %10llu %6llu %12llu %10llu %6llu %12llu %4d",
            name, (unsigned long)r->Mtu, net, mac,
            (unsigned long long)(r->InUcastPkts + r->InNUcastPkts), (unsigned long long)r->InErrors, (unsigned long long)r->InOctets,
            (unsigned long long)(r->OutUcastPkts + r->OutNUcastPkts), (unsigned long long)r->OutErrors, (unsigned long long)r->OutOctets, 0);
        if (drops) wprintf(L" %6llu", (unsigned long long)r->InDiscards);
        wprintf(L"\n");
    }
    FreeMibTable(t);
}

static void ns_totals(const wchar_t* only, unsigned long long v[7]) {
    PMIB_IF_TABLE2 t = NULL;
    for (int k = 0; k < 7; k++) v[k] = 0;
    if (GetIfTable2(&t) != NO_ERROR || !t) return;
    for (ULONG i = 0; i < t->NumEntries; i++) {
        const MIB_IF_ROW2* r = &t->Table[i];
        if (only && _wcsicmp(r->Alias, only) != 0) continue;
        v[0] += r->InUcastPkts + r->InNUcastPkts;
        v[1] += r->InErrors;
        v[2] += r->InOctets;
        v[3] += r->OutUcastPkts + r->OutNUcastPkts;
        v[4] += r->OutErrors;
        v[5] += r->OutOctets;
        v[6] += 0;
    }
    FreeMibTable(t);
}

static void ns_watch(const wchar_t* only, unsigned long wait_sec, unsigned long long howmany, bool has_count) {
    unsigned long long prev[7], cur[7];
    unsigned long long n = 0;
    wprintf(L"%12ls %8ls %12ls %12ls %8ls %12ls\n", L"input", L"errs", L"bytes", L"output", L"errs", L"bytes");
    ns_totals(only, prev);
    wprintf(L"%12llu %8llu %12llu %12llu %8llu %12llu\n", prev[0], prev[1], prev[2], prev[3], prev[4], prev[5]);
    while (!has_count || n < howmany) {
        Sleep(wait_sec * 1000);
        ns_totals(only, cur);
        wprintf(L"%12llu %8llu %12llu %12llu %8llu %12llu\n",
            cur[0] - prev[0], cur[1] - prev[1], cur[2] - prev[2], cur[3] - prev[3], cur[4] - prev[4], cur[5] - prev[5]);
        for (int k = 0; k < 7; k++) prev[k] = cur[k];
        n++;
    }
}

static void ns_stats_ip(ULONG fam) {
    MIB_IPSTATS s;
    if (GetIpStatisticsEx(&s, fam) != NO_ERROR) { fwprintf(stderr, L"netstat: unable to read IP statistics\n"); return; }
    wprintf(L"%hs:\n", fam == AF_INET6 ? "ip6" : "ip");
    wprintf(L"\t%lu total packets received\n", (unsigned long)s.dwInReceives);
    wprintf(L"\t%lu packets with bad header\n", (unsigned long)s.dwInHdrErrors);
    wprintf(L"\t%lu packets with bad destination address\n", (unsigned long)s.dwInAddrErrors);
    wprintf(L"\t%lu packets forwarded\n", (unsigned long)s.dwForwDatagrams);
    wprintf(L"\t%lu packets with unknown protocol\n", (unsigned long)s.dwInUnknownProtos);
    wprintf(L"\t%lu packets delivered\n", (unsigned long)s.dwInDelivers);
    wprintf(L"\t%lu packets sent from this host\n", (unsigned long)s.dwOutRequests);
    wprintf(L"\t%lu output packets discarded\n", (unsigned long)s.dwOutDiscards);
    wprintf(L"\t%lu packets dropped due to no route\n", (unsigned long)s.dwOutNoRoutes);
    wprintf(L"\t%lu fragments received\n", (unsigned long)s.dwReasmReqds);
    wprintf(L"\t%lu datagrams reassembled\n", (unsigned long)s.dwReasmOks);
}

static void ns_stats_tcp(ULONG fam) {
    MIB_TCPSTATS s;
    if (GetTcpStatisticsEx(&s, fam) != NO_ERROR) { fwprintf(stderr, L"netstat: unable to read TCP statistics\n"); return; }
    wprintf(L"tcp:\n");
    wprintf(L"\t%lu active connection openings\n", (unsigned long)s.dwActiveOpens);
    wprintf(L"\t%lu passive connection openings\n", (unsigned long)s.dwPassiveOpens);
    wprintf(L"\t%lu failed connection attempts\n", (unsigned long)s.dwAttemptFails);
    wprintf(L"\t%lu connection resets received\n", (unsigned long)s.dwEstabResets);
    wprintf(L"\t%lu connections currently established\n", (unsigned long)s.dwCurrEstab);
    wprintf(L"\t%lu segments received\n", (unsigned long)s.dwInSegs);
    wprintf(L"\t%lu segments sent\n", (unsigned long)s.dwOutSegs);
    wprintf(L"\t%lu segments retransmitted\n", (unsigned long)s.dwRetransSegs);
    wprintf(L"\t%lu bad segments received\n", (unsigned long)s.dwInErrs);
    wprintf(L"\t%lu resets sent\n", (unsigned long)s.dwOutRsts);
}

static void ns_stats_udp(ULONG fam) {
    MIB_UDPSTATS s;
    if (GetUdpStatisticsEx(&s, fam) != NO_ERROR) { fwprintf(stderr, L"netstat: unable to read UDP statistics\n"); return; }
    wprintf(L"udp:\n");
    wprintf(L"\t%lu datagrams received\n", (unsigned long)s.dwInDatagrams);
    wprintf(L"\t%lu datagrams to port with no listener\n", (unsigned long)s.dwNoPorts);
    wprintf(L"\t%lu receive errors\n", (unsigned long)s.dwInErrors);
    wprintf(L"\t%lu datagrams output\n", (unsigned long)s.dwOutDatagrams);
}

static void ns_usage(void) {
    wprintf(L"Usage: netstat [-46alnSvW] [-f family] [-p proto]\n"
            L"       netstat -i [-dn] [-I interface]\n"
            L"       netstat -r [-46n] [-f family]\n"
            L"       netstat -s [-46v] [-f family] [-p proto]\n"
            L"       netstat -w wait [-I interface] [-q count]\n"
            L"Show network status and statistics.\n\n"
            L"  -4 / -6         Restrict to IPv4 / IPv6 (same as -f inet / -f inet6)\n"
            L"  -a              Show all sockets, including listening ones\n"
            L"  -d              With -i: show the Idrop (discarded inbound) column\n"
            L"  -f family       Address family: inet, inet4, inet6, link\n"
            L"  -i              Show per-interface packet and byte counters\n"
            L"  -I interface    Restrict -i or -w output to one interface\n"
            L"  -l              Show only listening sockets\n"
            L"  -n              Show addresses and ports numerically (no name lookups)\n"
            L"  -p proto        Protocol: tcp, tcp4, tcp6, udp, udp4, udp6, ip\n"
            L"  -q count        With -w: stop after count samples\n"
            L"  -r              Show the routing table\n"
            L"  -S              Show ports symbolically (default)\n"
            L"  -s              Show protocol statistics\n"
            L"  -v              Verbose: add owning process ID to socket output\n"
            L"  -W              Do not truncate addresses (addresses are never truncated)\n"
            L"  -w wait         Repeat interface counters every wait seconds\n");
}

bool Builtin_Netstat(CommandAST* cmd, int* code) {
    bool opt_a = false, opt_l = false, opt_n = false, opt_v = false, opt_i = false, opt_r = false;
    bool opt_s = false, opt_d = false, has_wait = false, has_count = false;
    unsigned long wait_sec = 0;
    unsigned long long howmany = 0;
    unsigned mask = NS_TCP4 | NS_TCP6 | NS_UDP4 | NS_UDP6;
    unsigned mask_set = 0;
    int family = 0;               /* 0 = both, AF_INET, AF_INET6 */
    const wchar_t* iface = NULL;

    if (cmd->argc > 1 && (wcscmp(cmd->argv[1], L"-h") == 0 || wcscmp(cmd->argv[1], L"--help") == 0 || wcscmp(cmd->argv[1], L"/?") == 0)) {
        ns_usage();
        *code = 0;
        return true;
    }

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] != L'-' || arg[1] == 0) {
            fwprintf(stderr, L"netstat: unexpected argument: %ls\n", arg);
            ns_usage();
            *code = 1;
            return true;
        }
        for (const wchar_t* c = arg + 1; *c; c++) {
            switch (*c) {
            case '4': family = AF_INET; break;
            case '6': family = AF_INET6; break;
            case 'a': opt_a = true; break;
            case 'd': opt_d = true; break;
            case 'i': opt_i = true; break;
            case 'l': opt_l = true; break;
            case 'n': opt_n = true; break;
            case 'S': case 'W': break;
            case 'r': opt_r = true; break;
            case 's': opt_s = true; break;
            case 'v': opt_v = true; break;
            case 'f':
            case 'p':
            case 'I':
            case 'w':
            case 'q': {
                const wchar_t* val;
                if (c[1]) val = c + 1;
                else if (i + 1 < cmd->argc) val = cmd->argv[++i];
                else { fwprintf(stderr, L"netstat: -%lc requires an argument\n", *c); *code = 1; return true; }
                if (*c == 'f') {
                    if (_wcsicmp(val, L"inet") == 0 || _wcsicmp(val, L"inet4") == 0) family = AF_INET;
                    else if (_wcsicmp(val, L"inet6") == 0) family = AF_INET6;
                    else if (_wcsicmp(val, L"link") == 0) opt_i = true;
                    else { fwprintf(stderr, L"netstat: unsupported address family: %ls\n", val); *code = 1; return true; }
                } else if (*c == 'p') {
                    unsigned pm;
                    if (_wcsicmp(val, L"tcp") == 0) pm = NS_TCP4 | NS_TCP6;
                    else if (_wcsicmp(val, L"tcp4") == 0) pm = NS_TCP4;
                    else if (_wcsicmp(val, L"tcp6") == 0) pm = NS_TCP6;
                    else if (_wcsicmp(val, L"udp") == 0) pm = NS_UDP4 | NS_UDP6;
                    else if (_wcsicmp(val, L"udp4") == 0) pm = NS_UDP4;
                    else if (_wcsicmp(val, L"udp6") == 0) pm = NS_UDP6;
                    else if (_wcsicmp(val, L"ip") == 0 || _wcsicmp(val, L"ip4") == 0) pm = NS_IP;
                    else { fwprintf(stderr, L"netstat: unknown protocol: %ls\n", val); *code = 1; return true; }
                    mask_set |= pm;
                } else if (*c == 'I') {
                    iface = val;
                } else if (*c == 'w') {
                    if (!net_parse_uint(val, &wait_sec) || wait_sec == 0 || wait_sec > 86400) { fwprintf(stderr, L"netstat: invalid wait interval: %ls\n", val); *code = 1; return true; }
                    has_wait = true;
                } else {
                    unsigned long q;
                    if (!net_parse_uint(val, &q) || q == 0) { fwprintf(stderr, L"netstat: invalid count: %ls\n", val); *code = 1; return true; }
                    howmany = q;
                    has_count = true;
                }
                goto next_arg;
            }
            default:
                fwprintf(stderr, L"netstat: option -%lc is not supported on Windows\n", *c);
                *code = 1;
                return true;
            }
        }
    next_arg:
        continue;
    }

    ns_wsa_init();
    ns_ncache = 0;
    if (mask_set) mask = mask_set;
    if (family == AF_INET) mask &= (NS_TCP4 | NS_UDP4 | NS_IP);
    else if (family == AF_INET6) mask &= (NS_TCP6 | NS_UDP6);

    if (has_wait) {
        ns_watch(iface, wait_sec, howmany, has_count);
    } else if (iface && !opt_i) {
        ns_interfaces(iface, opt_d, opt_n);
    } else if (opt_i) {
        ns_interfaces(iface, opt_d, opt_n);
    } else if (opt_r) {
        if (family != AF_INET6) ns_routes(AF_INET, opt_n);
        if (family != AF_INET) ns_routes(AF_INET6, opt_n);
    } else if (opt_s) {
        ULONG fam = family == AF_INET6 ? AF_INET6 : AF_INET;
        bool any = mask_set == 0 && family == 0;
        if (any || (mask & NS_IP)) {
            if (family == 0) { ns_stats_ip(AF_INET); ns_stats_ip(AF_INET6); }
            else ns_stats_ip(fam);
        }
        if (any || (mask & (NS_TCP4 | NS_TCP6))) {
            if (family == 0) { ns_stats_tcp(AF_INET); ns_stats_tcp(AF_INET6); }
            else ns_stats_tcp(fam);
        }
        if (any || (mask & (NS_UDP4 | NS_UDP6))) {
            if (family == 0) { ns_stats_udp(AF_INET); ns_stats_udp(AF_INET6); }
            else ns_stats_udp(fam);
        }
    } else {
        ns_socket_list(mask & ~NS_IP, opt_a, opt_l, opt_n, opt_v);
    }

    *code = 0;
    return true;
}

/* ========================================================================= */
/*                                  devinfo                                  */
/* ========================================================================= */

#define DEVINFO_MAX_NODES 8192

typedef struct {
    wchar_t inst[256];
    wchar_t desc[256];
    wchar_t cls[128];
    wchar_t hwid[256];
    wchar_t driver[256];
    DEVINST dn;
    DEVINST parent;
    bool attached;
} DevNode;

typedef struct {
    int kind;                /* 0 = ioport, 1 = irq, 2 = iomem */
    unsigned long long start, end;
    int node;
} DevRes;

typedef struct {
    DevRes* items;
    size_t count, cap;
} DevResList;

static void devres_add(DevResList* l, int kind, unsigned long long s, unsigned long long e, int node) {
    if (l->count == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 64;
        DevRes* p = (DevRes*)realloc(l->items, nc * sizeof(DevRes));
        if (!p) return;
        l->items = p;
        l->cap = nc;
    }
    l->items[l->count].kind = kind;
    l->items[l->count].start = s;
    l->items[l->count].end = e;
    l->items[l->count].node = node;
    l->count++;
}

static void devres_collect(DEVINST dn, int node, DevResList* out) {
    LOG_CONF lc = 0;
    RES_DES prev, cur = 0;
    RESOURCEID rid = 0;
    bool first = true;
    if (CM_Get_First_Log_Conf(&lc, dn, ALLOC_LOG_CONF) != CR_SUCCESS) return;
    prev = (RES_DES)lc;
    while (CM_Get_Next_Res_Des(&cur, prev, ResType_All, &rid, 0) == CR_SUCCESS) {
        union {
            IO_DES io;
            MEM_DES mem;
            IRQ_DES_32 irq;
            unsigned char raw[256];
        } buf;
        memset(&buf, 0, sizeof(buf));
        if (CM_Get_Res_Des_Data(cur, &buf, sizeof(buf), 0) == CR_SUCCESS) {
            if (rid == ResType_IO) {
                devres_add(out, 0, (unsigned long long)buf.io.IOD_Alloc_Base, (unsigned long long)buf.io.IOD_Alloc_End, node);
            } else if (rid == ResType_Mem) {
                devres_add(out, 2, (unsigned long long)buf.mem.MD_Alloc_Base, (unsigned long long)buf.mem.MD_Alloc_End, node);
            } else if (rid == ResType_IRQ) {
                devres_add(out, 1, (unsigned long long)buf.irq.IRQD_Alloc_Num, (unsigned long long)buf.irq.IRQD_Alloc_Num, node);
            }
        }
        if (!first) CM_Free_Res_Des_Handle(prev);
        first = false;
        prev = cur;
    }
    if (!first) CM_Free_Res_Des_Handle(prev);
    CM_Free_Log_Conf_Handle(lc);
}

static void devres_print(const DevRes* r, const wchar_t* prefix) {
    switch (r->kind) {
    case 0: wprintf(L"%ls" L"ioport 0x%llx-0x%llx\n", prefix, r->start, r->end); break;
    case 1: wprintf(L"%ls" L"irq %llu\n", prefix, r->start); break;
    default: wprintf(L"%ls" L"iomem 0x%llx-0x%llx\n", prefix, r->start, r->end); break;
    }
}

static int devres_sort_cmp(const void* a, const void* b) {
    const DevRes* x = (const DevRes*)a;
    const DevRes* y = (const DevRes*)b;
    if (x->kind != y->kind) return x->kind - y->kind;
    if (x->start != y->start) return x->start < y->start ? -1 : 1;
    return x->node - y->node;
}

typedef struct {
    DevNode* nodes;
    int* pidx;
    bool* vis;
    int count;
    bool verbose, resources;
    DevResList res;
} DevTree;

/* Precompute visibility bottom-up (a node is shown if it or any descendant is attached);
   the bounded walk also protects against parent cycles. */
static void devinfo_compute_visible(DevTree* t) {
    for (int i = 0; i < t->count; i++) {
        if (!(t->verbose || t->nodes[i].attached)) continue;
        for (int j = i, steps = 0; j >= 0 && steps <= t->count && !t->vis[j]; j = t->pidx[j], steps++)
            t->vis[j] = true;
    }
}

static bool devinfo_visible(const DevTree* t, int i) {
    return t->vis[i];
}

static void devinfo_print_node(const DevTree* t, int i, int depth) {
    const DevNode* n = &t->nodes[i];
    wchar_t indent[256];
    int pad = depth * 2;
    if (depth > 128) return;
    if (pad > 200) pad = 200;
    for (int k = 0; k < pad; k++) indent[k] = L' ';
    indent[pad] = 0;

    if (!devinfo_visible(t, i)) return;

    if (t->verbose) {
        wprintf(L"%ls%ls  desc=\"%ls\" class=%ls hwid=%ls driver=%ls state=%ls\n", indent, n->inst, n->desc,
            n->cls[0] ? n->cls : L"-", n->hwid[0] ? n->hwid : L"-", n->driver[0] ? n->driver : L"-",
            n->attached ? L"attached" : L"unattached");
    } else {
        wprintf(L"%ls%ls\n", indent, n->inst);
    }

    if (t->resources) {
        wchar_t rpad[256];
        for (int k = 0; k < pad + 2 && k < 255; k++) rpad[k] = L' ';
        rpad[pad + 2 < 255 ? pad + 2 : 255] = 0;
        for (size_t r = 0; r < t->res.count; r++) {
            if (t->res.items[r].node == i) devres_print(&t->res.items[r], rpad);
        }
    }

    for (int j = 0; j < t->count; j++) {
        if (t->pidx[j] == i) devinfo_print_node(t, j, depth + 1);
    }
}

static void devinfo_usage(void) {
    wprintf(L"Usage: devinfo [-rv] [-u] [-v]\n"
            L"       devinfo -p dev [-v]\n"
            L"       devinfo -u [-v]\n"
            L"Display the hierarchy of devices in the system, starting from the root.\n\n"
            L"  -p dev        Print the path from dev back to the root of the device tree.\n"
            L"                dev may be an instance ID or a substring of one.\n"
            L"  -r            Also list hardware resources (IRQ, I/O ports, I/O memory) per device.\n"
            L"  -u            List resources sorted by resource type instead of by device.\n"
            L"  -v            Show all devices (not just attached ones) with verbose details.\n");
}

static bool devinfo_load(DevTree* t, bool verbose) {
    HDEVINFO hset;
    SP_DEVINFO_DATA d;
    DWORD i;
    DWORD flags = DIGCF_ALLCLASSES | (verbose ? 0 : DIGCF_PRESENT);

    t->nodes = (DevNode*)calloc(DEVINFO_MAX_NODES, sizeof(DevNode));
    t->pidx = (int*)malloc(sizeof(int) * DEVINFO_MAX_NODES);
    if (!t->nodes || !t->pidx) return false;
    t->count = 0;

    hset = SetupDiGetClassDevsW(NULL, NULL, NULL, flags);
    if (hset == INVALID_HANDLE_VALUE) return false;

    memset(&d, 0, sizeof(d));
    d.cbSize = sizeof(d);
    for (i = 0; t->count < DEVINFO_MAX_NODES && SetupDiEnumDeviceInfo(hset, i, &d); i++) {
        DevNode* n = &t->nodes[t->count];
        ULONG st = 0, pr = 0;
        DWORD type = 0;
        n->dn = d.DevInst;
        CM_Get_Device_IDW(d.DevInst, n->inst, 256, 0);
        SetupDiGetDeviceRegistryPropertyW(hset, &d, SPDRP_DEVICEDESC, &type, (PBYTE)n->desc, sizeof(n->desc), NULL);
        SetupDiGetDeviceRegistryPropertyW(hset, &d, SPDRP_CLASS, &type, (PBYTE)n->cls, sizeof(n->cls), NULL);
        SetupDiGetDeviceRegistryPropertyW(hset, &d, SPDRP_HARDWAREID, &type, (PBYTE)n->hwid, sizeof(n->hwid), NULL);
        SetupDiGetDeviceRegistryPropertyW(hset, &d, SPDRP_DRIVER, &type, (PBYTE)n->driver, sizeof(n->driver), NULL);
        n->attached = CM_Get_DevNode_Status(&st, &pr, d.DevInst, 0) == CR_SUCCESS && (st & DN_STARTED) != 0;
        n->inst[255] = n->desc[255] = n->hwid[255] = n->driver[255] = n->cls[127] = 0;
        if (CM_Get_Parent(&n->parent, d.DevInst, 0) != CR_SUCCESS) n->parent = 0;
        t->count++;
        memset(&d, 0, sizeof(d));
        d.cbSize = sizeof(d);
    }
    SetupDiDestroyDeviceInfoList(hset);

    for (int a = 0; a < t->count; a++) {
        t->pidx[a] = -1;
        for (int b = 0; b < t->count; b++) {
            if (t->nodes[b].dn == t->nodes[a].parent && b != a) { t->pidx[a] = b; break; }
        }
    }
    return true;
}

static void devinfo_free(DevTree* t) {
    free(t->nodes);
    free(t->pidx);
    free(t->vis);
    free(t->res.items);
}

bool Builtin_Devinfo(CommandAST* cmd, int* code) {
    DevTree t;
    bool opt_r = false, opt_u = false, opt_v = false;
    const wchar_t* path_dev = NULL;

    memset(&t, 0, sizeof(t));

    if (cmd->argc > 1 && (wcscmp(cmd->argv[1], L"-h") == 0 || wcscmp(cmd->argv[1], L"--help") == 0 || wcscmp(cmd->argv[1], L"/?") == 0)) {
        devinfo_usage();
        *code = 0;
        return true;
    }

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] != L'-' || arg[1] == 0) {
            fwprintf(stderr, L"devinfo: unexpected argument: %ls\n", arg);
            devinfo_usage();
            *code = 1;
            return true;
        }
        for (const wchar_t* c = arg + 1; *c; c++) {
            switch (*c) {
            case 'r': opt_r = true; break;
            case 'u': opt_u = true; break;
            case 'v': opt_v = true; break;
            case 'p':
                if (c[1]) path_dev = c + 1;
                else if (i + 1 < cmd->argc) path_dev = cmd->argv[++i];
                else { fwprintf(stderr, L"devinfo: -p requires a device\n"); *code = 1; return true; }
                if (!*path_dev) { fwprintf(stderr, L"devinfo: -p requires a device\n"); *code = 1; return true; }
                goto next_arg;
            default:
                fwprintf(stderr, L"devinfo: illegal option -- %lc\n", *c);
                devinfo_usage();
                *code = 1;
                return true;
            }
        }
    next_arg:
        continue;
    }

    if (!devinfo_load(&t, opt_v)) {
        fwprintf(stderr, L"devinfo: unable to enumerate devices\n");
        devinfo_free(&t);
        *code = 1;
        return true;
    }

    if (path_dev) {
        int idx = -1;
        for (int i = 0; i < t.count && idx < 0; i++) {
            if (_wcsicmp(t.nodes[i].inst, path_dev) == 0) idx = i;
        }
        for (int i = 0; i < t.count && idx < 0; i++) {
            if (wcsstr(t.nodes[i].inst, path_dev)) idx = i;
        }
        if (idx < 0) {
            fwprintf(stderr, L"devinfo: %ls: device not found\n", path_dev);
            devinfo_free(&t);
            *code = 1;
            return true;
        }
        for (int cur = idx, steps = 0; cur >= 0 && steps <= t.count; cur = t.pidx[cur], steps++) {
            if (opt_v) wprintf(L"%ls  %ls\n", t.nodes[cur].inst, t.nodes[cur].desc);
            else wprintf(L"%ls%ls", cur == idx ? L"" : L" ", t.nodes[cur].inst);
        }
        if (!opt_v) wprintf(L"\n");
        devinfo_free(&t);
        *code = 0;
        return true;
    }

    if (opt_r || opt_u) {
        for (int i = 0; i < t.count; i++) {
            if (t.nodes[i].attached || opt_v) devres_collect(t.nodes[i].dn, i, &t.res);
        }
    }

    if (opt_u) {
        if (t.res.count) qsort(t.res.items, t.res.count, sizeof(DevRes), devres_sort_cmp);
        wprintf(L"Resources by type:\n");
        for (size_t r = 0; r < t.res.count; r++) {
            wprintf(L"  ");
            devres_print(&t.res.items[r], L"  ");
            wprintf(L"    %ls\n", t.nodes[t.res.items[r].node].inst);
        }
        devinfo_free(&t);
        *code = 0;
        return true;
    }

    t.verbose = opt_v;
    t.resources = opt_r;
    t.vis = (bool*)calloc(t.count ? (size_t)t.count : 1, sizeof(bool));
    if (!t.vis) {
        fwprintf(stderr, L"devinfo: out of memory\n");
        devinfo_free(&t);
        *code = 1;
        return true;
    }
    devinfo_compute_visible(&t);
    for (int i = 0; i < t.count; i++) {
        if (t.pidx[i] < 0) devinfo_print_node(&t, i, 0);
    }

    devinfo_free(&t);
    *code = 0;
    return true;
}

/* ========================================================================= */
/*                          curl & wget (WinHTTP)                            */
/* ========================================================================= */

static int NetDownloadUrl(const wchar_t* url,
                          const wchar_t* out_file,
                          bool remote_name,
                          bool silent,
                          bool verbose,
                          bool head_only,
                          bool insecure,
                          const wchar_t* method_override,
                          const wchar_t* post_data,
                          const wchar_t* headers_extra)
{
    if (!url || !*url) {
        if (!silent) fwprintf(stderr, L"curl: no URL specified\n");
        return 1;
    }

    wchar_t wurl[2048];
    if (wcsncmp(url, L"http://", 7) != 0 && wcsncmp(url, L"https://", 8) != 0) {
        swprintf(wurl, _countof(wurl), L"http://%ls", url);
    } else {
        wcsncpy_s(wurl, _countof(wurl), url, _countof(wurl) - 1);
    }

    URL_COMPONENTS urlComp;
    memset(&urlComp, 0, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = {0};
    wchar_t urlPath[2048] = {0};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = _countof(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = _countof(urlPath);

    if (!WinHttpCrackUrl(wurl, (DWORD)wcslen(wurl), 0, &urlComp)) {
        if (!silent) fwprintf(stderr, L"curl: malformed URL '%ls'\n", url);
        return 1;
    }

    HINTERNET hSession = WinHttpOpen(L"curl/7.88.1 (ntksh; Windows NT)",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        if (!silent) fwprintf(stderr, L"curl: failed to initialize WinHTTP\n");
        return 1;
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        if (!silent) fwprintf(stderr, L"curl: could not connect to '%ls'\n", hostName);
        WinHttpCloseHandle(hSession);
        return 1;
    }

    const wchar_t* method = L"GET";
    if (head_only) method = L"HEAD";
    else if (post_data) method = L"POST";
    if (method_override && *method_override) method = method_override;

    DWORD reqFlags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, method,
                                           urlPath[0] ? urlPath : L"/",
                                           NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           reqFlags);
    if (!hRequest) {
        if (!silent) fwprintf(stderr, L"curl: failed to create request\n");
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 1;
    }

    if (insecure && (reqFlags & WINHTTP_FLAG_SECURE)) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    if (headers_extra && *headers_extra) {
        WinHttpAddRequestHeaders(hRequest, headers_extra, (DWORD)-1,
                                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    char* postBody = NULL;
    DWORD postLen = 0;
    if (post_data) {
        int need = WideCharToMultiByte(CP_UTF8, 0, post_data, -1, NULL, 0, NULL, NULL);
        if (need > 1) {
            postBody = (char*)malloc(need);
            if (postBody) {
                WideCharToMultiByte(CP_UTF8, 0, post_data, -1, postBody, need, NULL, NULL);
                postLen = (DWORD)(need - 1);
            }
        }
    }

    BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  postBody, postLen, postLen, 0);
    if (!sent || !WinHttpReceiveResponse(hRequest, NULL)) {
        if (!silent) fwprintf(stderr, L"curl: send/receive failed (%lu)\n", GetLastError());
        if (postBody) free(postBody);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 1;
    }
    if (postBody) free(postBody);

    if (verbose || head_only) {
        DWORD hdrSize = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX, NULL, &hdrSize, WINHTTP_NO_HEADER_INDEX);
        if (hdrSize > 0) {
            wchar_t* hdrBuf = (wchar_t*)malloc(hdrSize);
            if (hdrBuf) {
                if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                        WINHTTP_HEADER_NAME_BY_INDEX, hdrBuf, &hdrSize, WINHTTP_NO_HEADER_INDEX)) {
                    fputws(hdrBuf, head_only ? stdout : stderr);
                }
                free(hdrBuf);
            }
        }
    }

    if (head_only) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

    FILE* outFp = stdout;
    bool mustClose = false;
    wchar_t resolvedFile[MAX_PATH] = {0};

    if (remote_name && (!out_file || !*out_file)) {
        const wchar_t* p = wcsrchr(urlPath, L'/');
        if (p && *(p + 1)) {
            wcsncpy_s(resolvedFile, _countof(resolvedFile), p + 1, _countof(resolvedFile) - 1);
            wchar_t* q = wcschr(resolvedFile, L'?');
            if (q) *q = L'\0';
        }
        if (!resolvedFile[0]) wcscpy_s(resolvedFile, _countof(resolvedFile), L"index.html");
        out_file = resolvedFile;
    }

    if (out_file && *out_file && wcscmp(out_file, L"-") != 0) {
        outFp = _wfsopen(out_file, L"wb", _SH_DENYWR);
        if (!outFp) {
            if (!silent) fwprintf(stderr, L"curl: cannot open '%ls' for writing\n", out_file);
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return 1;
        }
        mustClose = true;
    }

    char buffer[8192];
    DWORD bytesRead = 0;
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        fwrite(buffer, 1, bytesRead, outFp);
    }

    if (mustClose) fclose(outFp);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return 0;
}

bool Builtin_Curl(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: curl [options...] <url>\n"
                L"Transfer data from or to a server (HTTP / HTTPS).\n\n"
                L"  -s, --silent          Silent mode (don't output anything to stderr)\n"
                L"  -o, --output <file>   Write to file instead of stdout\n"
                L"  -O, --remote-name     Write output to a local file named like the remote file\n"
                L"  -H, --header <header> Pass custom header(s) to server\n"
                L"  -X, --request <cmd>   Specifies a custom request method (GET, POST, etc.)\n"
                L"  -d, --data <data>     HTTP POST data\n"
                L"  -I, --head            Show document info / headers only\n"
                L"  -k, --insecure        Allow insecure server connections\n"
                L"  -v, --verbose         Make the operation more talkative\n");
        *code = 0;
        return true;
    }

    bool silent = false, verbose = false, head_only = false, insecure = false, remote_name = false;
    const wchar_t* out_file = NULL;
    const wchar_t* method = NULL;
    const wchar_t* post_data = NULL;
    wchar_t headers_buf[4096] = {0};
    const wchar_t* target_url = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && wcscmp(arg, L"-") != 0) {
            if (wcscmp(arg, L"-s") == 0 || wcscmp(arg, L"--silent") == 0) silent = true;
            else if (wcscmp(arg, L"-v") == 0 || wcscmp(arg, L"--verbose") == 0) verbose = true;
            else if (wcscmp(arg, L"-I") == 0 || wcscmp(arg, L"--head") == 0) head_only = true;
            else if (wcscmp(arg, L"-k") == 0 || wcscmp(arg, L"--insecure") == 0) insecure = true;
            else if (wcscmp(arg, L"-O") == 0 || wcscmp(arg, L"--remote-name") == 0) remote_name = true;
            else if ((wcscmp(arg, L"-o") == 0 || wcscmp(arg, L"--output") == 0) && i + 1 < cmd->argc) {
                out_file = cmd->argv[++i];
            } else if ((wcscmp(arg, L"-X") == 0 || wcscmp(arg, L"--request") == 0) && i + 1 < cmd->argc) {
                method = cmd->argv[++i];
            } else if ((wcscmp(arg, L"-d") == 0 || wcscmp(arg, L"--data") == 0) && i + 1 < cmd->argc) {
                post_data = cmd->argv[++i];
            } else if ((wcscmp(arg, L"-H") == 0 || wcscmp(arg, L"--header") == 0) && i + 1 < cmd->argc) {
                const wchar_t* h = cmd->argv[++i];
                if (headers_buf[0]) wcscat_s(headers_buf, _countof(headers_buf), L"\r\n");
                wcscat_s(headers_buf, _countof(headers_buf), h);
            }
        } else {
            target_url = arg;
        }
    }

    if (!target_url) {
        if (!silent) fwprintf(stderr, L"curl: try 'curl --help' for more information\n");
        *code = 2;
        return true;
    }

    *code = NetDownloadUrl(target_url, out_file, remote_name, silent, verbose, head_only, insecure, method, post_data, headers_buf);
    return true;
}

bool Builtin_Wget(CommandAST* cmd, int* code) {
    if (cmd->argc > 1 && (_wcsicmp(cmd->argv[1], L"/?") == 0 || wcscmp(cmd->argv[1], L"--help") == 0)) {
        wprintf(L"Usage: wget [options...] <url>\n"
                L"The non-interactive network downloader (HTTP / HTTPS).\n\n"
                L"  -O, --output-document=<file> Write documents to file (use '-' for stdout)\n"
                L"  -q, --quiet                  Quiet (no output)\n"
                L"  --no-check-certificate        Don't validate the server's certificate\n");
        *code = 0;
        return true;
    }

    bool quiet = false, insecure = false;
    const wchar_t* out_file = NULL;
    const wchar_t* target_url = NULL;

    for (int i = 1; i < cmd->argc; i++) {
        const wchar_t* arg = cmd->argv[i];
        if (arg[0] == L'-' && arg[1] != L'\0' && wcscmp(arg, L"-") != 0) {
            if (wcscmp(arg, L"-q") == 0 || wcscmp(arg, L"--quiet") == 0) quiet = true;
            else if (wcscmp(arg, L"--no-check-certificate") == 0) insecure = true;
            else if ((wcscmp(arg, L"-O") == 0 || wcscmp(arg, L"--output-document") == 0) && i + 1 < cmd->argc) {
                out_file = cmd->argv[++i];
            } else if (wcsncmp(arg, L"--output-document=", 18) == 0) {
                out_file = arg + 18;
            }
        } else {
            target_url = arg;
        }
    }

    if (!target_url) {
        if (!quiet) fwprintf(stderr, L"wget: missing URL\nUsage: wget [OPTION]... [URL]...\n");
        *code = 1;
        return true;
    }

    bool remote_name = (out_file == NULL);
    *code = NetDownloadUrl(target_url, out_file, remote_name, quiet, false, false, insecure, L"GET", NULL, NULL);
    return true;
}
