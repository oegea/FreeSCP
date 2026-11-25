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
  std::int64_t mtimeSecs = 0;   // modification time in whole seconds (local time, comparable between
                                // local and remote listings); 0 = unknown
  bool mtimeHasSeconds = true;  // false when the listing only gave minute (or coarser) precision
  std::string rights;     // remote: unix rights string (e.g. "rwxr-xr-x"); empty for local
  std::string owner;      // remote: owner[/group]; empty for local
  std::string ownerName;  // remote: owner alone (name, or numeric id when the server gives no name)
  std::string groupName;  // remote: group alone
  bool isLink = false;    // a symbolic link (local or remote)
  std::string linkTarget; // where it points, when known
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

// Everything the Login dialog can configure for one connection.
struct ConnectOptions
{
  std::string host; int port = 22;
  std::string user, password;
  Protocol protocol = Protocol::Sftp;
  bool tls = false;               // WebDAV/S3: HTTPS. FTP: TLS (see ftpExplicitTls)
  bool ftpExplicitTls = true;     // FTP + tls: explicit (AUTH TLS, port 21) vs implicit (port 990)
  bool ftpPassive = true;         // FTP passive mode
  bool serverCrLf = false;        // the server's text files use CR/LF line endings (Windows servers);
                                  // only matters for text-mode transfers over SFTP/SCP
  int utf8Names = 0;              // UTF-8 encoding for file names: 0 = auto-detect, 1 = on, 2 = off.
                                  // (SCP on a server whose shell locale is not UTF-8, or an FTP server
                                  // that does not announce UTF8, needs "on" for non-ASCII names.)
  std::string keyFile;            // SSH private key; `password` is then its passphrase
  // SSH tunnel (jump host): the session is opened through a local port forwarded by this SSH server.
  bool tunnel = false;
  std::string tunnelHost; int tunnelPort = 22;
  std::string tunnelUser, tunnelPassword, tunnelKeyFile;   // tunnelPassword = key passphrase if a key is set
  // Proxy. method: 0 = none, 1 = SOCKS4, 2 = SOCKS5, 3 = HTTP (CONNECT).
  int proxyMethod = 0;
  std::string proxyHost; int proxyPort = 0;
  std::string proxyUser, proxyPassword;
};

// An authentication prompt the server (or the tunnel's server) asks during connect:
// password, key passphrase, keyboard-interactive / 2FA challenges, proxy credentials.
struct PromptRequest
{
  int kind = 0;                         // engine TPromptKind (informational)
  bool forTunnel = false;               // asked by the SSH tunnel (jump host), not the target server
  std::string name;                     // dialog caption (e.g. "Server prompt - user@host")
  std::string instructions;             // server-supplied text, may be empty
  std::vector<std::string> prompts;     // one label per field
  std::vector<bool> echo;               // per field: false = hide typing (password-style)
};
// Asked on the thread that called connect() (the UI thread). Fill `answers` (one per prompt) and
// return true, or return false to cancel the login. If no callback is set, every field is answered
// with the session password (headless tools).
void setPromptCallback(const std::function<bool(const PromptRequest &, std::vector<std::string> & answers)> & cb);

// Open a remote session (blocking) and make it the active one (a new tab).
ConnectResult connect(const ConnectOptions & options);

// Open a remote session (blocking). One session at a time; a new connect replaces the old.
// `protocol` selects SFTP (default) or SCP — both run over the same SSH transport.
ConnectResult connectSftp(const std::string & host, int port,
                          const std::string & user, const std::string & password,
                          Protocol protocol = Protocol::Sftp, bool tls = false,
                          const std::string & keyFile = "");   // private key path (SSH); password = its passphrase
bool remoteConnected();
std::string remoteCurrentDir();
// The active session was open and its connection is gone (dropped by the server, the network, a
// sleeping laptop): every remote call now fails with "not connected".
bool sessionLost();
// Open the active session again with the options (and the credentials typed at its prompts) it was
// opened with, in the same tab. On failure the lost session stays, so it can be retried or closed.
ConnectResult reconnect();
// Test hook: close the active session's connection without removing the session.
void debugDropConnection();

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
// On failure (cannot change to / read the directory) returns what could be read - usually nothing -
// and sets `error` to the engine's message; the session stays in its previous directory.
std::vector<DirEntry> listRemoteDir(const std::string & utf8Path, std::string * error = nullptr);

//--- protecting stored passwords with a master password ---
// WinSCP's own scheme (Cryptography.cpp): AES-256 with a key derived from the master password and a
// random salt, plus a MAC. The result is text (hex) for the settings store.
std::string protectSecret(const std::string & plainUtf8, const std::string & masterPassword);
// False if the master password is wrong or the value is damaged; `plain` is then left alone.
bool unprotectSecret(const std::string & hex, const std::string & masterPassword, std::string & plainUtf8);
// A value that tells whether a master password is the right one without storing it.
std::string masterVerifier(const std::string & masterPassword);
bool masterVerify(const std::string & masterPassword, const std::string & verifierHex);

