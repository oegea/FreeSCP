//---------------------------------------------------------------------------
// winmsg.cpp — Win32 message-pump + WSAAsyncSelect emulation (see winmsg.h).
//---------------------------------------------------------------------------
#include "winmsg.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>

#include <vector>
#include <deque>
#include <map>
#include <string>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <sys/select.h>

HINSTANCE afxCurrentResourceHandle = nullptr;

namespace {

struct FzWindow { WNDPROC proc = nullptr; INT_PTR userData = 0; };

std::mutex g_mtx;
std::condition_variable g_cv;
std::deque<MSG> g_queue;                       // global message queue (filtered by hwnd on pop)
// u16string key (std::wstring is 4-byte-ABI in libc++ under -fshort-wchar -> broken comparisons).
static std::u16string k16(const wchar_t * p) { std::u16string r; if (p) while (*p) r.push_back((char16_t)*p++); return r; }
std::map<std::u16string, WNDPROC> g_classes;   // RegisterClassEx: class name -> wndproc

struct SockReg { HWND hwnd; UINT msg; long events; };
std::map<SOCKET, SockReg> g_sockets;           // WSAAsyncSelect registrations
std::map<SOCKET, long> g_posted;               // events already pending/posted per socket (latch)

std::thread g_selThread;
std::atomic<bool> g_selRun{false};
int g_wake[2] = { -1, -1 };                    // self-pipe to wake the select loop

void wakeSelect() { if (g_wake[1] >= 0) { char c = 1; (void)::write(g_wake[1], &c, 1); } }

bool queueHas(HWND hwnd, UINT msg, WPARAM w)   // dedup: avoid flooding identical socket events
{
  for (const auto & m : g_queue) if (m.hwnd == hwnd && m.message == msg && m.wParam == w) return true;
  return false;
}

void postLocked(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
  g_queue.push_back(MSG{ hwnd, msg, w, l, 0 });
  g_cv.notify_all();
}

void selectLoop()
{
  while (g_selRun.load())
  {
    fd_set rfds, wfds; FD_ZERO(&rfds); FD_ZERO(&wfds);
    int maxfd = g_wake[0];
    FD_SET(g_wake[0], &rfds);
    {
      std::lock_guard<std::mutex> lk(g_mtx);
      for (auto & kv : g_sockets)
      {
        SOCKET s = kv.first; long ev = kv.second.events;
        if (ev & (FD_READ | FD_ACCEPT | FD_CLOSE)) FD_SET(s, &rfds);
        if (ev & (FD_WRITE | FD_CONNECT)) FD_SET(s, &wfds);
        if (s > maxfd) maxfd = s;
      }
    }
    timeval tv{ 0, 100000 };   // 100ms
    int r = ::select(maxfd + 1, &rfds, &wfds, nullptr, &tv);
    if (r < 0)
    {
      if (errno == EBADF)   // a socket was closed without unregistering; drop dead fds to stop spinning
      {
        std::lock_guard<std::mutex> lk(g_mtx);
        for (auto it = g_sockets.begin(); it != g_sockets.end(); )
          if (::fcntl(it->first, F_GETFD) < 0) { g_posted.erase(it->first); it = g_sockets.erase(it); } else ++it;
      }
      continue;
    }
    if (FD_ISSET(g_wake[0], &rfds)) { char buf[64]; while (::read(g_wake[0], buf, sizeof buf) > 0) {} }

    std::lock_guard<std::mutex> lk(g_mtx);
    for (auto & kv : g_sockets)
    {
      SOCKET s = kv.first; const SockReg & reg = kv.second; long & latch = g_posted[s];
      auto fire = [&](long fd) {
        if (!(reg.events & fd)) return;
        if ((fd == FD_WRITE || fd == FD_CONNECT) && (latch & fd)) return;  // one-shot for writable
        if (queueHas(reg.hwnd, reg.msg, (WPARAM)s)) return;
        if (getenv("FZ_TRACE")) fprintf(stderr, "[fz] post sock=%d event=0x%lx\n", s, fd);
        postLocked(reg.hwnd, reg.msg, (WPARAM)s, MAKELONG(fd, 0));
        latch |= fd;
      };
      if (FD_ISSET(s, &wfds)) { fire(FD_CONNECT); fire(FD_WRITE); }
      if (FD_ISSET(s, &rfds)) { fire(FD_READ); fire(FD_ACCEPT); fire(FD_CLOSE); }
    }
  }
}

void ensureSelectThread()
{
  if (g_selRun.load()) return;
  if (g_wake[0] < 0) { if (::pipe(g_wake) == 0) { ::fcntl(g_wake[0], F_SETFL, O_NONBLOCK); } }
  g_selRun = true;
  g_selThread = std::thread(selectLoop);
  g_selThread.detach();
}

} // namespace

//=== threading + sync emulation ============================================
namespace { struct FzThread { std::thread t; }; }

