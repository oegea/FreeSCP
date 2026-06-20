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

## State (Phase 3 — full SFTP engine compiles; first link done)
- Phase 0/1 done. Phase 2 threading/file/date/sockets done.
- **The entire SFTP engine compiles**: 11 SSH-side TUs build into libwinscpcore via the
  `winscpcore_ssh` CMake group (putty includes + WINSCP/NO_GSSAPI) — PuttyIntf, Cryptography,
  Configuration, HierarchicalStorage, SecureShell, SftpFileSystem, ScpFileSystem, Terminal,
  SessionInfo, SessionData — plus the 15 leaf TUs. Only **CoreMain** (FileZillaIntf.h / FTP) is
  deferred. Landmark infra (all under native/, see STATUS.md):
  - file-backed INI storage (IniFiles.cpp); BSD-backed Winsock async-select emulation
    (winsock2.h + WinSock.cpp); putty handle-wait stub (uxhandlewait.c); a self-contained XML
    DOM (XMLIntf.hpp + XmlDoc.cpp) + Variant-as-OleVariant; Variant int-arrays; 2-byte-wchar
    `UnicodeString::sprintf`.
  - genprops grew a lot: inline-`__closure`→std::function, fixed-slot closure wrappers
    (ProcessFiles/ProcessDirectory/DoAdd/...), arity-cast bridges, On/bare skip heuristics.
