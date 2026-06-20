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

## Test SSH/SFTP server (Docker) — for the runtime connect milestone
```sh
docker run -d --name winscp-test-sshd -e PASSWORD_ACCESS=true \
  -e USER_NAME=winscp -e USER_PASSWORD=winscp123 -p 2222:2222 \
  linuxserver/openssh-server:latest
# host localhost  port 2222  user winscp  pass winscp123  (SFTP subsystem on)
# home = /config ; seeded: /config/hello.txt, /config/testdir/nested.txt
# verify:  ssh -p 2222 winscp@localhost  (host key ed25519; password auth enabled)
docker start winscp-test-sshd            # if stopped;  docker rm -f winscp-test-sshd to drop
```
Verified live this session (handshake + auth negotiation OK). The engine can't connect yet
(no link; FSocketEvent->winscp_net_select wiring pending) — server is staged for that step.

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
- Phase 0/1 done (foundation, RTL, 35/36 headers parse). Phase 2 threading/file/date/sockets done.
- Phase 3: PuTTY core+platform compile+link. **6 SSH-side engine .cpp compile + build** into
  libwinscpcore via the new `winscpcore_ssh` CMake OBJECT group (putty includes + WINSCP/
  NO_GSSAPI): **PuttyIntf, Cryptography, Configuration, HierarchicalStorage, SecureShell** (+ the
  15 leaf TUs). See STATUS.md "Phase 3 step 2 progress" for the landmark infra added:
  - file-backed INI storage (rtlcompat/src/IniFiles.cpp) — the real config backend on non-Win
  - BSD-backed Winsock async-select emulation (rtlcompat winsock2.h + WinSock.cpp)
  - putty handle-wait stub (native/putty platform.h + src/uxhandlewait.c) — empty list on unix
  - genprops closure extensions: f(obj.Method) + X.OnXxx=&obj.Method
- ⚠ The build was RED at the prior HEAD (SecureShell.h SOCKET guard broke the header-parse
  guard); fixed first thing. Always `cmake --build build` before trusting green.
- ⚠ RUNTIME UNVALIDATED: SecureShell's EventSelectLoop waits on FSocketEvent, which our
  WSAEventSelect emulation can't auto-signal on socket activity. Wiring FSocketEvent to
  winscp_net_select() is THE first runtime task once a test SSH server exists (compiles/links now).

## NEXT STEPS (in order)
1. Compile the remaining connect-path TUs (add to CORE_SSH_NAMES once 0 errors via the survey
   one-liner): **SftpFileSystem (39), Terminal (49), SessionData (45), SessionInfo (71)**.
   - SessionData's bulk is the Xml.XMLIntf surface (IXMLNode/IXMLDocument ChildNodes/Count/
     FindNode/Get/Text/NodeName/GetAttribute + TXMLDocument/OleVariant) for session import/export
     — flesh out native/rtlcompat XMLIntf.hpp/XMLDoc to a working-enough node tree (expat-backed
     or stub-import). ScpFileSystem (5) needs a 2-byte-wchar `UnicodeString::sprintf` varargs
     formatter + TSafeHandleStream(THandle) ctor + multi-arg closure (ProcessDirectory). CoreMain
     (1) needs FileZillaIntf.h (FTP — deferred).
2. **Link** winscpcore + puttycore + puttyplatform + rtlcompat + openssl. NOTE: putty libs build
   WITHOUT -fshort-wchar (4-byte wchar) vs engine 2-byte — add -fshort-wchar to puttycore/
   puttyplatform for ABI safety before runtime (SFTP is char-based so compile/link is OK now).
3. **Wire the event loop**: tie FSocketEvent to winscp_net_select() so EventSelectLoop wakes on
   socket data (see the RUNTIME UNVALIDATED note). Guarded SecureShell.cpp edit if needed.
4. Headless harness: open a TTerminal/SecureShell SFTP session to a test server, list a dir.
   NEED A TEST SSH SERVER (local sshd on the Mac, or user-provided). Debug runtime.
5. Wire the Qt remote panel to a real SFTP session via enginebridge (mirror the local panel).
6. Phase 4 (neon/libs3 WebDAV/S3), Phase 7 (faithful dialog rebuild), Phase 9 (Linux).

## Conceptual nuts — ALL solved
__property (codegen), RTTI/__classid (typeid+dynamic_cast), wchar_t (u16+-fshort-wchar),
Format varargs, try/finally (codegen RAII), threading (std::thread), __closure events
(std::function+codegen), PuTTY platform (unix backend). Remaining = volume + runtime debug + UI.
