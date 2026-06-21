//---------------------------------------------------------------------------
// StringConv.cpp — UnicodeString <-> byte-string conversions + TObject::ClassName.
//---------------------------------------------------------------------------
#include "winscp/UnicodeString.h"
#include "winscp/AnsiStrings.h"
#include "Classes.hpp"
#include <typeinfo>
#include <cstdarg>
#include <cstdio>
#include <string>

//---------------------------------------------------------------------------
// UnicodeString::sprintf — manual printf (vswprintf is unusable under -fshort-wchar).
namespace {
  void appendWide(std::u16string & out, const wchar_t * w)
  { if (w) while (*w) out.push_back(static_cast<char16_t>(*w++)); }
  void appendNarrow(std::u16string & out, const char * s)
  { if (s) while (*s) out.push_back(static_cast<char16_t>(static_cast<unsigned char>(*s++))); }
}

int __cdecl UnicodeString::sprintf(const wchar_t * Format, ...)
{
  std::u16string out;
  va_list ap; va_start(ap, Format);
  const wchar_t * p = Format;
  while (p && *p)
  {
    if (*p != L'%') { out.push_back(static_cast<char16_t>(*p++)); continue; }
    const wchar_t * spec = p++;           // at the '%'
    if (*p == L'%') { out.push_back(L'%'); ++p; continue; }
    // flags, width, precision (copied verbatim into a narrow format for numerics)
    std::string narrow = "%";
    while (*p == L'-' || *p == L'+' || *p == L' ' || *p == L'0' || *p == L'#') narrow.push_back((char)*p++);
    while (*p >= L'0' && *p <= L'9') narrow.push_back((char)*p++);
    if (*p == L'.') { narrow.push_back('.'); ++p; while (*p >= L'0' && *p <= L'9') narrow.push_back((char)*p++); }
    int len64 = 0;                         // 0=int, 1=long, 2=long long/Int64
    while (*p == L'l' || *p == L'L' || *p == L'h')
    { if (*p == L'L') len64 = 2; else if (*p == L'l') len64 = (len64 == 1 ? 2 : 1); ++p; }
    wchar_t conv = *p ? *p++ : L'\0';
    char buf[256];
    switch (conv)
    {
      case L's': { const wchar_t * w = va_arg(ap, const wchar_t *); appendWide(out, w); break; }
      case L'c': { int c = va_arg(ap, int); out.push_back(static_cast<char16_t>(c)); break; }
      case L'd': case L'i':
      {
        if (len64 == 2) { snprintf(buf, sizeof(buf), (narrow + "lld").c_str(), va_arg(ap, long long)); }
        else if (len64 == 1) { snprintf(buf, sizeof(buf), (narrow + "ld").c_str(), va_arg(ap, long)); }
        else { snprintf(buf, sizeof(buf), (narrow + "d").c_str(), va_arg(ap, int)); }
        appendNarrow(out, buf); break;
      }
      case L'u': case L'x': case L'X': case L'o':
      {
        char c = (char)conv;
        if (len64 == 2) { snprintf(buf, sizeof(buf), (narrow + "ll" + c).c_str(), va_arg(ap, unsigned long long)); }
        else if (len64 == 1) { snprintf(buf, sizeof(buf), (narrow + "l" + c).c_str(), va_arg(ap, unsigned long)); }
        else { snprintf(buf, sizeof(buf), (narrow + c).c_str(), va_arg(ap, unsigned int)); }
        appendNarrow(out, buf); break;
      }
      case L'f': case L'g': case L'e': case L'G': case L'E':
      { snprintf(buf, sizeof(buf), (narrow + (char)conv).c_str(), va_arg(ap, double)); appendNarrow(out, buf); break; }
      case L'p': { snprintf(buf, sizeof(buf), "%p", va_arg(ap, void *)); appendNarrow(out, buf); break; }
      default:   // unknown — emit the spec literally
      { while (spec < p) out.push_back(static_cast<char16_t>(*spec++)); break; }
    }
  }
  va_end(ap);
  FData = out;
  return Length();
}


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

// AnsiString::Format (static) — delegate to the wide Format, then narrow to bytes (UTF-8).
#include "SysUtils.hpp"   // Format(UnicodeString, TVarRec*, int)
AnsiStringBase __cdecl AnsiStringBase::Format(const char * Fmt, const TVarRec * Args, int Count)
{
  UnicodeString R = ::Format(UnicodeString(Fmt), Args, Count);
  return AnsiStringBase(UTF8String(R).raw());
}
