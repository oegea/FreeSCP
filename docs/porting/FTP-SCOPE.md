# FTP backend (FileZilla) — port scope

FTP is the hardest backend (CLAUDE.md: "HARDEST; FTP can be deferred"). This file maps the work so
the next session starts with the terrain known. Status: **engine side compiles; the FileZilla lib
is the remaining mountain.**

## What's done

- `source/core/FtpFileSystem.cpp` **compiles clean** (0 errors) with the engine flags + WcsCompat
  force-include. RTL gaps it needed are filled (commit "FtpFileSystem.cpp compiles"): `WORD`,
  `_wcsdup`, `Variant(double)`+arithmetic, `SystemTimeToFileTime`/`LocalFileTimeToFileTime`.
- It cannot LINK yet — it calls into the FileZilla library (`TFileZillaIntf`), which isn't built.

## The two mountains in `source/filezilla/` (16 .cpp)

### 1. MFC subset (`libs/mfc`)
FileZilla is written against MFC. `stdafx.h` pulls `<afx.h>` → `libs/mfc/include/afxv_w32.h` →
`#include <windows.h>` (hard Win32). Classes actually used (count):
- `CString` (261), `CStringA` (34) — the big one. Map to a CString shim over `UnicodeString` /
  `AnsiString` (rtlcompat already has both; CString API ≈ a subset).
- `CTime` (49), `CTimeSpan` (3) — map to rtlcompat `TDateTime` / `std::chrono`.
- `CFile` (24, aliased `CFileFix`), `CFileStatus` (6), `CFileException` (2) — map to POSIX file IO.
- `CObject` (1), collections (`CMap`/`CList`/`CArray` via `afxtempl.h`) — replace with std
  containers or a thin shim.
- `CCriticalSectionWrapper` (7) — `std::mutex`.

Approach: a **minimal `afx.h` shim** (own include dir, ahead of `libs/mfc`) defining only the
classes/methods FileZilla touches, over rtlcompat + std — do NOT try to compile `libs/mfc` itself
(it wants full Win32). Same playbook as the neon/libs3 platform shims.

### 2. Async socket layer (the deep part)
`CAsyncSocketEx` (162) + `CAsyncSocketExLayer` (76) + `CAsyncProxySocketLayer` (36) +
`CAsyncSslSocketLayer` (65, OpenSSL — reusable). The Windows design:
- `WSAAsyncSelect` (9) posts socket events (FD_READ/WRITE/CONNECT/CLOSE) as **window messages** to
  a hidden helper window `CAsyncSocketExHelperWindow` (`CreateWindow`, `PostMessage` ×29).
- `MainThread.cpp` runs a `CWinThread` with a Windows **message loop**.

Port: replace the message-pump model with a POSIX event loop — a dedicated thread running
`select()`/`poll()`/`kqueue` that translates readiness into the same `OnReceive/OnSend/OnConnect/
OnClose` callbacks. `CAsyncSocketExHelperWindow` + the `WSAAsyncSelect`/`PostMessage` plumbing
becomes a self-pipe + fd-set loop. This is the bulk of the effort.

## Suggested order (next sessions)
1. `afx.h` shim (CString/CTime/CFile/collections/CCriticalSection) → get the non-socket filezilla
   TUs (ServerPath, structures, FzApiStructures, FtpListResult) to compile.
2. POSIX `CAsyncSocketEx` (select-loop thread + callback dispatch); keep `CAsyncSslSocketLayer`
   (OpenSSL) mostly as-is.
3. `FtpControlSocket` / `TransferSocket` / `MainThread` / `FileZillaApi` / `FileZillaIntf` compile.
4. CMake `native/filezilla` static lib; link into harness + winscp-qt (whole-archive).
5. Un-guard the FTP branch in `TTerminal::Open()` (`source/core/Terminal.cpp`), wire `Protocol::Ftp`
   in enginebridge, test vs a Docker FTP server (e.g. `fauria/vsftpd` or `delfer/alpine-ftp-server`).

