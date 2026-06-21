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
