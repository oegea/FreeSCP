// 2-byte-wchar_t implementations of the wcs* functions the engine uses (see WcsCompat.h).
// Compiled with -fshort-wchar (rtlcompat), so wchar_t is 2 bytes here too; these operate on the
// engine's UTF-16 buffers correctly, unlike the libc 4-byte versions.
//
// This TU is NOT force-included with WcsCompat.h, so the wcs* names are NOT macro-redirected
// here; we define the winscp_* targets directly and never call the libc originals.

#include <stddef.h>
#include <stdlib.h>

extern "C" {

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
