/*
 * uxnet.c — BSD-socket network backend for the WinSCP PuTTY fork (replaces windows/network.c).
 *
 * Implements the Socket/Plug vtable over POSIX sockets with bufchain output buffering. Like
 * WinSCP's windows/network.c (MPEXT), the connect is synchronous with a timeout and walks the
 * resolved addresses in order inside sk_new; the socket is handed to the engine's event
 * machinery (do_select) only once connected.
 *
 * ERROR STRINGS: every `const char *` error exposed here (sk_addr_error, sk_socket_error,
 * plug_closing_error) has STATIC lifetime, as on Windows. PuTTY relies on that, e.g. ssh.c does
 * `sk_addr_free(addr); return dupstr(err);` — a heap string owned by the SockAddr is read after
 * free there and surfaces as a garbage error message. select_result(fd, events) dispatches readiness to the Plug;
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
#include <poll.h>
#include <signal.h>
#include <stdio.h>

#include "putty.h"
#include "network.h"

struct SockAddr {
    int refcount;
    const char *error;        /* static lifetime (see header comment) */
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
    bool notify_connected;    /* connected in sk_new; PLUGLOG_CONNECT_SUCCESS still to be delivered */
    const char *error;        /* static lifetime (see header comment) */
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

/* ---------- error strings (static lifetime; same texts as windows/network.c so the engine's
 * message translation table in SecureShell.cpp matches) ---------- */
static const char *ux_error_string(int err)
{
    switch (err) {
      case EACCES:        return "Network error: Permission denied";
      case EADDRINUSE:    return "Network error: Address already in use";
      case EADDRNOTAVAIL: return "Network error: Cannot assign requested address";
      case EAFNOSUPPORT:  return "Network error: Address family not supported by protocol family";
      case ECONNABORTED:  return "Network error: Software caused connection abort";
      case ECONNREFUSED:  return "Network error: Connection refused";
      case ECONNRESET:    return "Network error: Connection reset by peer";
      case EPIPE:         return "Network error: Connection reset by peer";
      case EHOSTDOWN:     return "Network error: Host is down";
      case EHOSTUNREACH:  return "Network error: No route to host";
      case EMFILE:        return "Network error: Too many open files";
      case ENETDOWN:      return "Network error: Network is down";
      case ENETRESET:     return "Network error: Network dropped connection on reset";
      case ENETUNREACH:   return "Network error: Network is unreachable";
      case ENOBUFS:       return "Network error: No buffer space available";
      case ENOTCONN:      return "Network error: Socket is not connected";
      case ETIMEDOUT:     return "Network error: Connection timed out";
      default: {
        /* Rare codes: intern "Network error: <strerror>" once per errno (bounded, never freed). */
        static struct { int err; char text[160]; } cache[16];
        static int used = 0;
        const char *result = "Network error: Unknown network error";
        WINSCP_PUTTY_SECTION_ENTER;
        int i;
        for (i = 0; i < used && cache[i].err != err; i++);
        if (i == used && used < (int)lenof(cache)) {
            cache[i].err = err;
            snprintf(cache[i].text, sizeof(cache[i].text), "Network error: %s", strerror(err));
            used++;
        }
        if (i < used) result = cache[i].text;
        WINSCP_PUTTY_SECTION_LEAVE;
        return result;
      }
    }
}

static const char *ux_namelookup_error(int gai)
{
    switch (gai) {
      case EAI_NONAME:
#if defined(EAI_NODATA) && (EAI_NODATA != EAI_NONAME)
      case EAI_NODATA:
#endif
        return "Host does not exist";
      case EAI_AGAIN:
        return "Host not found";
      default:
        return gai_strerror(gai);   /* static storage */
    }
}

