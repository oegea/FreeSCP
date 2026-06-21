# Port status — single source of truth for progress

Update this whenever a target starts/stops building. Verify by actually building.

_Last updated: 2026-06-21 — SFTP + SCP fully operational (connect/list/nav/transfer/mkdir/rename/delete/chmod); see entries at bottom._

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

## Phase 3 START: PuTTY core compiles natively (149/150 C files)
- Unix native/putty/include/platform.h supplies the PuTTY platform contract (types, timing,
  THREADLOCAL, CRITICAL_SECTION->pthread, WPARAM/LPARAM, tree234 fwd, HELPCTX, clipboard
  consts) INSTEAD of source/putty/windows/platform.h — no source edit.
- libputtycore.a: 149 object files (all of crypto/ssh/utils/proxy/stubs) build on clang/arm64
  with -DWINSCP -DMPEXT -DNO_GSSAPI -include platform.h. Excluded: windows/* (Win backend),
  x86 hardware crypto (*-ni on arm), conf_data.c (one MSVC (int)"" const-init — revisit).
- Backslash include `ssh\gss.h` handled with a literal-backslash stub (macOS allows it);
  GSSAPI/Kerberos disabled for now.

### Phase 3 remaining (the runtime backend — makes it LINK + connect)
native/putty/src (not yet written): BSD-socket network.c (sk_new/connect/send/recv/close +
WinSCP MPEXT sk_new extras), do_select/select_result + socket iteration over select()/poll,
noise.c (/dev/urandom + rusage entropy), getticktime (mach_absolute_time), storage (random
seed file; host keys via WinSCP), callback_set pthread sync. Then compile PuttyIntf.cpp/
SecureShell.cpp (C++ engine side) and wire SecureShell's event loop to select() — at which
point the remote panel can do a real SSH/SFTP connect.

## Phase 3 backend: uxsupport/uxstubs/uxstore done (119 -> 31 undefined)
native/putty/src (libputtyplatform): uxsupport.c (getticktime/noise/entropy, get_username,
platform_default_*, CriticalSection->pthread, Filename/FontSpec), uxstubs.c (stricmp,
UTF-8 charset mb/wc, no-agent, platform_new_connection->NULL), uxstore.c (settings stubs —
WinSCP uses Conf; random-seed file real; host-key STUBBED -> Seat asks WinSCP). defs.h
guarded so HAVE_AES_NI is x86-only (arm uses software AES; UPSTREAM-PATCHES).

Remaining undefined when force-linking puttycore+puttyplatform: 31.
- 16 = NETWORK (sk_init/sk_namelookup/sk_new/sk_addr_*/...) -> the last platform file
  native/putty/src/uxnet.c (BSD sockets + Socket/Plug vtable + async connect + do_select).
- 15 = engine-side (get_callback_set/get_seat_callback_set/get_log_*, modalfatalbox,
  ldisc_echoedit_update, old_keyfile_warning, pinger_*, schedule_timer, expire_timer_context,
  sshver, putty_section, in_memory_key_data, argon2_internal_vs) — provided by WinSCP
  PuttyIntf.cpp/SecureShell.cpp/timing when the engine side compiles+links.

### Next: uxnet.c (the network backend) -> then compile PuttyIntf.cpp/SecureShell.cpp ->
wire SecureShell event loop to select() -> real SSH/SFTP connect from the remote panel.

## Phase 3 platform backend COMPLETE — uxnet.c (network) done
native/putty/src/uxnet.c: BSD-socket backend replacing windows/network.c — SockAddr via
getaddrinfo, NetSocket Socket/Plug vtable, non-blocking async connect (multi-candidate),
bufchain output buffering, set_frozen, select_result(fd,events) dispatch, do_select/
first_socket/next_socket/socket_writable, and winscp_net_select(ms) (one select() pass the
engine loop will call). Force-linking puttycore+puttyplatform now leaves only 16 undefined,
ALL engine-side (provided by WinSCP when PuttyIntf.cpp/SecureShell.cpp compile): get_callback_
set/get_seat_callback_set/get_log_callback_set/get_log_seat, modalfatalbox, ldisc_echoedit_
update, old_keyfile_warning, pinger_*/schedule_timer/expire_timer_context (WinSCP timing),
sshver/putty_section/in_memory_key_data/argon2_internal_vs.

### Phase 3 step 2 (next): compile the engine SSH side
PuttyIntf.cpp + SecureShell.cpp (+ Cryptography/HierarchicalStorage/Configuration/SessionData
which include PuttyIntf.h). They include putty headers via the engine flags + need genprops
(__property/__closure already handled) + putty include path. They supply the last 16 symbols
and call backend_init/backend_send/select_result. Then wire SecureShell's event loop to
winscp_net_select() -> real SSH/SFTP connect from the remote panel.

## Phase 3 step 2 START: PuttyIntf.cpp (engine<->putty bridge) compiles
The hardest C++ bridge file compiles (0 errors) under engine flags (-fshort-wchar -fms-
extensions) + putty include path. Key reconciliations:
- native/putty/include/windows/platform.h redirects PuTTY's <windows\platform.h> to the unix
  platform.h (clang normalises the backslash; -I native/putty/include precedes source/putty).
- platform.h gained REG_*/PUTTY_REG_*/_T/TEXT, CriticalSection decls, do_select(bool).
- ssh\gss.h backslash stub + uxstubs GSS no-op (ssh_gss_setup->empty liblist) +
  filename_from_utf8.
