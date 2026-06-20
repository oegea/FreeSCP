# WinSCP → macOS/Linux native port — Master Plan

Status tracker: see `STATUS.md`. This file is the roadmap and the reasoning behind it.

## Goal

Native macOS (then Linux) build of WinSCP with the **full GUI** and **all features**,
behaving **identically or near-identically** to the Windows original. Not a CLI.

## Decisions (locked)

1. **GUI: Qt 6 Widgets** (C++). Closest paradigm to VCL; same language as the engine →
   direct in-process calls, no IPC. Faithful rebuild of the 48 dialogs.
2. **Engine: port in place** via an Embarcadero-RTL compatibility layer + platform adapters.
   Reuse the actual `source/core` code → identical protocol/transfer behavior.
3. **No Wine.** Native compile only.
4. **macOS first**, Linux second, with a clean POSIX/`__APPLE__` split from day one.
5. **Windows upstream build stays intact** (reference + upstream merges).

## Why not the alternatives (for the record)

- *Rewrite engine from scratch*: throws away 20 years of battle-tested protocol edge cases
  → guarantees behavior drift. Rejected.
- *Electron UI*: web look ≠ native, heavy runtime, C++↔JS bridge on every engine call.
  Rejected in favor of Qt.
- *Wine-packaged .exe*: identical but not native; declined by user.

## Architecture (target)

```
            ┌─────────────────────────────────────────┐
            │  native/ui-qt   (Qt 6 Widgets GUI)       │  ← rewrite of source/forms+windows
            └───────────────┬─────────────────────────┘
                            │ direct C++ calls (TTerminal, TConfiguration, ...)
            ┌───────────────▼─────────────────────────┐
            │  libwinscpcore  (source/core, ported)    │
            │   + PuttyIntf / NeonIntf / FileZillaIntf │
            └───────┬───────────────┬──────────────────┘
                    │               │
   ┌────────────────▼──┐   ┌────────▼───────────────────────────────┐
   │ native/rtlcompat  │   │ native/platform (Mac/Linux adapters)    │
   │ UnicodeString,    │   │ storage(registry→file), sockets,        │
   │ TStringList, TObj,│   │ threads, time, filesystem, paths,       │
   │ __property, Format│   │ system info, clipboard hooks            │
   └───────────────────┘   └─────────────────────────────────────────┘
                    │
   ┌────────────────▼──────────────────────────────────────────────┐
   │ vendored libs (native builds): openssl, expat, neon, libs3,    │
   │ putty (with unix platform), filezilla (later)                  │
   └────────────────────────────────────────────────────────────────┘
```

## Phases

Each phase ends with a **verifiable artifact** (something that builds and/or runs). Build and
run at every step; update `STATUS.md`.

### Phase 0 — Foundation & proof of base  ◀ in progress
- [x] Architecture analysis (engine/GUI/libs coupling) — done.
- [x] CLAUDE.md + this plan.
- [ ] Toolchain installed (clang, cmake, ninja, qt6, nasm, autotools).
- [ ] `native/` scaffold + top-level CMake.
- [ ] Build **one already-portable vendored lib** natively (expat) → proves base toolchain.
- [ ] Qt "hello" window builds + runs → proves GUI toolchain.
- **Artifact**: empty Qt window opens on macOS; expat builds.

### Phase 1 — RTL compatibility layer (`native/rtlcompat`)
The keystone. Without it nothing in `source/core` compiles.
- [ ] `UnicodeString` (UTF-16, Delphi 1-based indexing, `c_str`, `Length`, `SubString`,
      `Pos`, `+`, comparison, `Format`/`FmtLoadStr`, `IntToStr`/`StrToInt`, case ops).
- [ ] `AnsiString`, `RawByteString`, `UTF8String` + conversions.
- [ ] `__fastcall`/`__closure` macros; `Property<T>` proxy template for `__property`.
- [ ] `TObject`, `Exception`/`ExtException`, `Sysutils` helpers, `TVarRec`/`ARRAYOFCONST`.
- [ ] `TStrings`/`TStringList`, `TList`, `TStream`/`TFileStream`/`TMemoryStream`.
- [ ] `TDateTime`, `TCriticalSection`/`TGuard`, `Classes.hpp`/`SysUtils.hpp`/`System.hpp`
      umbrella headers + `vcl.h` shim.
