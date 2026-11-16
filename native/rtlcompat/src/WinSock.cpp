//---------------------------------------------------------------------------
// WinSock.cpp — Winsock-async-select emulation over BSD sockets (see winapi/winsock2.h).
//
// WSAEventSelect records a (socket -> mask, event) association and makes the socket
// non-blocking. WSAEnumNetworkEvents probes the socket with a zero-timeout select() and reports
// the ready bits intersected with the mask. All events are edge-style, like Windows - tracked per
// socket: FD_CONNECT and FD_CLOSE are one-shot; FD_READ is reported once and again only after the
// owner has read from the socket; FD_WRITE once after the connect and again only after a send
// could not complete. The socket owner (PuTTY uxnet) reports those two moments with
// winscp_socket_rearm(). (FD_READ/FD_WRITE used to re-report at level: a connected socket is
// almost always writable, so the engine's event loop never slept - a full CPU core for as long as
// it waited, permanently for the thread that services an SSH tunnel.) The blocking readiness wait
// is provided separately by winscp_net_select() (PuTTY uxnet) and the engine's rewired event loop.
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
  bool listener = false;     // listening socket: readable = FD_ACCEPT (declared by the owner)
  bool readArmed = true;     // FD_READ may be reported (cleared when reported, set again by a recv)
  bool writeArmed = true;    // FD_WRITE may be reported (cleared when reported, set again by EWOULDBLOCK)
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
    // A new association on a different event object means a new socket that happens to reuse the
    // fd number of one closed without closesocket(): its one-shot FD_CONNECT/FD_CLOSE start over.
    if (reg.event != hEventObject) { reg.connectReported = false; reg.closeReported = false; reg.readArmed = true; reg.writeArmed = true; reg.listener = false; }
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
  bool wantConnectShot = false, wantCloseShot = false, readArmed = false, writeArmed = false, listening = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_socks.find(s);
    if (it == g_socks.end()) { g_lastError = ENOTCONN; return SOCKET_ERROR; }
    mask = it->second.mask;
    wantConnectShot = (mask & FD_CONNECT) && !it->second.connectReported;
    wantCloseShot   = (mask & FD_CLOSE)   && !it->second.closeReported;
    readArmed = it->second.readArmed;
    writeArmed = it->second.writeArmed;
    listening = it->second.listener;
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
  // A listening socket (SSH port forwarding) is "readable" when a connection is waiting: that is
  // FD_ACCEPT on Windows, never FD_READ/FD_CLOSE. The owner declares it (winscp_socket_mark_listener):
  // SO_ACCEPTCONN cannot be queried on macOS (ENOPROTOOPT).
  if (listening)
  {
    if (rd && (mask & FD_ACCEPT)) ev |= FD_ACCEPT;
    lpNetworkEvents->lNetworkEvents = ev;
    return 0;
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
    else if (readArmed)
    {
      ev |= FD_READ;
      std::lock_guard<std::mutex> lock(g_mutex);
      auto it = g_socks.find(s); if (it != g_socks.end()) it->second.readArmed = false;
    }
  }
  if (wr && (mask & FD_WRITE) && !(ev & FD_CONNECT) && !wantConnectShot && writeArmed)
  {
    ev |= FD_WRITE;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_socks.find(s); if (it != g_socks.end()) it->second.writeArmed = false;
  }
  if (ex && (mask & FD_OOB)) ev |= FD_OOB;
  lpNetworkEvents->lNetworkEvents = ev;
  return 0;
}

// The socket owner declares a listening socket (after registering it with WSAEventSelect).
void winscp_socket_mark_listener(SOCKET s)
{
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_socks.find(s);
  if (it != g_socks.end()) it->second.listener = true;
}

// The socket owner tells the emulation that an event may be reported again: FD_READ after it has
// read from the socket (or is ready to read again), FD_WRITE after a send came back EWOULDBLOCK.
void winscp_socket_rearm(SOCKET s, long events)
{
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_socks.find(s);
  if (it == g_socks.end()) return;
  if (events & FD_READ) it->second.readArmed = true;
  if (events & FD_WRITE) it->second.writeArmed = true;
}

// Bridge socket readiness to the WSA event handles: zero-timeout select() over every registered
// socket; if any has something WSAEnumNetworkEvents would report, SetEvent on its associated handle.
// The engine's WaitForMultipleObjects calls this each poll cycle so its FSocketEvent wakes on socket
// activity (the WSAEventSelect emulation can't auto-signal at the kernel level). Returns #events signaled.
int winscp_pump_socket_events(void)
{
  struct Item { SOCKET s; WSAEVENT event; bool wantRead, wantEof, wantWrite; };
  std::vector<Item> snapshot;
  long maxfd = -1;
  fd_set rd, wr;
  FD_ZERO(&rd); FD_ZERO(&wr);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto & kv : g_socks)
    {
      const SockReg & reg = kv.second;
      Item it;
      it.s = kv.first; it.event = reg.event;
      // (after FD_CLOSE was reported there is nothing more to read: EOF stays "readable" forever)
      it.wantRead = reg.readArmed && !reg.closeReported && (reg.mask & (FD_READ | FD_ACCEPT));
      it.wantEof = (reg.mask & FD_CLOSE) && !reg.closeReported;
      it.wantWrite = ((reg.mask & FD_CONNECT) && !reg.connectReported) || ((reg.mask & FD_WRITE) && reg.writeArmed);
      if (!it.wantRead && !it.wantEof && !it.wantWrite) continue;
      if (it.wantRead || it.wantEof) FD_SET(it.s, &rd);
      if (it.wantWrite) FD_SET(it.s, &wr);
      if (it.s > maxfd) maxfd = it.s;
      snapshot.push_back(it);
    }
  }
  if (snapshot.empty()) return 0;
  struct timeval tv{0, 0};
  if (::select((int)maxfd + 1, &rd, &wr, nullptr, &tv) <= 0) return 0;
  int signaled = 0;
  for (auto & p : snapshot)
  {
    bool ready = p.wantWrite && FD_ISSET(p.s, &wr);
    if (!ready && FD_ISSET(p.s, &rd))
    {
      if (p.wantRead) ready = true;
      else { char c; ready = (::recv(p.s, &c, 1, MSG_PEEK) == 0); }   // unread data is not news; EOF is
    }
    if (ready && p.event) { SetEvent(p.event); ++signaled; }
  }
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
