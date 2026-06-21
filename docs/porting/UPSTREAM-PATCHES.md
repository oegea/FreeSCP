# Upstream source edits (guarded; Windows build unaffected)

Minimal, guarded edits to source/ for the native port. Each is `#ifdef`-guarded so the
Windows/C++Builder build is byte-identical.

- **source/putty/defs.h** — `HAVE_AES_NI` guarded to x86 only (`__i386__/__x86_64__/_M_*`);
  arm64 uses the software AES path. (Was unconditional `1` under WINSCP.)
- **source/core/SecureShell.h** — `typedef UINT_PTR SOCKET;` wrapped in `#ifdef _WIN32`; the
  `#else` branch is `typedef int SOCKET;`, identical to PuTTY platform.h's own
  `typedef int SOCKET` so the two coexist as a legal redefinition. This keeps the header
  self-contained for the header-parse guard (which omits putty includes) while matching the
  putty backend's fd type. Avoids the engine↔putty SOCKET type clash.
- **source/core/PuttyIntf.cpp** — `HasGSSAPI()` body guarded with `#ifdef NO_GSSAPI` (returns
  false); GSSAPI/Kerberos is not yet wired on the native build.
- **source/core/Terminal.h** — under `#ifndef _WIN32`, two inline bridges `WinscpToEventEx` /
  `WinscpToEvent` between the 3-arg `TProcessFileEvent` and 4-arg `TProcessFileEventEx`. Delphi
  permits casting between these closures; the ported `std::function` types can't convert, so
  genprops rewrites the two arity casts in Terminal.cpp to call these. Compiled out on Windows
  (the typedefs are `__closure`s there).
- **source/core/FileBuffer.h / FileBuffer.cpp** — extra `TSafeHandleStream(void *)` ctor under
  `#ifndef _WIN32`. Native callers cast OS file handles to `THandle` (which is `void*` in the
  rtlcompat layer, since it doubles as the Win threading HANDLE), so the existing `(int)` ctor
  doesn't match; the new ctor unpacks the fd via `reinterpret_cast<intptr_t>`. On Windows
  `THandle` is integer-convertible so the `(int)` ctor already serves and this is compiled out.

- **source/core/Common.h** — `IsWideChar` made `inline` (was a non-inline function body in a
  header → ODR multiple-definition once more than one TU odr-uses it under clang). `inline` is
  valid on every compiler; no behavior change.
- **source/core/Terminal.cpp** — the FTP/WebDAV/S3 branches of `TTerminal::Open()` are guarded
  `#ifdef _WIN32`; on the native build they `throw` "not supported yet" instead of instantiating
  `T{FTP,WebDAV,S3}FileSystem` (those backends are Phase 4). Keeps the engine SFTP/SCP-only so it
  links without pulling the unported filesystem classes.
- **source/core/SessionInfo.cpp** — `TSessionLog::GetCmdLineLog` guards the `TManagementScript`
  (scripting, unported) password-masking under `#ifdef _WIN32`; the headless engine logs no
  command line so there is nothing to mask.
- **source/core/SftpFileSystem.cpp** — `TSFTPPacket::PeekCardinal/GetCardinal/CanGetCardinal`
  used `sizeof(unsigned long)` for the SFTP 4-byte cardinal. `unsigned long` is 8 bytes on LP64
  (macOS/Linux) vs 4 on Windows (LLP64), so reads over-consumed 4 bytes each and desynced every
  SFTP response (e.g. SSH_FXP_NAME count came out as 7 instead of 1). Hardcoded the cardinal width
  to 4 — identical to `sizeof(unsigned long)` on Windows, so no behavior change there.
- **source/core/Configuration.cpp** — `GetStorage()` resolves `stDetect` to `stIniFile` under
  `#ifndef _WIN32`. There is no Windows registry on the native port, so storage (incl. host-key
  storage, which otherwise built a registry-backed `TRegistryStorage` whose `RootKey==NULL` made
  `RootKeyToStr` call `Abort()` during host-key save) must be INI/file-backed. On Windows the
  detection is overridden by TWinConfiguration as before.
- **source/putty/be_list.c** / **source/putty/settings.c** — `appname` and `get_remote_username`
  wrapped in `#ifndef MPEXT`; WinSCP's PuttyIntf.cpp supplies both under MPEXT (the engine build
  defines MPEXT), so the PuTTY originals would duplicate them at link.