//--- transfer settings (WinSCP "Transfer Settings": TCopyParamType) ---
// Apply to every upload, download and synchronization started after they are set.
struct TransferSettings
{
  int mode = 0;                    // 0 = binary, 1 = text (convert line endings), 2 = automatic: text for
                                   //     files matching textMask, binary for the rest
  std::string textMask;            // "*.txt; *.html; ..." - empty = the engine's default list
  bool preserveTime = true;        // keep the source file's modification time
  bool setRights = false;          // upload: set these permissions on uploaded files...
  std::string rightsOctal = "644"; //   (directories also get x where r is set)
  int fileNameCase = 0;            // 0 = no change, 1 = UPPER CASE, 2 = lower case
  int resume = 1;                  // transfer to a temporary ".filepart" and resume an interrupted transfer:
                                   //   0 = all files, 1 = files above resumeThresholdKB, 2 = never
  std::int64_t resumeThresholdKB = 100;
  int speedLimitKB = 0;            // KB/s, 0 = unlimited
  std::string fileMask;            // include/exclude mask ("*.jpg; *.png | *.tmp"), empty = everything
  bool newerOnly = false;          // transfer only files newer than the target
  bool excludeHidden = false;      // skip hidden (dot) files
};
void setTransferSettings(const TransferSettings & settings);
TransferSettings transferSettings();
// The engine's default text-file mask (shown when the user has not set one).
std::string defaultTextMask();
// Is `mask` a valid file mask? On failure `error` gets the reason.
bool validateFileMask(const std::string & mask, std::string * error = nullptr);
// Which of `entries` match the WinSCP file mask `mask` ("*.txt", "*.jpg; *.png | thumb*", "*>1M", a
// trailing "/" for directories): matches[i] for entries[i]. An empty mask matches everything.
// Returns false (and everything matching) for an invalid mask.
bool matchFileMask(const std::string & mask, const std::vector<DirEntry> & entries, std::vector<bool> & matches,
                   std::string * error = nullptr);

// Transfer a single file over the live SFTP session (TTerminal CopyToRemote/CopyToLocal).
// localPath/remotePath are full paths; the target is a directory. Returns true on success;
// on failure `error` (if non-null) gets a human-readable message.
// `plain` = an internal transfer of exactly this file (edit / open / compare / drag-out / new file):
// binary, no file mask, no renaming, no speed limit, no .filepart - the user's Transfer Settings
// are for the transfers they start, and e.g. a mask of "*.jpg" must not stop F4 from opening a .txt.
bool uploadToRemote(const std::string & localPathUtf8, const std::string & remoteDirUtf8,
                    std::string * error = nullptr, bool plain = false);
bool downloadFromRemote(const std::string & remotePathUtf8, const std::string & localDirUtf8,
                        std::string * error = nullptr, bool plain = false);

// Remote file operations on the live SFTP session (TTerminal). `name` is the entry name in the
// current remote directory (rename/delete look it up in the live listing). Return true on success.
bool remoteMakeDir(const std::string & nameUtf8, std::string * error = nullptr);
bool remoteDelete(const std::string & nameUtf8, std::string * error = nullptr);
bool remoteRename(const std::string & oldNameUtf8, const std::string & newNameUtf8,
                  std::string * error = nullptr);

// Current unix permissions of a remote entry as an octal string (e.g. "644"), empty if unknown.
std::string remoteFileOctal(const std::string & nameUtf8);
// Set remote permissions from an octal string. Returns true on success.
bool remoteChmod(const std::string & nameUtf8, const std::string & octalUtf8, bool recursive = false, std::string * error = nullptr);

//--- properties: permissions, owner, group ---
struct RemoteProps
{
  bool setRights = false;  std::string octal;      // e.g. "644" (or "0644")
  bool addXToDirs = false;                         // recursive: directories also get x where r is set
  bool setOwner = false;   std::string owner;      // a name, or a numeric id
  bool setGroup = false;   std::string group;
  bool recursive = false;                          // apply to a directory's contents too
};
// Change the properties of an entry of the CURRENT remote directory. Only the parts marked "set" are sent.
bool remoteSetProperties(const std::string & nameUtf8, const RemoteProps & props, std::string * error = nullptr);

// Create a link named `name` in the current remote directory pointing to `target`
// (symbolic = false: a hard link, where the server supports it).
bool remoteCreateLink(const std::string & nameUtf8, const std::string & targetUtf8, bool symbolic, std::string * error = nullptr);

// Server-side copy ("Duplicate") / move of an entry of the current remote directory into `targetDir`
// under `newName` (empty = same name). No data passes through this machine.
bool remoteDuplicate(const std::string & nameUtf8, const std::string & targetDirUtf8, const std::string & newNameUtf8, std::string * error = nullptr);
bool remoteMoveTo(const std::string & nameUtf8, const std::string & targetDirUtf8, const std::string & newNameUtf8, std::string * error = nullptr);

