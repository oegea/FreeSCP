//---------------------------------------------------------------------------
// rtldefs.h — Embarcadero C++Builder language/keyword shims for clang/gcc.
//
// bcc64 is clang-based but adds Delphi-isms (calling conventions, closures, packages).
// We neutralize the ones that are pure compiler syntax; semantic types live in the
// System.* / SysUtils.* umbrella headers.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_RTLDEFS_H
#define WINSCP_RTLCOMPAT_RTLDEFS_H

#include <cstddef>   // NULL, size_t
#include <cstdint>
#include <type_traits>

// Borland/Embarcadero calling conventions — no-ops on clang for x86_64/arm64.
#ifndef __fastcall
#define __fastcall
#endif
#ifndef __closure
#define __closure
#endif

// Mirror of the clang-branch macros in source/core/Global.h, so engine headers that do
// NOT pull in Global.h (e.g. Cryptography.h) still parse standalone. Same definitions.
#ifndef CLANG_INITIALIZE
#define CLANG_INITIALIZE(V) = (V)
#endif
#ifndef NORETURN
#define NORETURN [[noreturn]]
#endif
#ifndef UNREACHABLE_AFTER_NORETURN
#define UNREACHABLE_AFTER_NORETURN(STATEMENT)
#endif
#ifndef EXCEPT
#define EXCEPT noexcept(false)
#endif

// __int64 is a builtin type under -fms-extensions (== long long); no typedef needed.

// Delphi Math Min/Max (generic).
template <class T> T Min(T a, T b) { return a < b ? a : b; }
template <class T> T Max(T a, T b) { return a > b ? a : b; }

// Delphi-style integer aliases used across the engine.
typedef int            Integer;
typedef unsigned int   Cardinal;
typedef bool           Boolean;
typedef wchar_t        Char;
typedef std::intptr_t  NativeInt;
typedef std::uintptr_t NativeUInt;

// Delphi boolean constants (capitalized).
static const bool True = true;
static const bool False = false;

#endif