HANDLE CreateThread(void *, size_t, LPTHREAD_START_ROUTINE fn, void * param, DWORD, DWORD * tid)
{
  if (tid) *tid = 0;
  auto * h = new FzThread();
  h->t = std::thread([fn, param]{ if (fn) fn(param); });   // CREATE_SUSPENDED ignored; ResumeThread is a no-op
  h->t.detach();
  return (HANDLE)h;
}
// ResumeThread / SetThreadPriority / WaitForSingleObject / CloseHandle are provided by rtlcompat
// (WinThreads/SysExtra). FileZilla's CreateThread returns a detached std::thread handle that those
// rtlcompat fns don't recognize (Lookup fails -> no-op), which is fine: our threads run immediately.
BOOL  PostThreadMessage(DWORD, UINT msg, WPARAM w, LPARAM l)
{ std::lock_guard<std::mutex> lk(g_mtx); postLocked(nullptr, msg, w, l); return TRUE; }  // thread msgs: hwnd=NULL
UINT  RegisterWindowMessage(const wchar_t *)
{ static std::atomic<UINT> next{0xC000}; return next++; }

unsigned short RegisterClassEx(const WNDCLASSEX * wc)
{
  if (!wc || !wc->lpszClassName) return 0;
  std::lock_guard<std::mutex> lk(g_mtx);
  g_classes[k16(wc->lpszClassName)] = wc->lpfnWndProc;
  return 1;
}

HWND CreateWindow(const wchar_t * cls, const wchar_t *, DWORD, int, int, int, int, HWND, void *, HINSTANCE, void *)
{
  auto * w = new FzWindow();
  if (cls) { std::lock_guard<std::mutex> lk(g_mtx); auto it = g_classes.find(k16(cls)); if (it != g_classes.end()) w->proc = it->second; }
  return (HWND)w;
}

BOOL DestroyWindow(HWND hWnd)
{
  if (!hWnd) return FALSE;
  { std::lock_guard<std::mutex> lk(g_mtx);
    for (auto it = g_queue.begin(); it != g_queue.end(); )   // drop pending messages for this window
      if (it->hwnd == hWnd) it = g_queue.erase(it); else ++it; }
  delete (FzWindow *)hWnd;
  return TRUE;
}

INT_PTR GetWindowLongPtr(HWND hWnd, int index)
{
  if (!hWnd) return 0;
  if (index == GWLP_USERDATA) return ((FzWindow *)hWnd)->userData;
  if (index == GWL_WNDPROC) return (INT_PTR)((FzWindow *)hWnd)->proc;
  return 0;
}

INT_PTR SetWindowLongPtr(HWND hWnd, int index, INT_PTR value)
{
  if (!hWnd) return 0;
  FzWindow * w = (FzWindow *)hWnd;
  INT_PTR old = 0;
  if (index == GWLP_USERDATA) { old = w->userData; w->userData = value; }
  else if (index == GWL_WNDPROC) { old = (INT_PTR)w->proc; w->proc = (WNDPROC)value; }
  return old;
}

BOOL PostMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  std::lock_guard<std::mutex> lk(g_mtx);
  postLocked(hWnd, msg, wParam, lParam);
  return TRUE;
}

static bool matches(const MSG & m, HWND hwnd, UINT lo, UINT hi)
{
  if (hwnd && m.hwnd != hwnd) return false;
  if (lo == 0 && hi == 0) return true;
  return m.message >= lo && m.message <= hi;
}

BOOL PeekMessage(LPMSG out, HWND hWnd, UINT lo, UINT hi, UINT removeFlag)
{
  std::lock_guard<std::mutex> lk(g_mtx);
  for (auto it = g_queue.begin(); it != g_queue.end(); ++it)
    if (matches(*it, hWnd, lo, hi))
    {
      if (out) *out = *it;
      if (removeFlag & PM_REMOVE) { SOCKET s = (SOCKET)it->wParam; (void)s; g_queue.erase(it); }
      return TRUE;
    }
  return FALSE;
}

BOOL GetMessage(LPMSG out, HWND hWnd, UINT lo, UINT hi)
{
  std::unique_lock<std::mutex> lk(g_mtx);
  for (;;)
  {
    for (auto it = g_queue.begin(); it != g_queue.end(); ++it)
      if (matches(*it, hWnd, lo, hi)) { if (out) *out = *it; g_queue.erase(it); return TRUE; }
    g_cv.wait(lk);
  }
}

LRESULT DispatchMessage(const MSG * msg)
{
  if (!msg || !msg->hwnd) return 0;
  WNDPROC p = ((FzWindow *)msg->hwnd)->proc;
  return p ? p(msg->hwnd, msg->message, msg->wParam, msg->lParam) : 0;
}

LRESULT DefWindowProc(HWND, UINT, WPARAM, LPARAM) { return 0; }

int WSAAsyncSelect(SOCKET s, HWND hWnd, UINT msg, long lEvent)
{
  if (getenv("FZ_TRACE")) fprintf(stderr, "[fz] WSAAsyncSelect sock=%d msg=%u events=0x%lx\n", s, msg, lEvent);
  std::lock_guard<std::mutex> lk(g_mtx);
  if (lEvent == 0) { g_sockets.erase(s); g_posted.erase(s); }
  else { g_sockets[s] = SockReg{ hWnd, msg, lEvent }; g_posted[s] = 0; }
  // (re)start + wake the select loop; ensureSelectThread re-locks via atomics only, safe here.
  if (!g_selRun.load()) { /* started outside the lock below */ }
  wakeSelect();
  static std::once_flag once; std::call_once(once, []{ ensureSelectThread(); });
  return 0;
}
