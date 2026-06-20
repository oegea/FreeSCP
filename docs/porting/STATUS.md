# Port status — single source of truth for progress

Update this whenever a target starts/stops building. Verify by actually building.

_Last updated: 2026-06-20 — Phase 0 complete._

## Current phase: 0 ✅ complete → starting 1 (RTL compatibility layer)

## Buildable native targets
| Target | Status | Notes |
|--------|--------|-------|
| `native/rtlcompat` (+ tests) | ✅ builds + ctest green | UnicodeString (1-based UTF-16) + Property<T> proxy proven on clang arm64, `-fshort-wchar` |
| expat (vendored) | ✅ builds | `libexpat.a` arm64 via cmake, out-of-source (`/tmp/expat-build`) |
| `native/ui-qt` skeleton | ✅ builds + launches | Qt6 dual-pane Commander silhouette; `winscp-qt.app` opens (verified offscreen) |

## Done
- Architecture analysis of engine / GUI / libs coupling (3 explorer passes).
- CLAUDE.md, PLAN.md, STATUS.md.
- `native/` CMake scaffold (top-level + rtlcompat + ui-qt).
- Phase 0 proofs: rtlcompat keystone test, expat native build, Qt window builds+runs.

## Toolchain (macOS arm64)
- clang 14 (Xcode CLT) ✅, cmake 4 ✅, make ✅, git ✅, brew ✅
- qt6 ✅ (`/opt/homebrew/opt/qt`), nasm ✅, autoconf/automake/libtool ✅, pkg-config ✅

## Build commands
```sh
cd native && cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build && ctest --test-dir build
# run GUI skeleton: open build/ui-qt/winscp-qt.app
```

## Phase 1 progress (RTL compat layer)
- ✅ RTL umbrella headers: vcl.h, System.hpp, SysUtils.hpp, Classes.hpp, Contnrs.hpp,
  System.SyncObjs.hpp, System.Types.hpp, winscp/{rtldefs,wintypes,AnsiStrings,UnicodeString}.
- ✅ `__property` solved: `native/tools/genprops.py` rewrites it to clang
  `__declspec(property)` (`-fms-extensions`); CMake generates shadow headers for all 36 core
  headers into `build/geninclude/core` (searched before source/core). Field targets (`Fxxx`)
  get generated accessors; methods used directly; indexed + RO/WO handled.
- ✅ `Common.h` (gateway header) parses; `Global.cpp` compiles into libwinscpcore.a.
- ✅ Header-parse regression guard (`winscpcore_parsecheck`): 7/36 core headers parse clean
  standalone — Global, Common, NamedObjs, Http, Option, Security, KeyGen.

## Next up (Phase 1, the grind)
1. Grow rtlcompat to make the remaining 29 headers parse (Exceptions.h next — foundational:
   needs TVarRec/ARRAYOFCONST, PResStringRec, more Exception ctors). Add each to
   CORE_PARSE_HEADERS as it passes.
2. Then compile leaf .cpp bodies (FileMasks/CopyParam/RemoteFiles) — implement RTL method
   bodies (UnicodeString Format/Trim/IntToStr, TStringList, TObject) on demand.
3. Phase 2 platform adapters once config/threading/socket headers are reached.

## Legend
✅ done · 🟡 in progress · ⬜ todo · 🔴 blocked
