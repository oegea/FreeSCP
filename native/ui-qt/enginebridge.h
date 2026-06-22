//---------------------------------------------------------------------------
// enginebridge.h — clean boundary between the Qt UI and the ported engine.
//
// The engine + rtlcompat are compiled with -fshort-wchar (2-byte wchar_t, Delphi UTF-16) and
// expose UnicodeString everywhere. Qt is compiled normally (4-byte wchar_t, QString). Mixing
// the two ABIs in one translation unit is unsafe, so ALL engine access goes through this
// header, which uses only std types (UTF-8 std::string). enginebridge.cpp is the ONLY TU
// compiled with the engine flags; the Qt code includes just this header.
//
// This is the pattern the whole GUI uses to talk to the engine (local now; remote sessions
// the same way once the SSH/SFTP backend lands).
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_ENGINEBRIDGE_H
#define WINSCP_UIQT_ENGINEBRIDGE_H

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace engine {

struct DirEntry
{
  std::string name;       // UTF-8
  std::int64_t size = 0;
  bool isDir = false;
  bool isParent = false;  // the ".." entry
  std::string modified;   // formatted timestamp (empty for "..")
  std::string rights;     // remote: unix rights string (e.g. "rwxr-xr-x"); empty for local
  std::string owner;      // remote: owner[/group]; empty for local
};

// Engine version/banner (proves winscpcore is linked and callable).
std::string banner();

//--- diagnostics log ---
// Append a line to the diagnostics log (/tmp/winscp-native.log), flushed immediately.
void logLine(const std::string & msg);
// Path of the diagnostics log file (for showing the user / opening it).
std::string logPath();
// Install signal handlers that dump a backtrace to the log on a hard crash. Call once at startup.
void installCrashHandler();

// List a local directory via the ported engine file enumeration (FindFirst/TSearchRec).
// Returns entries sorted dirs-first then by name; includes ".." unless at root.
std::vector<DirEntry> listLocalDir(const std::string & utf8Path);

// Path helpers (ported engine routines).
std::string homeDir();
std::string parentDir(const std::string & utf8Path);
std::string joinPath(const std::string & dir, const std::string & name);
std::string formatSize(std::int64_t bytes);

// Copy a local file (real file operation via the platform layer). Returns true on success.
bool copyFile(const std::string & srcUtf8, const std::string & dstUtf8);

// Local file operations (std::filesystem). `dir` is the directory; `name`/`old`/`new` are entries.
bool localMakeDir(const std::string & dirUtf8, const std::string & nameUtf8, std::string * error = nullptr);
bool localDelete(const std::string & dirUtf8, const std::string & nameUtf8, std::string * error = nullptr);
bool localRename(const std::string & dirUtf8, const std::string & oldUtf8, const std::string & newUtf8,
                 std::string * error = nullptr);

//--- remote SFTP session (a single active session; mirrors the local panel) ---
struct ConnectResult
{
  bool ok = false;
  std::string error;        // human-readable on failure
  std::string currentDir;   // resolved home directory on success
};

// File-transfer protocol for a remote session.
enum class Protocol { Sftp = 0, Scp = 1, WebDav = 2, S3 = 3, Ftp = 4 };

// Open a remote session (blocking). One session at a time; a new connect replaces the old.
// `protocol` selects SFTP (default) or SCP — both run over the same SSH transport.
ConnectResult connectSftp(const std::string & host, int port,
                          const std::string & user, const std::string & password,
                          Protocol protocol = Protocol::Sftp, bool tls = false,
                          const std::string & keyFile = "");   // private key path (SSH); password = its passphrase
bool remoteConnected();
std::string remoteCurrentDir();

// Richer transfer progress, reported during upload/download.
struct TransferProgress
{
  std::string file;            // current file name (UTF-8)
  std::int64_t transferred = 0; // bytes transferred for this file
  std::int64_t total = 0;       // total bytes for this file (0 if unknown)
  std::int64_t cps = 0;         // current speed, bytes/sec
};
// Register a transfer-progress sink. Return TRUE to request cancellation of the current transfer.
// Pass an empty std::function to clear. Fired on the thread running the transfer.
void setProgressSink(const std::function<bool(const TransferProgress &)> & cb);
// Host-key (Yes/No) confirmation callback; returns true to accept. Unset -> auto-accept.
void setConfirmCallback(const std::function<bool(const std::string &)> & cb);
// List the remote directory (empty path = current/home). Sorted dirs-first; includes "..".
std::vector<DirEntry> listRemoteDir(const std::string & utf8Path);

// Transfer a single file over the live SFTP session (TTerminal CopyToRemote/CopyToLocal).
// localPath/remotePath are full paths; the target is a directory. Returns true on success;
// on failure `error` (if non-null) gets a human-readable message.
bool uploadToRemote(const std::string & localPathUtf8, const std::string & remoteDirUtf8,
                    std::string * error = nullptr);
bool downloadFromRemote(const std::string & remotePathUtf8, const std::string & localDirUtf8,
                        std::string * error = nullptr);

// Remote file operations on the live SFTP session (TTerminal). `name` is the entry name in the
// current remote directory (rename/delete look it up in the live listing). Return true on success.
bool remoteMakeDir(const std::string & nameUtf8, std::string * error = nullptr);
bool remoteDelete(const std::string & nameUtf8, std::string * error = nullptr);
bool remoteRename(const std::string & oldNameUtf8, const std::string & newNameUtf8,
                  std::string * error = nullptr);

// Current unix permissions of a remote entry as an octal string (e.g. "644"), empty if unknown.
std::string remoteFileOctal(const std::string & nameUtf8);
// Set remote permissions from an octal string. Returns true on success.
bool remoteChmod(const std::string & nameUtf8, const std::string & octalUtf8, std::string * error = nullptr);

void disconnectSftp();

//--- parallel-transfer connection pool (extra connections to the same server) ---
// Open an additional connection to the primary's server. Returns a handle (>=1) or 0 on failure.
bool parallelSupported();   // false for FTP (its backend can't run concurrent connections reliably)
int openParallelConnection(std::string * error = nullptr);
void setProgressSinkVia(int handle, const std::function<bool(const TransferProgress &)> & cb);
bool uploadVia(int handle, const std::string & localPath, const std::string & remoteDir, std::string * error = nullptr);
bool downloadVia(int handle, const std::string & remotePath, const std::string & localDir, std::string * error = nullptr);
void closeParallelConnections();

//--- directory synchronization (TSynchronizeChecklist) ---
struct SyncItem
{
  std::string action;   // "Upload" | "Download" | "Delete remote" | "Delete local"
  std::string name;     // relative file/dir name
  std::int64_t size = 0;
  bool isDir = false;
};
// Compare localDir vs remoteDir. mode: 0 = mirror local->remote, 1 = mirror remote->local, 2 = both.
// `del` includes deletions of extra files on the target. Returns the change list and keeps an internal
// checklist for synchronizeApply(). On failure returns empty + sets error.
std::vector<SyncItem> synchronizeCollect(const std::string & localDir, const std::string & remoteDir,
                                         int mode, bool del, std::string * error = nullptr);
bool synchronizeApply(std::string * error = nullptr);    // execute the collected checklist
void synchronizeRelease();                                // free the checklist

} // namespace engine

#endif
