# LEARNINGS — porting-class bugs & debug techniques (READ BEFORE DEBUGGING)

Hard-won lessons from porting the WinSCP engine to native macOS. Each entry is a *class* of bug
that already cost real debugging time — recognize the symptom, apply the known fix, don't
re-derive. All fixes live under `native/` (never edit `source/` except guarded portability fixes
listed in UPSTREAM-PATCHES.md).

---

## 1. `-fshort-wchar`: libc wide-char functions are POISON (literally)

The engine + rtlcompat compile with `-fshort-wchar` → `wchar_t` is **2 bytes** (Delphi UTF-16).
**libc was built with 4-byte `wchar_t`.** Any libc `wchar`/`wcs` function called on our strings
misreads memory.

- **Symptom**: garbage results from string ops that "look right" — e.g. `wcspbrk` matching past the
  end → corrupted filenames (`hello.txt` → `hello.txt01`); `swscanf` failing to parse → "Illegal
  time format"; `LastDelimiter` returning 0.
- **Fix**: route every libc wide function the engine uses through
  `native/rtlcompat/include/winscp/WcsCompat.h` (force-included via `-include` on
  `winscpcore_flags`). It provides 2-byte `winscp_*` reimplementations and `#define`s the libc
  names to them. Currently covers `wcschr/wcsrchr/wcspbrk/wcsstr/wcslen/wcscmp/wcsncmp/wcscpy/
  wcsncpy/wcsdup/swscanf`. **Add any new one here.**
