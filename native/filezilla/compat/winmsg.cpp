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
#include <set>
#include <string>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <atomic>
#include <sys/select.h>
#include <sys/socket.h>

HINSTANCE afxCurrentResourceHandle = nullptr;

namespace {

struct FzWindow { WNDPROC proc = nullptr; INT_PTR userData = 0; DWORD owner = 0; };

// Every queued message is addressed to ONE thread, as on Windows: a thread message to the id given
// to PostThreadMessage, a window message to the thread that created the window. (All messages used
// to share one anonymous queue, so with two FTP sessions open one session's worker thread could
// swallow the other's commands, socket events and WM_QUIT.) The target id travels in MSG::time,
// which nothing else uses. Id 0 = "whoever asks" (not produced any more; kept as a safe fallback).
std::atomic<DWORD> g_nextTid{1};
thread_local DWORD t_tid = 0;
DWORD myTid() { if (t_tid == 0) t_tid = g_nextTid++; return t_tid; }

std::mutex g_mtx;
std::condition_variable g_cv;
std::deque<MSG> g_queue;                       // global message queue (filtered by hwnd on pop)
// u16string key (std::wstring is 4-byte-ABI in libc++ under -fshort-wchar -> broken comparisons).
static std::u16string k16(const wchar_t * p) { std::u16string r; if (p) while (*p) r.push_back((char16_t)*p++); return r; }
std::map<std::u16string, WNDPROC> g_classes;   // RegisterClassEx: class name -> wndproc

struct SockReg { HWND hwnd; UINT msg; long events; DWORD owner; };   // owner: thread of hwnd (captured while it is alive)
std::map<SOCKET, SockReg> g_sockets;           // WSAAsyncSelect registrations
std::map<SOCKET, long> g_posted;               // events already pending/posted per socket (latch)
std::set<SOCKET> g_listening;                  // sockets that had listen() called (see fz_mark_listening)
std::set<SOCKET> g_connectCalled;              // sockets that had connect() called (see fz_mark_connect)

// A socket that was created and registered but is not yet connected, connecting or listening has
// no events on Windows. Linux reports such a socket as writable AND readable (POLLOUT|POLLHUP), so
// the passive-mode data socket - created before the PASV reply, connected after it - got a bogus
// FD_CONNECT ("Data connection opened" before connecting), the one-shot latch then swallowed the
// real one, the socket never became "connected" and every listing/transfer timed out. (BSD/macOS
// report nothing for such a socket, which is why it only showed on Linux.)
bool socketIdle(SOCKET s)   // g_mtx held
{
  if (g_listening.count(s) != 0 || g_connectCalled.count(s) != 0) return false;
  sockaddr_storage ss; socklen_t l = sizeof ss;
  return ::getpeername(s, (sockaddr *)&ss, &l) < 0 && errno == ENOTCONN;   // accepted sockets have a peer
}

// Timers (SetTimer/KillTimer). Fired from the select loop.
struct FzTimer { HWND hwnd; DWORD owner; UINT_PTR id; UINT ms; std::chrono::steady_clock::time_point next; };
std::vector<FzTimer> g_timers;
UINT_PTR g_nextThreadTimerId = 0x7000;

std::thread g_selThread;
std::atomic<bool> g_selRun{false};
int g_wake[2] = { -1, -1 };                    // self-pipe to wake the select loop

void wakeSelect() { if (g_wake[1] >= 0) { char c = 1; (void)::write(g_wake[1], &c, 1); } }

bool queueHas(HWND hwnd, UINT msg, WPARAM w, LPARAM l)   // dedup a specific (socket,event) — NOT all
{ // events of a socket: collapsing all of them suppressed a data socket's FD_READ behind its pending
  for (const auto & m : g_queue) if (m.hwnd == hwnd && m.message == msg && m.wParam == w && m.lParam == l) return true;
  return false;   // FD_CONNECT/FD_WRITE, so downloads never read their data.
}

void postLocked(HWND hwnd, UINT msg, WPARAM w, LPARAM l, DWORD tid)
{
  g_queue.push_back(MSG{ hwnd, msg, w, l, tid });
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
        if (socketIdle(s)) continue;
        if (ev & (FD_READ | FD_ACCEPT | FD_CLOSE)) FD_SET(s, &rfds);
        if (ev & (FD_WRITE | FD_CONNECT)) FD_SET(s, &wfds);
        if (s > maxfd) maxfd = s;
      }
    }
    long waitUs = 100000;   // 100ms, or less if a timer is due sooner
    {
      std::lock_guard<std::mutex> lk(g_mtx);
      const auto now = std::chrono::steady_clock::now();
      for (const FzTimer & t : g_timers)
      {
        long us = (long)std::chrono::duration_cast<std::chrono::microseconds>(t.next - now).count();
        if (us < waitUs) waitUs = us < 1000 ? 1000 : us;
      }
    }
    timeval tv{ 0, (int)waitUs };
    int r = ::select(maxfd + 1, &rfds, &wfds, nullptr, &tv);
    {
      // due timers -> WM_TIMER (one pending message per timer at most, like Windows)
      std::lock_guard<std::mutex> lk(g_mtx);
      const auto now = std::chrono::steady_clock::now();
      for (FzTimer & t : g_timers)
      {
        if (t.next > now) continue;
        t.next = now + std::chrono::milliseconds(t.ms);
        if (!queueHas(t.hwnd, WM_TIMER, (WPARAM)t.id, 0)) postLocked(t.hwnd, WM_TIMER, (WPARAM)t.id, 0, t.owner);
      }
    }
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
        if ((fd == FD_WRITE || fd == FD_CONNECT) && (latch & fd)) return;  // one-shot for writable/connect
        LPARAM l = MAKELONG(fd, 0);
        if (queueHas(reg.hwnd, reg.msg, (WPARAM)s, l)) return;   // dedup this exact (socket,event)
        if (getenv("FZ_TRACE")) fprintf(stderr, "[fz] post sock=%d event=0x%lx\n", s, fd);
        postLocked(reg.hwnd, reg.msg, (WPARAM)s, l, reg.owner);
        latch |= fd;
      };
      if (FD_ISSET(s, &wfds)) { fire(FD_CONNECT); fire(FD_WRITE); }
      if (FD_ISSET(s, &rfds))
      {
        // the event mask is always 0x3f, so only a REAL listening socket gets FD_ACCEPT
        if (g_listening.count(s) != 0) { fire(FD_ACCEPT); }
        else
        {
          fire(FD_READ);   // level-triggered: drives FileZilla's state machine + reads data-channel bytes
          // FD_CLOSE on a REAL EOF only (peek==0), latched, so the data channel signals end-of-data.
          if ((reg.events & FD_CLOSE) && !(latch & FD_CLOSE))
          {
            char c;   // MSG_DONTWAIT: never block the select thread if FileZilla already drained the data
            if (::recv(s, &c, 1, MSG_PEEK | MSG_DONTWAIT) == 0)
            {
              if (getenv("FZ_TRACE")) fprintf(stderr, "[fz] post sock=%d event=0x20 (CLOSE/EOF)\n", s);
              postLocked(reg.hwnd, reg.msg, (WPARAM)s, MAKELONG(FD_CLOSE, 0), reg.owner);
              latch |= FD_CLOSE;
            }
          }
        }
      }
    }
  }
}