/* ---------- init ---------- */
void sk_init(void)
{
    /* A send() to a connection the peer already closed raises SIGPIPE, whose default action
     * kills the process (no Windows equivalent). Sockets here also use SO_NOSIGPIPE/MSG_NOSIGNAL,
     * but the neon/libs3/FileZilla/OpenSSL sockets don't, so ignore it process-wide. */
    signal(SIGPIPE, SIG_IGN);
}
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
    if (rc != 0) { a->error = ux_namelookup_error(rc); a->ais = NULL; }
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
    sfree(addr);
}
bool sk_hostname_is_local(const char *name)
{ return !name || !*name || !strcmp(name, "localhost") || !strcmp(name, "127.0.0.1") || !strcmp(name, "::1"); }
bool sk_address_is_local(SockAddr *addr) { return sk_hostname_is_local(addr->hostname); }
bool sk_address_is_special_local(SockAddr *addr) { (void)addr; return false; }

/* ---------- socket vtable ---------- */
static void set_nonblock(int fd) { int fl = fcntl(fd, F_GETFL, 0); fcntl(fd, F_SETFL, fl | O_NONBLOCK); }
#ifdef MSG_NOSIGNAL
#define UX_SEND_FLAGS MSG_NOSIGNAL
#else
#define UX_SEND_FLAGS 0
#endif

static void ns_try_flush(NetSocket *s)
{
    size_t before = bufchain_size(&s->output);
    while (bufchain_size(&s->output) > 0) {
        ptrlen pl = bufchain_prefix(&s->output);
        ssize_t n = send(s->fd, pl.ptr, pl.len, UX_SEND_FLAGS);
        if (n > 0) bufchain_consume(&s->output, n);
        else {
            /* EAGAIN: wait for writable. A real error (peer gone: EPIPE/ECONNRESET...) is
             * remembered and reported from the event loop, not from inside the caller's write. */
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR && !s->pending_error) {
                s->pending_error = true; s->pending_errno = errno;
            }
            break;
        }
    }
    size_t after = bufchain_size(&s->output);
    /* If the send backlog shrank, notify the upper layer (ssh_sent): it unthrottles and queues the
     * next chunk of output. Without this, once the socket backlogs (slow network) the transfer
     * stalls after the first burst — ssh never learns the buffer drained. */
    if (after < before && s->plug) plug_sent(s->plug, after);
}

static Plug *ns_plug(Socket *ss, Plug *p) { NetSocket *s = (NetSocket *)ss; Plug *old = s->plug; if (p) s->plug = p; return old; }
static void ns_close(Socket *ss)
{
    NetSocket *s = (NetSocket *)ss;
    if (s->fd >= 0) {
        ns_unlink(s);
        do_select(s->plug, s->fd, false);   /* as windows/network.c: unselect before closing */
        close(s->fd);
    }
    bufchain_clear(&s->output);
    sk_addr_free(s->addr);
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

/* Connect to one candidate address, waiting up to timeout_ms (<=0: OS default) for completion.
 * Returns the connected non-blocking fd, or -1 with *perr set. */
static int ns_connect_one(NetSocket *s, struct addrinfo *ai, bool nodelay, bool keepalive,
                          int timeout_ms, int sndbuf, int *perr)
{
    int fd = socket(ai->ai_family, SOCK_STREAM, 0);
    if (fd < 0) { *perr = errno; return -1; }
    if (fd >= FD_SETSIZE) { close(fd); *perr = EMFILE; return -1; }   /* the event loop uses select() */
    set_nonblock(fd);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    int one = 1;
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    if (nodelay) setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    if (keepalive) setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
    if (sndbuf > 0) setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    /* set the port into a copy of the sockaddr */
    struct sockaddr_storage ss; socklen_t sl = ai->ai_addrlen;
    memcpy(&ss, ai->ai_addr, sl);
    if (ss.ss_family == AF_INET) ((struct sockaddr_in *)&ss)->sin_port = htons(s->port);
    else if (ss.ss_family == AF_INET6) ((struct sockaddr_in6 *)&ss)->sin6_port = htons(s->port);

    int err = 0;
    if (connect(fd, (struct sockaddr *)&ss, sl) != 0) {
        err = errno;
        if (err == EINPROGRESS || err == EINTR) {
            struct pollfd pfd; pfd.fd = fd; pfd.events = POLLOUT; pfd.revents = 0;
            int rc;
            do { rc = poll(&pfd, 1, timeout_ms > 0 ? timeout_ms : -1); } while (rc < 0 && errno == EINTR);
            if (rc == 0) err = ETIMEDOUT;
            else if (rc < 0) err = errno;
            else {
                socklen_t el = sizeof(err);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) != 0) err = errno;
            }
        }
    }
    if (err != 0) { close(fd); *perr = err; return -1; }
    return fd;
}