- **THE ENGINE LINKS** — winscpcore + winscp_harness + puttycore + puttyplatform + puttycrypto_vs
  + rtlcompat + openssl link into a 6.7MB arm64 Mach-O with **0 undefined, 0 duplicate**. Got there
  via: puttycrypto_vs (software crypto under -DWINSCP_VS, 77→58); resolving the putty side (conf_data
  shadow / argon2 VS / unix cleanup+host-key stubs / Masks.cpp, 58→45); the headless Interface stub
  (native/harness/interface_stub.cpp, 45→0 undefined); then dedup (removed redundant uxnet/uxstore
  stubs that PuttyIntf.cpp now provides + #ifndef MPEXT guards on be_list.c/settings.c, 32 dups→0).
- Link command: the "Survey one-liners" force-load + `-Wl,-force_load,...libwinscp_harness.a` +
  `...libputtycrypto_vs.a`.
- ⚠ RUNTIME UNVALIDATED: SecureShell's EventSelectLoop waits on FSocketEvent, which the
  WSAEventSelect emulation can't auto-signal on socket activity. Wiring FSocketEvent to
  winscp_net_select() is THE first runtime task once linked.
- A Docker test SFTP server is staged (see "Test SSH/SFTP server" above).

## The 58 remaining link symbols (mapped) — these are the headless-harness boundary
Run the link via the "Survey one-liners" force-load command + `-Wl,-force_load,...libputtycrypto_vs.a`.
1. **GUI/Interface callbacks (~25)** — implement a headless stub TU (these live in source/windows
   GUITools/Interface upstream): ProcessGUI, BusyStart/BusyEnd, AnswerNameAndCaption,
   CopyToClipboard/TextFromClipboard, AppNameString, SshVersionString, SystemRequired,
   IsAuthenticationPrompt/IsPasswordOrPassphrasePrompt, TQueryParams ctor/Assign,
   TQueryButtonAlias::*, T{Instant,}OperationVisualizer ctor/dtor, GetGlobalOptions,
   GetExceptionDebugInfo, AppendExceptionStackTraceAndForget, certificate funcs (CertificateSummary
   /CertificateVerificationMessage/NeonWindowsValidateCertificateWithMessage).
2. **Config globals/glue (~8)** — _Configuration, _StoredSessions, _AnySession, _ApplicationLog,
   _ToggleNames, GetRegistryKey, GetCompanyRegistryKey, EncryptPassword/DecryptPassword. Upstream
   in CoreMain.cpp/Common; provide a minimal CoreMain-lite stub or compile the needed bits.
3. **Deferred filesystem ctors (3)** + TManagementScript (3) — TFTPFileSystem/TS3FileSystem/
   TWebDAVFileSystem ctors referenced by the CreateFileSystem factory; stub (throw "unsupported")
   until Phase 4. TManagementScript is scripting (not needed for SFTP) — stub.
4. **Masks::TMask::Matches** — compile source/core/Masks.cpp (or stub).
5. **PuTTY bits (~10)** — conf_data.c (currently EXCLUDED for one MSVC `(int)""` const-init; fix
   the init and compile → conf_key_info/conf_enum_*); argon2.c under -DWINSCP_VS → argon2_internal_vs;
   unix stubs for win_misc_cleanup/win_secur_cleanup/wingss_cleanup/retrieve_host_key/
   in_memory_key_data (add to uxstubs.c/uxstore.c).

## RUNTIME STATUS — the engine connects via SSH (debugging userauth)
`native/build/harness/winscp-harness 127.0.0.1 2222 winscp winscp123` (Docker sshd). The engine:
creates Configuration/SessionData/Terminal, opens the session, the **event loop pumps**
(select_result fires thousands of times), SSH2 version exchange completes, **host-key verification
fires** (OnQueryUser shows the server's ed25519 fingerprint), then it reaches **userauth and the
client closes the connection during preauth** → the harness FATALs.
- Confirmed NOT a credential issue: right vs wrong password fail identically, and the server log
  shows "Connection closed by ... [preauth]" both ways. So our SSH client aborts userauth itself.
- Clues for the next session: `TSecureShell::PuttyLogEvent` (SecureShell.cpp:660, the LogPolicy
  `eventlog` sink) does NOT fire, and `OnPromptUser` is never called — yet the host-key Seat
  callback DOES fire. So the Seat path works but the LogPolicy eventlog path (and possibly the
  userpass-input Seat path) doesn't. Investigate: is FLogCtx's policy wired so `logevent` reaches
  `eventlog`? Does ScpSeat::get_userpass_input route to FUI->PromptUser? Is putty's userauth
  picking a method (CONF_try_password/CONF_try_ki_auth from StoreToConfig)? Re-add the temp trace
  `fprintf(stderr,"[putty] %s\n",AStr)` at the top of PuttyLogEvent to watch the SSH transcript.
- Debug aids: harness logs via Configuration->Logging (file /tmp/winscp-harness.log, but only the
  startup banner is captured so far — the per-line log write path needs checking too). sshd has
  LogLevel DEBUG2; read `docker exec winscp-test-sshd cat /config/logs/openssh/current`.
- LoadStr is still a stub (messages show as `str#NNN`); wiring real resource strings (generate an
  id->text table from source/resource/TextsCore.h + TextsCore1.rc) would make all errors readable
  and is the highest-leverage debug investment.

## NEXT STEPS (in order) — now a RUNTIME problem
1. **Runtime harness main** (native/harness/main.cpp + a CMake exe target linking the libs above):
   - create a TConfiguration (set the `Configuration` global the stub declares), a TSessionData
     (HostName=localhost, PortNumber=2222, UserName=winscp, Password=winscp123, FSProtocol=fsSFTP),
     a TTerminal, and a minimal callback/Seat that answers prompts from the session credentials and
     auto-accepts the host key (have_ssh_host_key/PuttyIntf path). Call Terminal->Open(), then list
     /config. The Docker server is staged (see "Test SSH/SFTP server").
2. **Wire FSocketEvent → winscp_net_select()** — SecureShell's EventSelectLoop waits on FSocketEvent
   via WaitForMultipleObjects, but the WSAEventSelect emulation can't auto-signal it on socket
   activity, so the loop won't wake on data. This is the #1 runtime blocker. Options: make the
   WinThreads WaitForMultipleObjects integrate a winscp_net_select() pass for socket-backed events,
   or a guarded SecureShell.cpp edit that calls winscp_net_select(timeout) in the loop.
3. Build `-fshort-wchar` into puttycore/puttyplatform/puttycrypto_vs for wchar ABI safety (SFTP is
   char-based so the current 4-byte-wchar putty linked fine, but UTF-16 crossings need it).
4. Debug the connect end-to-end (auth, host-key, SFTP init, readdir). Iterate.
5. Wire the Qt remote panel to a real SFTP session via enginebridge (mirror the local panel).
6. Phase 4 (neon/libs3 WebDAV/S3 + FTP — unguard Terminal.cpp), Phase 7 (dialogs), Phase 9 (Linux).

## Conceptual nuts — ALL solved
__property (codegen), RTTI/__classid (typeid+dynamic_cast), wchar_t (u16+-fshort-wchar),
Format varargs, try/finally (codegen RAII), threading (std::thread), __closure events
(std::function+codegen), PuTTY platform (unix backend). Remaining = volume + runtime debug + UI.
