//---------------------------------------------------------------------------
// Threading.cpp — Win32 thread/event emulation (std::thread + condition_variable) and the
// engine's StartThread (declared in Interface.h). HANDLEs are integer ids in g_objects.
//---------------------------------------------------------------------------
#include "winscp/WinThreads.h"
#include "Classes.hpp"          // TThreadFunc
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <memory>
#include <chrono>

// Socket-readiness pump (WinSock.cpp): signals WSA event handles whose sockets are ready, so the
// engine's WaitForMultipleObjects loop wakes on network activity. Weakly used; harmless if no
// sockets are registered.
extern "C" int winscp_pump_socket_events(void);

namespace {

struct WaitObject
{
  virtual ~WaitObject() {}
  // returns true if signaled within ms (INFINITE waits forever)
  virtual bool Wait(DWORD ms) = 0;
};

struct EventObject : WaitObject
{
  std::mutex M;
  std::condition_variable CV;
  bool Signaled;
  bool Manual;
  EventObject(bool manual, bool initial) : Signaled(initial), Manual(manual) {}
  void Set()   { std::lock_guard<std::mutex> l(M); Signaled = true; CV.notify_all(); }
  void Reset() { std::lock_guard<std::mutex> l(M); Signaled = false; }
  bool Wait(DWORD ms) override
  {
    std::unique_lock<std::mutex> l(M);
    bool ok = true;
    if (ms == INFINITE) CV.wait(l, [&]{ return Signaled; });
    else ok = CV.wait_for(l, std::chrono::milliseconds(ms), [&]{ return Signaled; });
    if (ok && !Manual) Signaled = false;   // auto-reset
    return ok;
  }
};

struct ThreadObject : WaitObject
{
  std::thread Th;
  std::mutex M;
  std::condition_variable StartCV;
  bool Resumed = false;
  bool Done = false;
  std::condition_variable DoneCV;
  TThreadFunc Fn;
  void * Param;
  DWORD ExitCode = 0;

  ThreadObject(TThreadFunc fn, void * param, bool suspended) : Fn(fn), Param(param)
  {
    Resumed = !suspended;
    Th = std::thread([this]{
      { std::unique_lock<std::mutex> l(M); StartCV.wait(l, [&]{ return Resumed; }); }
      ExitCode = static_cast<DWORD>(Fn(Param));
      { std::lock_guard<std::mutex> l(M); Done = true; } DoneCV.notify_all();
    });
  }
  ~ThreadObject() override { if (Th.joinable()) { Resume(); Th.join(); } }
  void Resume() { { std::lock_guard<std::mutex> l(M); Resumed = true; } StartCV.notify_all(); }
  bool Wait(DWORD ms) override
  {
    std::unique_lock<std::mutex> l(M);
    if (ms == INFINITE) { DoneCV.wait(l, [&]{ return Done; }); return true; }
    return DoneCV.wait_for(l, std::chrono::milliseconds(ms), [&]{ return Done; });
  }
};

// A thread started through the Win32-style CreateThread (the FileZilla backend). Its HANDLE is
// signaled when the routine returns, and may be closed at any moment - also by the thread itself
// (CMainThread::Run ends with `delete this`, whose destructor closes its own handle) and while
// another thread is still waiting on it (CFileZillaApi::Destroy waits up to 10 s for the thread to
// finish before the objects it uses are freed).
struct RoutineThreadObject : WaitObject
{
  std::mutex M;
  std::condition_variable CV;
  bool Done = false;
  void SetDone() { { std::lock_guard<std::mutex> l(M); Done = true; } CV.notify_all(); }
  bool Wait(DWORD ms) override
  {
    std::unique_lock<std::mutex> l(M);
    if (ms == INFINITE) { CV.wait(l, [&]{ return Done; }); return true; }
    return CV.wait_for(l, std::chrono::milliseconds(ms), [&]{ return Done; });
  }
};

// Objects are shared: whoever looked one up keeps it alive for the duration of its call, so
// CloseHandle on one thread cannot free an event/thread another thread is waiting on.
typedef std::shared_ptr<WaitObject> WaitObjectPtr;
std::mutex g_mtx;
std::map<int, WaitObjectPtr> g_objects;
int g_next = 1;

int Register(WaitObjectPtr o) { std::lock_guard<std::mutex> l(g_mtx); int id = g_next++; g_objects[id] = std::move(o); return id; }
int Register(WaitObject * o) { return Register(WaitObjectPtr(o)); }
WaitObjectPtr Lookup(HANDLE h) { std::lock_guard<std::mutex> l(g_mtx); auto it = g_objects.find((int)(NativeInt)h); return it == g_objects.end() ? WaitObjectPtr() : it->second; }
HANDLE AsHandle(int id) { return (HANDLE)(NativeInt)id; }

} // namespace

