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

// Borland/Embarcadero calling conventions — no-ops on clang for x86_64/arm64.
#ifndef __fastcall
#define __fastcall
#endif
#ifndef __closure
#define __closure
#endif

// MSVC/bcc 64-bit integer keyword. As a macro so `unsigned __int64` also works.
#ifndef __int64
#define __int64 long long
#endif

// Delphi-style integer aliases used across the engine.
typedef int            Integer;
typedef unsigned int   Cardinal;
typedef bool           Boolean;
typedef wchar_t        Char;
typedef std::intptr_t  NativeInt;
typedef std::uintptr_t NativeUInt;

#endif
