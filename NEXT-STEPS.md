# NEXT-STEPS — WinSCP → macOS native port (read this FIRST, you start with zero context)

You are continuing a port of **WinSCP** (Windows-only SFTP/SCP/FTP file manager) to **native
macOS** (then Linux), keeping the full GUI in **Qt 6**. This file is the authoritative handoff.
Read it fully, then read **`docs/porting/LEARNINGS.md`** (porting-class bugs & debug techniques —
read BEFORE debugging anything, it will save you hours), `docs/porting/RESUME.md`,
`docs/porting/STATUS.md`, `docs/porting/UPSTREAM-PATCHES.md`, and `CLAUDE.md`.

## TL;DR — where we are (2026-06-21)

**FOUR protocols work end-to-end, natively, via a WinSCP-faithful Qt GUI: SFTP, SCP, WebDAV, S3.**
The entire engine (~76k LOC welded to the Embarcadero RTL) compiles/links/runs on macOS arm64 via
an RTL-compat layer + platform adapters; PuTTY (SSH), neon+expat (WebDAV) and libs3 (S3) all build
natively. No Wine. Per protocol: connect / list / navigate / upload / download / mkdir / rename /
delete / chmod (S3 currently connect+list+transfer; ops vary by protocol). The GUI is a WinSCP
Commander: Login dialog + Site Manager, dual panes (Back/Forward/Up/Home address bars,
Name/Size/Changed/Rights/Owner columns), bottom F-key bar (F2/F4/F5/F6/F7/F8/F9/F10), Properties
(rwx) dialog, context menus, wired menu bar, transfer progress, session log, sync browsing.

Run it right now:
```sh
cd native && cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build && ctest --test-dir build          # all green
open build/ui-qt/winscp-qt.app                          # Login dialog -> browse/transfer
```
Test servers (Docker): SFTP/SCP `localhost:2222` (winscp/winscp123), WebDAV `localhost:8086`
(bytemark/webdav), S3 `localhost:9100` (MinIO minioadmin/minioadmin123). The Login dialog's
protocol picker defaults the port to the matching test server.

Headless harness (engine without GUI) — env toggles: `WINSCP_SCP` / `WINSCP_DAV` / `WINSCP_S3`
select protocol; `WINSCP_XFER` / `WINSCP_OPS` run transfer / file-op self-tests:
```sh
./native/build/harness/winscp-harness 127.0.0.1 2222 winscp winscp123          # SFTP
WINSCP_S3=1 ./native/build/harness/winscp-harness 127.0.0.1 9100 minioadmin minioadmin123
```

## >>> NEXT TASKS (in priority order) <<<

1. **S3 depth** — bucket/object nav + upload/download/ops runtime shake-out (connect+list proven).
2. **WebDAV HTTPS/TLS** — cert path (ftpsImplicit); needs an https test server.
3. **GUI toward full fidelity** — background transfer **queue pane**, preferences dialog, more of
   the 48 WinSCP dialogs, drive/bookmark bar.
4. **FTP backend** (FileZilla — hardest, deferred): un-guard the FTP branch in Terminal.cpp Open().
5. **Linux** — the platform split is POSIX/`__APPLE__`-clean; should follow with little change.

### (historical) earlier #1 tasks — all DONE

Over SFTP: nav, upload/download (F5), mkdir/rename/delete, properties (chmod, F9) all WORK. SCP
protocol RUNTIME also works now (connect + list, harness-validated with WINSCP_SCP=1 / fsSCPonly) —
see STATUS.md. The putty-libs -fshort-wchar item is DEFERRED (see STATUS.md). Next options:
(a) small: add a protocol dropdown (SFTP/SCP) to the Connect dialog + thread FSProtocol through
enginebridge::connectSftp (engine side proven for both); exercise the file ops over SCP.
(b) Phase 4: FTP (FileZilla — hardest), S3/WebDAV (neon/libs3) — Terminal.cpp Open() branches are
`#ifdef _WIN32`-guarded to throw "not supported"; un-guard when ready.
(c) Phase 7: faithful rebuild of the 48 VCL dialogs in Qt.
Watch the two transfer-era hazards: (a) any TStrings passed to ProcessFiles must carry TRemoteFile*
as each entry's Object; (b) never call libc wcs* on engine strings — route through WcsCompat.h.

