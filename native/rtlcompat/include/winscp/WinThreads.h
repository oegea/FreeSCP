//---------------------------------------------------------------------------
// WinThreads.h — Win32 thread + event API emulated over std::thread/condition_variable.
// HANDLEs are small integer ids cast to HANDLE (an id table avoids 64-bit pointer
// truncation, since the engine's StartThread returns int). Phase 2 threading adapter.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_WINTHREADS_H
#define WINSCP_RTLCOMPAT_WINTHREADS_H

#include "winscp/rtldefs.h"
#include "winscp/wintypes.h"
#include "winscp/WinCompat.h"

// Events (manual/auto-reset).
HANDLE __fastcall CreateEvent(void * SecAttrs, BOOL ManualReset, BOOL InitialState, const wchar_t * Name);
BOOL   __fastcall SetEvent(HANDLE Event);
BOOL   __fastcall ResetEvent(HANDLE Event);
BOOL   __fastcall PulseEvent(HANDLE Event);

// Waiting.
DWORD __fastcall WaitForSingleObject(HANDLE Object, DWORD Milliseconds);
DWORD __fastcall WaitForMultipleObjects(DWORD Count, const HANDLE * Handles, BOOL WaitAll, DWORD Milliseconds);

// Threads.
DWORD __fastcall ResumeThread(HANDLE Thread);
DWORD __fastcall SuspendThread(HANDLE Thread);
BOOL  __fastcall SetThreadPriority(HANDLE Thread, int Priority);
int   __fastcall GetThreadPriority(HANDLE Thread);
DWORD __fastcall GetCurrentThreadId();
HANDLE __fastcall GetCurrentThread();
BOOL  __fastcall GetExitCodeThread(HANDLE Thread, DWORD * ExitCode);
BOOL  __fastcall TerminateThread(HANDLE Thread, DWORD ExitCode);

#endif