- **Strategy**: implement on demand, driven by compiling real core files leaf-first
  (`Common`, `Exceptions`, `RemoteFiles`, `FileMasks`, `CopyParam`, `FileBuffer`).
- **Artifact**: `ctest` green for rtlcompat unit tests; ≥5 leaf core files compile.

### Phase 2 — Platform adapters (`native/platform`, macOS)
- [ ] Storage: `THierarchicalStorage` backend → INI/plist files (replaces registry).
- [ ] Sockets: Winsock `SOCKET`/`WSA*` → BSD sockets + `select`/`poll`.
- [ ] Threading: `HANDLE` events/threads → `std::thread`/`condition_variable`.
- [ ] Time: `FILETIME`/`SYSTEMTIME` ↔ `std::chrono`/`timespec`.
- [ ] Filesystem: `FindFirstFile`, attrs, paths → `std::filesystem`/POSIX.
- [ ] System info: `IsWin*`/`WindowsProductName` → macOS equivalents/stubs.
- **Artifact**: engine config load/save + a unit test doing a local file op natively.

### Phase 3 — PuTTY (SSH/SFTP/SCP) native backend
- [ ] Add PuTTY's **unix** platform layer (network/select/noise) to `source/putty` build
      (port from upstream PuTTY unix sources; keep WinSCP's `conf.winscp.h`).
- [ ] Fix `PuttyIntf` glue for non-Win build.
- **Artifact**: native binary connects over SSH and lists a remote dir (headless test
      harness, no GUI yet).

### Phase 4 — neon (WebDAV) + libs3 (S3) + OpenSSL wiring
- [ ] Build neon, libs3, openssl natively; fix `NeonIntf` cert validation
      (Windows cert store → OpenSSL/Keychain).
- **Artifact**: native WebDAV + S3 dir listing via test harness.

### Phase 5 — Terminal/session engine end-to-end (no GUI)
- [ ] `TTerminal`, `TSecureShell`, `TConfiguration`, queue/threads compile + link into
      `libwinscpcore`.
- [ ] Headless harness: connect, list, upload, download, sync over SFTP — diff against
      Windows reference.
- **Artifact**: `libwinscpcore` does real transfers identically to upstream engine.

### Phase 6 — Qt GUI skeleton wired to engine
- [ ] App shell: main window, dual-pane Commander layout, local + remote file views
      (`UnixDirView`/`UnixDriveView` → `QTreeView` models backed by engine).
- [ ] Login dialog (subset) → open a real session → browse remote.
- **Artifact**: connect via GUI and browse a remote server on macOS.

### Phase 7 — Faithful dialog rebuild (the long tail)
- [ ] Port the 48 forms in priority order: Login/Sites, Copy/Transfer, Progress,
      Preferences, Synchronize, Properties, Editor, etc. Match layout/labels/behavior;
      reuse `source/resource` strings + `translations/`.
- **Artifact**: feature parity checklist in STATUS.md ticked off.

### Phase 8 — Integration, packaging, parity QA
- [ ] `.app` bundle, code signing, icons; drag&drop within app.
- [ ] Behavior-parity test matrix vs Windows (transfers, masks, sync, scripting).
- **Artifact**: distributable `WinSCP.app`.

### Phase 9 — Linux
- [ ] Flip platform layer to Linux (most POSIX already shared); package (AppImage/Flatpak).
- [ ] Optional: Nautilus/Dolphin integration (replaces `dragext`).

## Deferred / out of scope (for now)
- FileZilla FTP backend (Phase 3+ alt): heavy Winsock/MFC coupling. SFTP/SCP/WebDAV/S3
  cover most use; FTP added once engine is stable.
- `.NET assembly`, Inno Setup installer, Windows shell `dragext`.

## Risk register
- **`__property` emulation** is the biggest unknown — proxy template may not cover indexed/
  defaulted properties; fallback is targeted source edits behind `#ifndef _WIN32`.
- **RTL surface** larger than expected → schedule risk; mitigate by leaf-first on-demand impl.
- **Behavior drift** → continuous diff against Windows reference (headless harness).
- **Effort**: this is a multi-month project. Phases 1–5 (engine) are the long pole before any
  GUI value; Phase 6 is the first user-visible native milestone.