- Guarded source edits (see UPSTREAM-PATCHES.md): defs.h HAVE_AES_NI x86-only, SecureShell.h
  SOCKET, PuttyIntf.cpp HasGSSAPI under NO_GSSAPI.
- wchar_t note: putty libs currently build WITHOUT -fshort-wchar (4-byte) while the engine
  uses 2-byte; SFTP/SSH is char-based so the boundary is fine, but for full ABI safety the
  putty libs should also be built with -fshort-wchar before runtime use.

### Next: SecureShell.cpp (event loop -> winscp_net_select) + the PuttyIntf.h-including engine
.cpp (Configuration/SessionData/Cryptography/HierarchicalStorage), then link + real connect.

## Phase 3 step 2 progress: 6 SSH-side engine TUs compile+build (winscpcore_ssh group)
The build was RED at HEAD (SecureShell.h SOCKET typedef left the header-parse guard broken);
fixed (non-Windows `typedef int SOCKET` matching putty). New CMake `winscpcore_ssh` OBJECT lib
compiles PuttyIntf.h-including TUs with the putty include set + WINSCP/NO_GSSAPI, folded into
libwinscpcore. **Now compiling: PuttyIntf, Cryptography, Configuration, HierarchicalStorage,
SecureShell.** ctest green.

Landmark pieces this round (all no-source-edit except the SOCKET guard):
- **Storage backend** `native/rtlcompat/src/IniFiles.cpp`: real file-backed TCustomIniFile/
  TMemIniFile (case-insensitive, UTF-8, UpdateFile persist, GetStrings/SetStrings). TRegistry =
  honest absent-registry stub (Configuration picks INI on non-Windows). + TStrings SaveToStream/
  LoadFromStream/Equals/BeginUpdate/EndUpdate; escape_registry_key.c (portable putty util) into
  puttyplatform.
- **Winsock async-select emulation** `native/rtlcompat/{include/winapi/winsock2.h,src/WinSock.cpp}`:
  real BSD-backed WSAEventSelect/WSAEnumNetworkEvents over select() (one-shot FD_CONNECT/FD_CLOSE),
  FD_*/WSANETWORKEVENTS/SOCKADDR_IN/hostent/WSAIoctl/closesocket. Made SecureShell 44->11 errors.
- **handle-wait stub** `native/putty/{include/platform.h,src/uxhandlewait.c}`: empty wait list on
  unix (sockets via select) → SecureShell's EventSelectLoop waits only on its socket event.
- **genprops closure extensions**: `f(obj.Method)` call-arg + `X.OnXxx = &obj.Method` bind
  (TimeoutPrompt, CopyAlias.OnSubmit) — generalizes the __closure codegen beyond bare this-methods.
- RTL adds: HIWORD/LOWORD/MAKEWORD/DWORD_PTR, ParamStr, TPath::IsDriveRooted, StrToFloat/
  ForceDirectories/FileSetAttr/ExpandUNCFileName, free LastDelimiter, UnicodeString(char16_t,
  count=1) implicit single-char ctor + (const char*,int), KEY_WRITE/REG_*/ERROR_* consts.

⚠ RUNTIME UNVALIDATED: SecureShell's EventSelectLoop waits on FSocketEvent via WaitForMultiple-
Objects, but our WSAEventSelect emulation can't auto-signal that event on socket activity (Windows
ties them at the kernel). Compiles+links, but the loop won't wake on data until FSocketEvent is
wired to winscp_net_select(). This is THE first runtime task once a test SSH server exists.

### Remaining SSH .cpp (error counts w/ putty flags): ScpFileSystem 5 (sprintf 2-byte-wchar
formatter + TSafeHandleStream(THandle) + multi-arg closure ProcessDirectory), SftpFileSystem 39,
Terminal 49, SessionData 45 (Xml.XMLIntf surface: IXMLNode/IXMLDocument ChildNodes/Count/FindNode/
Get/Text/NodeName/GetAttribute — session import/export), SessionInfo 71, CoreMain 1 (FileZillaIntf.h
— FTP, deferred). Next best path to a connect: SftpFileSystem + Terminal + SessionData.

