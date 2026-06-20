//---------------------------------------------------------------------------
// AnsiStrings.h — the byte-string family (AnsiString / RawByteString / UTF8String).
// In Delphi these are distinct types (drive overload resolution) backed by bytes.
// Distinct subclasses keep the overloads in Common.h (PackStr/Shred) resolvable.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_ANSISTRINGS_H
#define WINSCP_RTLCOMPAT_ANSISTRINGS_H

#include "winscp/rtldefs.h"
#include <string>

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

protected:
  std::string FData;
};

class AnsiString    : public AnsiStringBase { public: using AnsiStringBase::AnsiStringBase; };
class RawByteString : public AnsiStringBase { public: using AnsiStringBase::AnsiStringBase; };
class UTF8String    : public AnsiStringBase { public: using AnsiStringBase::AnsiStringBase; };

#endif
