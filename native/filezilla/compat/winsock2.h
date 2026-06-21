//---------------------------------------------------------------------------
// winsock2.h — BSD-socket shim for FileZilla's async socket layer (native port).
//
// Maps the Winsock2 surface AsyncSocketEx uses onto POSIX sockets. Struct layouts come from the
// system headers (so they interop with getaddrinfo); the only Windows-ism patched is in6_addr's
// `s6_bytes` member (Windows) -> POSIX `s6_addr`. The WSAAsyncSelect message model is NOT here —
// CAsyncSocketEx is ported to a select()/poll() loop in the .cpp (see FTP-SCOPE.md).
//---------------------------------------------------------------------------
#ifndef WINSCP_FZ_WINSOCK2_H
#define WINSCP_FZ_WINSOCK2_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

#include "afx.h"   // win scalar types (DWORD, BYTE, ...)

// Windows IN6_ADDR exposes s6_bytes[16]; POSIX exposes s6_addr (a macro to the byte array).
#ifndef s6_bytes
  #define s6_bytes s6_addr
#endif

typedef int SOCKET;
#define INVALID_SOCKET  (-1)
#define SOCKET_ERROR    (-1)

typedef struct sockaddr      SOCKADDR;
typedef struct sockaddr *    LPSOCKADDR;
typedef struct in_addr       IN_ADDR;
typedef struct in_addr *     LPIN_ADDR;
typedef struct in6_addr      IN6_ADDR;
typedef struct sockaddr_in   SOCKADDR_IN;
typedef struct sockaddr_in6  SOCKADDR_IN6;
typedef struct sockaddr_storage SOCKADDR_STORAGE;
typedef struct hostent       HOSTENT;
typedef struct hostent *     LPHOSTENT;
typedef struct addrinfo      ADDRINFOT;

// Async event bits (WSAAsyncSelect FD_* masks) — kept as constants; the port dispatches them
// from the select loop.
#define FD_READ     0x01
#define FD_WRITE    0x02
#define FD_OOB      0x04
#define FD_ACCEPT   0x08
#define FD_CONNECT  0x10
#define FD_CLOSE    0x20

// Winsock error codes -> errno equivalents.
#define WSAEWOULDBLOCK   EWOULDBLOCK
#define WSAEINPROGRESS   EINPROGRESS
#define WSAEISCONN       EISCONN
#define WSAENOTCONN      ENOTCONN
#define WSAECONNRESET    ECONNRESET
#define WSAECONNABORTED  ECONNABORTED
#define WSAESHUTDOWN     ESHUTDOWN
#define WSAEINVAL        EINVAL
#define WSAEAFNOSUPPORT  EAFNOSUPPORT
#define WSAEADDRINUSE    EADDRINUSE
#define WSAETIMEDOUT     ETIMEDOUT
#define WSAEALREADY      EALREADY
#define WSAEMFILE        EMFILE
#define WSAENOBUFS       ENOBUFS
#define WSAEHOSTUNREACH  EHOSTUNREACH
#define WSAENETUNREACH   ENETUNREACH
#define WSANOTINITIALISED (-100001)
#define WSAHOST_NOT_FOUND (-100002)

#define SD_RECEIVE  SHUT_RD
#define SD_SEND     SHUT_WR
#define SD_BOTH     SHUT_RDWR

inline int  closesocket(SOCKET s) { return ::close(s); }
inline int  WSAGetLastError() { return errno; }
inline void WSASetLastError(int e) { errno = e; }
#include <sys/ioctl.h>
inline int  ioctlsocket(SOCKET s, long cmd, unsigned long * argp)
{
  if (cmd == (long)FIONBIO)
  {
    int fl = ::fcntl(s, F_GETFL, 0); if (fl < 0) return SOCKET_ERROR;
    if (argp && *argp) fl |= O_NONBLOCK; else fl &= ~O_NONBLOCK;
    return ::fcntl(s, F_SETFL, fl) < 0 ? SOCKET_ERROR : 0;
  }
  return ::ioctl(s, cmd, argp) < 0 ? SOCKET_ERROR : 0;   // FIONREAD etc
}
inline int WSACancelAsyncRequest(HANDLE) { return 0; }

// FileZilla passes `int*` for socklen args; POSIX wants socklen_t*. Provide int*-len overloads.
inline SOCKET accept(SOCKET s, sockaddr * a, int * len)
{ socklen_t l = len ? (socklen_t)*len : 0; SOCKET r = ::accept(s, a, len ? &l : nullptr); if (len) *len = (int)l; return r; }
inline int getsockname(SOCKET s, sockaddr * a, int * len)
{ socklen_t l = len ? (socklen_t)*len : 0; int r = ::getsockname(s, a, len ? &l : nullptr); if (len) *len = (int)l; return r; }
inline int getpeername(SOCKET s, sockaddr * a, int * len)
{ socklen_t l = len ? (socklen_t)*len : 0; int r = ::getpeername(s, a, len ? &l : nullptr); if (len) *len = (int)l; return r; }
inline int getsockopt(SOCKET s, int lvl, int opt, void * val, int * len)
{ socklen_t l = len ? (socklen_t)*len : 0; int r = ::getsockopt(s, lvl, opt, val, len ? &l : nullptr); if (len) *len = (int)l; return r; }

// WSAStartup/Cleanup are no-ops on POSIX.
typedef struct { WORD wVersion; WORD wHighVersion; char szDescription[257]; } WSADATA;
typedef WSADATA * LPWSADATA;
inline int  WSAStartup(WORD, LPWSADATA) { return 0; }
inline int  WSACleanup() { return 0; }
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | (((WORD)((BYTE)(b))) << 8)))

// WSAAsyncSelect: registered by the port's CAsyncSocketEx select loop (declared, defined in .cpp).
LRESULT WSAAsyncSelect_stub();   // placeholder so headers referencing the model still parse

#endif
