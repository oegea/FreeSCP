# Upstream (WinSCP) base & sync process

FreeSCP is a port of WinSCP: its protocol **engine is WinSCP's own source** (`source/`, `libs/`),
compiled natively. This file records exactly which WinSCP revision we're based on and how to pull
later upstream changes in safely.

## Fork base (current)

| | |
|---|---|
| **Upstream commit** | `74a8c03f4b77a3de5930dc689de3b193cdcfb6a9` |
| Subject | `Merge remote-tracking branch 'origin/translations'` |
| Author / date | Martin Přikryl — 2026-06-17 |
| First FreeSCP commit on top | `bea2d9b5c` (Add macOS/Linux native port foundation, Phase 0) |
| Upstream repo | https://github.com/winscp/winscp (official mirror) |

Everything before `74a8c03f4` in this repo's history is unmodified WinSCP. Everything after is the
port. The port lives in `native/` + `docs/porting/`; the only edits inside `source/`/`libs/` are the
small, `#ifndef _WIN32`-guarded (or correct-everywhere) patches listed in **UPSTREAM-PATCHES.md**.

## How to update to a newer WinSCP

When asked to "update to the latest WinSCP", follow this:

1. **Add the upstream remote** (once):
   ```sh
   git remote add winscp https://github.com/winscp/winscp.git
   git fetch winscp
   ```
2. **Pick the target** upstream commit/tag, call it `$NEW`. Record the old base: `OLD=74a8c03f4`.
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
   # plus the WINSCP_SCP/DAV/S3/FTP harness modes (see LEARNINGS.md "HARNESS SELF-TESTS")
   ```
   Watch especially for: genprops output (engine headers changed → `__property`/`__closure` codegen),
   new RTL symbols (implement in `native/rtlcompat`), new resource-string IDs (`native/tools/genstrings`).
7. **Record the new base**: update the table above to `$NEW` + date, and note the range applied.

## Notes
- Keep the merges so this history stays a true superset of upstream (makes the next diff trivial).
- If a security fix touches code we've guarded, the guard must be reconciled, not blindly kept.
- The vendored libs (`libs/openssl|neon|expat|libs3`, `source/putty`, `source/filezilla`) get their
  OWN native CMake builds under `native/`; an upstream lib bump may need those build files refreshed.