## (DONE) SCP protocol runtime — connect + list + transfer WORK

ScpFileSystem connects/lists/transfers over a shell channel (harness WINSCP_SCP=1, transfer
self-test WINSCP_XFER=1). Fixes: TStrings::Assign no-op (list crash); genprops field-backed
property lvalue (SCP sent mode 0000 — a CLASS bug: any `fieldProp.member=x` was a no-op);
winscp_swscanf 2-byte shim (SCP timestamp record). See STATUS.md.

Possible smaller follow-ups: owner/group/timestamp in the properties path (chmod done; the engine
ChangeFileProperties already supports vpOwner/vpGroup/vpModification — just needs UI + bridge);
a real properties dialog (rights checkboxes) instead of the octal input box.

## (DONE) remote file properties — chmod (F9) WORKS

ChangeFileProperties over SFTP, harness-validated; Qt GUI F9 with octal prefill. enginebridge:
remoteFileOctal + remoteChmod. (Early "failure" was just root-owned seed files; code was correct.)
See STATUS.md.

## (DONE) remote file ops — mkdir / rename / delete WORK

Engine ops harness-validated; Qt GUI wired (F7 new folder, F2 rename, Del delete; remote-vs-local
dispatch on the active panel). enginebridge: remoteMakeDir/Rename/Delete + local* equivalents.
See STATUS.md.

## (DONE) remote upload/download (F5) — WORKS