## Test server (for later)
`docker run -d --name winscp-test-ftp -p 21:21 -p 21000-21010:21000-21010 -e FTP_USER=winscp
-e FTP_PASS=winscp123 delfer/alpine-ftp-server`

## Progress: MFC shim built (native/filezilla/compat/)
Built the afx shim layer so FileZilla's `<afx.h>` chain resolves natively:
- `afx.h` — win scalar types, ASSERT/DebugAssert no-ops, InterlockedIncrement/Decrement (std::atomic),
  CObject, **CString** (2-byte-wchar, std::wstring-backed; the ~25 methods FileZilla uses: GetLength/
  Left/Mid/Right/Find/ReverseFind/Trim*/Replace/Compare*/Format/GetBuffer/...), CFile/CFileException/
  CFileStatus (POSIX FILE*), CTime/CTimeSpan, WPARAM/LPARAM/LRESULT.
- `afxconv.h` — USES_CONVERSION / A2T / T2A / T2CA / ... via a rotating thread_local buffer pool.
- `afx_format.cpp` — CString::FormatV (custom; libc vswprintf breaks under -fshort-wchar).
- `wtypes.h`, `oleauto.h` (empty — no OLE used), `TextsFileZilla.h` (IDS_* enum; LoadStr empty).

Verified: afx types COEXIST with rtlcompat (Global.h pulls the engine RTL) — no type-redefinition
clashes (identical typedefs). Remaining blockers, in order, found by compiling structures.cpp:
1. **genprops integration** — FileZilla TUs include engine headers with `__property` (FileBuffer.h),
   so they must compile against the genprops-processed `geninclude/core` (like engine TUs): run
   genprops on the filezilla .cpp too, include geninclude/core ahead of source/core.
2. **winsock2 shim** (the deep part) — `AsyncSocketEx.h` uses Win socket structs (`in6_addr.s6_bytes`,
   SOCKADDR/SOCKADDR_IN6, WSADATA, SOCKET, closesocket, ioctlsocket, WSAGetLastError, FD_* events).
   Provide `native/filezilla/compat/winsock2.h` mapping to BSD sockets + a Windows-layout in6_addr
   (`s6_bytes` union member). Then port `CAsyncSocketEx` (WSAAsyncSelect + helper-window message pump)
   to a select()/poll() event loop dispatching OnReceive/OnSend/OnConnect/OnClose.

## Progress: FTP backend BUILDS, LINKS, WIRED, reaches the server
The entire FileZilla lib compiles + links into harness/winscp-qt; FtpFileSystem is in winscpcore;
TTerminal::Open()'s FTP branch is un-guarded; harness `WINSCP_FTP=1` exercises it against
delfer/alpine-ftp-server. The Win32 message pump + WSAAsyncSelect select-loop emulation
(winmsg.cpp) runs: the control socket connects and in one run the server logged `OK LOGIN`
(172.17.0.1) — so connect + async send/recv event delivery + the message loop all function.

Current runtime frontier (not yet a clean session): the control socket is closed early in some
runs (select -> EBADF; now handled by dropping dead fds), and the engine reports "Connection
failed". Likely causes to chase next:
- CREATE_SUSPENDED is ignored (CreateThread starts immediately; ResumeThread is rtlcompat's no-op
  for our std::thread handle) — possible startup race between the worker's message loop and main
  thread setup. Consider a real suspended-start gate keyed on the thread handle.
- immediate connect() success on localhost (no EINPROGRESS) vs FileZilla expecting FD_CONNECT.
- LoadStr returns empty (no resource strings) -> empty [info] status lines; wire TextsCore strings
  for readable diagnostics.
- select-loop edge-trigger semantics (one-shot writable latch, FD_READ dedup) need validation under
  real traffic. FZ_TRACE=1 prints WSAAsyncSelect/select/post events.