## Remote directory navigation WORKS — nav-crash fixed (rtlcompat property binding)
Double-clicking a remote dir (GUI) / `Terminal->ChangeDirectory()` (harness) segfaulted with a
stack overflow inside `TRemoteDirectoryChangesCache::GetValue` (RemoteFiles.cpp:1928). Root cause:
that engine class (`: private TStringList`) redeclares non-virtual `GetValue`/`SetValue` AND uses
the inherited `Values[]` property internally. In Delphi a property binds *statically* to the
accessor of its declaring class (TStrings), so `Values[]` -> TStrings.GetValue (no recursion). With
clang `__declspec(property)` the bare accessor name `GetValue` instead resolves in the *derived*
scope -> `TRemoteDirectoryChangesCache::GetValue` -> infinite recursion -> guard-page crash (lldb
mis-shows `this=NULL`; it's actually a stack overflow at the function prologue).
Fix (rtlcompat only, no source/ edit): bind `Names[]`/`Values[]` to non-shadowable forwarder
accessors `DoGetName`/`DoGetValue`/`DoSetValue` in TStrings (Classes.hpp). Replicates Delphi's
declaring-class binding; derived shadows can no longer capture the property. Verified: harness
`ChangeDirectory("/config/testdir")` now lists 3 entries; ctest green. NEXT: remote upload/download.

## Remote upload/download WORKS (F5) — engine transfer path + two wchar/ABI fixes
TTerminal CopyToRemote/CopyToLocal now run end-to-end over the live SFTP session. Validated via
the harness (upload /tmp file -> server, download server file -> /tmp, byte-correct) and wired into
the Qt GUI: doCopy branches local->local (copyFile), local->remote (uploadToRemote), remote->local
(downloadFromRemote); remote->remote is rejected for now. enginebridge gained uploadToRemote/
downloadFromRemote (download attaches the TRemoteFile* from the live listing as each entry's Object,
which TTerminal::ProcessFiles requires — a bare string list yields a NULL File and crashes).

Two root-cause fixes (rtlcompat only, no source/ edits):
1. **wchar single-char ctor**: `UnicodeString(L'/')` integral-PROMOTED the wchar_t arg to int and hit
   the numeric ctor -> "47" instead of "/". So UnixExtractFileName's `LastDelimiter(L'/')` returned 0
   and the download's local name became the full remote path ("/tmp//config/hello.txt"). Added an
   exact-match `UnicodeString(wchar_t, count=1)` ctor (UnicodeString.h).
2. **2-byte wcs* shims (WcsCompat.{h,cpp})**: the engine calls libc wcspbrk/wcschr/wcslen/wcscmp/...
   which were built for 4-byte wchar_t; on our -fshort-wchar 2-byte strings they misread memory.
   ValidLocalFileName's wcspbrk "matched" garbage and corrupted every download name ("hello.txt" ->
   "hello.txt01"). Force-included WcsCompat.h (-include via winscpcore_flags) redirects the wcs* names
   the engine uses to 2-byte-correct implementations in rtlcompat. **Audit class: any libc wide-string
   function on engine strings is unsafe under -fshort-wchar — route new ones through WcsCompat.**
ctest green. NEXT: remaining file ops (mkdir/delete/rename/properties) via TTerminal.

## Remote file ops WORK — mkdir / rename / delete (engine + Qt GUI)
TTerminal CreateDirectory / RenameFile / DeleteFile run over the live SFTP session. Harness-validated
(create /config/optest_dir -> rename -> delete, server ends clean). enginebridge gains
remoteMakeDir/remoteRename/remoteDelete (rename/delete look the entry up in the live listing via a
shared FindInListing helper; CreateDirectory gets a default TRemoteProperties, which it asserts
non-null). Local equivalents localMakeDir/localDelete/localRename (std::filesystem) added too, so the
GUI actions work on either panel. Qt GUI: toolbar + shortcuts F7 (new folder), F2 (rename, single
selection), Del (delete, with confirm); each dispatches remote-vs-local on the active panel.
ctest green; winscp-qt builds + launches. NEXT: ChangeFileProperties (rights/timestamp) + a
properties dialog; then -fshort-wchar on the putty libs (ABI safety) / SCP runtime.

## Remote file properties (chmod) WORK — TTerminal ChangeFileProperties
Changing unix permissions over the live SFTP session works (harness-validated: upload a
winscp-owned file, ChangeFileProperties octal 600, server reflects 600). enginebridge gains
remoteFileOctal (current octal from the live listing's TRemoteFile->Rights->Octal, for prefill)
and remoteChmod (builds TRemoteProperties{Valid<<vpRights, Rights.Octal=...}). Qt GUI: F9
Properties on the active remote panel -> octal input dialog prefilled with current perms.
Debugging note: an early test "failed" only because the seed files were created via `docker exec`
(root-owned) and the winscp SFTP user can't chmod root's files — the engine correctly sent
SETSTAT(flags=0x4, perms=0600) and the server correctly returned permission-denied. Test against
files the login user owns (e.g. uploaded ones). The wire path (AddProperties/AddCardinal, 4-byte)
was correct throughout. NEXT: -fshort-wchar on the putty libs (ABI), then SCP runtime.

## Investigated: -fshort-wchar on the putty libs — DEFERRED (clang poisons wcs*, off the SFTP path)
Attempted to build puttycore/puttyplatform/puttycrypto_vs with -fshort-wchar (2-byte wchar_t) to
match the engine. Blocked by design: under -fshort-wchar clang POISONS the libc wcs* identifiers
(`#pragma`-style) as soon as <wchar.h> is included in C, precisely to prevent the 4-byte/2-byte ABI
mix. PuTTY's wide-string utilities (utils/burnwcs.c, dupwcs.c, dup_{mb_to_wc,wc_to_mb}.c, the
utf8<->wide converters, stripctrl.c, settings.c) `#include <wchar.h>` and call wcslen/wcscpy, so they
fail to compile under -fshort-wchar; a macro-redirect doesn't survive because the later <wchar.h>
poison pragma clobbers our macro. Compiling them 4-byte while the rest is 2-byte would make putty
internally inconsistent. The only clean route is editing ~9 upstream putty files to use 2-byte-safe
replacements — high churn on vendored code for ZERO functional payoff: those utils are terminal/
charset/display helpers that are NOT on the SFTP path, which is char/UTF-8 based. Every SFTP feature
(connect, auth, host-key, listing, nav, upload/download, mkdir/rename/delete, chmod) works correctly
with the current 4-byte putty, i.e. no wide string is corrupted crossing PuttyIntf in practice.
Decision: keep putty at 4-byte wchar_t. Revisit only if a concrete UTF-16-crossing putty path
(e.g. GSSAPI principal names, certain key-file paths) is ever exercised. The wcs* class-bug on the
ENGINE side is already handled by WcsCompat.h (-include via winscpcore_flags). Made WcsCompat.h
include <stddef.h> instead of <wchar.h> in C mode (future-proof; avoids the poison if ever used in C).