CopyToRemote/CopyToLocal run end-to-end (harness-validated byte-correct, GUI F5 wired). Fixes:
UnicodeString(wchar_t) ctor (L'/' was becoming "47"); WcsCompat.{h,cpp} 2-byte wcs* shims
(libc's 4-byte wcspbrk corrupted download names to "hello.txt01"). See STATUS.md.

## (DONE) the remote-directory navigation crash — FIXED

In the GUI, **Connect works and lists the home dir, but double-clicking a remote directory
crashes** (segfault). Same crash reproducible by un-simplifying the harness (calling
`Terminal->ChangeDirectory(L"/somedir")`).

**FIXED** (rtlcompat property binding). The diagnosis below ("`this=NULL`") was wrong: it was a
stack overflow from infinite recursion. `TRemoteDirectoryChangesCache` redeclares non-virtual
`GetValue`/`SetValue` and uses the inherited `Values[]` property internally; clang
`__declspec(property)` resolved the accessor in the derived scope -> self-recursion. Bound
`Names[]`/`Values[]` to non-shadowable forwarders (`DoGetName`/`DoGetValue`/`DoSetValue`) in
TStrings (native/rtlcompat/include/Classes.hpp). See STATUS.md.

**Known cause** (already diagnosed via lldb earlier): `TTerminal::ChangeDirectory` →
`TRemoteDirectoryChangesCache::GetValue(this=NULL)` — `FDirectoryChangesCache` is NULL.
It's NULL because `Configuration->CacheDirectoryChanges` is false (default) so the cache is never
created, but some path dereferences it without a guard. Look at:
- `source/core/Terminal.cpp` — `TTerminal::ChangeDirectory`, `DoChangeDirectory`, and where
  `FDirectoryChangesCache` is created (search `FDirectoryChangesCache = new`). It's likely created
  only when `Configuration->CacheDirectoryChanges` is true.
- `source/core/RemoteFiles.cpp:1928` — `TRemoteDirectoryChangesCache::GetValue` (the crashing
  method; `this` is null).

Fix options (pick the engine-faithful one):
1. Set `Configuration->CacheDirectoryChanges = true` (and `CacheDirectories`) in the harness /
   `BridgeConfiguration` so the cache object exists. Check the property exists + is writable.
2. OR find the unguarded `FDirectoryChangesCache->...` call reached on our path and guard it
   (`#ifndef _WIN32` if it's an upstream bug, documented in UPSTREAM-PATCHES.md).

Reproduce with the harness (fastest, no GUI): temporarily re-add to `native/harness/main.cpp`
after the listing:
```cpp
g_terminal-style: Terminal->ChangeDirectory(L"/config/testdir");
Terminal->ReadDirectory(false);
```
then `lldb -b -o "run ..." -o "bt 14" -o "kill" ./native/build/harness/winscp-harness`.

After this: remote upload/download (F5 in the GUI, mirroring the local copyFile path), then the
rest of the file operations.

## How debugging works here (LEARNINGS — these saved hours, use them)

1. **READ THE EXCEPTION, don't lldb-spelunk.** WinSCP wraps errors in `ExtException` with a
   `MoreMessages` (`TStrings*`) chain and a `HelpKeyword`. The harness catch already dumps
   `E.Message` + `MoreMessages`. When that's empty, get the **type**: a temp
   `fprintf(stderr,"%s\n", typeid(E).name())` in the catch told us it was `Sysutils::EAbort`
   immediately. Most "mystery" failures were one `typeid`/`MoreMessages` print away.
2. **`__cxa_throw` breakpoints don't fire reliably here** (lldb -b). Don't rely on them. Break on
   the concrete exception **constructor** instead (e.g. `br set -r 'EOSExtException'`) and `bt` —
   that gives the exact throw site. This is how we found every runtime bug.
3. **lldb crash bt with no frames** = the crashing routine is a leaf libc SIMD string op on a null
   pointer, OR `std::terminate` (uncaught exception through C frames). Run the binary plain and
   read stderr: `libc++abi: terminating due to uncaught exception of type X` names the type.
4. **The Docker sshd is your oracle.** `docker exec winscp-test-sshd cat
   /config/logs/openssh/current` shows the server's view; we set `LogLevel DEBUG2`. "Connection
   closed by client [preauth]" + "without attempting authentication" told us the client aborted
   during NEWKEYS, before auth — pointing at our side, not creds.
5. **PuTTY's own event log** routes through `TSecureShell::PuttyLogEvent` (SecureShell.cpp ~660).
   A temp `fprintf(stderr,"[putty] %s\n",AStr)` there prints the full SSH transcript ("Doing NTRU
   Prime / Curve25519 kex", "Host key fingerprint", "Authenticated", etc) — invaluable. NOTE: it
   only fires once `Configuration->Logging` is OFF in our build (enabling WinSCP file logging
   broke an early path — the `[info]` status events still show via `OnInformation`).
6. **`LoadStr` now returns real strings** (we generated `native/rtlcompat/src/ResStrings.cpp` from
   `source/resource/*.{h,rc}` via `native/tools/genstrings.py`). Regenerate if resource files
   change: `python3 native/tools/genstrings.py source/resource native/rtlcompat/src/ResStrings.cpp`.

## RUNTIME BUGS WE HIT AND FIXED (so you understand the failure modes)

In order, each was the next blocker after the previous:
- **`Fixed file info not available`** — version-info queries. Fixed: `GetFileVersionInfo`/
  `VerQueryValue` (rtlcompat SysExtra.cpp) synthesize a VS_FIXEDFILEINFO (6.3.0.0) + a translation
  + string sub-blocks (`ReleaseType=stable`...).
- **Crash in `load_open_settings`** — `conf.winscp.h` used MSVC `(int)"str"` to init the
  ConfKeyInfo `default_value` union; on arm64 that truncates a 64-bit string pointer to 32 bits ->
  garbage `.sval` -> crash. Fixed: `native/putty/patch_conf.cmake` rewrites to **designated union
  initializers** (`{ .sval="" }` / `{ .bval=... }` / `{ .ival=... }`) into a build/putty-gen shadow.
- **`conf_get_str` assert on STR_AMBI** — putty asserts; WinSCP ships NDEBUG. Fixed: putty built
  with `-DNDEBUG` (then `ltime` got pulled in -> unix impl in uxsupport.c).
- **Socket never serviced (hang)** — `uxnet.c sk_new` didn't register the fd. Fixed: it now calls
  `do_select()` (mirrors windows/network.c) and `WinSock.cpp::winscp_pump_socket_events()` (called
  from `WaitForMultipleObjects` each poll) `SetEvent`s the WSA event when a socket is ready, so the
  engine's `FSocketEvent` wakes. This is THE event-loop bridge.
- **`EAbort` after host-key accept** — host-key SAVE built a registry-backed `TRegistryStorage`
  (RootKey NULL) -> `RootKeyToStr(NULL)` -> `Abort()`. Fixed: `Configuration::GetStorage` resolves
  `stDetect`->`stIniFile` on non-Windows (guarded).
- **`EOSExtException: Can't create file '~/.config/winscp-harness.ini'`** — host-key INI write.
  Two stubs were empty: `SHGetFolderPath` (now returns real $HOME/.config etc) and `CreateFile`
  (now a real POSIX `open()` returning the fd packed in a HANDLE).
- **THE protocol bug: `SSH_FXP_NAME count=7` instead of 1** — `TSFTPPacket::{Peek,Get}Cardinal`
  used `sizeof(unsigned long)` (8 on LP64 macOS vs 4 on Windows LLP64) -> over-consumed 4 bytes per
  cardinal -> every SFTP response desynced. Fixed: hardcoded the cardinal width to 4 (identical on
  Windows). **WATCH FOR MORE `sizeof(unsigned long)` / `sizeof(long)` / LLP64-vs-LP64 assumptions**
  in the protocol code — that's a whole class of latent bugs. (Also any `int`/`long` packed into
  `HANDLE`/`void*` — we standardized on "fd packed in a HANDLE via intptr_t"; see FileSeek,
  TSafeHandleStream(void*), CreateFile.)

## Architecture (where everything lives)

ALL port work is under `native/` and `docs/porting/`. The upstream Windows tree (`source/`,
`libs/`, `*.cbproj`, `build.bat`) must keep building — edits to `source/` are `#ifdef`-guarded and
listed in `docs/porting/UPSTREAM-PATCHES.md`. Golden rule: don't break the Windows build.

- `native/rtlcompat/` — Embarcadero RTL emulation (compiled static lib `rtlcompat`). UnicodeString
  (1-based UTF-16 over std::u16string, `-fshort-wchar`), AnsiString family, TObject+RTTI, Format
  (TVarRec varargs), TStrings/TList/TStream, TDateTime, Set<>, threading (WinThreads.h over
  std::thread+condvar), SysExtra (dates/paths/Win stubs), **WinSock.cpp** (Winsock async-select
  emulation over BSD sockets), **IniFiles.cpp** (file-backed config), **XmlDoc.cpp** (hand-rolled
  XML DOM for SessionData import), **Masks.cpp** (TMask glob), **ResStrings.cpp** (generated
  resource strings), Variant (scalar/string/int-array). Flags: `-fshort-wchar -fms-extensions`.
- `native/tools/genprops.py` — rewrites engine headers/cpp into `build/geninclude/core`:
  `__property`→`__declspec(property)`, Borland `try/__finally`→RAII, `__closure` events+inline
  declarators→`std::function` (+ `winscp::MakeClosure` binds, including fixed-slot call-arg rules
  for ProcessFiles/ProcessDirectory/DoAdd/etc, and the 3-arg<->4-arg TProcessFileEvent bridges).
- `native/tools/genstrings.py` — generates ResStrings.cpp (LoadStr table) from source/resource.
- `native/core/` — ported engine -> `libwinscpcore`. CMake compiles two groups: leaf TUs
  (`CORE_PORTED_NAMES`) and the SSH group `winscpcore_ssh` (PuttyIntf, Cryptography, Configuration,
  HierarchicalStorage, SecureShell, SftpFileSystem, ScpFileSystem, Terminal, SessionInfo,
  SessionData) compiled with the putty include set + `-DWINSCP -DNO_GSSAPI`. To add a new engine
  .cpp: get it to 0 errors via the survey one-liner in RESUME.md, then add it to the right list.
- `native/putty/` — PuTTY 0.83 (WinSCP fork). `puttycore` (platform-independent C, our unix
  `include/platform.h` replaces windows/platform.h), `puttyplatform` (`src/ux*.c` = BSD network,
  time/noise/storage, host-key in-memory cache, handle-wait stub), `puttycrypto_vs` (aes-sw.c/
  sha256-sw.c/argon2.c recompiled with `-DWINSCP_VS` because WinSCP gates the C software crypto
  behind that flag). Built with `-DNDEBUG -fexceptions` (so C++ exceptions unwind through C frames).
- `native/harness/` — `interface_stub.cpp` (the ~37 GUI/Interface callbacks the engine needs:
  ProcessGUI/BusyStart/TQueryParams/cert funcs/EncryptPassword/etc + engine globals
  Configuration/StoredSessions/ApplicationLog/ToggleNames) in lib `winscp_harness`; `main.cpp` =
  the headless `winscp-harness` connect exe.
- `native/ui-qt/` — Qt 6 GUI. `enginebridge.{h,cpp}` is the ONLY UI TU compiled with engine flags
  (isolates the `-fshort-wchar`/UnicodeString ABI from Qt's QString behind a std-only API). It now
  has the live SFTP session API (`connectSftp`/`listRemoteDir`/`disconnectSftp`). `main.cpp` = the
  dual-pane Commander; local panel + remote panel + Connect dialog.

## Linking (how the engine becomes a runnable binary)

Both `winscp-harness` and `winscp-qt` whole-archive link (`-Wl,-force_load`) the engine archives
(they carry global ctors + cross-refs): `winscpcore winscp_harness puttycore puttyplatform
puttycrypto_vs rtlcompat` + `OpenSSL::SSL OpenSSL::Crypto`. See `native/ui-qt/CMakeLists.txt` and
`native/harness/CMakeLists.txt`.

⚠ PuTTY libs are currently built WITHOUT `-fshort-wchar` (4-byte wchar) while the engine uses
2-byte. SFTP/SSH is char-based so it links + runs, but before trusting any UTF-16-crossing path,
add `-fshort-wchar` to puttycore/puttyplatform/puttycrypto_vs.

## Test server (Docker)

```sh
docker run -d --name winscp-test-sshd -e PASSWORD_ACCESS=true \
  -e USER_NAME=winscp -e USER_PASSWORD=winscp123 -p 2222:2222 \
  linuxserver/openssh-server:latest
# home = /config ; seed: docker exec winscp-test-sshd sh -c \
#   'echo hi > /config/hello.txt && mkdir -p /config/testdir && echo x > /config/testdir/nested.txt'
# verbose server log: docker exec winscp-test-sshd cat /config/logs/openssh/current
```

## Roadmap after the nav crash

1. Remote directory navigation (the crash above) + remote refresh/up.
2. Remote upload/download (F5) — wire `enginebridge` copy to/from remote, mirroring local copyFile.
3. The rest of the file ops (mkdir/delete/rename/properties) via TTerminal.
4. `-fshort-wchar` on the putty libs (ABI safety).
5. SCP protocol runtime (ScpFileSystem compiles; needs the same runtime shake-out as SFTP).
6. Phase 4: FTP (FileZilla — hardest, deferred), S3/WebDAV (neon/libs3 — Terminal.cpp Open()
   branches are currently `#ifdef _WIN32`-guarded to throw "not supported"; un-guard when ready).
7. Phase 7: faithful rebuild of the 48 VCL dialogs in Qt.
8. Phase 9: Linux (the platform split is already POSIX/`__APPLE__`-clean).

## Upstream provenance (for pulling WinSCP updates later)

This port was made from WinSCP upstream commit:
**`74a8c03f4b77a3de5930dc689de3b193cdcfb6a9`**
(https://github.com/winscp/winscp/commit/74a8c03f4b77a3de5930dc689de3b193cdcfb6a9)
The `master` branch in this repo is the unmodified WinSCP base; `main` (this branch) is the port.
To pull upstream updates, diff from that commit forward and replay onto `master`, then re-port.

## Gotchas (don't relearn these)

- The Write tool sometimes appends a literal `</content>` line — after any Write, strip it:
  `perl -ni -e 'print unless /^<\/content>\s*$/' <file>`.
- LSP/clangd shows false errors on rtlcompat/engine headers (no flags). Trust `cmake --build`.
- macOS has no `timeout`; run in background + `sleep` + `kill -9` to bound a hanging run.
- `unsigned long` is 8 bytes here (LP64) vs 4 on Windows — audit any protocol/struct use.
- Always `cmake --build build` (and `ctest`) before trusting "green"; verify by actually running.
