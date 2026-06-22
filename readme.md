<p align="center">
  <img src="docs/freescp-icon.png" width="160" alt="FreeSCP icon">
</p>

<h1 align="center">FreeSCP</h1>

<p align="center"><b>A native macOS &amp; Linux port of WinSCP.</b></p>

<p align="center">
  🌐 <a href="https://oegea.github.io/FreeSCP/">Website</a> ·
  ⬇️ <a href="https://github.com/oegea/FreeSCP/releases/latest">Download (macOS, signed &amp; notarized)</a>
</p>

---

## Why

[WinSCP](https://winscp.net/) is the file manager a lot of people grew up with — SFTP/FTP/FTPS/SCP/S3/
WebDAV, a dual-pane Commander, a built-in editor, synchronized browsing. But it has only ever existed
on **Windows**. On macOS and Linux that gap has stayed open for ~20 years: you end up juggling
Cyberduck, FileZilla, `sftp`, sshfs… none of them *is* WinSCP.

**FreeSCP closes that gap** by running WinSCP's actual engine natively on macOS and Linux, behind a
faithful Qt rebuild of its interface.

## How it works (ported vs rewritten)

- **The protocol engine is WinSCP's own code, recompiled natively — not a reimplementation.**
  `source/core` (~76k lines of C++: the SFTP/SCP/FTP/WebDAV/S3 state machines, transfer logic,
  synchronization) is the upstream WinSCP engine. It's welded to Embarcadero C++Builder's RTL, so
  instead of rewriting it we wrote that RTL from scratch:
  - **`native/rtlcompat`** — a from-zero reimplementation of the Embarcadero RTL the engine needs
    (`UnicodeString`, `TStringList`, `__property`, `TStream`, threading, …), so the engine compiles
    unmodified with clang. Transfers therefore behave *identically* to WinSCP — same code path.
  - **`native/platform` + `native/putty`** — POSIX adapters (registry→INI, Win32→POSIX, BSD sockets,
    a PuTTY unix backend) so the SSH/network layer runs on Unix.
  - Vendored protocol libraries built natively: **PuTTY** (SSH), **neon** (WebDAV), **libs3** (S3),
    **FileZilla** (FTP), plus OpenSSL/expat.
- **The GUI is rewritten from scratch in Qt 6 Widgets** (`native/ui-qt`), closely mirroring WinSCP's
  Commander — it calls the engine directly (no IPC) across one small UTF-8 boundary
  (`enginebridge`).

The original Windows build tree (`source/`, `*.cbproj`, `build.bat`) is left untouched and still
builds on Windows; all port work lives under `native/`.

## Features

Dual-pane Commander · **SFTP · SCP · FTP · WebDAV · S3** (＋TLS) · public-key auth (OpenSSH keys
auto-converted) · host-key verification · drag &amp; drop (incl. from Finder) · background transfer
**queue** with parallel transfers · **edit remote files in any editor** (auto re-upload on save) ·
built-in editor · directory **tree** · **session tabs** · synchronized browsing · bookmarks ·
overwrite confirmation · recursive chmod · file masks · Open Terminal.

## Build (macOS, Apple Silicon)

```sh
brew install qt cmake ninja nasm autoconf automake libtool pkg-config openssl
cd native
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
ctest --test-dir build        # rtlcompat + engine unit tests
```

(Linux follows the same shape — the platform layer is POSIX/`__APPLE__`/`__linux__`-clean; build the
vendored libs + Qt6 there. See `docs/porting/STATUS.md`.)

## Run

```sh
open native/build/ui-qt/winscp-qt.app          # macOS
# or: ./native/build/ui-qt/winscp-qt.app/Contents/MacOS/winscp-qt
```

Connect from the Login dialog (protocol, host, user, password or private key). Save sites for reuse;
open several at once with the **+** tab.

## Project layout

```
source/            upstream WinSCP source (engine = source/core). Unchanged; still builds on Windows.
native/rtlcompat   Embarcadero RTL reimplemented from scratch (lets the engine compile with clang)
native/platform    macOS/Linux platform adapters
native/putty       PuTTY unix backend (sockets/select/entropy)
native/neon|libs3|filezilla   native builds of the vendored protocol libs
native/ui-qt       the Qt 6 GUI + enginebridge (the UI↔engine boundary)
docs/porting       PLAN / STATUS / LEARNINGS / UPSTREAM-PATCHES
```

## Status & license

All five protocols work end-to-end (connect / list / transfer / file ops). GPLv3, inherited from
WinSCP. FreeSCP is an independent port and is not affiliated with or endorsed by Martin Přikryl /
WinSCP.

---

<details>
<summary>Original WinSCP README (Windows build)</summary>

[WinSCP](https://winscp.net/) is a popular free file manager for Windows supporting SFTP, FTP, FTPS,
SCP, S3, WebDAV and local-to-local file transfers. To build the upstream Windows version you need
Embarcadero C++Builder 11 and Visual Studio 2022 build tools; see [`build.bat`](build.bat). The
upstream source lives under `source/` and is unmodified by the port (except small `#ifndef _WIN32`
guards documented in `docs/porting/UPSTREAM-PATCHES.md`).

</details>
