//---------------------------------------------------------------------------
// WinSock.cpp — Winsock-async-select emulation over BSD sockets (see winapi/winsock2.h).
//
// WSAEventSelect records a (socket -> mask, event) association and makes the socket
// non-blocking. WSAEnumNetworkEvents probes the socket with a zero-timeout select() and reports
// the ready bits intersected with the mask. FD_CONNECT and FD_CLOSE are one-shot (edge) like
// Windows — tracked per socket — while FD_READ/FD_WRITE/FD_OOB re-report at level. The blocking
// readiness wait is provided separately by winscp_net_select() (PuTTY uxnet) and the engine's
// rewired event loop.
//---------------------------------------------------------------------------
#include <map>
#include <mutex>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include "winapi/winsock2.h"

namespace {

struct SockReg
{
  long mask = 0;
  WSAEVENT event = nullptr;
  bool connectReported = false;
  bool closeReported = false;
};

std::mutex g_mutex;
std::map<SOCKET, SockReg> g_socks;
thread_local int g_lastError = 0;

// zero-timeout readiness probe
void probe(SOCKET s, bool & rd, bool & wr, bool & ex)
{
  fd_set r, w, e;
  FD_ZERO(&r); FD_ZERO(&w); FD_ZERO(&e);
  FD_SET(s, &r); FD_SET(s, &w); FD_SET(s, &e);
  struct timeval tv{0, 0};
  rd = wr = ex = false;
  if (::select(s + 1, &r, &w, &e, &tv) > 0)
  {
    rd = FD_ISSET(s, &r); wr = FD_ISSET(s, &w); ex = FD_ISSET(s, &e);
  }
}

} // namespace

extern "C" {

int WSAEventSelect(SOCKET s, WSAEVENT hEventObject, long lNetworkEvents)
{
  std::lock_guard<std::mutex> lock(g_mutex);
  if (lNetworkEvents == 0)
  {
    g_socks.erase(s);
  }
  else
  {
    SockReg & reg = g_socks[s];
    reg.mask = lNetworkEvents;
    reg.event = hEventObject;
    // mirror WSAEventSelect: socket goes non-blocking
    int flags = ::fcntl(s, F_GETFL, 0);
    if (flags != -1) ::fcntl(s, F_SETFL, flags | O_NONBLOCK);
  }
  return 0;
}

int WSAEnumNetworkEvents(SOCKET s, WSAEVENT /*hEventObject*/, LPWSANETWORKEVENTS lpNetworkEvents)
{
  if (lpNetworkEvents == nullptr) { g_lastError = EINVAL; return SOCKET_ERROR; }
  lpNetworkEvents->lNetworkEvents = 0;
  for (int i = 0; i < FD_MAX_EVENTS; ++i) lpNetworkEvents->iErrorCode[i] = 0;

  long mask;
  bool wantConnectShot = false, wantCloseShot = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_socks.find(s);
    if (it == g_socks.end()) { g_lastError = ENOTCONN; return SOCKET_ERROR; }
    mask = it->second.mask;
    wantConnectShot = (mask & FD_CONNECT) && !it->second.connectReported;
    wantCloseShot   = (mask & FD_CLOSE)   && !it->second.closeReported;
  }

  bool rd, wr, ex;
  probe(s, rd, wr, ex);

  long ev = 0;
  // connect completion: socket becomes writable; surface SO_ERROR (one-shot)
  if (wr && wantConnectShot)
  {
    int soerr = 0; socklen_t l = sizeof(soerr);
    ::getsockopt(s, SOL_SOCKET, SO_ERROR, &soerr, &l);
    ev |= FD_CONNECT;
    lpNetworkEvents->iErrorCode[FD_CONNECT_BIT] = soerr;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_socks.find(s); if (it != g_socks.end()) it->second.connectReported = true;
  }
  if (rd && (mask & FD_READ))
  {
    // distinguish a graceful close: peek for 0 bytes
    char c; ssize_t n = ::recv(s, &c, 1, MSG_PEEK);
    if (n == 0)
    {
      if (wantCloseShot)
      {
        ev |= FD_CLOSE;
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_socks.find(s); if (it != g_socks.end()) it->second.closeReported = true;
      }
    }
    else
    {
      ev |= FD_READ;
    }
  }
  if (wr && (mask & FD_WRITE) && !(ev & FD_CONNECT)) ev |= FD_WRITE;
  if (ex && (mask & FD_OOB)) ev |= FD_OOB;

  lpNetworkEvents->lNetworkEvents = ev;
  return 0;
}

int WSAGetLastError(void) { return g_lastError ? g_lastError : errno; }
void WSASetLastError(int iError) { g_lastError = iError; }

int closesocket(SOCKET s)
{
  { std::lock_guard<std::mutex> lock(g_mutex); g_socks.erase(s); }
  return ::close(s);
}

int WSAIoctl(SOCKET /*s*/, unsigned long code, void * /*inBuf*/, unsigned long /*inLen*/,
             void * outBuf, unsigned long outLen, unsigned long * bytesReturned,
             void * /*overlapped*/, void * /*completion*/)
{
  if (code == SIO_IDEAL_SEND_BACKLOG_QUERY && outBuf != nullptr && outLen >= sizeof(unsigned long))
  {
    *static_cast<unsigned long *>(outBuf) = 256 * 1024;   // a reasonable default ideal backlog
    if (bytesReturned) *bytesReturned = sizeof(unsigned long);
    return 0;
  }
  g_lastError = EINVAL;
  return SOCKET_ERROR;
}

} // extern "C"
