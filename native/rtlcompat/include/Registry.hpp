//---------------------------------------------------------------------------
// Registry.hpp — System::Win::Registry subset (TRegistry / TRegistryIniFile).
// On the native port the Windows registry is replaced by file-based storage (Phase 2);
// this declares the surface TRegistryStorage references so the headers parse. Method
// bodies are provided by the platform adapter, not here.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_REGISTRY_HPP
#define WINSCP_RTLCOMPAT_REGISTRY_HPP

#include "winscp/rtldefs.h"
#include "winscp/wintypes.h"
#include "winscp/UnicodeString.h"
#include "Classes.hpp"

class TRegistry : public TObject
{
public:
  __fastcall TRegistry(unsigned int Access = 0);
  __fastcall ~TRegistry();
  HKEY RootKey = nullptr;
  HKEY CurrentKey = nullptr;
  bool __fastcall OpenKey(const UnicodeString & Key, bool CanCreate);
  bool __fastcall KeyExists(const UnicodeString & Key);
  bool __fastcall DeleteKey(const UnicodeString & Key);
  bool __fastcall ValueExists(const UnicodeString & Name);
  void __fastcall CloseKey();
  void __fastcall GetKeyNames(TStrings * Strings);
  void __fastcall GetValueNames(TStrings * Strings);
};

#endif
