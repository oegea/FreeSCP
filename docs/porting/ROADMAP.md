# FreeSCP — WinSCP fidelity roadmap

What WinSCP does that FreeSCP still needs, to pick up later. ✅ done · 🟡 partial · ⬜ todo.
(Engine/protocol parity is essentially complete; this is mostly GUI/UX + auth fidelity.)

## Done (for reference)
✅ 5 protocols (SFTP/SCP/FTP/WebDAV/S3 + TLS) connect/list/transfer/ops · dual-pane Commander ·
session tabs · directory tree · background queue + parallel transfers · drag&drop (panels + Finder→app)
· edit-remote-in-any-editor (watch+reupload) · internal editor · public-key auth (OpenSSH auto-convert)
· host-key verification · overwrite confirmation · Copy/Move dialog · recursive chmod · Properties info
· bookmarks · select-by-mask · copy path/URL · create file · Open Terminal · sync browsing ·
synchronize (collect/apply) · Site Manager (save/load) · preferences · column sort · hidden-files toggle.

## High value, next
- ⬜ **Drag OUT to Finder** — drag remote files out to download (local: real URLs; remote: download to
  temp then provide URLs, or macOS file promises). *In progress next.*
- ⬜ **Recent directories** dropdown in the address bar (per panel, persisted).
- ⬜ **Find files** — recursive remote search (TTerminal search / per-dir walk).
- ⬜ **Calculate directory sizes** (Space on a dir; show total in the panel).
- ⬜ **Keep remote directory up to date** — watch local dir, auto-sync changes (WinSCP's killer sync).

## Auth / connection (fidelity gaps)
- ⬜ **SSH tunneling** (jump host / bastion) — SessionData Tunnel* fields exist in the engine.
- ⬜ **Proxy** (HTTP/SOCKS) UI + wiring.
- ⬜ **ssh-agent / Pageant**, multiple keys.
- ⬜ **2FA / keyboard-interactive** prompt UI (currently auto-answers the password).
- ⬜ GSSAPI/Kerberos (engine has it stubbed: NO_GSSAPI).
- ⬜ FTP: passive/active toggle UI, FTPS explicit vs implicit choice.
- ⬜ S3: region/endpoint/session-token/path-style UI; ACLs.
- ⬜ Site folders, per-site color/notes, master password for stored credentials.

## Transfer
- ⬜ **Transfer settings**: binary/text/auto by mask (CRLF↔LF), preserve timestamps/permissions,
  filename mods. (Engine supports via TCopyParamType; UI only does binary now.)
- ⬜ **Resume** interrupted transfers (resume/append).
- ⬜ Speed limit (throttle).
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
- ⬜ Compare directories (visual diff).

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
- Sync browsing mirror is relative + no-ops if the subdir doesn't exist (WinSCP is similar).
- Parallel transfers gated to serial for FTP (single-connection backend).
- putty libs built with 4-byte wchar_t (off the SFTP path; see LEARNINGS §13) — fine in practice.
