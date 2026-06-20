//---------------------------------------------------------------------------
// Registry.hpp — System::Win::Registry + System::IniFiles subset.
//
// On the native port there is no Windows registry: TRegistryStorage is a compile-only path
// (Configuration selects INI/file storage on non-Windows), so TRegistry's methods are honest
// stubs that report "absent" (ReadXxx throw / KeyExists false). The real storage backend is
// TCustomIniFile / TMemIniFile (file-backed INI), implemented in IniFiles.cpp.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_REGISTRY_HPP
#define WINSCP_RTLCOMPAT_REGISTRY_HPP

#include "winscp/rtldefs.h"
#include "winscp/wintypes.h"
#include "winscp/UnicodeString.h"
#include "SysUtils.hpp"   // TDateTime
#include "Classes.hpp"

class TRegistry : public TObject
{
public:
  __fastcall TRegistry(unsigned int Access = 0) : FAccess(Access) {}
  __fastcall ~TRegistry() {}
  HKEY RootKey = nullptr;
  HKEY CurrentKey = nullptr;
  unsigned int Access = 0;                 // KEY_READ | KEY_WRITE | wow flags (unused natively)
  UnicodeString CurrentPath;               // current open subkey path
  bool __fastcall OpenKey(const UnicodeString & Key, bool CanCreate) { CurrentPath = Key; CurrentKey = nullptr; return false; }
  bool __fastcall KeyExists(const UnicodeString & Key) { return false; }
  bool __fastcall DeleteKey(const UnicodeString & Key) { return false; }
  bool __fastcall DeleteValue(const UnicodeString & Name) { return false; }
  bool __fastcall ValueExists(const UnicodeString & Name) { return false; }
  void __fastcall CloseKey() { CurrentKey = nullptr; CurrentPath = UnicodeString(); }
  void __fastcall GetKeyNames(TStrings * Strings) {}
  void __fastcall GetValueNames(TStrings * Strings) {}
  size_t __fastcall GetDataSize(const UnicodeString & Name) { return 0; }
  // value read/write — registry is absent natively; reads are unreachable (callers gate on
  // ValueExists()==false first) so they return defaults; writes no-op.
  bool __fastcall ReadBool(const UnicodeString & Name) { return false; }
  int __fastcall ReadInteger(const UnicodeString & Name) { return 0; }
  __int64 __fastcall ReadInt64(const UnicodeString & Name) { return 0; }
  UnicodeString __fastcall ReadString(const UnicodeString & Name) { return UnicodeString(); }
  double __fastcall ReadFloat(const UnicodeString & Name) { return 0; }
  TDateTime __fastcall ReadDateTime(const UnicodeString & Name) { return TDateTime(); }
  size_t __fastcall ReadBinaryData(const UnicodeString & Name, void * Buffer, size_t Size) { return 0; }
  void __fastcall WriteBool(const UnicodeString & Name, bool Value) {}
  void __fastcall WriteInteger(const UnicodeString & Name, int Value) {}
  void __fastcall WriteString(const UnicodeString & Name, const UnicodeString & Value) {}
  void __fastcall WriteBinaryData(const UnicodeString & Name, const void * Buffer, size_t Size) {}
private:
  unsigned int FAccess = 0;
};

// System::IniFiles subset — the real file-backed config storage on the native port.
class TStringList;
class TCustomIniFile : public TObject
{
public:
  __fastcall TCustomIniFile(const UnicodeString & FileName) : FFileName(FileName) {}
  virtual __fastcall ~TCustomIniFile() {}
  UnicodeString __fastcall GetFileName() { return FFileName; }
  __declspec(property(get=GetFileName)) UnicodeString FileName;
  virtual UnicodeString __fastcall ReadString(const UnicodeString & Section, const UnicodeString & Ident, const UnicodeString & Default);
  virtual void __fastcall WriteString(const UnicodeString & Section, const UnicodeString & Ident, const UnicodeString & Value);
  virtual int  __fastcall ReadInteger(const UnicodeString & Section, const UnicodeString & Ident, int Default);
  virtual void __fastcall WriteInteger(const UnicodeString & Section, const UnicodeString & Ident, int Value);
  virtual bool __fastcall ReadBool(const UnicodeString & Section, const UnicodeString & Ident, bool Default);
  virtual void __fastcall WriteBool(const UnicodeString & Section, const UnicodeString & Ident, bool Value);
  virtual bool __fastcall ValueExists(const UnicodeString & Section, const UnicodeString & Ident);
  virtual bool __fastcall SectionExists(const UnicodeString & Section);
  virtual void __fastcall DeleteKey(const UnicodeString & Section, const UnicodeString & Ident);
  virtual void __fastcall EraseSection(const UnicodeString & Section);
  virtual void __fastcall ReadSection(const UnicodeString & Section, TStrings * Strings);
  virtual void __fastcall ReadSections(TStrings * Strings);
  virtual void __fastcall UpdateFile();   // persist to FFileName (UTF-8 INI)
protected:
  UnicodeString FFileName;
  // section -> ordered (ident,value) pairs. std::map<UnicodeString> keyed by std::u16string-ish.
  void __fastcall EnsureLoaded();
  bool FLoaded = false;
  // opaque backing in IniFiles.cpp via pImpl-ish vector; kept here so TMemIniFile shares it.
  void * FData = nullptr;   // -> Impl
  struct Impl & __fastcall D();
};

// TMemIniFile parses on construction and can dump every section/key as "Section" + "k=v" lines.
class TMemIniFile : public TCustomIniFile
{
public:
  __fastcall TMemIniFile(const UnicodeString & FileName) : TCustomIniFile(FileName) { EnsureLoaded(); }
  void __fastcall GetStrings(TStrings * Strings);
  void __fastcall SetStrings(TStrings * Strings);
};
class TIniFile : public TMemIniFile { public: using TMemIniFile::TMemIniFile; };
class TRegistryIniFile : public TMemIniFile { public: using TMemIniFile::TMemIniFile; };

#endif