HANDLE __fastcall CreateEvent(void *, BOOL ManualReset, BOOL InitialState, const wchar_t *)
{ return AsHandle(Register(new EventObject(ManualReset != 0, InitialState != 0))); }
BOOL __fastcall SetEvent(HANDLE Event)   { WaitObjectPtr sp = Lookup(Event); auto * e = dynamic_cast<EventObject *>(sp.get()); if (e) e->Set(); return e != nullptr; }
BOOL __fastcall ResetEvent(HANDLE Event) { WaitObjectPtr sp = Lookup(Event); auto * e = dynamic_cast<EventObject *>(sp.get()); if (e) e->Reset(); return e != nullptr; }
BOOL __fastcall PulseEvent(HANDLE Event) { WaitObjectPtr sp = Lookup(Event); auto * e = dynamic_cast<EventObject *>(sp.get()); if (e) { e->Set(); e->Reset(); } return e != nullptr; }

DWORD __fastcall WaitForSingleObject(HANDLE Object, DWORD Milliseconds)
{ WaitObjectPtr o = Lookup(Object); if (!o) return WAIT_FAILED; return o->Wait(Milliseconds) ? WAIT_OBJECT_0 : WAIT_TIMEOUT; }

DWORD __fastcall WaitForMultipleObjects(DWORD Count, const HANDLE * Handles, BOOL WaitAll, DWORD Milliseconds)
{
  // Simplified: WaitAll waits each in turn; else poll until one signals or timeout.
  if (WaitAll)
  { for (DWORD i = 0; i < Count; ++i) { WaitObjectPtr o = Lookup(Handles[i]); if (!o || !o->Wait(Milliseconds)) return WAIT_TIMEOUT; } return WAIT_OBJECT_0; }
  const bool infinite = (Milliseconds == INFINITE);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(infinite ? 0 : Milliseconds);
  for (;;)
  {
    winscp_pump_socket_events();   // wake FSocketEvent when its socket becomes ready (WinSock.cpp)
    for (DWORD i = 0; i < Count; ++i) { WaitObjectPtr o = Lookup(Handles[i]); if (o && o->Wait(0)) return WAIT_OBJECT_0 + i; }
    if (!infinite && std::chrono::steady_clock::now() >= deadline) return WAIT_TIMEOUT;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

DWORD __fastcall ResumeThread(HANDLE Thread)  { WaitObjectPtr sp = Lookup(Thread); auto * t = dynamic_cast<ThreadObject *>(sp.get()); if (t) t->Resume(); return 0; }
DWORD __fastcall SuspendThread(HANDLE)        { return 0; }  // not supported post-start
BOOL  __fastcall SetThreadPriority(HANDLE, int) { return TRUE; }
int   __fastcall GetThreadPriority(HANDLE)    { return THREAD_PRIORITY_NORMAL; }
DWORD __fastcall GetCurrentThreadId()         { return (DWORD)(NativeUInt)std::hash<std::thread::id>{}(std::this_thread::get_id()); }
HANDLE __fastcall GetCurrentThread()          { return (HANDLE)(NativeInt)-2; }
BOOL  __fastcall GetExitCodeThread(HANDLE Thread, DWORD * ExitCode)
{ WaitObjectPtr sp = Lookup(Thread); auto * t = dynamic_cast<ThreadObject *>(sp.get()); if (t && ExitCode) *ExitCode = t->ExitCode; return t != nullptr; }
BOOL  __fastcall TerminateThread(HANDLE, DWORD) { return FALSE; }  // unsafe on POSIX; no-op

// CloseHandle for our objects (overrides the SysExtra stub via the linker? No — defined once
// here; SysExtra's stub is removed).
BOOL __fastcall CloseHandle(HANDLE h)
{
  WaitObjectPtr dying;   // destroyed after g_mtx is released (a ThreadObject joins in its destructor)
  {
    std::lock_guard<std::mutex> l(g_mtx);
    auto it = g_objects.find((int)(NativeInt)h);
    if (it != g_objects.end()) { dying = std::move(it->second); g_objects.erase(it); }
  }
  return TRUE;
}

HANDLE __fastcall StartRoutineThread(unsigned long (* Routine)(void *), void * Parameter)
{
  auto obj = std::make_shared<RoutineThreadObject>();
  int id = Register(WaitObjectPtr(obj));
  std::thread([obj, Routine, Parameter]{ if (Routine) Routine(Parameter); obj->SetDone(); }).detach();
  return AsHandle(id);
}

// Engine's StartThread (Interface.h). Returns the thread HANDLE as int.
int __fastcall StartThread(void *, unsigned, TThreadFunc ThreadFunc, void * Parameter,
                           unsigned CreationFlags, TThreadID & ThreadId)
{
  bool suspended = (CreationFlags & CREATE_SUSPENDED) != 0;
  int id = Register(new ThreadObject(ThreadFunc, Parameter, suspended));
  ThreadId = (TThreadID)id;
  return id;
}
