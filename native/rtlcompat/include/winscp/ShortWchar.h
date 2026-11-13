#ifndef WINSCP_SHORTWCHAR_H
#define WINSCP_SHORTWCHAR_H
//---------------------------------------------------------------------------
// ShortWchar.h — keep the C++ standard library correct under -fshort-wchar.
//
// Force-included (-include, see rtlcompat/CMakeLists.txt) into EVERY translation unit compiled
// with -fshort-wchar, BEFORE any standard header is parsed.
//
// libc++ routes wide-character work to the C library through compiler builtins:
//   char_traits<wchar_t>::length/compare/find  -> __builtin_wcslen / wmemcmp / wmemchr
//   std::find over ANY trivially comparable element with sizeof == sizeof(wchar_t)
//     (so also char16_t, i.e. std::u16string::find(ch), the storage of UnicodeString)
//                                              -> __builtin_wmemchr
// Those builtins lower to calls into libc, whose wchar_t is 4 bytes. On our 2-byte data they
// read the wrong width: with Xcode 26's libc++, std::u16string::find(char16_t) stopped finding
// anything, so UnicodeString::LastDelimiter returned 0 and ExtractFilePath/ExtractFileDir
// returned "" (which in turn broke locating the configuration INI, etc.).
//
// The function-like macros below replace the builtins with width-correct constexpr loops.
// They are function-like on purpose: libc++ tests `__has_builtin(__builtin_wmemchr)`, where the
// name is not followed by '(' and so is not expanded — the "builtin" still looks available and
// libc++ keeps calling it, now landing here instead of in libc.
//
// LIMIT (libc++): this covers the header-inline paths only. std::wstring members that libc++ exports
// from its shared library (append/operator+/assign(const wchar_t *)...) were compiled for 4-byte
// wchar_t and remain unusable under -fshort-wchar. The engine only constructs/reads the two
// std::wstring values the FileZilla interface requires; do not add std::wstring manipulation —
// use UnicodeString (std::u16string storage, fully inline).
//---------------------------------------------------------------------------
#if defined(__cplusplus) && defined(__clang__)
#include <stddef.h>

namespace winscp_shortwchar {

constexpr size_t wcslen_(const wchar_t * s)
{
  size_t n = 0;
  while (s[n] != 0) ++n;
  return n;
}

constexpr int wmemcmp_(const wchar_t * a, const wchar_t * b, size_t n)
{
  for (; n != 0; --n, ++a, ++b)
  {
    if (*a < *b) return -1;
    if (*b < *a) return 1;
  }
  return 0;
}

// template so both `const wchar_t *` and `wchar_t *` haystacks keep their constness
template<class W>
constexpr W * wmemchr_(W * s, wchar_t c, size_t n)
{
  for (; n != 0; --n, ++s)
    if (*s == c) return s;
  return nullptr;
}

} // namespace winscp_shortwchar

// variadic: libc++ passes arguments containing template commas (reinterpret_cast<__copy_cv_t<T, wchar_t>*>)
#define __builtin_wcslen(...)  ::winscp_shortwchar::wcslen_(__VA_ARGS__)
#define __builtin_wmemcmp(...) ::winscp_shortwchar::wmemcmp_(__VA_ARGS__)
#define __builtin_wmemchr(...) ::winscp_shortwchar::wmemchr_(__VA_ARGS__)


// libstdc++ (Linux) takes the other route: char_traits<wchar_t> calls the C functions by name -
// wcslen, wmemcmp, wmemchr and also wmemcpy / wmemmove / wmemset, all 4-byte in glibc. Under
// -fshort-wchar that is not just a failed search: copying a std::wstring moves twice the bytes.
// <wchar.h> is included first so its declarations are parsed untouched; from here on the names
// are function-like macros, which the library templates (parsed later) expand at their call sites.
#if __has_include(<bits/c++config.h>)
#include <wchar.h>
namespace winscp_shortwchar {
inline wchar_t * wmemcpy_(wchar_t * d, const wchar_t * s, size_t n)
{ for (size_t i = 0; i < n; ++i) d[i] = s[i]; return d; }
inline wchar_t * wmemmove_(wchar_t * d, const wchar_t * s, size_t n)
{
  if (d < s) { for (size_t i = 0; i < n; ++i) d[i] = s[i]; }
  else { for (size_t i = n; i != 0; --i) d[i - 1] = s[i - 1]; }
  return d;
}
inline wchar_t * wmemset_(wchar_t * d, wchar_t c, size_t n)
{ for (size_t i = 0; i < n; ++i) d[i] = c; return d; }
} // namespace winscp_shortwchar
#define wmemcmp(...)  ::winscp_shortwchar::wmemcmp_(__VA_ARGS__)
#define wmemchr(...)  ::winscp_shortwchar::wmemchr_(__VA_ARGS__)
#define wmemcpy(...)  ::winscp_shortwchar::wmemcpy_(__VA_ARGS__)
#define wmemmove(...) ::winscp_shortwchar::wmemmove_(__VA_ARGS__)
#define wmemset(...)  ::winscp_shortwchar::wmemset_(__VA_ARGS__)
#define wcslen(...)   ::winscp_shortwchar::wcslen_(__VA_ARGS__)   /* WcsCompat.h re-points this at its own 2-byte wcslen */
#endif

#endif // __cplusplus && __clang__
#endif
