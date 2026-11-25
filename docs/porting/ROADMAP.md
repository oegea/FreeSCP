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

✅ Added 2026-11-25 (UI fidelity round): panels laid out like WinSCP's Commander (per-panel navigation
and operation toolbars, path caption, WinSCP's column sets, one date format, WinSCP's status lines, lock /
protocol / session time in the status bar, menus in WinSCP's order, own icon set) · **Properties** with
owner / group / special bits / several files / "add X to directories", also for local files · **New > Link**
(symbolic, hard) · **Duplicate** and **Move To** (server-side on a remote panel) · panel **filter** by file
mask · Select / Unselect dialog, select all, invert · **Open Directory** with bookmarks + **recent
directories** (persisted) · Tabs > **Sites** · **Server and Protocol Information** (with free space) ·
**reconnect** a lost session in the same tab · Login dialog arranged like WinSCP's (Manage > Rename / Delete) ·
**Keep Remote Directory up to Date** (watch a local directory, upload every change) · **internal editor**
that keeps the file's encoding / byte-order mark / line endings, with find & replace, go to line, reload ·
**master password** (saved passwords encrypted with the engine's AES-256 scheme).

## High value, next
- 🟡 **Drag OUT of folders** — drag-out works for files; remote *directories* aren't exported yet
  (needs recursive temp download or macOS file promises).
- ⬜ **Synchronize two local directories** (new in WinSCP 6.6; engine side merged, needs a native
  replacement for the Windows shell copy/delete + UI).
- 🟡 **Keep remote directory up to date**: done (delete files, subdirectories, synchronize on start).
  Not there: "existing files only", minimising to the tray, beep on change.

## Auth / connection (fidelity gaps)
- ⬜ **Time zone offset / DST mode** per site (SCP shows the server's clock; FTP servers without MLSD
  need it for files across a DST change).
- 🟡 Proxy: SOCKS4/5 + HTTP done; Telnet / local-command proxies and "proxy localhost too" not exposed.
- ⬜ **ssh-agent** (Unix `SSH_AUTH_SOCK` client; 6.7 added an agent-implementation switch upstream), multiple keys.
- ⬜ GSSAPI/Kerberos (engine has it stubbed: NO_GSSAPI).
- ⬜ S3: region/endpoint/session-token/path-style UI; ACLs.
- 🟡 **Master password**: done, opt-in (Preferences > Security). Without it site, tunnel and proxy
  passwords are still stored as typed - WinSCP obfuscates them by default; the system keychain
  (macOS Keychain / libsecret) would be the better default. No "remember the master password".
- ⬜ Site folders, per-site color/notes.

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
- 🟡 Bookmarks: Open Directory dialog (Ctrl+O) and the Local / Remote > Bookmarks menus; no per-session
  bookmarks and no bookmark toolbar.
- 🟡 Columns: WinSCP's default sets, widths remembered; no add/remove/reorder, sort order not persisted.

## Operations
- 🟡 Owner / group: by name or id (SFTP-3 sends ids only: a name must be one the session has seen); no
  drop-down of the server's users and groups.
- ⬜ Edit an existing link's target; touch / set timestamps.
- 🟡 Duplicate: files everywhere but FTP; a directory only where the server copies trees (SCP, WebDAV).
  WinSCP falls back to a second shell session or a local temporary copy.
- ⬜ Custom commands (run a command on the selection, local/remote).
- 🟡 Internal editor: encodings detected (UTF-8 / BOM / UTF-16 / ISO-8859-1), not selectable; no
  "save in another encoding", no colour / font options, no editor list (external editors per mask).
- 🟡 Compare Files: read-only side-by-side view done; no merge / external diff tool hook (WinSCP does this through extensions).

## UI / polish
- ⬜ **Dark theme** (option) + font/color config.
- 🟡 Toolbars: WinSCP's arrangement, own icons; not configurable, no bookmark bar.
- 🟡 Icon overlays: symbolic links are marked; hidden files are not dimmed.
- 🟡 Free space: in Server and Protocol Information (SFTP, WebDAV); not in the status bar.
- ⬜ Full Preferences (WinSCP has ~15 pages: Storage, Logging, Integration, Editors list, presets…).
- 🟡 Reconnect: a lost session is offered to be reopened in the same tab (same directory); no automatic
  retry after a delay, no keep-alive setting, a transfer that was running is not resumed by itself.
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
- SCP: "Set permissions recursively" with "Add X to directories" runs `find ... -exec chmod` (directories
  first). WinSCP runs `chmod -R <mode>` and then fixes the top directory only, which fails as soon as
  the mode has no x (e.g. 644).
- S3: folders cannot be renamed, moved or duplicated (same as WinSCP); the panel says so up front.
- The local panel shows a Type column from the file name (MIME description, or "EXT File"), not the
  operating system's registered type names.
- putty libs built with 4-byte wchar_t (off the SFTP path; see LEARNINGS §13) — fine in practice.
