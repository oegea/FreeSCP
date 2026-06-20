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
  // value read/write (file-backed in the platform adapter; declared here for compile)
  bool __fastcall ReadBool(const UnicodeString & Name);
  int __fastcall ReadInteger(const UnicodeString & Name);
  __int64 __fastcall ReadInt64(const UnicodeString & Name);
  UnicodeString __fastcall ReadString(const UnicodeString & Name);
  double __fastcall ReadFloat(const UnicodeString & Name);
  void __fastcall WriteBool(const UnicodeString & Name, bool Value);
  void __fastcall WriteInteger(const UnicodeString & Name, int Value);
  void __fastcall WriteString(const UnicodeString & Name, const UnicodeString & Value);
};

// System::Inifiles subset — base for TCustomIniFileStorage. File-backed config (Phase 2).
class TCustomIniFile : public TObject
{
public:
  __fastcall TCustomIniFile(const UnicodeString & FileName) : FFileName(FileName) {}
  virtual __fastcall ~TCustomIniFile() {}
  UnicodeString __fastcall GetFileName() { return FFileName; }
  __declspec(property(get=GetFileName)) UnicodeString FileName;
  virtual UnicodeString __fastcall ReadString(const UnicodeString & Section, const UnicodeString & Ident, const UnicodeString & Default);
  virtual void __fastcall WriteString(const UnicodeString & Section, const UnicodeString & Ident, const UnicodeString & Value);
  virtual void __fastcall ReadSection(const UnicodeString & Section, TStrings * Strings);
  virtual void __fastcall ReadSections(TStrings * Strings);
protected:
  UnicodeString FFileName;
};

class TMemIniFile : public TCustomIniFile { public: using TCustomIniFile::TCustomIniFile; };
class TIniFile : public TCustomIniFile { public: using TCustomIniFile::TCustomIniFile; };

#endif
