# RESUME — native port handoff (authoritative; read this first)

Branch **`native-port`** (~35 commits, NOT pushed). Goal: WinSCP → native macOS (then Linux),
full GUI in Qt6, engine ported via RTL-compat + platform layers. See CLAUDE.md, PLAN.md,
STATUS.md, UPSTREAM-PATCHES.md. macOS arm64, Qt6 at `$(brew --prefix qt)`.

## Build / test
```sh
cd native && cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build && ctest --test-dir build
open build/ui-qt/winscp-qt.app          # working dual-pane local file manager (engine-backed)
```

## Architecture / layers (all under native/, Windows tree untouched)
- **rtlcompat** (`native/rtlcompat`, lib): Embarcadero RTL emulation. Flags `-fshort-wchar
  -fms-extensions`. UnicodeString(u16,1-based)/AnsiString family, TObject+RTTI(typeid/
  dynamic_cast), Format(TVarRec varargs), TStringList/TList/TStrings, TStream family,
  TDateTime, Set<>, DynamicArray/TBytes, threading (WinThreads.h: CreateEvent/WaitForSingle/
  StartThread over std::thread+condvar, id-keyed HANDLE table), SysExtra (dates/paths via
  std::filesystem/FindFirst, Win stubs), Object.h has winscp::MakeClosure + MakeFinally.
- **genprops.py** (`native/tools`): rewrites engine headers/cpp into `build/geninclude/core`:
  `__property`→`__declspec(property)` (forwarding accessors, const, index=, indexed); Borland
  `try{}__finally{}`→RAII (winscp::MakeFinally, recursive); `__closure` events→`std::function`
  typedef + `X->OnXxx = Method` → `winscp::MakeClosure(this,&remove_reference<decltype(*this)>
  ::type::Method)` (On[A-Z] only; skips F-fields/On-copies/null; RunAction call-arg too).
- **winscpcore** (`native/core`, lib): ported source/core .cpp. genprops shadow headers
  (geninclude/core) searched first. CORE_PORTED_NAMES list = the .cpp that compile (currently
  15: Global,FileBuffer,NamedObjs,Common,FileSystems,Option,Usage,FileMasks,Bookmarks,FileInfo,
  CopyParam,FileOperationProgress,RemoteFiles,Exceptions,Queue). winscpcore_flags = -fms-
  extensions -fshort-wchar -Wno-unknown-pragmas + includes geninclude/core, source/core,
  rtlcompat/include/winapi, source/resource, libs/{neon/src,expat/lib,libs3/inc,openssl/include}.
  Parse guard `winscpcore_parsecheck`: 35/36 headers parse (all but PuttyIntf.h).
