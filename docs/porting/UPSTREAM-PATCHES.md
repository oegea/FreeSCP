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