- **clang actively fights you**: under `-fshort-wchar`, including `<wchar.h>` in **C** mode makes
  clang `#pragma`-POISON the `wcs*` identifiers (to stop the 4/2-byte mix). So WcsCompat.h includes
  `<stddef.h>` (not `<wchar.h>`) in C, and `#define`-redirects. A macro redirect does NOT survive a
  later `<wchar.h>` include in C (its poison pragma clobbers the macro) — this is why we did NOT
  flip the putty libs to `-fshort-wchar` (their wide-string utils `#include <wchar.h>` and call
  `wcslen`/`wcscpy`; they're off the SFTP path so 4-byte putty is fine — see STATUS.md).
- **Audit command**: `grep -rhoE '\b(wcs[a-z]+|swscanf|swprintf|vswscanf|wmem[a-z]+)\(' source/core/*.cpp`.

## 2. LP64 vs LLP64: `sizeof(unsigned long)`/`sizeof(long)` is 8 here, 4 on Windows

macOS is LP64 (`long`=8); Windows is LLP64 (`long`=4). Any protocol/struct code using
`sizeof(unsigned long)` for a wire field is wrong here.

- **Symptom**: protocol desync — e.g. SFTP `SSH_FXP_NAME count=7` instead of 1 (cardinals
  over-consumed 4 bytes each). Already fixed in `TSFTPPacket::{Peek,Get}Cardinal` (hardcoded 4).
- **Also**: anything packed into `HANDLE`/`void*` must go through `intptr_t` (fd-in-HANDLE pattern;
  see CreateFile/FileSeek/TSafeHandleStream).
- **Watch for more** in any newly-compiled protocol TU.

## 3. genprops `__property` codegen: field-backed props must be LVALUES

`__property T P = { read=FFld, write=FFld }` in Borland gives **direct field (lvalue) access**, so
`obj->P.member = x` mutates the field. A naive by-value getter (`return (T)FFld;`) mutates a
throwaway copy → **the assignment silently vanishes**.

- **Symptom**: a `.Default()`/setter that "runs" but the value stays zero — e.g.
  `Rights.Number = rfDefault` lost → SCP sent file mode **0000** → uploads unreadable.
- **Fix (already in genprops.py)**: for pure field-backed props (`read==write==same field`,
  non-indexed), emit a **non-const reference getter** (`T & __pg_P() { return FFld; }`) + a const
  by-value getter. This was a CLASS bug — *any* `fieldProperty.member = x` across the engine was a
  no-op before the fix.
- **Lesson**: when porting a new header, sub-member assignment through a `__property` is a red flag;
  verify the getter is an lvalue.

## 4. RTL stubs that are silently no-ops / empty

Several `native/rtlcompat` methods started as empty stubs that compiled fine but did nothing at
runtime. Each caused a downstream crash far from the cause.

- `TStrings::Assign` was `{}` → `OutputCopy->Assign(FOutput)` left the copy empty → `Strings[0]`
  out-of-bounds → crash in `basic_string::__is_long`. (Now implemented.)
- **Lesson**: a crash deep in `std::basic_string`/STL with a tiny/garbage `this` usually means an
  rtlcompat method upstream returned nothing/garbage. Grep the rtlcompat impl of the methods on the
  path for `{ }` / `(void)` stubs before lldb-spelunking.

## 5. Delphi property binds to its DECLARING class (static), C++ `__declspec` binds by scope

A `property X read GetX` in a base class always calls *that base's* `GetX`, even if a derived class
redeclares `GetX`. C++ `__declspec(property)` resolves the accessor name in the **use scope**, so a
derived method using the inherited property calls the *derived* accessor → infinite recursion.

- **Symptom**: stack overflow at a function prologue (lldb mis-reports `this=NULL`, EXC_BAD_ACCESS
  code=2 at a stack address) — e.g. `TRemoteDirectoryChangesCache::GetValue` recursing via the
  inherited `Values[]`.
- **Fix**: bind base properties to non-shadowable accessor names (`DoGetValue`/`DoSetValue` etc) so
  a derived shadow can't capture them (see `TStrings` in Classes.hpp).

## 6. `ProcessFiles` requires `TRemoteFile*` as each entry's `Object`

`TStrings` passed to `CopyToLocal`/`DeleteFiles`/`ChangeFilesProperties` must carry the
`TRemoteFile*` as `Objects[i]` (the engine does `FileList->Objects[Index]`). A bare string list →
NULL `File` → crash in `DoAllowRemoteFileTransfer`/`Resolve`. Upload (`CopyToRemote`) tolerates NULL
objects; download/ops do not. Use the live listing (`g_terminal->Files`) — see
`enginebridge::FindInListing`.

## 7. Vendored libs (neon) conflate `WINSCP` with Windows

The WinSCP forks of vendored libs gate *both* WinSCP-integration hooks AND Windows-specific code on
`#ifdef WINSCP`. You need `-DWINSCP` for the hooks the engine expects (e.g. neon's
`NE_207_LIBERAL_ESCAPING`, `NE_DBG_WINSCP_*`), but then the Windows branches (`ioctlsocket`/`FIONBIO`,
`<windows.h>`, `<io.h>`) activate on macOS and break. Fix per-branch: `#if defined(WINSCP) &&
defined(_WIN32)` so unix takes the portable `#else`. Also: a vendored lib's `src/config.h` may be
entirely `#ifdef WIN32` — give it a `#else` that includes a captured autotools config (see
`native/neon/neon_config_unix.h`, generated by an out-of-tree `./configure` then `config.status
config.h`). And disable features you don't want in that captured config (we set `/* #undef
HAVE_GSSAPI */` to avoid a Kerberos link dep). All such edits are `#ifdef`-guarded → Windows build
untouched (UPSTREAM-PATCHES.md).

## 8. genprops over-eager closure wrapping on method DEFINITIONS

The fixed-slot closure rules (ProcessFiles/CalculateFilesChecksum/InitNeonTls/...) match a function
NAME + arg slot. A method DEFINITION with an UNNAMED event-type parameter
(`void T::CalculateFilesChecksum(..., TCalculatedChecksumEvent, ...)`) looks exactly like a call
with a method-ref in that slot, so genprops wrapped the TYPE name in `MakeClosure(...)` → garbage.
Guard: skip tokens matching `T[A-Z]\w*Event$` (a Delphi event TYPE, never a callable). When adding a
new slot rule, remember declarations with NAMED params are already skipped (two tokens fail the
single-token anchor) but unnamed-type params are not.

## 9. Protocol-agnostic test paths

The remote root differs per protocol: SFTP/SCP home = `/config` (the Docker user's home), WebDAV
root = `/`. Hardcoding `/config/...` in a transfer/op test gives 403/409 on WebDAV. Use
`Terminal->CurrentDirectory` (via `UnixIncludeTrailingBackslash`) as the target — works everywhere.
Seed/verify WebDAV with curl: `curl -u u:p -X PROPFIND -H "Depth: 1" http://host/`,
`curl -u u:p -T file http://host/x`. Test server:
`docker run -d --name winscp-test-dav -p 8086:80 -e AUTH_TYPE=Basic -e USERNAME=winscp
-e PASSWORD=winscp123 bytemark/webdav`.

## 10. New protocol/library init must be called explicitly

`CoreInitialize` does library init the harness/bridge skip. WebDAV needs `NeonInitialize()`
(`ne_sock_init`) after `PuttyInitialize()` — without it `RequireNeon` throws "Neon HTTP library
initialization failed". When wiring a new backend, check what global init upstream's CoreMain does.

## 11. rtlcompat string/date methods must match Embarcadero EXACTLY (not std:: intuition)

The engine relies on precise Embarcadero RTL semantics; a "reasonable" std-style implementation
silently corrupts data far away. Confirmed cases (all caused a remote bug):
- **`UnicodeString::SubString(start, count)`**: a `start < 1` is clamped to 1 with **count
  UNCHANGED** (so `SubString(0, n)` = first n chars). std::string-style "reduce count by the
  underflow" drops a char — `S3FileSystem::ParsePath` does `SubString(0, P-1)` and the S3 bucket
  name lost its last char ("testbucket" → "testbucke" → MinIO "bucket does not exist").
- **`AnsiString::data()`** returns **NULL for an empty string** (unlike `c_str()` which returns
  `""`). S3 passes `SecurityToken.data()`; libs3 adds `x-amz-security-token` only when non-NULL — an
  empty `""` sent a bogus signed header → SigV4 "headers not signed".
- **`AnsiString::c_str() const`** returns a non-const `char*` (engine assigns it to C `char*`
  fields like `ne_uri.path`).
- **`FormatDateTime`** must interpret the Delphi picture (yyyy/mm/mmm/dd/ddd/hh/nn/ss/'lit'/am-pm),
  not return ISO — WebDAV's HTTP-date header and any formatted date depend on it.
- **`UnicodeString(wchar_t)`** must be a 1-char string, not the numeric int ctor (L'/' → "/", not
  "47"); see §1-adjacent. **Lesson**: when an engine value is subtly wrong (off by one char, empty,
  wrong format), suspect the rtlcompat primitive and check Delphi/Embarcadero docs for exact
  semantics before assuming std behavior.

---

## DEBUG TECHNIQUES THAT WORKED (use these first)

1. **Read the exception, don't lldb-spelunk.** Harness catch dumps `E.Message` + ExtException
   `MoreMessages`. The harness `OnQueryUser` now also prints `MoreMessages` (the 3rd param) — that's
   where the *real* error hides when an op "succeeds" but does nothing (the query was auto-skipped).
   Example: "Copying file failed" → MoreMessages → "scp: ...: Permission denied" / "Illegal time
   format" pinpointed the bug instantly.
2. **lldb single-frame backtrace = optimized/leaf crash.** When `bt` shows only frame 0 (often a
   libc/STL leaf on a bad pointer), don't trust it. Use a **conditional breakpoint** (`br set -f X.cpp
   -l N -c 'this==0'` then `bt`) or just add targeted `fprintf(stderr,...)` traces — far faster than
   fighting the unwinder. Build is `-O0` already; inlining still flattens frames.
3. **`fprintf` trace bisection.** Drop `fprintf(stderr,"[TAG] ...")` at method entries along the
   suspected path, narrow to the failing statement, then trace the values. Remove before commit
   (these are `source/` edits — `git checkout` the file to revert all at once).
4. **The Docker sshd is the oracle.** `docker exec winscp-test-sshd cat /config/logs/openssh/current`
   (LogLevel DEBUG2). And `stat -c '%a %U %n' <file>` reveals perms/owner — caught both the
   root-owned-seed-file false alarm (chmod) and the mode-0000 upload bug.
5. **Seed-file ownership gotcha**: files created via `docker exec ... echo > /config/x` are
   **root-owned**; the `winscp` SFTP user can't chmod/overwrite them. Test perms/writes against files
   the login user owns (upload one first). A "permission denied" may be the *test*, not the code.

## HARNESS SELF-TESTS (native/harness/main.cpp, env-gated)

- `WINSCP_SCP=1` — use SCP instead of SFTP (`fsSCPonly`).
- `WINSCP_XFER=1` — upload+download round-trip self-test.
- `WINSCP_OPS=1` — mkdir/rename/delete self-test.
- Combine, e.g. `WINSCP_SCP=1 WINSCP_XFER=1 ./native/build/harness/winscp-harness 127.0.0.1 2222 winscp winscp123`.

## GOTCHAS (also in RESUME/NEXT-STEPS)

- After Write, strip any stray `</content>` line: `perl -ni -e 'print unless /^<\/content>\s*$/' <file>`.
- LSP/clangd shows false errors on rtlcompat/engine headers (no flags) — trust `cmake --build`.
- CMake **de-duplicates** repeated compile flags (e.g. a second `-include`); wrap as
  `"SHELL:-include foo.h"` to keep pairs intact.
- macOS has no `timeout`; bound a hanging run with background + `sleep` + `kill -9`.
- Always `cmake --build build` + `ctest` + actually run before claiming green.
