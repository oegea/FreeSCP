# Port status — single source of truth for progress

Update this whenever a target starts/stops building. Verify by actually building.

_Last updated: 2026-06-20 — Phase 1 header surface done; Phase 1.5 (.cpp bodies) underway._

## Current phase: 1.5 — compiling engine .cpp bodies (RTL bodies on demand)
Phase 0 ✅ (foundation). Phase 1 ✅ (35/36 headers parse). Now: compile .cpp into libwinscpcore.

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

## Phase 1.5 progress — compiled rtlcompat + first .cpp bodies
- ✅ rtlcompat is now a **compiled static lib** (was header-only): Streams.cpp (full TStream
  family, POSIX-backed), SysUtils.cpp (RaiseLastOSError, FileRead/FileWrite),
  SysStrFuncs.cpp (IntToStr/StrToInt/Trim/UpperCase/SameText/...).
- ✅ UnicodeString fleshed out: Trim/Upper/LowerCase, Delete/Insert, LastDelimiter,
  IsDelimiter, Compare/CompareIC, SetLength, numeric+ANSI ctors. AnsiString Pos/Delete.
- ✅ .cpp body build pipeline (shadow .cpp in geninclude). `Global.cpp`, `FileBuffer.cpp`, `NamedObjs.cpp` compile into libwinscpcore.a.
- Added include for source/resource (TextsCore.h); StrUtils.hpp stubs.

- ✅ Delphi containers implemented (TList/TObjectList/TStrings/TStringList + True/False).
  NamedObjs.cpp compiles → 3 engine bodies in libwinscpcore.a (Global, FileBuffer, NamedObjs).

- ✅ Delphi RTTI shim: `__classid`/`InheritsFrom`/`ClassName` via typeid + dynamic_cast
  (winscp::classid<C>), no engine edits. String cross-conversions (UnicodeString<->Ansi/
  Raw/UTF8, real UTF-8 codec). HKEY_* consts, Abort, StrUtils (IsDelimiter/StartsStr/EndsStr/
  ReplaceStr/PosEx...). Windows-header stubs (shlobj/shlwapi/psapi/tlhelp32/windows/winsock).

### Immediate next — Common.cpp (the hub, ~370 compile errors, mostly repeats)
Needs, in order of leverage:
1. ✅ **Format / FmtLoadStr** implemented (TVarRec varargs, %s/d/u/x/f, width/prec/flags,
   positional) + ctest. Common.cpp 370->326 errors.
2. **LoadStr / resource strings** — map int ident -> string. Source tables in source/resource;
   start with a stub table, wire real strings later.
3. **SEH** (`__try/__except`) — Windows structured exception handling, unsupported on clang.
   Neutralize via macros (`#define __try if(true)` / `__except(x) ... `) or #ifdef.
4. Win shell/process funcs (GetShellFolderPath, process enum) -> platform-adapter stubs.
5. Date/time helpers (EncodeDate, Now, FormatDateTime) -> std::chrono.
Then leaf data models (RemoteFiles/FileMasks/CopyParam/Option) fall quickly. Survey:
`for f in /tmp/gi/*.cpp; do clang++ -fsyntax-only ...` (see build note above for the flags).

## Next up (Phase 1 → 2/3): compile .cpp bodies
1. Compile engine **.cpp bodies** leaf-first (FileMasks/CopyParam/RemoteFiles), implementing
   RTL method bodies on demand: UnicodeString Format/Trim/IntToStr/case ops, TStringList,
   TStream IO, TDateTime math.
2. Phase 2 platform adapters as config/threading/socket bodies demand them (registry→file
   storage, Winsock→BSD sockets, HANDLE→std::thread, FILETIME→chrono, FindFirst→std::fs).
3. Phase 3: PuTTY unix backend → unblocks PuttyIntf + SecureShell/Sftp/Scp runtime.

## Legend
✅ done · 🟡 in progress · ⬜ todo · 🔴 blocked

## Phase 1.5 update (.cpp bodies): 14/34 compile + link
Compiling into libwinscpcore.a: Global, FileBuffer, NamedObjs, Common, FileSystems, Option,
Usage, FileMasks, Bookmarks, FileInfo, CopyParam, FileOperationProgress, RemoteFiles,
Exceptions. genprops now handles every __property form (field/method/indexed/index=N/const)
+ try/finally->RAII. rtlcompat ~full SysUtils/Classes/strings/dates/Format/containers.

Remaining 20 split by workstream (NOT missing-RTL-symbol grind anymore):
- Threading: Queue (+) need TThread/CreateThread/event adapter (Phase 2 threading).
- Closures: Script needs __closure method-pointer fidelity.
- PuTTY backend (Phase 3): Configuration, CoreMain, Cryptography, HierarchicalStorage,
  SessionData, SessionInfo, SecureShell, SftpFileSystem, ScpFileSystem, FtpFileSystem,
  PuttyIntf, Terminal (include PuttyIntf.h / putty.h).
- neon/libs3 (Phase 4): NeonIntf, Http, WebDAVFileSystem, S3FileSystem.
- Edge: KeyGen (#included into another TU), Security (Windows CryptoAPI cert chain -> OpenSSL).

## Threading adapter done; __closure is the next deep blocker
- ✅ Phase 2 threading: winscp/WinThreads.h + src/Threading.cpp — Win32 thread/event API
  (StartThread/CreateEvent/SetEvent/WaitForSingleObject/ResumeThread/CloseHandle/...) over
  std::thread + condition_variable, via an id-keyed handle table. Verified by a standalone
  test (spawn/join/exit-code/event signal+wait). Queue's threading symbols now resolve.

- 
## __closure SOLVED (lightweight — no libclang needed)
The feared third nut has a regex+template solution in genprops:
- event typedefs `RET __fastcall (__closure *T)(ARGS)` -> `typedef std::function<RET(ARGS)> T`.
- handler binds `X->OnXxx = Method;` -> `X->OnXxx = winscp::MakeClosure(this,
  &remove_reference<decltype(*this)>::type::Method)` (no class name needed; decltype trick).
- event-to-event copies, F-prefixed event fields, nulls, `OnceDone...` (On+lowercase) are
  left alone; `On[A-Z]` distinguishes real events. Call-arg closures handled for known funcs
  (RunAction). MakeClosure binds receiver via a lambda; std::function is callable/nullable
  like the Delphi closure. Queue.cpp now compiles -> 15/34 in libwinscpcore.a.

## Phase 6 start: visual Qt Commander (local panel = real ported engine)
winscp-qt is now a dual-pane Commander. The local panel lists real directories via the
ported engine (FindFirst/TSearchRec/path helpers) through native/ui-qt/enginebridge — the
clean std-only boundary that isolates the engine's -fshort-wchar/UnicodeString ABI from Qt's
QString. enginebridge.cpp is the only UI TU built with engine flags; main.cpp is plain Qt.
This is the exact pattern the remote (SSH/SFTP) panel will use once the backend lands.
Verified: app launches; bridge enumerates real files (dirs-first, "..", sizes).
