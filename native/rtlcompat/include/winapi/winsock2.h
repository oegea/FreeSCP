//---------------------------------------------------------------------------
// winsock2.h — Winsock-async-select emulation over BSD sockets for the native port.
//
// WinSCP's TSecureShell drives socket I/O through the Windows async-select API
// (WSAEventSelect + WSAEnumNetworkEvents + an event handle). On the native port SOCKET is a
// real BSD fd (from PuTTY's uxnet), so we emulate that API for real: WSAEventSelect records the
// (socket,mask,event); WSAEnumNetworkEvents does a non-blocking select() on the socket and
// reports the matching ready bits. The blocking wait that ties events to readiness is the one
// piece that can't be emulated 1:1 and is rewired in SecureShell.cpp to winscp_net_select().
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_WINAPI_WINSOCK2_H
#define WINSCP_RTLCOMPAT_WINAPI_WINSOCK2_H

#include "winscp/wintypes.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

// SOCKET is a real BSD fd here (identical to PuTTY platform.h's `typedef int SOCKET`, so the
// two coexist as a legal redefinition in any C++ TU that pulls both).
typedef int SOCKET;

//--- network-event bit indices (FD_*_BIT) and masks (FD_*) ---
#define FD_READ_BIT    0
#define FD_WRITE_BIT   1
#define FD_OOB_BIT     2
#define FD_ACCEPT_BIT  3
#define FD_CONNECT_BIT 4
#define FD_CLOSE_BIT   5
#define FD_MAX_EVENTS  10

#define FD_READ    (1 << FD_READ_BIT)
#define FD_WRITE   (1 << FD_WRITE_BIT)
#define FD_OOB     (1 << FD_OOB_BIT)
#define FD_ACCEPT  (1 << FD_ACCEPT_BIT)
#define FD_CONNECT (1 << FD_CONNECT_BIT)
#define FD_CLOSE   (1 << FD_CLOSE_BIT)

//--- error/result codes ---
#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#define WSAEWOULDBLOCK   EWOULDBLOCK
#define WSAEINPROGRESS   EINPROGRESS
#define WSAECONNRESET    ECONNRESET
#define WSAENOTCONN      ENOTCONN
#define WSAEINTR         EINTR

//--- WSA event handle: reuse the rtlcompat event HANDLE (WinThreads) ---
typedef HANDLE WSAEVENT;
#define WSA_INVALID_EVENT ((WSAEVENT)nullptr)
#define WSAMAKESELECTREPLY(event, error) ((unsigned short)(((unsigned short)(event)) | (((unsigned short)(error)) << 8)))

//--- WSAIoctl control codes the engine names (send-backlog query is best-effort/no-op) ---
#define SIO_IDEAL_SEND_BACKLOG_QUERY 0x4004747B

//--- Winsock type aliases over BSD structs ---
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr     SOCKADDR;
typedef struct hostent      HOSTENT;
typedef struct hostent *    LPHOSTENT;
typedef struct in_addr      IN_ADDR;

//--- network-events report struct (WSAEnumNetworkEvents output) ---
typedef struct _WSANETWORKEVENTS
{
  long lNetworkEvents;
  int  iErrorCode[FD_MAX_EVENTS];
} WSANETWORKEVENTS, * LPWSANETWORKEVENTS;

#ifdef __cplusplus
extern "C" {
#endif

// Associate (or clear, when lEvents==0) the event mask for a socket. Sets the socket
// non-blocking, mirroring WSAEventSelect semantics.
int WSAEventSelect(SOCKET s, WSAEVENT hEventObject, long lNetworkEvents);
// Non-blocking probe: fills lpNetworkEvents with the currently-ready bits of the registered
// mask for s, and resets hEventObject. Returns 0 on success, SOCKET_ERROR on error.
int WSAEnumNetworkEvents(SOCKET s, WSAEVENT hEventObject, LPWSANETWORKEVENTS lpNetworkEvents);
int WSAGetLastError(void);
void WSASetLastError(int iError);
int closesocket(SOCKET s);
// best-effort: only SIO_IDEAL_SEND_BACKLOG_QUERY is used (reports a default ideal backlog).
int WSAIoctl(SOCKET s, unsigned long code, void * inBuf, unsigned long inLen,
             void * outBuf, unsigned long outLen, unsigned long * bytesReturned,
             void * overlapped, void * completion);

#ifdef __cplusplus
}
#endif

#endif