## SCP protocol RUNTIME WORKS — connect + list (and fixed a latent TStrings::Assign no-op)
The SCP filesystem now connects, runs its shell-command startup (clear-aliases/detect-return-var/
pwd) and lists a real directory over an exec/shell channel. Validated via the harness with
FSProtocol=fsSCPonly (added a WINSCP_SCP=1 env toggle to native/harness/main.cpp): connects, lists
/config (10 entries incl. hello.txt). Same Docker server (the winscp user has /bin/bash, so SCP's
shell commands run).
Root-cause fixed (rtlcompat, no source/ edit): **TStrings::Assign was a no-op stub** in the base
TPersistent — TSCPFileSystem::ReadDirectory does `OutputCopy->Assign(FOutput)` then reads
`OutputCopy->Strings[0]`, which on the empty copy went out of bounds and crashed in
basic_string::__is_long. Implemented TStrings::Assign (clear + copy strings & objects via the
virtual interface). This was a LATENT bug affecting ~13 Assign call sites across the engine, not
just SCP. NEXT: wire SCP into enginebridge/GUI as a protocol choice (engine side proven); then
Phase 4 (FTP/S3/WebDAV) and Phase 7 (Qt dialogs).

## GUI: protocol choice (SFTP / SCP) in the Connect dialog
The Connect dialog gained a Protocol dropdown (SFTP default, SCP). enginebridge::connectSftp now
takes an engine::Protocol and sets FSProtocol = fsSFTPonly / fsSCPonly accordingly. Both engine
paths are runtime-proven (harness); the GUI now lets the user pick. winscp-qt builds + launches.

## SCP upload + download WORK — two more porting-class fixes (field-property lvalue, swscanf)
SCP file transfer now round-trips correctly (harness WINSCP_SCP=1 WINSCP_XFER=1: upload /tmp file ->
server with mode 0644, download back byte-identical). SFTP transfers unaffected (regression-checked).
Two root causes, both rtlcompat/genprops (no source/ edit):
1. **genprops field-backed property lvalue.** A `__property T P = { read=FFld, write=FFld }` was
   codegen'd with a by-VALUE getter (`__pg_P() const { return (T)FFld; }`). So `obj->P.member = x`
   mutated a throwaway copy — e.g. TCopyParamType::Default's `Rights.Number = rfDefault` was silently
   lost, and SCP sent file mode **0000** (upload created unreadable files; download then failed
   "Permission denied"). In Borland, `read=Field` is a direct lvalue. Fix: for pure field-backed
   props (read==write==same field, non-indexed) genprops now emits a non-const reference getter
   (`T & __pg_P() { return FFld; }`) plus a const by-value getter, so `obj->P.member = x` mutates the
   field. This is a CLASS fix — any `fieldProperty.member = x` across the engine was silently a no-op
   before. Full engine recompiled clean; ctest green; SFTP regression clean.
2. **swscanf under -fshort-wchar.** SCP's T (timestamp) control-record parse used libc `swscanf`,
   which reads 4-byte wchar_t and failed on our 2-byte strings -> "SCP protocol error: Illegal time
   format". Added a numeric-only `winscp_swscanf` to WcsCompat (ASCII wide->narrow, vsscanf); it's
   the only swscanf in the engine. (swprintf: none.)
Harness gained an optional transfer self-test (WINSCP_XFER=1) and now prints query MoreMessages.
NEXT: Phase 4 (FTP/S3/WebDAV) or Phase 7 (Qt dialogs).

## Phase 4 (WebDAV/S3) SCOPED — compile is close; link needs neon/libs3 built natively
Surveyed the deferred protocol TUs against the real geninclude shadow set (genprops each into
/tmp/gi4, compile with -I native/build/geninclude/core + neon/expat/libs3/openssl includes,
-DWINSCP -DNO_GSSAPI). Real COMPILE error counts (NOT link): NeonIntf 1, Http 1, S3FileSystem 2,
WebDAVFileSystem 13 — all small RTL gaps, same flavor as the SFTP grind. Inventory:
- **neon header**: `libs/neon/src/ne_defs.h:44` uses `off64_t` (Linux-ism) — macOS has only off_t.
  Add `#ifdef __APPLE__ typedef off_t off64_t;` via a guarded fix or a -Doff64_t=off_t. (UPSTREAM
  candidate; affects every neon-including TU.)
- **rtlcompat adds**: `UTF8String::vprintf` (NeonIntf:415); Win CRT `_close`, `O_BINARY`,
  `FILE_BEGIN` (WebDAV file I/O); a `FormatDateTime` overload (WebDAV:1483).
- **codegen/closure**: WebDAV:1253 `winscp::MakeClosure` not resolved in one context (genprops
  call-arg closure case — like the ProcessFiles/RunAction ones); WebDAV:310 & Http:82 "reference to
  non-static member function must be called" (a `&obj.Method` / event-bind genprops needs to handle).
- **string-literal width**: WebDAV:1113 `char32_t[2]` and :1882 `wchar_t[2]` vs UTF8String — a U""/
  L"" literal concatenated with the wrong string type (fix the operator+ or the literal).
- **S3 only**: `System.JSON.hpp` not found (S3 uses Delphi System.JSON) — needs a small JSON
  shim/DOM like XmlDoc.cpp, or compile S3 last.

