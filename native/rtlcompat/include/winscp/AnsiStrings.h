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
  int Pos(char c) const { std::string::size_type p = FData.find(c); return (p == std::string::npos) ? 0 : (int)p + 1; }
  int Pos(const AnsiStringBase & sub) const
  { std::string::size_type p = FData.find(sub.FData); return (p == std::string::npos) ? 0 : static_cast<int>(p) + 1; }
  void Delete(int index, int count)
  { if (index >= 1 && index <= Length() && count > 0) FData.erase(static_cast<size_t>(index - 1), static_cast<size_t>(count)); }
  char * Unique() { return &FData[0]; }
  AnsiStringBase SubString(int start, int count) const { if (start<1){count+=start-1;start=1;} if (count<=0||start>Length()) return AnsiStringBase(); return AnsiStringBase(FData.substr(start-1,count)); }
  AnsiStringBase SubString(int start) const { return SubString(start, Length()-start+1); }
  AnsiStringBase & operator=(const char * s) { FData = s ? s : ""; return *this; }
  AnsiStringBase & operator=(const wchar_t * s) { FData.clear(); if (s) while (*s) FData.push_back((char)(*s++)); return *this; }
  AnsiStringBase & operator+=(char c) { FData.push_back(c); return *this; }
  AnsiStringBase & operator+=(const AnsiStringBase & o) { FData += o.FData; return *this; }
  AnsiStringBase & operator+=(const char * s) { if (s) FData += s; return *this; }
  AnsiStringBase operator+(const AnsiStringBase & o) const { AnsiStringBase r(*this); r.FData += o.FData; return r; }
  bool operator==(const AnsiStringBase & o) const { return FData == o.FData; }
  bool operator!=(const AnsiStringBase & o) const { return FData != o.FData; }

protected:
  std::string FData;
};

class AnsiString : public AnsiStringBase
{ public: using AnsiStringBase::AnsiStringBase;
  using AnsiStringBase::operator=;
  AnsiString() = default;
  AnsiString(const AnsiStringBase & o) : AnsiStringBase(o.raw()) {}  // from Raw/UTF8
  AnsiString(const UnicodeString & s);                              // UTF-16 -> latin1
};
class RawByteString : public AnsiStringBase
{ public: using AnsiStringBase::AnsiStringBase;
  using AnsiStringBase::operator=;
  RawByteString() = default;
  RawByteString(const AnsiStringBase & o) : AnsiStringBase(o.raw()) {}
  RawByteString(const UnicodeString & s);
};
class UTF8String : public AnsiStringBase
{ public: using AnsiStringBase::AnsiStringBase;
  using AnsiStringBase::operator=;
  UTF8String() = default;
  UTF8String(const AnsiStringBase & o) : AnsiStringBase(o.raw()) {}
  UTF8String(const UnicodeString & s);                             // UTF-16 -> UTF-8
};

#endif
