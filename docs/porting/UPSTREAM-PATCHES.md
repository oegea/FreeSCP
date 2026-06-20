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

All other reconciliation is done outside source/ (native/putty/include shims, genprops, the
rtlcompat/platform layers).
