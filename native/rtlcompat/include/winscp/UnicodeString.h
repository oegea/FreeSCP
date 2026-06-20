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
#include <cstdlib>
#include "winscp/AnsiStrings.h"

class UnicodeString
{
public:
  UnicodeString() = default;
  UnicodeString(const char16_t * s) : FData(s ? s : u"") {}
  UnicodeString(const char16_t * s, int len) : FData(s, s + len) {}
  UnicodeString(const std::u16string & s) : FData(s) {}
  // count defaults to 1 so a lone char (e.g. Delphi's PathDelim) implicitly becomes a string.
  UnicodeString(char16_t c, int count = 1) : FData(static_cast<size_t>(count), c) {}

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
  // From the byte-string family (latin1 widening; UTF-8 decode for UTF8String is a TODO).
  UnicodeString(const AnsiStringBase & s)
  { for (char c : s.raw()) FData.push_back(static_cast<char16_t>(static_cast<unsigned char>(c))); }
  UnicodeString(const wchar_t * s, int len)
  { for (int i = 0; i < len; ++i) FData.push_back(static_cast<char16_t>(s[i])); }
  // narrow (ANSI/ASCII) buffer + length — e.g. from a putty strbuf of escaped key chars.
  UnicodeString(const char * s, int len)
  { for (int i = 0; i < len; ++i) FData.push_back(static_cast<char16_t>(static_cast<unsigned char>(s[i]))); }

  // Delphi semantics: Length() is the char count; indexing is 1-based.
  int Length() const { return static_cast<int>(FData.size()); }
  bool IsEmpty() const { return FData.empty(); }

  // Public API is wchar_t-typed to match the engine (wchar_t == 2 bytes under -fshort-wchar,
  // bit-identical to the char16_t storage); reinterpret is safe. Internals use FData/char16_t.
  wchar_t & operator[](int index) { return reinterpret_cast<wchar_t &>(FData[static_cast<size_t>(index - 1)]); }
  wchar_t operator[](int index) const { return static_cast<wchar_t>(FData[static_cast<size_t>(index - 1)]); }

  // C++Builder c_str() is non-const (writable internal buffer); provide both.
  wchar_t * c_str() { return reinterpret_cast<wchar_t *>(&FData[0]); }
  const wchar_t * c_str() const { return reinterpret_cast<const wchar_t *>(FData.c_str()); }
  const wchar_t * data() const { return c_str(); }
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

  void SetLength(int len) { FData.resize(static_cast<size_t>(len)); }
  wchar_t * Unique() { return reinterpret_cast<wchar_t *>(&FData[0]); }
  wchar_t * c_str_mutable() { return reinterpret_cast<wchar_t *>(&FData[0]); }
  static UnicodeString StringOfChar(wchar_t Ch, int Count) { return UnicodeString(static_cast<char16_t>(Ch), Count); }
  int ToInt() const { std::string n; for (char16_t c : FData) n.push_back((char)c); return (int)std::strtol(n.c_str(),nullptr,10); }
  int ToIntDef(int d) const { if (FData.empty()) return d; std::string n; for (char16_t c : FData) n.push_back((char)c); char*e=nullptr; long v=std::strtol(n.c_str(),&e,10); return *e?d:(int)v; }
  double ToDouble() const { std::string n; for (char16_t c : FData) n.push_back((char)c); return std::strtod(n.c_str(),nullptr); }

  // In-place edits (Delphi 1-based).
  void Delete(int index, int count)
  {
    if (index >= 1 && index <= Length() && count > 0)
      FData.erase(static_cast<size_t>(index - 1), static_cast<size_t>(count));
  }
  void Insert(const UnicodeString & s, int index)
  {
    if (index < 1) index = 1;
    if (index > Length() + 1) index = Length() + 1;
    FData.insert(static_cast<size_t>(index - 1), s.FData);
  }

  // Trim / case (ASCII for now; full Unicode folding deferred to the platform layer).
  UnicodeString Trim() const
  {
    size_t b = 0, e = FData.size();
    while (b < e && FData[b] <= u' ') ++b;
    while (e > b && FData[e - 1] <= u' ') --e;
    return UnicodeString(FData.substr(b, e - b));
  }
  UnicodeString TrimLeft() const
  { size_t b = 0; while (b < FData.size() && FData[b] <= u' ') ++b; return UnicodeString(FData.substr(b)); }
  UnicodeString TrimRight() const
  { size_t e = FData.size(); while (e > 0 && FData[e - 1] <= u' ') --e; return UnicodeString(FData.substr(0, e)); }
  UnicodeString UpperCase() const
  { std::u16string r = FData; for (auto & c : r) if (c >= u'a' && c <= u'z') c = static_cast<char16_t>(c - 32); return UnicodeString(r); }
  UnicodeString LowerCase() const
  { std::u16string r = FData; for (auto & c : r) if (c >= u'A' && c <= u'Z') c = static_cast<char16_t>(c + 32); return UnicodeString(r); }

  // 1-based index of last char that is one of Delimiters; 0 if none.
  int LastDelimiter(const UnicodeString & Delimiters) const
  {
    for (int i = Length(); i >= 1; --i)
      if (Delimiters.FData.find((*this)[i]) != std::u16string::npos) return i;
    return 0;
  }
  bool IsDelimiter(const UnicodeString & Delimiters, int index) const
  {
    return index >= 1 && index <= Length() &&
           Delimiters.FData.find((*this)[index]) != std::u16string::npos;
  }
  // Case (in)sensitive 3-way compare.
  int Compare(const UnicodeString & rhs) const { return FData.compare(rhs.FData); }
  int CompareIC(const UnicodeString & rhs) const { return LowerCase().FData.compare(rhs.LowerCase().FData); }

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