THE BIG CHUNK is LINK, not compile: neon (autotools, needs OpenSSL+expat+zlib) and libs3
(GNUmakefile, needs libcurl+libxml2) must build as native static libs first, then the 4 TUs join a
new CMake group (mirror winscpcore_ssh) and Terminal.cpp's `Open()` WebDAV/S3 branches (currently
`#ifdef _WIN32`-guarded to throw "not supported") get un-guarded. Suggested order: (1) build expat
(already proven) + neon static; (2) fix the ~15 WebDAV/Http/NeonIntf compile gaps above; (3) link +
runtime-debug WebDAV against a test server (e.g. a dockerized `bytemark/webdav` or rclone serve);
(4) then S3 (build libs3 + JSON shim) against MinIO. FTP (FileZilla) remains the hardest, last.

### Phase 4 neon build — SCOUTED (configure works; 15/26 .c compile; 11 need fork-porting)
Out-of-tree autotools configure works and detects everything we need:
```sh
mkdir /tmp/neon-build && cd /tmp/neon-build
sh <repo>/libs/neon/configure --with-ssl=openssl --without-zlib --disable-shared \
   --enable-static --with-expat CFLAGS="-I$(brew --prefix openssl)/include -I<repo>/libs/expat/lib" \
   LDFLAGS="-L$(brew --prefix openssl)/lib"
# config.status dies on a missing test/Makefile.in (test dir not vendored) AFTER making
# src/Makefile but BEFORE config.h — generate config.h directly:
sh ./config.status config.h   # -> HAVE_EXPAT, NE_HAVE_SSL, HAVE_OPENSSL_SSL_H, NEON 0.34.2
```
Compiling the 26 src/*.c with that config.h: **15 compile clean, 11 fail** — all because this is
the WinSCP-PATCHED neon (Windows assumptions). The 11 + cause:
- printf length macros expand to nothing → `%" NE_FMT_TIME_T`, etc (ne_auth/basic/request/session/
  utils). **ROOT CAUSE FOUND**: the neon .c do `#include "config.h"`, which quote-include resolves
  to the SIBLING `libs/neon/src/config.h` (NOT our generated /tmp/neon-build/config.h — a sibling
  always wins over -I). That sibling is a WinSCP hand-written config that gates everything under
  `#if defined(_WIN32)` (`NE_FMT_TIME_T`, `#include <io.h>`, ...), so on macOS the macros are empty
  and Windows headers leak. THE FIX for the whole neon build: add a `#ifndef _WIN32` branch to
  `libs/neon/src/config.h` (guarded source edit -> UPSTREAM-PATCHES) that pulls the autotools-
  generated values (NE_FMT_*, HAVE_EXPAT, NE_HAVE_SSL, sized-int types, errno/headers), OR have the
  CMake compile neon-source COPIES with our config.h as their sibling (genprops-style shadow). This
  single fix unblocks most of the 11 (and the `errno`, `ne_md5` 32-bit-type, `ne_xml` parser, and
  `ne_openssl`/<windows.h> ones are all the same _WIN32-gated-config symptom). ne_207's
  NE_207_LIBERAL_ESCAPING is a separate WinSCP-added flag.
- `ne_openssl.c` `#include <windows.h>` (WinSCP added Windows crypto-store code) — needs an
  `#ifndef _WIN32` guard / unix branch.
- `ne_xml.c` "need an XML parser" — HAVE_EXPAT set in config.h but ne_xml selects via a different
  macro (NE_HAVE_EXPAT / EXPAT vs LIBXML2). Set the macro it actually checks.
- `ne_md5.c` "Cannot determine unsigned 32-bit data type" — define the size macro it wants.
- `errno` undeclared in ne_locks/ne_socket — add `#include <errno.h>` (config gap).
- `NE_207_LIBERAL_ESCAPING` undeclared (ne_207) — a WinSCP-added flag; provide it.
Plan: capture a working `config.h` into `native/neon/` + a small `ne_defs_compat.h`/extra-defines
header, guard the 2-3 WinSCP Windows-isms (#ifndef _WIN32, listed in UPSTREAM-PATCHES), then a
`native/neon/CMakeLists.txt` (mirror expat) compiles libneon.a. Then the 4 engine TUs (their ~15
compile gaps are inventoried above) + link + un-guard Terminal.cpp. This is multi-session.

### Phase 4 neon — libneon.a NOW BUILDS (all 26 .c compile on clang/arm64)
Resolved the _WIN32-gated-config root cause: `native/neon/neon_config_unix.h` (captured autotools
config) is pulled by a guarded `#else` in `libs/neon/src/config.h`; built with `-DWINSCP
-DHAVE_CONFIG_H`; the 3 WinSCP-Windows branches (`ne_openssl` <windows.h>, `ne_socket`
ioctlsocket/FIONBIO ×3) are now `_WIN32`-guarded (UPSTREAM-PATCHES.md). `native/neon/CMakeLists.txt`
(in the build via add_subdirectory) produces `build/neon/libneon.a` (591 KB); full build + ctest
green. NEXT Phase 4 steps: (1) expat as a CMake target (neon's XML dep, needed at link);
(2) fix the ~15 WebDAV/Http/NeonIntf compile gaps (inventory above) + add a winscpcore_neon CMake
group; (3) link (libneon + expat + OpenSSL) and un-guard Terminal.cpp Open() WebDAV branch;
(4) runtime-debug against a WebDAV test server. Then S3 (libs3 + System.JSON shim) and FTP last.

### Phase 4 progress — NeonIntf + Http compile AND link into the engine
2 of the 4 WebDAV/S3 TUs are now in the build (winscpcore_neon OBJECT group, folded into
libwinscpcore): **NeonIntf.cpp, Http.cpp**. Both compile and the harness/qt exes link them with
libneon + libexpat. Fixes this round:
- rtlcompat: `AnsiStringBase::vprintf(const char*, va_list)` (NeonIntf's neon debug forwarder).
- genprops: slot rules for `InitNeonTls`/`SetNeonTlsInit` (TNeonTlsInit closure in arg slot 1).
- link: neon was pulling GSSAPI -> set `/* #undef HAVE_GSSAPI */` in native/neon/neon_config_unix.h
  (matches the engine's NO_GSSAPI). Added `neon expat` to the harness + qt link lists (on-demand).
  NeonIntf.cpp now provides CertificateSummary/CertificateVerificationMessage/NeonWindowsValidate-
  CertificateWithMessage, so those interface_stub.cpp stubs were removed (were duplicates); added a
  `WindowsValidateCertificate` stub (Security.cpp not compiled). Full build + ctest green; SFTP
  regression clean.
Remaining for the WebDAV/S3 group (re-enable in CORE_NEON_NAMES): **WebDAVFileSystem.cpp** ~6 gaps —
UTF-32 literal `U"\U0001F512"` + UnicodeString (needs a char32_t ctor/operator+), genprops wrongly
wrapping the `CalculateFilesChecksum` DEFINITION (slot rule matched a method def, not a call — needs
a def-guard), neon `off64_t` in ne_defs.h (NE_LFS path; define off64_t or force the off_t branch),
`_close`/`O_BINARY`/`FILE_BEGIN` (rtlcompat winapi stubs), a `FormatDateTime(fmt, dt, FormatSettings)`
overload, and a const-qualifier assign (Uri.path). **S3FileSystem.cpp** also needs a System.JSON shim.
Then link (libs3 for S3) + un-guard Terminal.cpp Open() + runtime-debug vs a WebDAV server.

### Phase 4 — WebDAVFileSystem.cpp compiles + links (3/4 WebDAV/S3 TUs in the build)
WebDAVFileSystem.cpp now joins NeonIntf+Http in winscpcore_neon (compiles AND links into harness/qt
with libneon+libexpat). Fixes this round:
- rtlcompat: `_open_osfhandle`/`_close`/`SetFilePointer` + `O_BINARY`/`FILE_BEGIN`/`FILE_CURRENT`/
  `FILE_END` (WebDAV upload/download fd path, over our fd-packed-HANDLE); `AnsiStringBase::c_str()
  const` now returns `char*` (Embarcadero fidelity — `ne_uri.path = Path.c_str()`); free
  `operator+(const wchar_t*/char*, AnsiStringBase)`; `UnicodeString(const char32_t*)` ctor (UTF-32
  literal `U"\U0001F512"` -> UTF-16 surrogates); a 3-arg `FormatDateTime(fmt, dt, TFormatSettings)`
  overload (NOTE: still ISO-format, not the Delphi picture — HTTP-date header wrong until a real
  picture formatter; TODO).
- genprops: slot-rule guard skipping `T…Event` type tokens (an unnamed event-type PARAM in a method
  definition, e.g. WebDAV's CalculateFilesChecksum override, was being wrapped as a closure).
- neon: `libs/neon/src/ne_defs.h` off64_t typedef for non-Windows under NE_LFS (UPSTREAM-PATCHES).
Full build + ctest green; SFTP/SCP regression clean. REMAINING: S3FileSystem.cpp (needs a
System.JSON shim + libs3 built) — then un-guard Terminal.cpp Open() WebDAV branch + runtime-debug
against a WebDAV server (FormatDateTime picture + the upload fd path are the likely first runtime
issues).

## WebDAV WORKS END-TO-END — third protocol live (neon, native)
The ported engine connects to a real WebDAV server over neon (native libneon+libexpat), authenticates
(HTTP Basic), and lists a directory. Validated via the harness: `WINSCP_DAV=1
./native/build/harness/winscp-harness 127.0.0.1 8086 winscp winscp123` -> CONNECTED, lists `/`
(hello-dav.txt). Two final wiring fixes: harness/bridge must call `NeonInitialize()` (ne_sock_init;
normally done by CoreInitialize) — added after PuttyInitialize; and Terminal.cpp's WebDAV Open()
branch un-guarded (UPSTREAM-PATCHES). SFTP+SCP regression clean; full build + ctest green.
Test server: `docker run -d --name winscp-test-dav -p 8086:80 -e AUTH_TYPE=Basic -e USERNAME=winscp
-e PASSWORD=winscp123 bytemark/webdav` (PUT/GET/PROPFIND verified with curl).
NEXT: wire WebDAV into enginebridge/GUI (protocol dropdown already exists — add WebDAV + host/path);
real FormatDateTime picture formatter (HTTP-date header is currently ISO — affects upload timestamps);
WebDAV upload/download + file ops runtime shake-out; HTTPS (ftpsImplicit/TLS) cert path. Then S3
(libs3 + System.JSON) and FTP (FileZilla) last.

### WebDAV upload + download WORK (validated)
WebDAV file transfer round-trips over neon (harness WINSCP_DAV=1 WINSCP_XFER=1: upload /tmp file ->
server, download back byte-identical). The native fd path (_open_osfhandle/_close/SetFilePointer over
fd-packed-HANDLE) works at runtime. Fixed the harness self-test to target Terminal->CurrentDirectory
(protocol-agnostic) instead of a hardcoded /config (WebDAV root is /). SFTP+SCP xfer regression clean.

### WebDAV file ops WORK — SFTP/SCP/WebDAV now at feature parity
WebDAV mkdir/rename/delete (MKCOL/MOVE/DELETE) validated via harness WINSCP_OPS=1. All three
protocols now pass the same self-tests: connect, list, upload, download, mkdir, rename, delete.
(Harness WINSCP_OPS uses Terminal->CurrentDirectory too, so it's protocol-agnostic.) ctest green.

### Phase 4 — S3FileSystem.cpp COMPILES (System.JSON shim + RTL/XML adds); link pending libs3
S3FileSystem.cpp now compiles clean in the winscpcore_neon group (verified by building the object;
excluded from the engine LINK until libs3 is built, since it pulls S3_* symbols). Added:
- **System.JSON shim**: native/rtlcompat/include/System.JSON.hpp + src/JsonDoc.cpp — a compact
  recursive-descent JSON parser + the tiny Delphi DOM S3 uses (TJSONObject::ParseJSONValue,
  Values[name], Value(), GetEnumerator/MoveNext/Current, TJSONPair JsonString/JsonValue). Only the
  AWS instance-metadata credential path; arrays/numbers kept as text.
- rtlcompat: AnsiStringBase::data() + static AnsiString::Format (byte Format via the wide Format);
  SysExtra FileAge (stat mtime->TDateTime) + ISO8601ToDate (sscanf YYYY-MM-DDThh:mm:ss).
- XML DOM (XMLIntf.hpp/XmlDoc.cpp): IXMLNodeList::FindNode(Name, Namespace) 2-arg; IXMLDocument
  LoadFromXML(str) + ParseOptions (TParseOptions/poPreserveWhiteSpace, stored/ignored);
  XmlDocImpl::LoadString (parse from buffer, refactored out of Load).
REMAINING for S3 runtime: build libs3 natively (17 src/*.c; needs libcurl + libxml2 — brew has both;
its own config like neon), add it to the neon link, re-enable S3FileSystem.cpp in CORE_NEON_NAMES,
un-guard Terminal.cpp Open() S3 branch, then runtime-debug vs MinIO (docker). FTP (FileZilla) last.

### Phase 4 libs3 — SCOUTED (compiles as C++ with neon+expat; 10/13, 3 need small edits)
Key finding: the WinSCP libs3 fork uses **neon (HTTP) + expat (XML)**, NOT curl/libxml2 (simplexml.c
includes <expat.h>; request.h uses ne_session/ne_request/NeonCode), and is compiled as **C++**
(request.c uses `new[]`; libs3.h uses bare struct-tag-as-type `ne_session_s *`). So build with
`clang++ -x c++ -std=gnu++17 -DWINSCP -Ilibs/libs3/inc -Ilibs/neon/src -Inative/neon
-Ilibs/expat/lib -I$(brew --prefix openssl)/include`. Exclude `s3.c` (CLI tool), `testsimplexml.c`,
`mingw_*`. Result: 10/13 compile; the 3 remaining need:
- response_headers_handler.c: `strnicmp` -> add `-Dstrnicmp=strncasecmp`.
- request_context.c: `memset` undeclared -> add `-include cstring` (C++ build needs it).
- request.c: (a) `SYSTEMTIME`/`GetSystemTime` (a WINSCP "inspired by PuTTY ltime" block ~line 1567)
  — guard `#ifdef _WIN32` and use `gmtime_r` on unix, OR provide SYSTEMTIME+GetSystemTime; (b)
  `ne_set_request_body_provider(..., neon_read_func, ...)` — `neon_read_func` returns `int` but
  `ne_provide_body` is `ssize_t(*)(...)`; C++ rejects the return-type mismatch. Change neon_read_func
  to return `ssize_t` (correct everywhere) or cast `(ne_provide_body)`. (Guarded source edits ->
  UPSTREAM-PATCHES.)
