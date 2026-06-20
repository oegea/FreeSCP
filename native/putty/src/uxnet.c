/*
 * uxnet.c — BSD-socket network backend for the WinSCP PuTTY fork (replaces windows/network.c).
 *
 * Implements the Socket/Plug vtable over POSIX sockets with non-blocking async connect and
 * bufchain output buffering. select_result(fd, events) dispatches readiness to the Plug;
 * winscp_net_select(ms) runs one select() pass over all live sockets (called by the engine's
 * event loop). do_select/first_socket/next_socket round out the WinSCP platform contract.
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/select.h>

#include "putty.h"
#include "network.h"

struct SockAddr {
    int refcount;
    char *error;
    struct addrinfo *ais;     /* getaddrinfo result (owned) */
    char hostname[512];
};

typedef struct NetSocket {
    Socket sock;              /* vt first */
    Plug *plug;
    int fd;
    bool connected;
    bool frozen;
    bool pending_error;
    int  pending_errno;
    char *error;
    bufchain output;
    SockAddr *addr;
    struct addrinfo *step;    /* current candidate while connecting */
    int port;
    struct NetSocket *next, *prev;
} NetSocket;

static NetSocket *g_head = NULL;

static void ns_link(NetSocket *s)   { s->next = g_head; s->prev = NULL; if (g_head) g_head->prev = s; g_head = s; }
static void ns_unlink(NetSocket *s) { if (s->prev) s->prev->next = s->next; else g_head = s->next; if (s->next) s->next->prev = s->prev; }
static NetSocket *ns_find(int fd)   { for (NetSocket *s = g_head; s; s = s->next) if (s->fd == fd) return s; return NULL; }

/* ---------- init ---------- */
void sk_init(void) {}
void sk_cleanup(void) {}

/* ---------- address resolution ---------- */
SockAddr *sk_namelookup(const char *host, char **canonical, int address_family)
{
    SockAddr *a = snew(SockAddr);
    memset(a, 0, sizeof(*a));
    a->refcount = 1;
    strncpy(a->hostname, host, sizeof(a->hostname) - 1);
    struct addrinfo hints; memset(&hints, 0, sizeof(hints));
    hints.ai_family = (address_family == 1 ? AF_INET : address_family == 2 ? AF_INET6 : AF_UNSPEC);
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;
    int rc = getaddrinfo(host, NULL, &hints, &a->ais);
    if (rc != 0) { a->error = dupstr(gai_strerror(rc)); a->ais = NULL; }
    if (canonical) *canonical = dupstr(a->ais && a->ais->ai_canonname ? a->ais->ai_canonname : host);
    return a;
}
SockAddr *sk_nonamelookup(const char *host)
{
    SockAddr *a = snew(SockAddr); memset(a, 0, sizeof(*a));
    a->refcount = 1; strncpy(a->hostname, host, sizeof(a->hostname) - 1);
    return a;
}
void sk_getaddr(SockAddr *addr, char *buf, int buflen)
{
    if (addr->ais) {
        if (getnameinfo(addr->ais->ai_addr, addr->ais->ai_addrlen, buf, buflen, NULL, 0, NI_NUMERICHOST) != 0)
            strncpy(buf, addr->hostname, buflen - 1);
    } else strncpy(buf, addr->hostname, buflen - 1);
    buf[buflen - 1] = '\0';
}
const char *sk_addr_error(SockAddr *addr) { return addr->error; }
bool sk_addr_needs_port(SockAddr *addr) { (void)addr; return true; }
int sk_addrtype(SockAddr *addr) { return addr->ais && addr->ais->ai_family == AF_INET6 ? 2 : 1; }
void sk_addrcopy(SockAddr *addr, char *buf)
{ if (addr->ais) memcpy(buf, addr->ais->ai_addr, addr->ais->ai_addrlen); }
SockAddr *sk_addr_dup(SockAddr *addr) { if (addr) addr->refcount++; return addr; }
void sk_addr_free(SockAddr *addr)
{
    if (!addr) return;
    if (--addr->refcount > 0) return;
    if (addr->ais) freeaddrinfo(addr->ais);
    sfree(addr->error); sfree(addr);
}
bool sk_hostname_is_local(const char *name)
{ return !name || !*name || !strcmp(name, "localhost") || !strcmp(name, "127.0.0.1") || !strcmp(name, "::1"); }
bool sk_address_is_local(SockAddr *addr) { return sk_hostname_is_local(addr->hostname); }
bool sk_address_is_special_local(SockAddr *addr) { (void)addr; return false; }

