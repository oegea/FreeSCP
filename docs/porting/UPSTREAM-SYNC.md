# Upstream (WinSCP) base & sync process

FreeSCP is a port of WinSCP: its protocol **engine is WinSCP's own source** (`source/`, `libs/`),
compiled natively. This file records exactly which WinSCP revision we're based on and how to pull
later upstream changes in safely.

## Fork base (current)

| | |
|---|---|
| **Upstream commit** | `82e192fff` — `Bumping devel version to 6.7.2` (= tag `6.7.1` + 6 post-release commits) |
| Upstream version | WinSCP **6.7.1** (stable) plus the fixes for issues 2482–2485 |
| Author / date | Martin Přikryl — 2026-11-11 |
| Merged into the port | `8e38711ad` (2026-11-13), merge of `winscp/master` |
| Bundled libraries at this base | PuTTY 0.85 · neon 0.37.1 · Expat 2.10.0 · OpenSSL 3.5.9 (vendored copy; the native build links the system/Homebrew OpenSSL 3) |
| Upstream repo | https://github.com/winscp/winscp (official mirror) — remote `winscp` |

History of bases:

| Base | Date | Notes |
|---|---|---|
| `74a8c03f4` | 2026-06-17 | Original fork point. First FreeSCP commit on top: `bea2d9b5c`. |
| `82e192fff` | 2026-11-13 | 737 upstream commits (6.5.x → 6.6 → 6.7.1). Triage and the port adaptation it needed are in the merge commit message of `8e38711ad`. |

Everything in `source/`, `libs/`, `dotnet/`, `deployment/` is WinSCP's; the port lives in `native/` +
`docs/porting/`. The only edits inside `source/`/`libs/` are the small guarded patches listed in
**UPSTREAM-PATCHES.md**.

### What the 6.5 → 6.7.1 update needed (so the next one is quicker)

- **PuTTY bump (0.83 → 0.85)**: check `native/putty/CMakeLists.txt` against the new file layout
  (the `conf.winscp.h` shadow and the `WINSCP_VS` crypto sub-build disappeared upstream), the
  platform contract in `native/putty/src/*.c` (`platform_start_subprocess` gained a parameter) and
  new globals the Windows backend defines (`auth_agent_implementation`).
- **FileZilla / `libs/mfc`**: upstream replaced the MFC subset with a tiny `UnicodeString`-backed
  one. Our shim (`native/filezilla/compat/afx.h`) must expose the same surface: diff
  `libs/mfc/include/afx.h` after every update.
- **genprops**: new spellings of constructs it rewrites (this time `reinterpret_cast<TProcessFileEventEx>`
  and `StartAction(Method)`). A compile error "reference to non-static member function must be
  called" or a `reinterpret_cast` between `std::function`s means a rule needs extending.
- **rtlcompat**: new unit-scoped includes (`System.Math.hpp`, `Winapi.Windows.hpp`, ...) are one-line
  forwards; new RTL functions go in `SysExtra`.
- **Windows-only engine code** (COM, reparse points, SSPI): guard with `#ifdef _WIN32` and list it in
  UPSTREAM-PATCHES.md.
- **Resource strings**: regenerate `native/rtlcompat/src/ResStrings.cpp`
  (`python3 native/tools/genstrings.py source/resource native/rtlcompat/src/ResStrings.cpp`).
- **CMake globs**: re-run `cmake -B build` (new/removed `.c` files in neon/expat/putty).

## How to update to a newer WinSCP

When asked to "update to the latest WinSCP", follow this:

1. **Add the upstream remote** (once):
   ```sh
   git remote add winscp https://github.com/winscp/winscp.git
   git fetch winscp
   ```
2. **Pick the target** upstream commit/tag, call it `$NEW`. Record the old base: `OLD=82e192fff`.
3. **Get the diff that matters** — only the parts the port compiles (engine + vendored libs), not the
   Windows GUI/IDE/build files we don't use:
   ```sh
   git diff $OLD..$NEW -- source/core source/putty source/filezilla \
       libs/openssl libs/neon libs/expat libs/libs3 > /tmp/winscp-upstream.diff
   git log --oneline $OLD..$NEW -- source/core source/putty source/filezilla libs/   # commit list
   ```
   (Ignore `source/forms`, `source/windows`, `source/components`, `source/packages`, `dotnet/`,
   `deployment/` — Windows-only, not in the FreeSCP build.)
4. **Triage impact** before applying. For each upstream change, classify:
   - 🔴 **Security** — CVE fixes, auth/crypto/host-key, SSH/TLS, path traversal, buffer handling.
     Highest priority; pull these even in isolation.
   - 🟠 **Protocol/correctness** — SFTP/SCP/FTP/WebDAV/S3 behavior, transfer logic, encoding.
   - 🟡 **Feature** — new options/capabilities (port only if we expose them).
   - ⚪ **Windows-only / irrelevant** — VCL/IDE/installer/.NET — skip.
   Write the verdict per area in the PR/commit message.
5. **Apply.** Because we don't fork `source/` (beyond guarded patches), the clean path is to merge or
   cherry-pick upstream into the engine files:
   ```sh
   git merge $NEW            # or cherry-pick specific commits for a security-only update
   ```
   Then **re-verify our guarded patches** (UPSTREAM-PATCHES.md) still apply and weren't clobbered —
   re-grep each `#ifndef _WIN32` / native fix; if upstream rewrote that code, re-port the guard.
6. **Rebuild + regression-test** (must stay green):
   ```sh
   cd native && cmake --build build && ctest --test-dir build
   ./native/build/harness/winscp-harness 127.0.0.1 2222 winscp winscp123   # SFTP smoke
   # plus the WINSCP_SCP/DAV/S3/FTP harness modes and the round-trip matrix in LEARNINGS.md §19
   ```
   Watch especially for: genprops output (engine headers changed → `__property`/`__closure` codegen),
   new RTL symbols (implement in `native/rtlcompat`), new resource-string IDs (`native/tools/genstrings`).
7. **Record the new base**: update the table above to `$NEW` + date, and note the range applied.

## Notes
- Keep the merges so this history stays a true superset of upstream (makes the next diff trivial).
- If a security fix touches code we've guarded, the guard must be reconciled, not blindly kept.
- The vendored libs (`libs/openssl|neon|expat|libs3`, `source/putty`, `source/filezilla`) get their
  OWN native CMake builds under `native/`; an upstream lib bump may need those build files refreshed.