THEN: native/libs3/CMakeLists.txt (static libs3, links neon+expat+OpenSSL) like native/neon; add it
to the engine link; re-enable S3FileSystem.cpp in CORE_NEON_NAMES; un-guard Terminal.cpp Open() S3
branch; runtime-debug vs MinIO (`docker run -p 9000:9000 minio/minio server /data`). FTP (FileZilla)
remains last + hardest.

## S3 builds + links + CONNECTS — at the AWS SigV4 frontier (libs3 native)
libs3 builds natively (native/libs3/CMakeLists.txt -> liblibs3.a, 404 KB; neon+expat-based, compiled
as C++; 3 guarded request.c edits — UPSTREAM-PATCHES). S3FileSystem.cpp re-enabled in
winscpcore_neon and LINKS into harness/qt (libs3+neon+expat); Terminal.cpp S3 Open() un-guarded;
removed the now-duplicate S3 env stubs from interface_stub. Full build + ctest green; SFTP/SCP/WebDAV
regression clean.
RUNTIME FRONTIER: `WINSCP_S3=1 ./native/build/harness/winscp-harness 127.0.0.1 9100 minioadmin
minioadmin123` (MinIO: `docker run -p 9100:9000 -e MINIO_ROOT_USER=minioadmin -e
MINIO_ROOT_PASSWORD=minioadmin123 minio/minio server /data`, bucket seeded via mc) reaches the
server and attempts a signed request, then fails: **"Access denied — There were headers present in
the request which were not signed"** (AWS Signature V4). Next: debug the SigV4 header signing — a
header is in the HTTP request but absent from SignedHeaders. Suspects: a neon-added default header
(e.g. our ne adds something WinSCP's didn't), the request-date path, or path-style/region wiring.
This is the S3 equivalent of the SFTP "userauth frontier" — characterized; needs a focused runtime
session (use the MinIO server log + a temp trace of the headers libs3 signs vs sends).
The S3 session uses s3usPath + region us-east-1 + ftpsNone (harness WINSCP_S3).