/* ---------- socket vtable ---------- */
static void set_nonblock(int fd) { int fl = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, fl | O_NONBLOCK); }

static void ns_try_flush(NetSocket *s)
{
    while (bufchain_size(&s->output) > 0) {
        ptrlen pl = bufchain_prefix(&s->output);
        ssize_t n = send(s->fd, pl.ptr, pl.len, 0);
        if (n > 0) bufchain_consume(&s->output, n);
        else break;   /* EAGAIN or error: stop, wait for writable */
    }
}

static Plug *ns_plug(Socket *ss, Plug *p) { NetSocket *s = (NetSocket *)ss; Plug *old = s->plug; if (p) s->plug = p; return old; }
static void ns_close(Socket *ss)
{
    NetSocket *s = (NetSocket *)ss;
    ns_unlink(s);
    if (s->fd >= 0) close(s->fd);
    bufchain_clear(&s->output);
    sk_addr_free(s->addr);
    sfree(s->error);
    sfree(s);
}
static size_t ns_write(Socket *ss, const void *data, size_t len)
{
    NetSocket *s = (NetSocket *)ss;
    bufchain_add(&s->output, data, len);
    if (s->connected) ns_try_flush(s);
    return bufchain_size(&s->output);
}
static size_t ns_write_oob(Socket *ss, const void *data, size_t len) { return ns_write(ss, data, len); }
static void ns_write_eof(Socket *ss) { NetSocket *s = (NetSocket *)ss; if (bufchain_size(&s->output) == 0) shutdown(s->fd, SHUT_WR); }
static void ns_set_frozen(Socket *ss, bool frozen) { ((NetSocket *)ss)->frozen = frozen; }
static const char *ns_socket_error(Socket *ss) { return ((NetSocket *)ss)->error; }
static SocketEndpointInfo *ns_endpoint_info(Socket *ss, bool peer) { (void)ss; (void)peer; return NULL; }

static const SocketVtable NetSocket_vt = {
    ns_plug, ns_close, ns_write, ns_write_oob, ns_write_eof,
    ns_set_frozen, ns_socket_error, ns_endpoint_info,
};

/* start a non-blocking connect to the current candidate address */
static bool ns_start_connect(NetSocket *s)
{
    for (; s->step; s->step = s->step->ai_next) {
        int fd = socket(s->step->ai_family, SOCK_STREAM, 0);
        if (fd < 0) continue;
        set_nonblock(fd);
        int one = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        /* set the port into the sockaddr */
        struct sockaddr_storage ss; socklen_t sl = s->step->ai_addrlen;
        memcpy(&ss, s->step->ai_addr, sl);
        if (ss.ss_family == AF_INET) ((struct sockaddr_in *)&ss)->sin_port = htons(s->port);
        else if (ss.ss_family == AF_INET6) ((struct sockaddr_in6 *)&ss)->sin6_port = htons(s->port);
        plug_log(s->plug, &s->sock, PLUGLOG_CONNECT_TRYING, s->addr, s->port, NULL, 0);
        int rc = connect(fd, (struct sockaddr *)&ss, sl);
        if (rc == 0) { s->fd = fd; s->connected = true; plug_log(s->plug, &s->sock, PLUGLOG_CONNECT_SUCCESS, s->addr, s->port, NULL, 0); return true; }
        if (errno == EINPROGRESS) { s->fd = fd; return true; }  /* wait for writable */
        close(fd);
    }
    s->error = dupstr("unable to connect");
    return false;
}

Socket *sk_new(SockAddr *addr, int port, bool privport, bool oobinline,
               bool nodelay, bool keepalive, Plug *p
#ifdef MPEXT
               , int timeout, int sndbuf, const char *srcaddr
#endif
              )
{
    (void)privport; (void)oobinline; (void)nodelay; (void)keepalive;
#ifdef MPEXT
    (void)timeout; (void)sndbuf; (void)srcaddr;
#endif
    NetSocket *s = snew(NetSocket);
    memset(s, 0, sizeof(*s));
    s->sock.vt = &NetSocket_vt;
    s->plug = p;
    s->fd = -1;
    s->addr = sk_addr_dup(addr);
    s->step = addr ? addr->ais : NULL;
    s->port = port;
    bufchain_init(&s->output);
    if (addr && addr->error) { s->error = dupstr(addr->error); }
    else if (!ns_start_connect(s)) { /* error set */ }
    if (s->fd >= 0) {
        ns_link(s);
        /* Register the fd with the engine's event machinery (WinSCP's do_select -> SecureShell ->
         * WSAEventSelect). Without this the socket is never serviced and the connect never
         * completes. Mirrors windows/network.c, which calls do_select on every new socket. */
        const char *err = do_select(s->plug, s->fd, true);
        if (err && !s->error) s->error = dupstr(err);
    }
    return &s->sock;
}

