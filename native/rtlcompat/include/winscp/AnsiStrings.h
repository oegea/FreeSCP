//---------------------------------------------------------------------------
// AnsiStrings.h — the byte-string family (AnsiString / RawByteString / UTF8String).
// In Delphi these are distinct types (drive overload resolution) backed by bytes.
// Distinct subclasses keep the overloads in Common.h (PackStr/Shred) resolvable.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_ANSISTRINGS_H
#define WINSCP_RTLCOMPAT_ANSISTRINGS_H

#include "winscp/rtldefs.h"
#include <string>

class UnicodeString;  // for cross-conversion ctors (defined in src/StringConv.cpp)

class AnsiStringBase
{
public:
  AnsiStringBase() = default;
  AnsiStringBase(const char * s) : FData(s ? s : "") {}
  AnsiStringBase(const char * s, int len) : FData(s, static_cast<size_t>(len)) {}
  AnsiStringBase(const std::string & s) : FData(s) {}

  int Length() const { return static_cast<int>(FData.size()); }
  bool IsEmpty() const { return FData.empty(); }
  const char * c_str() const { return FData.c_str(); }
  char * c_str() { return &FData[0]; }
  char operator[](int index) const { return FData[static_cast<size_t>(index - 1)]; }
  char & operator[](int index) { return FData[static_cast<size_t>(index - 1)]; }
  void SetLength(int len) { FData.resize(static_cast<size_t>(len)); }
  const std::string & raw() const { return FData; }
  std::string & raw() { return FData; }

  // Delphi 1-based Pos/Delete on byte strings.
  int Pos(const AnsiStringBase & sub) const
  { std::string::size_type p = FData.find(sub.FData); return (p == std::string::npos) ? 0 : static_cast<int>(p) + 1; }
  void Delete(int index, int count)
  { if (index >= 1 && index <= Length() && count > 0) FData.erase(static_cast<size_t>(index - 1), static_cast<size_t>(count)); }
  char * Unique() { return &FData[0]; }

protected:
  std::string FData;
};

class AnsiString : public AnsiStringBase
{ public: using AnsiStringBase::AnsiStringBase;
  AnsiString() = default;
  AnsiString(const AnsiStringBase & o) : AnsiStringBase(o.raw()) {}  // from Raw/UTF8
  AnsiString(const UnicodeString & s);                              // UTF-16 -> latin1
};
class RawByteString : public AnsiStringBase
{ public: using AnsiStringBase::AnsiStringBase;
  RawByteString() = default;
  RawByteString(const AnsiStringBase & o) : AnsiStringBase(o.raw()) {}
  RawByteString(const UnicodeString & s);
};
class UTF8String : public AnsiStringBase
{ public: using AnsiStringBase::AnsiStringBase;
  UTF8String() = default;
  UTF8String(const AnsiStringBase & o) : AnsiStringBase(o.raw()) {}
  UTF8String(const UnicodeString & s);                             // UTF-16 -> UTF-8
};

#endif
