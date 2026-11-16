// 2-byte-wchar_t implementations of the wcs* functions the engine uses (see WcsCompat.h).
// Compiled with -fshort-wchar (rtlcompat), so wchar_t is 2 bytes here too; these operate on the
// engine's UTF-16 buffers correctly, unlike the libc 4-byte versions.
//
// This TU is NOT force-included with WcsCompat.h, so the wcs* names are NOT macro-redirected
// here; we define the winscp_* targets directly and never call the libc originals.

#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <cstdlib>

extern "C" {

// See WcsCompat.h: numeric-only swscanf over 2-byte wchar_t. The format and input here are
// ASCII (digits, spaces, %ld/%*d), so a byte-narrowing conversion is exact; forward to vsscanf.
int winscp_swscanf(const wchar_t * s, const wchar_t * fmt, ...)
{
  std::string ns, nf;
  for (const wchar_t * p = s; *p; ++p) ns.push_back(static_cast<char>(*p));
  for (const wchar_t * p = fmt; *p; ++p) nf.push_back(static_cast<char>(*p));
  va_list ap;
  va_start(ap, fmt);
  int r = ::vsscanf(ns.c_str(), nf.c_str(), ap);
  va_end(ap);
  return r;
}

// The numeric text is ASCII: narrow the leading run that can belong to a number, let strtol /
// strtoul do the parsing (sign, base prefixes, overflow), and map the end pointer back.
static const char * winscp_narrow_number(const wchar_t * s, std::string & narrow)
{
  for (const wchar_t * p = s; *p != 0 && *p < 128; ++p) narrow.push_back(static_cast<char>(*p));
  return narrow.c_str();
}
long winscp_wcstol(const wchar_t * s, wchar_t ** end, int base)
{
  std::string narrow; char * nend = nullptr;
  const char * n = winscp_narrow_number(s, narrow);
  long v = ::strtol(n, &nend, base);
  if (end != nullptr) *end = const_cast<wchar_t *>(s) + (nend - n);
  return v;
}
unsigned long winscp_wcstoul(const wchar_t * s, wchar_t ** end, int base)
{
  std::string narrow; char * nend = nullptr;
  const char * n = winscp_narrow_number(s, narrow);
  unsigned long v = ::strtoul(n, &nend, base);
  if (end != nullptr) *end = const_cast<wchar_t *>(s) + (nend - n);
  return v;
}

size_t winscp_wcslen(const wchar_t * s)
{
  const wchar_t * p = s;
  while (*p) ++p;
  return static_cast<size_t>(p - s);
}

wchar_t * winscp_wcschr(const wchar_t * s, wchar_t c)
{
  for (;; ++s)
  {
    if (*s == c) return const_cast<wchar_t *>(s);
    if (*s == 0) return nullptr;
  }
}

wchar_t * winscp_wcsrchr(const wchar_t * s, wchar_t c)
{
  const wchar_t * last = nullptr;
  for (;; ++s)
  {
    if (*s == c) last = s;
    if (*s == 0) return const_cast<wchar_t *>(last);
  }
}

wchar_t * winscp_wcspbrk(const wchar_t * s, const wchar_t * set)
{
  for (; *s; ++s)
    for (const wchar_t * t = set; *t; ++t)
      if (*s == *t) return const_cast<wchar_t *>(s);
  return nullptr;
}

wchar_t * winscp_wcsstr(const wchar_t * haystack, const wchar_t * needle)
{
  if (*needle == 0) return const_cast<wchar_t *>(haystack);
  for (; *haystack; ++haystack)
  {
    const wchar_t * h = haystack;
    const wchar_t * n = needle;
    while (*h && *n && (*h == *n)) { ++h; ++n; }
    if (*n == 0) return const_cast<wchar_t *>(haystack);
  }
  return nullptr;
}

int winscp_wcscmp(const wchar_t * a, const wchar_t * b)
{
  while (*a && (*a == *b)) { ++a; ++b; }
  return static_cast<int>(*a) - static_cast<int>(*b);
}

int winscp_wcsncmp(const wchar_t * a, const wchar_t * b, size_t n)
{
  for (; n > 0; --n, ++a, ++b)
  {
    if (*a != *b) return static_cast<int>(*a) - static_cast<int>(*b);
    if (*a == 0) return 0;
  }
  return 0;
}

wchar_t * winscp_wcscpy(wchar_t * dst, const wchar_t * src)
{
  wchar_t * d = dst;
  while ((*d++ = *src++) != 0) {}
  return dst;
}

wchar_t * winscp_wcsncpy(wchar_t * dst, const wchar_t * src, size_t n)
{
  wchar_t * d = dst;
  for (; n > 0 && *src; --n) *d++ = *src++;
  for (; n > 0; --n) *d++ = 0;
  return dst;
}

wchar_t * winscp_wcsdup(const wchar_t * s)
{
  size_t n = winscp_wcslen(s) + 1;
  wchar_t * r = static_cast<wchar_t *>(::malloc(n * sizeof(wchar_t)));
  if (r) for (size_t i = 0; i < n; ++i) r[i] = s[i];
  return r;
}

} // extern "C"
