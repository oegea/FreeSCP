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

// Delphi-style integer aliases used across the engine.
typedef int            Integer;
typedef unsigned int   Cardinal;
typedef bool           Boolean;
typedef wchar_t        Char;
typedef std::int64_t   __int64_compat;

#endif