- **puttycore** (`native/putty`, lib): PuTTY 0.83 platform-independent C (149 .c: crypto/ssh/
  utils/proxy/stubs). Flags `-DWINSCP -DMPEXT -DNO_GSSAPI -include platform.h -std=gnu11 -w`,
  `-I native/putty/include` (BEFORE source/putty). Excludes windows/*, x86 `*-ni`, conf_data.c.
- **puttyplatform** (`native/putty/src`, lib): the Unix backend. uxsupport.c (time/noise/
  username/CriticalSection/Filename/FontSpec), uxstubs.c (stricmp/charset/agent/proxy stubs +
  GSS no-op + filename_from_utf8), uxstore.c (settings stub/seed/host-key stub), uxnet.c
  (BSD-socket Socket/Plug vtable, async connect, select_result, winscp_net_select(ms)).
- **native/putty/include**: `platform.h` (unix PuTTY platform contract), `windows/platform.h`
  (redirect to ../platform.h), and literal-backslash files `ssh\gss.h`, `proxy\proxy.h`,
  `windows\platform.h`(removed; use real subdir). clang normalises `\`→`/` for <> includes.
- **ui-qt** (`native/ui-qt`): `enginebridge.{h,cpp}` (ONLY UI TU with engine flags; std-only
  API; isolates -fshort-wchar/UnicodeString from Qt) + `main.cpp` (plain Qt Commander).
  Working: dual-pane, local panel via engine FindFirst, F5 real copy, modified column.

## Gotchas
- The Write tool sometimes appends a literal `</content>` line — after any Write, run
  `perl -ni -e 'print unless /^<\/content>\s*$/' <file>`.
- LSP/clangd shows false errors on rtlcompat/engine headers (no flags); trust CMake builds.
- putty libs currently built WITHOUT -fshort-wchar (4-byte wchar) vs engine 2-byte. SFTP is
  char-based so OK to compile/link; for runtime ABI safety, add -fshort-wchar to puttycore/
  puttyplatform before trusting wchar-crossing paths.

## Survey one-liners
```sh
# which core .cpp compile (regenerate shadows first):
rm -rf /tmp/gi && mkdir -p /tmp/gi
for f in source/core/*.h source/core/*.cpp; do python3 native/tools/genprops.py "$f" "/tmp/gi/$(basename "$f")"; done
for f in /tmp/gi/*.cpp; do clang++ -std=c++17 -fshort-wchar -fms-extensions -Wno-everything -fsyntax-only \
  -I native/rtlcompat/include -I native/rtlcompat/include/winapi -I native/putty/include -I source/putty \
  -I /tmp/gi -I source/core -I source/resource -I libs/neon/src -I libs/expat/lib -I libs/libs3/inc \
  -I libs/openssl/include "$f" >/dev/null 2>&1 && echo "OK $(basename $f)"; done
# putty undefined symbols (force-load):
clang++ -std=c++17 /tmp/linktest.cpp -Wl,-force_load,native/build/putty/libputtycore.a \
  -Wl,-force_load,native/build/putty/libputtyplatform.a native/build/rtlcompat/librtlcompat.a \
  -lssl -lcrypto -L$(brew --prefix openssl)/lib -o /tmp/lt 2>/tmp/e; grep '", referenced' /tmp/e
```
PuttyIntf.cpp compiles with: engine flags + `-DWINSCP -DMPEXT -DNO_GSSAPI` + `-I native/putty/
include -I source/putty` + the core include set above.

## State (Phase 3 in progress)
- Phase 0/1 done (foundation, RTL, 35/36 headers parse). Phase 1.5: 15/34 engine .cpp compile.
- Phase 2 threading done; file/date platform done; sockets done (uxnet).
- Phase 3: PuTTY core+platform compile+link; **PuttyIntf.cpp compiles**. Force-linking
  puttycore+puttyplatform leaves 16 engine-side undefined (get_callback_set/get_seat_callback_
  set/get_log_callback_set/get_log_seat, modalfatalbox, ldisc_echoedit_update, old_keyfile_
  warning, pinger_free/new/reconfig, schedule_timer, expire_timer_context, sshver,
  putty_section, in_memory_key_data, argon2_internal_vs) — supplied by PuttyIntf.cpp/
  SecureShell.cpp/WinSCP timing when those compile+link.

## NEXT STEPS (in order)
1. Compile **SecureShell.cpp** (engine flags + putty includes, genprops shadow). Wire its
   event loop to `winscp_net_select(ms)` (declared in uxnet.c) instead of WaitForMultipleObjects.
2. Compile the other PuttyIntf.h-including engine .cpp: Cryptography, HierarchicalStorage,
   Configuration, SessionData, CoreMain, SessionInfo, SecureShell, Sftp/Scp/Ftp FileSystem.
3. Build a `winscpcore` that links (add these .cpp to CORE_PORTED_NAMES; ensure puttycore/
   puttyplatform compiled WITH -fshort-wchar for ABI; link winscpcore+puttycore+puttyplatform+
   rtlcompat+openssl).
4. Headless harness: open a TTerminal/SecureShell session over SFTP to a test server, list a
   dir. NEED A TEST SSH SERVER (local sshd on the Mac, or user-provided). Debug runtime.
5. Then wire the Qt remote panel to a real SFTP session via enginebridge (listRemoteDir/
   download/upload mirroring the local panel).
6. Phase 4 (neon/libs3 WebDAV/S3), Phase 7 (faithful dialog rebuild), Phase 9 (Linux).

## Conceptual nuts — ALL solved
__property (codegen), RTTI/__classid (typeid+dynamic_cast), wchar_t (u16+-fshort-wchar),
Format varargs, try/finally (codegen RAII), threading (std::thread), __closure events
(std::function+codegen), PuTTY platform (unix backend). Remaining = volume + runtime debug + UI.
