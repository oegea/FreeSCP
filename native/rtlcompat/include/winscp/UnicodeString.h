//---------------------------------------------------------------------------
// UnicodeString.h — Delphi/C++Builder System::UnicodeString emulation.
//
// WinSCP uses ~5190 UnicodeString references. The Embarcadero type is UTF-16 with
// *1-based* indexing (s[1] is the first char) and value semantics.
//
// Storage is std::u16string (char16_t, always 2 bytes, with a pure-template char_traits —
// avoids the -fshort-wchar + libc wmem* ABI trap that std::wstring would hit). The engine
// is still compiled with -fshort-wchar so its L"..." literals are 2-byte; we convert
// wchar_t -> char16_t element-wise so it is correct whatever wchar_t's width.
//
// Minimal slice to de-risk Phase 1; grows on demand as core files compile leaf-first.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_UNICODESTRING_H
#define WINSCP_RTLCOMPAT_UNICODESTRING_H

#include <string>
#include <cstddef>

class UnicodeString
{
public:
  UnicodeString() = default;
  UnicodeString(const char16_t * s) : FData(s ? s : u"") {}
  UnicodeString(const char16_t * s, int len) : FData(s, s + len) {}
  UnicodeString(const std::u16string & s) : FData(s) {}
  UnicodeString(char16_t c, int count) : FData(static_cast<size_t>(count), c) {}

  // C++Builder UnicodeString has implicit numeric constructors that render the value as
  // its decimal text (engine relies on int/int64 -> string conversions). Non-explicit to
  // match Embarcadero overload resolution.
  UnicodeString(int value) { AppendAscii(std::to_string(value)); }
  UnicodeString(long long value) { AppendAscii(std::to_string(value)); }
  UnicodeString(unsigned int value) { AppendAscii(std::to_string(value)); }

  // From the engine's wchar_t literals/buffers (2-byte under -fshort-wchar; converted
  // element-wise so it is correct even if wchar_t is 4 bytes).
  UnicodeString(const wchar_t * s) { if (s) while (*s) FData.push_back(static_cast<char16_t>(*s++)); }
  // C++Builder UnicodeString accepts narrow (ANSI) literals/strings too.
  UnicodeString(const char * s) { if (s) while (*s) FData.push_back(static_cast<char16_t>(static_cast<unsigned char>(*s++))); }
  UnicodeString(const wchar_t * s, int len)
  { for (int i = 0; i < len; ++i) FData.push_back(static_cast<char16_t>(s[i])); }

  // Delphi semantics: Length() is the char count; indexing is 1-based.
  int Length() const { return static_cast<int>(FData.size()); }
  bool IsEmpty() const { return FData.empty(); }

  char16_t & operator[](int index) { return FData[static_cast<size_t>(index - 1)]; }
  char16_t operator[](int index) const { return FData[static_cast<size_t>(index - 1)]; }

  const char16_t * c_str() const { return FData.c_str(); }
  const std::u16string & raw() const { return FData; }

  // SubString(start, count) — 1-based start, Delphi semantics.
  UnicodeString SubString(int start, int count) const
  {
    if (start < 1) { count += (start - 1); start = 1; }
    if (count <= 0 || start > Length()) return UnicodeString();
    return UnicodeString(FData.substr(static_cast<size_t>(start - 1),
                                      static_cast<size_t>(count)));
  }
  UnicodeString SubString(int start) const { return SubString(start, Length() - start + 1); }

  // Pos(sub) — 1-based index of first match, 0 if none (Delphi).
  int Pos(const UnicodeString & sub) const
  {
    std::u16string::size_type p = FData.find(sub.FData);
    return (p == std::u16string::npos) ? 0 : static_cast<int>(p) + 1;
  }

  UnicodeString & operator+=(const UnicodeString & rhs) { FData += rhs.FData; return *this; }
  friend UnicodeString operator+(UnicodeString lhs, const UnicodeString & rhs)
  { lhs.FData += rhs.FData; return lhs; }

  bool operator==(const UnicodeString & rhs) const { return FData == rhs.FData; }
  bool operator!=(const UnicodeString & rhs) const { return FData != rhs.FData; }
  bool operator<(const UnicodeString & rhs) const { return FData < rhs.FData; }

private:
  void AppendAscii(const std::string & s) { for (char c : s) FData.push_back(static_cast<char16_t>(c)); }
  std::u16string FData;
};

#endif