void ensureSelectThread()
{
  static std::once_flag once;
  std::call_once(once, [] {
    if (g_wake[0] < 0) { if (::pipe(g_wake) == 0) { ::fcntl(g_wake[0], F_SETFL, O_NONBLOCK); ::fcntl(g_wake[1], F_SETFL, O_NONBLOCK); } }
    g_selRun = true;
    g_selThread = std::thread(selectLoop);
    g_selThread.detach();
  });
}

} // namespace

//=== threading + sync emulation ============================================
HANDLE StartRoutineThread(unsigned long (* Routine)(void *), void * Parameter);   // rtlcompat (WinThreads.h)

namespace {
struct ThreadStart { LPTHREAD_START_ROUTINE fn; void * param; DWORD tid; };
unsigned long threadTrampoline(void * p)
{
  ThreadStart st = *static_cast<ThreadStart *>(p);
  delete static_cast<ThreadStart *>(p);
  t_tid = st.tid;                                  // the id PostThreadMessage will be given
  unsigned long r = st.fn ? st.fn(st.param) : 0;
  { std::lock_guard<std::mutex> lk(g_mtx);         // nobody will read this thread's messages any more
    for (auto it = g_timers.begin(); it != g_timers.end(); )
      if (it->owner == st.tid) it = g_timers.erase(it); else ++it;
    for (auto it = g_queue.begin(); it != g_queue.end(); )
      if (it->time == st.tid) it = g_queue.erase(it); else ++it; }
  return r;
}
}

