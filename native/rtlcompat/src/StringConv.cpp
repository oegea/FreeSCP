//---------------------------------------------------------------------------
// StringConv.cpp — UnicodeString <-> byte-string conversions + TObject::ClassName.
//---------------------------------------------------------------------------
#include "winscp/UnicodeString.h"
#include "winscp/AnsiStrings.h"
#include "Classes.hpp"
#include <typeinfo>

// UTF-16 -> latin1 (1 byte/char, lossy above U+00FF — acceptable until full codec).
static std::string ToLatin1(const UnicodeString & s)
{
  const std::u16string & w = s.raw();
  std::string r; r.reserve(w.size());
  for (char16_t c : w) r.push_back(static_cast<char>(c & 0xFF));
  return r;
}
// UTF-16 -> UTF-8.
static std::string ToUtf8(const UnicodeString & s)
{
  const std::u16string & w = s.raw();
  std::string r; r.reserve(w.size());
  for (size_t i = 0; i < w.size(); ++i)
  {
    unsigned int cp = w[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size())  // surrogate pair
    { unsigned int lo = w[++i]; cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); }
    if (cp < 0x80) r.push_back(static_cast<char>(cp));
    else if (cp < 0x800) { r.push_back(0xC0 | (cp >> 6)); r.push_back(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { r.push_back(0xE0 | (cp >> 12)); r.push_back(0x80 | ((cp >> 6) & 0x3F)); r.push_back(0x80 | (cp & 0x3F)); }
    else { r.push_back(0xF0 | (cp >> 18)); r.push_back(0x80 | ((cp >> 12) & 0x3F)); r.push_back(0x80 | ((cp >> 6) & 0x3F)); r.push_back(0x80 | (cp & 0x3F)); }
  }
  return r;
}

AnsiString::AnsiString(const UnicodeString & s) : AnsiStringBase(ToLatin1(s)) {}
RawByteString::RawByteString(const UnicodeString & s) : AnsiStringBase(ToLatin1(s)) {}
UTF8String::UTF8String(const UnicodeString & s) : AnsiStringBase(ToUtf8(s)) {}

UnicodeString __fastcall TObject::ClassName() const
{
  // Mangled name for now (e.g. "9TTerminal"); good enough for logs/classification.
  return UnicodeString(typeid(*this).name());
}