// What the active session's protocol/server can do (WinSCP disables what is not available).
enum class Capability { ChangeMode, ChangeOwner, ChangeGroup, SymbolicLink, HardLink, RemoteCopy, RemoteMove,
                        SpaceAvailable, Rename, AnyCommand };
bool remoteCapable(Capability capability);

// Disk space of the file system holding `path` (SFTP statvfs / space-available extensions, WebDAV quota).
struct SpaceInfo { std::int64_t total = 0, unused = 0, totalForUser = 0, unusedForUser = 0; };
bool remoteSpaceAvailable(const std::string & pathUtf8, SpaceInfo & info, std::string * error = nullptr);

// "Server and protocol information" of the active session, as label/value rows per section.
struct InfoRow { std::string label, value; };
struct SessionInformation
{
  std::string protocol;              // e.g. "SFTP-3", "FTP", "WebDAV", "S3"
  bool encrypted = false;            // SSH or TLS
  std::int64_t loginSecs = 0;        // when the session was opened (seconds, local time; see DirEntry::mtimeSecs)
  std::vector<InfoRow> session;      // protocol, encryption, compression, server software, fingerprints
  std::vector<InfoRow> capabilities; // "Can change permissions" -> "Yes"/"No", ...
  std::string additional;            // free text from the server (SFTP extensions, FTP FEAT, ...)
};
SessionInformation sessionInformation();

//--- find files / directory sizes ---
struct FoundFile
{
  std::string path;        // full path
  std::string dir;         // directory it is in
  std::string name;
  std::int64_t size = 0;
  bool isDir = false;
  std::string modified;    // formatted timestamp
};
// Recursive search below `dir` for entries matching `mask` (WinSCP file mask: "*.txt", "*.log; *.tmp",
// "*.jpg | thumb*", "*>1M", ...). `onFound` is called for every match, `onDirectory` when a directory
// is entered; either may return true to stop. Returns false with `error` set if the search failed
// (a stopped search is not a failure). Blocking; call from a worker thread.
bool remoteFind(const std::string & dir, const std::string & mask,
                const std::function<bool(const FoundFile &)> & onFound,
                const std::function<bool(const std::string &)> & onDirectory, std::string * error = nullptr);
bool localFind(const std::string & dir, const std::string & mask,
               const std::function<bool(const FoundFile &)> & onFound,
               const std::function<bool(const std::string &)> & onDirectory, std::string * error = nullptr);

struct SizeResult { std::int64_t bytes = 0; int files = 0; int dirs = 0; };
// Total size of one entry of the CURRENT remote directory (a directory is summed recursively).
// `cancelled` (optional) is polled; returning true stops the calculation (the result is then partial
// and the function returns false with an empty error).
bool remoteCalculateSize(const std::string & name, SizeResult & result,
                         const std::function<bool()> & cancelled = nullptr, std::string * error = nullptr);
bool localCalculateSize(const std::string & path, SizeResult & result,
                        const std::function<bool()> & cancelled = nullptr, std::string * error = nullptr);

void disconnectSftp();   // closes + removes the active session (tab)

//--- session tabs (multiple concurrent connections) ---
int sessionCount();                 // number of open sessions
int activeSession();                // index of the active session (-1 if none)
std::string sessionLabel(int i);    // "user@host" for tab i
void switchSession(int i);          // make session i active (subsequent remote calls target it)
void closeSession(int i);           // close + remove session i

//--- parallel-transfer connection pool (extra connections to the same server) ---
// Open an additional connection to the primary's server. Returns a handle (>=1) or 0 on failure.
bool parallelSupported();   // false for FTP (its backend can't run concurrent connections reliably)
int openParallelConnection(std::string * error = nullptr);
void setProgressSinkVia(int handle, const std::function<bool(const TransferProgress &)> & cb);
bool uploadVia(int handle, const std::string & localPath, const std::string & remoteDir, std::string * error = nullptr);
bool downloadVia(int handle, const std::string & remotePath, const std::string & localDir, std::string * error = nullptr);
void closeParallelConnections();

// Orderly teardown at application exit: closes the parallel connections and every session while
// the engine is still fully alive. Without it they were destroyed by static destructors after
// exit(), in an order where the engine's own statics (mutexes) were already gone ->
// "mutex lock failed" abort = a crash report on every quit with a session open.
// Returns false if the engine stayed busy (a transfer still running) for timeoutMs; the caller
// should then leave with _exit() so no destructor touches the engine underneath the worker.
bool shutdown(int timeoutMs);

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
// `subdirs` = false compares the two directories' own files only.
std::vector<SyncItem> synchronizeCollect(const std::string & localDir, const std::string & remoteDir,
                                         int mode, bool del, std::string * error = nullptr, bool subdirs = true);
bool synchronizeApply(std::string * error = nullptr);    // execute the collected checklist
void synchronizeRelease();                                // free the checklist

} // namespace engine

#endif