Socket *sk_newlistener(const char *srcaddr, int port, Plug *plug, bool local_host_only, int address_family)
{ (void)srcaddr; (void)port; (void)plug; (void)local_host_only; (void)address_family; return NULL; }

/* ---------- select integration ----------
 * do_select is provided by WinSCP's PuttyIntf.cpp (routes socket events into the engine), so it
 * is not defined here (would duplicate). */
void select_result(WPARAM wParam, LPARAM lParam)
{
    int fd = (int)wParam;
    int event = (int)lParam;
    NetSocket *s = ns_find(fd);
    if (!s) return;

    if (!s->connected && (event & (FD_CONNECT | FD_WRITE))) {
        int err = 0; socklen_t el = sizeof(err);
        getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &err, &el);
        if (err == 0) { s->connected = true; plug_log(s->plug, &s->sock, PLUGLOG_CONNECT_SUCCESS, s->addr, s->port, NULL, 0); ns_try_flush(s); }
        else { plug_log(s->plug, &s->sock, PLUGLOG_CONNECT_FAILED, s->addr, s->port, strerror(err), err);
               s->step = s->step ? s->step->ai_next : NULL; close(s->fd); s->fd = -1;
               if (!ns_start_connect(s)) plug_closing_error(s->plug, s->error ? s->error : "connect failed"); }
        return;
    }
    if (s->connected && (event & FD_WRITE)) ns_try_flush(s);
    if (s->connected && (event & FD_READ) && !s->frozen) {
        char buf[16384];
        ssize_t n = recv(s->fd, buf, sizeof(buf), 0);
        if (n > 0) plug_receive(s->plug, 0, buf, (size_t)n);
        else if (n == 0) plug_closing_normal(s->plug);
        else if (errno != EAGAIN && errno != EWOULDBLOCK) plug_closing_error(s->plug, strerror(errno));
    }
}

/* iterate live socket fds (WinSCP event-loop helper) */
SOCKET first_socket(int *state) { *state = 0; return g_head ? g_head->fd : INVALID_SOCKET; }
SOCKET next_socket(int *state)
{
    int idx = ++(*state), i = 0;
    for (NetSocket *s = g_head; s; s = s->next, i++) if (i == idx) return s->fd;
    return INVALID_SOCKET;
}
int socket_writable(SOCKET skt) { NetSocket *s = ns_find(skt); return s && (!s->connected || bufchain_size(&s->output) > 0); }
void socket_reselect_all(void) {}

/* One select() pass over all live sockets; dispatch via select_result. Returns #ready.
   Called by the engine event loop (SecureShell). ms<0 => block. */
int winscp_net_select(int ms)
{
    fd_set rfd, wfd; FD_ZERO(&rfd); FD_ZERO(&wfd);
    int maxfd = -1;
    for (NetSocket *s = g_head; s; s = s->next) {
        if (s->fd < 0) continue;
        if (!s->frozen) FD_SET(s->fd, &rfd);
        if (!s->connected || bufchain_size(&s->output) > 0) FD_SET(s->fd, &wfd);
        if (s->fd > maxfd) maxfd = s->fd;
    }
    if (maxfd < 0) return 0;
    struct timeval tv; struct timeval *ptv = NULL;
    if (ms >= 0) { tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000; ptv = &tv; }
    int rc = select(maxfd + 1, &rfd, &wfd, NULL, ptv);
    if (rc <= 0) return rc;
    /* snapshot fds first (select_result may close/relink) */
    int fds[FD_SETSIZE], events[FD_SETSIZE], nf = 0;
    for (NetSocket *s = g_head; s; s = s->next) {
        if (s->fd < 0 || nf >= FD_SETSIZE) continue;
        int ev = 0;
        if (FD_ISSET(s->fd, &rfd)) ev |= FD_READ;
        if (FD_ISSET(s->fd, &wfd)) ev |= (s->connected ? FD_WRITE : FD_CONNECT);
        if (ev) { fds[nf] = s->fd; events[nf] = ev; nf++; }
    }
    for (int i = 0; i < nf; i++) select_result((WPARAM)fds[i], (LPARAM)events[i]);
    return rc;
}
