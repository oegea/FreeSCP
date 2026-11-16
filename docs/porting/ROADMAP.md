# FreeSCP — WinSCP fidelity roadmap

What WinSCP does that FreeSCP still needs, to pick up later. ✅ done · 🟡 partial · ⬜ todo.
(Engine/protocol parity is essentially complete; this is mostly GUI/UX + auth fidelity.)

## Done (for reference)
✅ 5 protocols (SFTP/SCP/FTP/WebDAV/S3 + TLS) connect/list/transfer/ops · dual-pane Commander ·
session tabs · directory tree · background queue + parallel transfers · drag&drop (panels + Finder→app)
· edit-remote-in-any-editor (watch+reupload) · internal editor · public-key auth (OpenSSH auto-convert)
· host-key verification · overwrite confirmation · Copy/Move dialog · recursive chmod · Properties info
· bookmarks · select-by-mask · copy path/URL · create file · Open Terminal · sync browsing ·
synchronize (collect/apply) · Site Manager (save/load) · preferences · column sort · hidden-files toggle
· **drag OUT to Finder** (files) · app icon · branded alerts · custom About (icon/repo/credit) · richer
toolbar · **signed + notarized macOS release** (v0.1.0) · public repo · **GitHub Pages website**.

✅ Added 2026-11-16: **SSH tunnel** (jump host) · **proxy** (SOCKS4/5, HTTP) · **2FA / keyboard-interactive
prompts** · **FTPS** explicit/implicit + FTP active mode · UTF-8 file names Auto/On/Off · **Transfer
Settings** (text/binary/auto, preserve timestamp, permissions, name case, file mask, newer only,
**resume**, **speed limit**) · **Find Files** · **Calculate Directory Sizes** · **Compare Directories** ·
**Compare Files** (side-by-side local/remote diff) · correct time zones + preserved timestamps everywhere ·
FTP time-outs + keep-alive · each session tab remembers its directories. (Released as **v0.3.0**.)

## High value, next
- 🟡 **Drag OUT of folders** — drag-out works for files; remote *directories* aren't exported yet
  (needs recursive temp download or macOS file promises).
- ⬜ **Recent directories** dropdown in the address bar (per panel, persisted).
- ⬜ **Synchronize two local directories** (new in WinSCP 6.6; engine side merged, needs a native
  replacement for the Windows shell copy/delete + UI).
- ⬜ **Keep remote directory up to date** — watch local dir, auto-sync changes (WinSCP's killer sync).

## Auth / connection (fidelity gaps)
- ⬜ **Time zone offset / DST mode** per site (SCP shows the server's clock; FTP servers without MLSD
  need it for files across a DST change).
- 🟡 Proxy: SOCKS4/5 + HTTP done; Telnet / local-command proxies and "proxy localhost too" not exposed.
- ⬜ **ssh-agent** (Unix `SSH_AUTH_SOCK` client; 6.7 added an agent-implementation switch upstream), multiple keys.
- ⬜ GSSAPI/Kerberos (engine has it stubbed: NO_GSSAPI).
- ⬜ S3: region/endpoint/session-token/path-style UI; ACLs.
- ⬜ Site folders, per-site color/notes, **master password** for stored credentials (site, tunnel and proxy
  passwords are stored as typed).

## Transfer
- 🟡 Transfer settings: done globally; **presets** and per-site / per-transfer settings still to do.
- 🟡 Resume: automatic via `.filepart` (verified on SFTP); no explicit "Resume / Append" choice in the overwrite prompt; FTP REST untested.
- 🟡 Speed limit: global setting done; no per-queue-item change while running.
- ⬜ Queue: pause/resume per item, reorder, persist across restart.
- ⬜ Background transfers + completion notification.
- ⬜ Remote↔remote (site-to-site) transfer.

## Navigation / panels
- ⬜ **Explorer mode** (single pane + tree) — only Commander now.
- ⬜ Workspaces (save/restore a set of tabs).
- ⬜ Bookmarks **bar** (toolbar), not just menu.
- ⬜ Panel file-mask **filter** box (show only *.txt).
- ⬜ Configurable columns (add/remove/reorder); per-column sort persistence.

## Operations
- ⬜ Owner/group edit (chown/chgrp); numeric + symbolic perms.
- ⬜ Create symlink / follow symlinks; touch / set timestamps.
- ⬜ Duplicate (server-side copy where supported).
- ⬜ Custom commands (run a command on the selection, local/remote).
- 🟡 Compare Files: read-only side-by-side view done; no merge / external diff tool hook (WinSCP does this through extensions).

## UI / polish
- ⬜ **Dark theme** (option) + font/color config.
- ⬜ Richer toolbar (drive/bookmark bar, more named buttons) + configurable.
- ⬜ File-type icon overlays (symlink, hidden); MIME-accurate icons.
- ⬜ Free space / quota in the status bar (SFTP statvfs extension).
- ⬜ Full Preferences (WinSCP has ~15 pages: Storage, Logging, Integration, Editors list, presets…).
- ⬜ Reconnect / auto-reconnect on dropped session.
- ⬜ Translations / i18n.

## Out of scope (for now)
Scripting CLI (.com) + .NET assembly · Finder/Nautilus shell extension · `sftp://` URL handler ·
portable INI mode.

## Known deltas vs WinSCP (intentional/simplifications)
- Updates: a notice that a newer version exists (0.3.1), with a link to the releases page. No
  in-app download or automatic install (WinSCP has both).
- File names are transferred verbatim: no `%XX` replacement of the characters Windows forbids.
- Transfer Settings are one global set (no presets); a file excluded by the mask shows "done".
- Sync browsing mirror is relative + no-ops if the subdir doesn't exist (WinSCP is similar).
- Parallel transfers gated to serial for FTP (single-connection backend).
- putty libs built with 4-byte wchar_t (off the SFTP path; see LEARNINGS §13) — fine in practice.
