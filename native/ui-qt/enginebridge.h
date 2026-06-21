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
enum class Protocol { Sftp = 0, Scp = 1, WebDav = 2, S3 = 3 };

// Open a remote session (blocking). One session at a time; a new connect replaces the old.
// `protocol` selects SFTP (default) or SCP — both run over the same SSH transport.
ConnectResult connectSftp(const std::string & host, int port,
                          const std::string & user, const std::string & password,
                          Protocol protocol = Protocol::Sftp);
bool remoteConnected();
std::string remoteCurrentDir();

// Register a transfer-progress sink: called during upload/download with (current file, percent
// 0-100 for that file). Pass an empty std::function to clear. Fired on the calling thread (the
// blocking transfer), so a Qt UI should processEvents() inside it.
void setProgressSink(const std::function<void(const std::string &, int)> & cb);
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

} // namespace engine

#endif