## S3 WORKS — FOURTH protocol live (libs3 native); SigV4 frontier was a one-liner
S3 connects to MinIO, authenticates (AWS SigV4), and lists buckets: `WINSCP_S3=1
./native/build/harness/winscp-harness 127.0.0.1 9100 minioadmin minioadmin123` -> CONNECTED, lists
`/` (testbucket). The "headers present which were not signed" frontier was `AnsiStringBase::data()`
returning a non-null "" for an empty string: S3FileSystem passes `SecurityToken.data()` and libs3
adds x-amz-security-token only when non-NULL — Embarcadero's data() returns NULL for empty, ours
didn't, so an empty signed security-token header was sent and MinIO rejected it. Fixed data() to
return NULL when empty (Embarcadero fidelity). GUI: Protocol dropdown gains S3 (s3usPath + region
us-east-1 + ftpsNone). All four protocols (SFTP/SCP/WebDAV/S3) connect+list; ctest green; full build
+ GUI launch OK. NEXT for S3: bucket/object nav + upload/download/ops runtime; then FTP (FileZilla).
Test server: `docker run -p 9100:9000 -e MINIO_ROOT_USER=minioadmin -e MINIO_ROOT_PASSWORD=minioadmin123 minio/minio server /data`.

## Phase 7 START — WinSCP-faithful GUI skeleton (Login dialog + Commander)
The Qt GUI now mirrors WinSCP's Commander interface so the working protocols can be exercised by
eye/hand: an iconic **Login dialog** on startup (sites tree + "Session" group: File protocol combo
[SFTP/SCP/FTP/WebDAV/Amazon S3], Host name + Port number, User name, Password, Tools/Manage +
Login/Close); the **dual-pane Commander** with per-panel title bars (Local / user@host — PROTOCOL),
address bars (path + Up/Refresh), the **Name | Size | Changed | Rights | Owner** columns (Name
stretches, others fixed; Rights/Owner populated for remote via DirEntry.rights/owner from
TRemoteFile RightsStr/Owner.DisplayText), per-panel status ("N directories, M files" / "K selected"),
and the classic bottom **function-key bar** (F2 Rename / F5 Copy / F6 Move / F7 Create Directory /
F8 Delete / F9 Properties / F10 Quit). Plus a Disconnect action, a toggleable Session log dock
(Ctrl+L; shows connect steps/errors), and protocol-aware default ports (SFTP/SCP 2222, WebDAV 8086,
S3 9100 — the local test servers). Engine wiring unchanged (connect/copy/mkdir/rename/delete/chmod
through enginebridge). winscp-qt builds + launches; verified by an offscreen render (WINSCP_SHOT=path).
NEXT GUI: real transfer progress + queue pane, context menus, drive/bookmark bar, faithful Properties
dialog (rights checkboxes), wire the menu bar actions; FTP backend later.

