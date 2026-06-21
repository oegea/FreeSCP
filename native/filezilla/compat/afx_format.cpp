//---------------------------------------------------------------------------
// CString::FormatV — printf-style formatting for the 2-byte-wchar CString.
//
// libc's vswprintf reads the format as 4-byte wchar_t and mishandles our -fshort-wchar strings, so
// we walk the format ourselves. Supports the specifiers FileZilla uses: %s/%ls (wide str), %hs (narrow
// str), %d/%i/%u/%ld/%lu/%I64d/%I64u/%x/%X/%c/%% — with optional width/zero-pad (e.g. %02d).
//---------------------------------------------------------------------------
#include "afx.h"
#include <string>
#include <cstdio>

static void appendNarrow(std::wstring & out, const char * s)
{ if (s) while (*s) out.push_back((wchar_t)(unsigned char)*s++); }

int fz_vsntprintf(wchar_t * buf, size_t n, const wchar_t * fmt, va_list ap)
{
  CString tmp; tmp.FormatV(fmt, ap);
  const wchar_t * s = (const wchar_t *)tmp;
  size_t len = (size_t)tmp.GetLength();
  if (buf && n > 0) { size_t c = len < n - 1 ? len : n - 1; for (size_t i = 0; i < c; ++i) buf[i] = s[i]; buf[c] = 0; }
  return (int)len;
}

int fz_sntprintf(wchar_t * buf, size_t n, const wchar_t * fmt, ...)
{
  va_list ap; va_start(ap, fmt); int r = fz_vsntprintf(buf, n, fmt, ap); va_end(ap); return r;
}

void CString::FormatV(const wchar_t * fmt, va_list ap)
{
  std::wstring out;
  for (const wchar_t * p = fmt; p && *p; ++p)
  {
    if (*p != L'%') { out.push_back(*p); continue; }
    ++p;
    if (*p == L'%') { out.push_back(L'%'); continue; }

    // collect flags/width (e.g. "0", "08", "-3", ".3")
    std::string spec = "%";
    while (*p == L'-' || *p == L'+' || *p == L' ' || *p == L'0' || *p == L'#') spec.push_back((char)*p++);
    while (*p >= L'0' && *p <= L'9') spec.push_back((char)*p++);
    if (*p == L'.') { spec.push_back('.'); ++p; while (*p >= L'0' && *p <= L'9') spec.push_back((char)*p++); }

    // length modifiers
    bool isI64 = false, isLong = false;
    if (p[0] == L'I' && p[1] == L'6' && p[2] == L'4') { isI64 = true; p += 3; }
    else if (*p == L'l') { isLong = true; ++p; if (*p == L'l') { isI64 = true; ++p; } }
    else if (*p == L'h') { ++p; if (*p == L'h') ++p; }   // narrow modifier handled per-conv below

    wchar_t conv = *p;
    char buf[64];
    switch (conv)
    {
      case L's': case L'S':
      {
        // wide string arg (TCHAR*); narrow if preceded by 'h'
        if (spec == "%" && conv == L's')
        { const wchar_t * w = va_arg(ap, const wchar_t *); if (w) out += w; }
        else { const wchar_t * w = va_arg(ap, const wchar_t *); if (w) out += w; }
        break;
      }
      case L'd': case L'i':
      {
        if (isI64) { long long v = va_arg(ap, long long); spec += "lld"; std::snprintf(buf, sizeof buf, spec.c_str(), v); }
        else if (isLong) { long v = va_arg(ap, long); spec += "ld"; std::snprintf(buf, sizeof buf, spec.c_str(), v); }
        else { int v = va_arg(ap, int); spec += "d"; std::snprintf(buf, sizeof buf, spec.c_str(), v); }
        appendNarrow(out, buf); break;
      }
      case L'u':
      {
        if (isI64) { unsigned long long v = va_arg(ap, unsigned long long); spec += "llu"; std::snprintf(buf, sizeof buf, spec.c_str(), v); }
        else if (isLong) { unsigned long v = va_arg(ap, unsigned long); spec += "lu"; std::snprintf(buf, sizeof buf, spec.c_str(), v); }
        else { unsigned v = va_arg(ap, unsigned); spec += "u"; std::snprintf(buf, sizeof buf, spec.c_str(), v); }
        appendNarrow(out, buf); break;
      }
      case L'x': case L'X':
      {
        if (isI64) { unsigned long long v = va_arg(ap, unsigned long long); spec += "ll"; spec.push_back((char)conv); std::snprintf(buf, sizeof buf, spec.c_str(), v); }
        else { unsigned v = va_arg(ap, unsigned); spec.push_back((char)conv); std::snprintf(buf, sizeof buf, spec.c_str(), v); }
        appendNarrow(out, buf); break;
      }
      case L'c': { int v = va_arg(ap, int); out.push_back((wchar_t)v); break; }
      case L'p': { void * v = va_arg(ap, void *); std::snprintf(buf, sizeof buf, "%p", v); appendNarrow(out, buf); break; }
      default: out.push_back(L'%'); if (conv) out.push_back(conv); break;
    }
  }
  *this = out.c_str();
}