All other reconciliation is done outside source/ (native/putty/include shims, genprops, the
rtlcompat/platform layers).

## Phase 4 — neon (libs/neon) guarded portability fixes

The WinSCP neon fork conflates "WINSCP integration" with "Windows". We build neon natively with
`-DWINSCP` (the engine/NeonIntf need the WinSCP hooks: NE_207_LIBERAL_ESCAPING, NE_DBG_WINSCP_*,
etc), so the WINSCP branches that were written for Windows must be `_WIN32`-guarded. All are
`#ifdef`-guarded so the upstream Windows build is byte-identical.

- `libs/neon/src/ne_defs.h` — under `NE_LFS` (WebDAVFileSystem.cpp defines it) `off64_t` was only typedef'd for _MSC_VER/__BORLANDC__; added `#if !defined(_WIN32) && !defined(__BORLANDC__) typedef off_t off64_t;` (off_t is 64-bit on LP64).
- `libs/neon/src/config.h` — the whole config is `#ifdef WIN32`. Added an `#else` (non-Windows)
  branch that `#include "neon_config_unix.h"` (captured autotools output, lives in `native/neon/`).
  Without this, non-Windows builds get an empty config (no NE_FMT_*, no HAVE_*).
- `libs/neon/src/ne_openssl.c` — `#include <windows.h>` (WinSCP Windows cert-store) wrapped in
  `#ifdef _WIN32`.
- `libs/neon/src/ne_socket.c` — three `#ifdef WINSCP` blocks using `ioctlsocket(fd, FIONBIO, …)`
  (Windows non-blocking-socket) changed to `#if defined(WINSCP) && defined(_WIN32)`, so unix takes
  the existing `#else` `fcntl(O_NONBLOCK)` path.

Result: all 26 portable `libs/neon/src/*.c` compile on clang/arm64; `native/neon/CMakeLists.txt`
produces `libneon.a`. (Linking into the engine + the WebDAV/S3 TUs is the next Phase 4 step; see
STATUS.md.)

## Phase 4 — Terminal.cpp WebDAV Open() un-guarded

`source/core/Terminal.cpp` TTerminal::Open(): the WebDAV branch's `#ifdef _WIN32` guard (which
threw "WebDAV protocol is not supported on this build yet") is removed now that TWebDAVFileSystem
builds natively — it unconditionally does `new TWebDAVFileSystem(this); Open()`. The FTP and S3
branches remain guarded (those backends are not built yet). The Windows build is unaffected (it
always ran this code). Marked `// WINSCP-NATIVE-PORT`.

## Phase 4 — libs3 + Terminal.cpp S3 (guarded)

- `libs/libs3/src/request.c`: (1) the `SYSTEMTIME`/`GetSystemTime` request-date block (a WinSCP
  "PuTTY ltime" replacement) is `#ifdef _WIN32`-guarded; the `#else` uses `gmtime_r` (UTC). (2)
  `neon_read_func` return type changed `int` -> `ssize_t` to match neon's `ne_provide_body` typedef
  (C++ rejects the mismatch at `ne_set_request_body_provider`; harmless on Windows). Built as C++.
- `source/core/Terminal.cpp`: S3 branch of TTerminal::Open() un-guarded (TS3FileSystem now builds
  natively via libs3). FTP branch remains guarded.

## source/filezilla S_un.S_addr -> s_addr (FTP port)
AsyncProxySocketLayer.cpp, FtpControlSocket.cpp: `sin_addr.S_un.S_addr` -> `sin_addr.s_addr`.
Safe on Windows too — `<winsock2.h>` defines `#define s_addr S_un.S_addr`, so `.s_addr` is the
portable spelling. Needed because POSIX `struct in_addr` has no `S_un` union member.

## FTP backend (Phase 8) — guarded source edits
- source/core/Terminal.cpp: un-guarded the fsFTP branch of TTerminal::Open() (FileZilla builds now).
- source/core/FtpFileSystem.cpp: PreserveDownloadFileTime body `#ifdef _WIN32`-guarded (handle-type
  mismatch on the native CFile); DeleteFile falls back to File->FileName for trailing-slash dir paths
  (both `#ifndef _WIN32`).
- source/filezilla/AsyncProxySocketLayer.cpp, FtpControlSocket.cpp: `sin_addr.S_un.S_addr` ->
  `sin_addr.s_addr` (portable; Windows winsock #defines s_addr to S_un.S_addr).
All keep the Windows build intact.