## Phase 7 cont. — Properties dialog, context menus, menu bar, transfer feedback
GUI faithfulness round 2: a WinSCP-style **Properties dialog** (Owner/Group/Others x R/W/X checkbox
grid synced to an octal field) replaces the bare octal prompt; **right-click context menus** on each
panel (Upload/Download, Rename, Delete, Create Directory, Properties); the **menu bar** is wired
(Local/Files/Session/Options/Remote/Help -> the same ops); transfers show a wait cursor + status.
Verified by offscreen renders (Login dialog + Commander both read as WinSCP). All four protocols are
now testable end-to-end through the GUI. NEXT GUI: real per-file transfer progress/queue pane,
drive/bookmark bar, synchronized-browsing, faithful site manager (save sessions).

## Phase 7 GUI — usable WinSCP-faithful file manager (testable end-to-end)
The Qt GUI is now a working WinSCP Commander, all four protocols exercisable by hand:
- Login dialog + **Site Manager** (save/load/delete sessions, QSettings).
- Dual panes, each with a WinSCP nav toolbar (**Back/Forward/Up/Home/Refresh** + path) + history,
  Name/Size/Changed/Rights/Owner columns (Rights/Owner real for remote), per-panel status.
- Function-key bar fully wired: **F2 Rename, F5 Copy, F6 Move, F7 Create Directory, F8 Delete,
  F9 Properties, F10 Quit** (+ shortcuts, + right-click context menu, + menu bar).
- WinSCP-style **Properties** dialog (rwx grid) and **transfer progress** dialog (engine OnProgress).
- Session log dock (Ctrl+L), Disconnect, About.
Verified by offscreen renders incl. a live SFTP connection showing real /config listings with
rights/owner. Dev affordances: WINSCP_SHOT (render) + WINSCP_AUTOCONNECT (connect-then-render).
NEXT GUI toward full fidelity: background transfer **queue pane**, synchronized browsing, F3 View /
F4 Edit, toolbar icons, the remaining WinSCP dialogs (preferences, copy-options, etc).

## FormatDateTime now interprets the Delphi picture (was an ISO stub)
Implemented a real Delphi-picture FormatDateTime in rtlcompat (yyyy/yy, m/mm/mmm/mmmm,
d/dd/ddd/dddd, h/hh, n/nn, s/ss, 'literal', am/pm; English names). Previously it ignored the
format and always returned ISO — wrong for WebDAV's HTTP-date header
("ddd, d mmm yyyy hh:nn:ss 'GMT'" -> "Sun, 21 Jun 2026 07:29:07 GMT" now) and for any
date routed through FormatDateTime (logs/display). Fixes the documented WebDAV upload-timestamp
TODO. Full build + ctest green; all protocols regression-clean.
