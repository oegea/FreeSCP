#ifndef WINSCP_WCSCOMPAT_H
#define WINSCP_WCSCOMPAT_H

// The engine is compiled with -fshort-wchar (wchar_t == 2 bytes, Delphi UTF-16), but the libc
// wcs* functions were built with the platform's 4-byte wchar_t. Calling them on our 2-byte
// strings misreads memory: e.g. ValidLocalFileName's wcspbrk scanned past the string and
// "matched" garbage, which corrupted every download filename ("hello.txt" -> "hello.txt01").
//
// This header is force-included (-include) into every engine translation unit. It first pulls
// in the system wide headers so their declarations stay pristine, then redirects the wcs* names
// the engine actually uses to 2-byte-correct implementations in rtlcompat (WcsCompat.cpp).
// rtlcompat's own sources are NOT force-included, so WcsCompat.cpp defines the winscp_* targets
// without the macros interfering.

// In C++ (the engine), including <wchar.h> first keeps the system declarations pristine and is
// harmless. In C (putty) under -fshort-wchar, clang POISONS the wcs* identifiers as soon as
// <wchar.h> is seen (to stop exactly the 4-byte/2-byte mismatch we are correcting), so our
// #define of those names would trip "attempt to use a poisoned identifier". There we include
// only <stddef.h>, which still provides wchar_t and size_t, and never pull <wchar.h>.
#ifdef __cplusplus
#include <wchar.h>
#else
#include <stddef.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
wchar_t * winscp_wcschr(const wchar_t * s, wchar_t c);
wchar_t * winscp_wcsrchr(const wchar_t * s, wchar_t c);
wchar_t * winscp_wcspbrk(const wchar_t * s, const wchar_t * set);
wchar_t * winscp_wcsstr(const wchar_t * haystack, const wchar_t * needle);
size_t    winscp_wcslen(const wchar_t * s);
int       winscp_wcscmp(const wchar_t * a, const wchar_t * b);
int       winscp_wcsncmp(const wchar_t * a, const wchar_t * b, size_t n);
wchar_t * winscp_wcscpy(wchar_t * dst, const wchar_t * src);
wchar_t * winscp_wcsncpy(wchar_t * dst, const wchar_t * src, size_t n);
wchar_t * winscp_wcsdup(const wchar_t * s);
// 2-byte-wchar swscanf: libc's swscanf reads the format/input as 4-byte wchar_t and fails on
// our UTF-16 strings (e.g. SCP's "Illegal time format" parsing the T control record). This shim
// converts the (ASCII) format + input to narrow and forwards to vsscanf. NUMERIC conversions
// only — %s/%ls would write the wrong width; the sole engine call site parses integers.
int winscp_swscanf(const wchar_t * s, const wchar_t * fmt, ...);
#ifdef __cplusplus
}
#endif

#define wcschr  winscp_wcschr
#define wcsrchr winscp_wcsrchr
#define wcspbrk winscp_wcspbrk
#define wcsstr  winscp_wcsstr
#define wcslen  winscp_wcslen
#define wcscmp  winscp_wcscmp
#define wcsncmp winscp_wcsncmp
#define wcscpy  winscp_wcscpy
#define wcsncpy winscp_wcsncpy
#define wcsdup  winscp_wcsdup
#define swscanf winscp_swscanf

#endif
