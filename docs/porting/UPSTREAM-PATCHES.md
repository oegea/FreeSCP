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

All other reconciliation is done outside source/ (native/putty/include shims, genprops, the
rtlcompat/platform layers).
