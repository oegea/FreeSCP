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
#include <vector>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include "winapi/winsock2.h"
#include "winscp/WinThreads.h"   // SetEvent — bridge socket readiness to the WSA event handle

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
{  std::lock_guard<std::mutex> lock(g_mutex);
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

int WSAEnumNetworkEvents(SOCKET s, WSAEVENT hEventObject, LPWSANETWORKEVENTS lpNetworkEvents)
{
  if (hEventObject != nullptr) ResetEvent(hEventObject);   // consume the readiness signal
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

// Bridge socket readiness to the WSA event handles: zero-timeout select() over every registered
// socket; if any has a ready bit within its mask, SetEvent on its associated handle. The engine's
// WaitForMultipleObjects calls this each poll cycle so its FSocketEvent wakes on socket activity
// (the WSAEventSelect emulation can't auto-signal at the kernel level). Returns #events signaled.
int winscp_pump_socket_events(void)
{
  std::vector<std::pair<SOCKET, WSAEVENT>> snapshot;
  long maxfd = -1;
  fd_set rd, wr, ex;
  FD_ZERO(&rd); FD_ZERO(&wr); FD_ZERO(&ex);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto & kv : g_socks)
    {
      SOCKET s = kv.first;
      FD_SET(s, &rd); FD_SET(s, &wr); FD_SET(s, &ex);
      if (s > maxfd) maxfd = s;
      snapshot.push_back({s, kv.second.event});
    }
  }
  if (snapshot.empty()) return 0;
  struct timeval tv{0, 0};
  if (::select((int)maxfd + 1, &rd, &wr, &ex, &tv) <= 0) return 0;
  int signaled = 0;
  for (auto & p : snapshot)
    if (FD_ISSET(p.first, &rd) || FD_ISSET(p.first, &wr) || FD_ISSET(p.first, &ex))
    { if (p.second) { SetEvent(p.second); ++signaled; } }
  return signaled;
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