Socket *sk_new(SockAddr *addr, int port, bool privport, bool oobinline,
               bool nodelay, bool keepalive, Plug *p
#ifdef MPEXT
               , int timeout, int sndbuf, const char *srcaddr
#endif
              )
{
    (void)privport; (void)oobinline;
#ifdef MPEXT
    (void)srcaddr;
#else
    int timeout = 0, sndbuf = 0;
#endif
    NetSocket *s = snew(NetSocket);
    memset(s, 0, sizeof(*s));
    s->sock.vt = &NetSocket_vt;
    s->plug = p;
    s->fd = -1;
    s->addr = sk_addr_dup(addr);
    s->port = port;
    bufchain_init(&s->output);

    if (addr && addr->error) { s->error = addr->error; return &s->sock; }

    /* Walk the resolved addresses in order (e.g. IPv6 then IPv4), as windows/network.c does with
     * `do { try_connect } while (err && sk_nextaddr)`: the first one that connects wins. */
    int lasterr = EHOSTUNREACH;
    for (s->step = addr ? addr->ais : NULL; s->step; s->step = s->step->ai_next) {
        plug_log(s->plug, &s->sock, PLUGLOG_CONNECT_TRYING, s->addr, s->port, NULL, 0);
        int fd = ns_connect_one(s, s->step, nodelay, keepalive, timeout, sndbuf, &lasterr);
        if (fd >= 0) { s->fd = fd; break; }
        plug_log(s->plug, &s->sock, PLUGLOG_CONNECT_FAILED, s->addr, s->port, strerror(lasterr), lasterr);
    }
    if (s->fd < 0) { s->error = ux_error_string(lasterr); return &s->sock; }

    s->connected = true;
    s->notify_connected = true;   /* PLUGLOG_CONNECT_SUCCESS is delivered from the event loop */
    ns_link(s);
    /* Register the fd with the engine's event machinery (WinSCP's do_select -> SecureShell ->
     * WSAEventSelect), only after the connect — mirrors windows/network.c under MPEXT. */
    const char *err = do_select(s->plug, s->fd, true);
    if (err && !s->error) s->error = err;
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
    NetSocket *s = ns_find(fd);
    if (!s) return;

    int event = (int)lParam & 0xFF;   /* rtlcompat's WSAMAKESELECTREPLY packs (event | error << 8) */

    if (s->notify_connected) {
        s->notify_connected = false;
        plug_log(s->plug, &s->sock, PLUGLOG_CONNECT_SUCCESS, s->addr, s->port, NULL, 0);
        if ((s = ns_find(fd)) == NULL) return;   /* the plug may have closed us */
    }
    if (event & (FD_WRITE | FD_CONNECT)) ns_try_flush(s);
    if (s->pending_error) {
        /* a send() failed (peer gone): report it once; the plug normally closes the socket */
        int err = s->pending_errno;
        s->pending_error = false;
        plug_closing_error(s->plug, ux_error_string(err));
        return;
    }
    if ((event & FD_CLOSE) && !(event & FD_READ)) {
        plug_closing_normal(s->plug);
        return;
    }
    if ((event & FD_READ) && !s->frozen) {
        char buf[16384];
        ssize_t n = recv(s->fd, buf, sizeof(buf), 0);
        if (n > 0) plug_receive(s->plug, 0, buf, (size_t)n);
        else if (n == 0) plug_closing_normal(s->plug);
        else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) plug_closing_error(s->plug, ux_error_string(errno));
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
