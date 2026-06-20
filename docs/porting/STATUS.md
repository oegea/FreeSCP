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
- ✅ Header-parse regression guard (`winscpcore_parsecheck`): **35/36** core headers parse
  in the real CMake build. Only `PuttyIntf.h` is excluded — it needs the PuTTY backend
  (putty.h assumes the Windows platform layer), which is Phase 3.
- winscpcore_flags adds vendored include dirs (neon/src, expat/lib, libs3/inc) so WebDAV/S3
  headers resolve.
- RTL surface now covers: full string family + numeric/ANSI ctors; TVarRec/PResStringRec +
  std exceptions + Variant; streams; DelphiSet `Set<>`; `System::DynamicArray` (TBytes);
  TNotifyEvent/TThreadFunc/TShiftState; System:: & Classes:: aliases; Win typedefs
  (UINT_PTR/WPARAM/LPARAM/INFINITE/VS_FIXEDFILEINFO/TShortCut); stubs Masks/Registry/
  SysInit/Xml.XMLIntf/TCustomIniFile.

## Build note for .cpp bodies (important)
A `.cpp` in `source/core` resolves `#include "Foo.h"` to its **sibling raw header** first
(C++ quote-include rule), bypassing the `__property`-rewritten shadow in `geninclude`. So to
compile bodies, also emit shadow **.cpp** copies into `geninclude/core` (via genprops, which
is a no-op on files without `__property`) and compile those, so their quoted includes hit the
shadow headers in the same dir. The parse guard avoids this because its stubs live in `build/`.

Per-file body surface is moderate (e.g. FileBuffer.cpp needs TStream ReadBuffer/WriteBuffer/
Seek + soFromBeginning/soCurrent, RaiseLastOSError, EReadError/EWriteError, THandleStream
ctor) — implement in rtlcompat on demand, leaf-first.

## Next up (Phase 1 → 2/3): compile .cpp bodies
1. Compile engine **.cpp bodies** leaf-first (FileMasks/CopyParam/RemoteFiles), implementing
   RTL method bodies on demand: UnicodeString Format/Trim/IntToStr/case ops, TStringList,
   TStream IO, TDateTime math.
2. Phase 2 platform adapters as config/threading/socket bodies demand them (registry→file
   storage, Winsock→BSD sockets, HANDLE→std::thread, FILETIME→chrono, FindFirst→std::fs).
3. Phase 3: PuTTY unix backend → unblocks PuttyIntf + SecureShell/Sftp/Scp runtime.

## Legend
✅ done · 🟡 in progress · ⬜ todo · 🔴 blocked