HANDLE CreateThread(void *, size_t, LPTHREAD_START_ROUTINE fn, void * param, DWORD, DWORD * tid)
{
  DWORD id = g_nextTid++;
  if (tid) *tid = id;
  // The handle must be WAITABLE: CFileZillaApi::Destroy does Quit() + WaitForSingleObject(thread,
  // 10000) before TFileZillaIntern and the tools object are deleted. With a handle rtlcompat did
  // not know, that wait returned at once and the still-running thread touched freed memory
  // (crash when an FTP session was closed). CREATE_SUSPENDED is ignored; ResumeThread is a no-op.
  return StartRoutineThread(threadTrampoline, new ThreadStart{ fn, param, id });
}
// ResumeThread / SetThreadPriority / WaitForSingleObject / CloseHandle are provided by rtlcompat
// (WinThreads/SysExtra).
BOOL  PostThreadMessage(DWORD tid, UINT msg, WPARAM w, LPARAM l)
{ if (getenv("FZ_TRACE")) fprintf(stderr, "[fz] PostThreadMessage tid=%lu msg=%u wParam=%lu\n", (unsigned long)tid, msg, (unsigned long)w);
  std::lock_guard<std::mutex> lk(g_mtx); postLocked(nullptr, msg, w, l, tid); return TRUE; }  // thread msgs: hwnd=NULL
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
  w->owner = myTid();   // window messages go to the creating thread's queue
  if (cls) { std::lock_guard<std::mutex> lk(g_mtx); auto it = g_classes.find(k16(cls)); if (it != g_classes.end()) w->proc = it->second; }
  return (HWND)w;
}

