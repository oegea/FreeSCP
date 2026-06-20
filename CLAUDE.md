# CLAUDE.md — WinSCP native port (Mac / Linux)

This file orients any agent working in this repo. Read it fully before touching code.

## What this repo is

This is the **WinSCP** source tree (GPLv3 file manager: SFTP/FTP/FTPS/SCP/S3/WebDAV).
Upstream is **Windows-only**, built with **Embarcadero C++Builder 11** (VCL GUI) + **Visual
Studio 2022 MSBuild** (for the C# .NET assembly). See `readme.md` for the upstream layout.

We are porting it to **macOS first, then Linux**, keeping the **full GUI** and **all
features** — not a CLI. The user requirement is "identical or near-identical" behavior and
look. Decided strategy (see `docs/porting/PLAN.md` for the full reasoning):

- **Engine**: port `source/core` + vendored libs to native code via an **Embarcadero RTL
  compatibility layer** (`native/rtlcompat`) + **platform adapters** (`native/platform`).
  Output: `libwinscpcore` static lib. Same engine code → identical transfer behavior.
- **GUI**: **rewrite in Qt 6 Widgets** (`native/ui-qt`). Qt Widgets maps closely to VCL
  (same C++ language → direct calls to the engine, no IPC). Rebuild the 48 dialogs faithfully.
- **No Wine.** Pure native compile. (Wine was offered as a stopgap and declined.)

## Golden rule: do NOT break the upstream Windows build

All port work lives in **new top-level dirs** (`native/`, `docs/porting/`). Do **not** edit
files under `source/`, `libs/`, `dotnet/`, `deployment/` to make the port build — instead
shim, wrap, or add include paths. The Windows tree (`*.cbproj`, `build.bat`) must keep
working so we can diff behavior against the reference and pull upstream updates.

Exception: if a `source/` file genuinely needs a portability fix, guard it with
`#ifndef _WIN32` / `#ifdef __APPLE__` so Windows is untouched, and note it in
`docs/porting/UPSTREAM-PATCHES.md`.

## Repository map

```
source/core        engine, ~76k LOC C++ — SSH/SFTP/FTP/WebDAV/S3/SCP. PORT TARGET.
source/putty       PuTTY fork (SSH), C. Windows backend only; needs unix platform layer.
source/filezilla   FileZilla fork (FTP), C++. Winsock + MFC. HARDEST; FTP can be deferred.
source/forms       48 VCL dialogs, ~55k LOC. REWRITE in Qt.
source/windows     GUI glue (config, tools), ~31k LOC. Logic extractable, UI rewritten.
source/components  custom VCL controls (UnixDirView, etc.). REWRITE in Qt.
source/packages    3rd-party VCL (tb2k, tbx, png, jcl, dragdrop). REPLACE with Qt equivs.
source/console     Windows IPC child-process host. NOT reused (Qt app calls engine directly).
source/dragext     Windows COM shell extension. Later: Finder/Nautilus extension.
libs/openssl       OpenSSL 3.x — builds natively on Unix. REUSE.
libs/expat         expat XML — builds natively (cmake/autotools). REUSE.
libs/neon          neon WebDAV — builds natively (autotools). REUSE.
libs/libs3         S3 client — builds natively (GNUmakefile). REUSE.
libs/mfc           custom Borland MFC subset. REPLACE (used by filezilla).
dotnet             .NET assembly = shell-out wrapper around winscp.exe. Out of scope for now.

native/            >>> ALL PORT WORK LIVES HERE <<<
  rtlcompat/       Embarcadero RTL emulation (UnicodeString, TStringList, __property, ...)
  platform/        Mac/Linux adapters (registry->file, Win32->POSIX, FILETIME->chrono)
  ui-qt/           Qt 6 Widgets GUI
  cmake/           shared CMake modules
  CMakeLists.txt   top-level native build
docs/porting/      PLAN.md (roadmap), STATUS.md (progress), UPSTREAM-PATCHES.md
```

## The core porting challenge (read this)

`source/core` is welded to the Embarcadero C++Builder RTL, not just Win32:

| Construct | Count | Port approach |
|-----------|------:|---------------|
| `UnicodeString` | ~5190 | `native/rtlcompat` class over UTF-16 `std::u16string` (Delphi-compatible 1-based indexing) |
| `__fastcall` | ~5341 | `#define __fastcall` to nothing (clang ignores) |
| `__property` | ~624 | `Property<T>` proxy template emulating `obj->X = v` / `T v = obj->X` |
| `TStrings`/`TStringList` | ~583 | reimplement on `std::vector<UnicodeString>` |
| `TObject` subclasses | ~13 | lightweight `TObject` base (no VCL RTTI) |
| `HKEY`/registry | ~29 | platform adapter → INI/plist file storage (`THierarchicalStorage` already abstracts) |
| `FILETIME`/`SYSTEMTIME` | ~38 | `std::chrono` / `timespec` conversions in platform layer |
| `HANDLE` threading | ~74 | `std::thread`/`std::condition_variable` adapter |
| Winsock `SOCKET`/`WSA*` | — | BSD sockets adapter |

Note: Embarcadero's `bcc64` is **clang-based**, so `source` already has `#ifdef __clang__`
branches — we are not fighting a foreign compiler, we are supplying the missing RTL.

The integration files `PuttyIntf`, `NeonIntf`, `FileZillaIntf` are thin adapters around
portable C libs — keep them, fix the calling glue.

## Build (native port)

> Toolchain: clang (Xcode CLT), CMake ≥ 3.20, Qt 6 (`brew install qt`), nasm/autoconf/
> automake/libtool/pkg-config (`brew install ...`). Apple Silicon (arm64).

```sh
cd native
cmake -B build -G Ninja          # or default generator
cmake --build build
ctest --test-dir build           # rtlcompat + engine unit tests
```

Current buildable targets are listed in `docs/porting/STATUS.md`. **Always update STATUS.md
when a target starts/stops building**, and verify by actually building — do not claim a
target works without `cmake --build` succeeding.

## Build (upstream Windows — reference only, do not run on Mac)

`build.bat` → needs C++Builder 11 + VS2022. Used only as the behavior reference.

## Working conventions

- **Iterate small, build often, run it.** The user wants to see steps working — after each
  meaningful change, build and (where possible) run. Prefer many verifiable increments over
  one big untested drop.
- **Match upstream code style** when adding shims (the `//---` separators, naming).
- Engine porting order: `rtlcompat` → smallest leaf core files → `platform` adapters →
  protocol filesystems → terminal/session → wire to Qt. See PLAN.md phases.
- Keep `docs/porting/STATUS.md` current; it is the single source of truth for progress.
- macOS is the active target; write platform code with a clean POSIX/`__APPLE__` split so
  Linux follows with minimal change.
