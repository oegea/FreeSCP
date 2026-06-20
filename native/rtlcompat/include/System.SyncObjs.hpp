//---------------------------------------------------------------------------
// System.SyncObjs.hpp — emulation of Embarcadero System::Syncobjs (subset).
// TCriticalSection -> std::recursive_mutex (Delphi critical sections are reentrant).
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_SYSTEM_SYNCOBJS_HPP
#define WINSCP_RTLCOMPAT_SYSTEM_SYNCOBJS_HPP

#include "winscp/rtldefs.h"
#include <mutex>

class TCriticalSection
{
public:
  __fastcall TCriticalSection() = default;
  __fastcall ~TCriticalSection() = default;
  void __fastcall Enter() { FMutex.lock(); }
  void __fastcall Leave() { FMutex.unlock(); }
  bool __fastcall TryEnter() { return FMutex.try_lock(); }

private:
  std::recursive_mutex FMutex;
};

#endif