BOOL DestroyWindow(HWND hWnd)
{
  if (!hWnd) return FALSE;
  { std::lock_guard<std::mutex> lk(g_mtx);
    for (auto it = g_timers.begin(); it != g_timers.end(); )  // its timers die with it
      if (it->hwnd == hWnd) it = g_timers.erase(it); else ++it;
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
  postLocked(hWnd, msg, wParam, lParam, hWnd ? ((FzWindow *)hWnd)->owner : myTid());
  return TRUE;
}

static bool matches(const MSG & m, HWND hwnd, UINT lo, UINT hi)
{
  if (m.time != 0 && m.time != myTid()) return false;   // addressed to another thread
  if (hwnd && m.hwnd != hwnd) return false;
  if (lo == 0 && hi == 0) return true;
  return m.message >= lo && m.message <= hi;
}

// WM_TIMER is a low-priority message on Win32: it is delivered only when nothing else is waiting.
// FileZilla depends on that order - e.g. the "transfer ended" notification must be handled before
// the next timeout check, or a control connection that was rightly silent during a long transfer is
// declared dead. pass 0 = everything but timers, pass 1 = timers.
BOOL PeekMessage(LPMSG out, HWND hWnd, UINT lo, UINT hi, UINT removeFlag)
{
  std::lock_guard<std::mutex> lk(g_mtx);
  for (int pass = 0; pass < 2; ++pass)
  for (auto it = g_queue.begin(); it != g_queue.end(); ++it)
    if (((it->message == WM_TIMER) == (pass == 1)) && matches(*it, hWnd, lo, hi))
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
    for (int pass = 0; pass < 2; ++pass)
    for (auto it = g_queue.begin(); it != g_queue.end(); ++it)
      if (((it->message == WM_TIMER) == (pass == 1)) && matches(*it, hWnd, lo, hi))
      {
        MSG m = *it; g_queue.erase(it);
        if (out) *out = m;
        // Win32: GetMessage returns 0 for WM_QUIT - that is what ends `while (GetMessage(...))`.
        // It used to return TRUE, so CMainThread::Run never left its loop: the FTP worker thread
        // outlived its session (and the objects it points to) every time a session was closed.
        return (m.hwnd == nullptr && m.message == WM_QUIT) ? FALSE : TRUE;
      }
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

void fz_mark_listening(SOCKET s, bool on)
{
  std::lock_guard<std::mutex> lk(g_mtx);
  if (on) g_listening.insert(s); else { g_listening.erase(s); g_connectCalled.erase(s); }   // off = closed
}

void fz_mark_connect(SOCKET s)
{
  { std::lock_guard<std::mutex> lk(g_mtx);
    g_connectCalled.insert(s);
    auto it = g_posted.find(s); if (it != g_posted.end()) it->second = 0; }   // its events start now
  wakeSelect();
}

int WSAAsyncSelect(SOCKET s, HWND hWnd, UINT msg, long lEvent)
{
  if (getenv("FZ_TRACE")) fprintf(stderr, "[fz] WSAAsyncSelect sock=%d msg=%u events=0x%lx\n", s, msg, lEvent);
  std::lock_guard<std::mutex> lk(g_mtx);
  if (lEvent == 0) { g_sockets.erase(s); g_posted.erase(s); }
  else { g_sockets[s] = SockReg{ hWnd, msg, lEvent, hWnd ? ((FzWindow *)hWnd)->owner : myTid() }; g_posted[s] = 0; }
  // (re)start + wake the select loop; ensureSelectThread re-locks via atomics only, safe here.
  wakeSelect();
  ensureSelectThread();
  return 0;
}

UINT_PTR SetTimer(HWND hWnd, UINT_PTR id, UINT ms, TIMERPROC)
{
  {
    std::lock_guard<std::mutex> lk(g_mtx);
    const DWORD owner = hWnd ? ((FzWindow *)hWnd)->owner : myTid();
    if (!hWnd) id = g_nextThreadTimerId++;                 // thread timer: the system picks the id
    FzTimer t{ hWnd, owner, id, ms ? ms : 1, std::chrono::steady_clock::now() + std::chrono::milliseconds(ms ? ms : 1) };
    bool replaced = false;
    for (FzTimer & e : g_timers) if (e.hwnd == hWnd && e.id == id && e.owner == owner) { e = t; replaced = true; }
    if (!replaced) g_timers.push_back(t);
  }
  ensureSelectThread();
  wakeSelect();
  return id;
}

BOOL KillTimer(HWND hWnd, UINT_PTR id)
{
  std::lock_guard<std::mutex> lk(g_mtx);
  bool found = false;
  for (auto it = g_timers.begin(); it != g_timers.end(); )
    if (it->hwnd == hWnd && it->id == id) { it = g_timers.erase(it); found = true; } else ++it;
  for (auto it = g_queue.begin(); it != g_queue.end(); )    // and its pending notifications
    if (it->hwnd == hWnd && it->message == WM_TIMER && it->wParam == (WPARAM)id) it = g_queue.erase(it); else ++it;
  return found ? TRUE : FALSE;
}
