//---------------------------------------------------------------------------
// enginebridge.cpp — the ONLY UI TU compiled with engine flags (-fshort-wchar -fms-extensions).
// Calls the ported engine/rtlcompat and converts UnicodeString <-> UTF-8 std::string.
//---------------------------------------------------------------------------
#include "enginebridge.h"

#include <vcl.h>              // ported RTL + platform layer (rtlcompat): UnicodeString,
                              // FindFirst/TSearchRec, path helpers, GetEnvironmentVariable.
#include "CoreMain.h"         // engine globals (Configuration, ApplicationLog, ...)
#include "PuttyTools.h"       // KeyType/LoadKey/SaveKey (OpenSSH -> .ppk conversion)
#include "Configuration.h"
#include "SessionData.h"
#include "Terminal.h"
#include "CopyParam.h"
#include "Interface.h"
#include "RemoteFiles.h"
#include "Exceptions.h"
#include "TextsCore.h"        // TUNNEL_INSTRUCTION2 (recognizing prompts asked by the tunnel)
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <csignal>
#include <execinfo.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <chrono>
#include <cmath>
#include <thread>
#include <unistd.h>
#include <sys/stat.h>

void __fastcall PuttyInitialize();   // one-time PuTTY init (sk_init + appname)
void __fastcall NeonInitialize();    // ne_sock_init (WebDAV/HTTP)

namespace {

std::string ToU8(const UnicodeString & s) { return std::string(UTF8String(s).c_str()); }
UnicodeString FromU8(const std::string & s) { return UTF8ToString(RawByteString(s.c_str())); }

//--- diagnostics log (flushes every line so the last entry survives a crash) -----------------
std::mutex g_logMtx;
FILE * g_logFp = nullptr;
const char * kLogPath = "/tmp/winscp-native.log";

void LogLine(const std::string & s)
{
  std::lock_guard<std::mutex> lk(g_logMtx);
  if (g_logFp == nullptr) g_logFp = fopen(kLogPath, "a");
  if (g_logFp == nullptr) return;
  char ts[32]; time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm);
  strftime(ts, sizeof(ts), "%H:%M:%S", &tm);
  fprintf(g_logFp, "%s %s\n", ts, s.c_str());
  fflush(g_logFp);
}

// Crash handler: dump signal + backtrace to the log (and stderr) so a hard crash leaves a trace.
void CrashHandler(int sig)
{
  const char * name = (sig == SIGSEGV) ? "SIGSEGV" : (sig == SIGABRT) ? "SIGABRT"
                    : (sig == SIGBUS) ? "SIGBUS" : (sig == SIGILL) ? "SIGILL" : "signal";
  void * bt[64]; int n = backtrace(bt, 64);
  if (g_logFp) { fprintf(g_logFp, "\n*** CRASH: %s ***\n", name); fflush(g_logFp);
                 backtrace_symbols_fd(bt, n, fileno(g_logFp)); fflush(g_logFp); }
  backtrace_symbols_fd(bt, n, 2);
  signal(sig, SIG_DFL); raise(sig);   // re-raise for the default handler / core
}

//--- remote SFTP session state (single session) ---
class BridgeConfiguration : public TConfiguration
{
public:
  __fastcall BridgeConfiguration() : TConfiguration() {}
  virtual UnicodeString TemporaryDir(bool = false) { return UnicodeString(L"/tmp/"); }
};

// Last successful connect options, so we can open additional (parallel) connections to the same server.
// `interactive`: the login needed answers only the user can give (2FA / keyboard-interactive codes) -
// such a session is not silently reconnected for parallel transfers.
struct ConnParams { engine::ConnectOptions o; bool valid = false; bool interactive = false; };

// One open session = one tab. The active session's members are reached via the g_terminal/g_sessionData/
// g_password/g_lastParams macros below, so the ~70 existing call sites are unchanged. When no session
// is active, AS() returns a static empty Sess (null term) so remoteConnected() is simply false.
struct Sess {
  std::unique_ptr<TTerminal> term;
  std::unique_ptr<TSessionData> data;
  UnicodeString password;
  ConnParams params;
  std::string label;
  std::vector<std::string> tempKeys;   // converted private keys, removed once the session is open
  int autoAnswers = 0;                 // prompts answered without a user (headless), per connect
};
std::vector<std::unique_ptr<Sess>> g_sess;
int g_active = -1;
Sess & AS() { static Sess empty; return (g_active >= 0 && g_active < (int)g_sess.size()) ? *g_sess[g_active] : empty; }
void DropActiveSession() {
  if (g_active < 0 || g_active >= (int)g_sess.size()) return;
  Sess * s = g_sess[g_active].get();
  if (s->term) { try { if (s->term->Active) s->term->Close(); } catch (...) {} }
  g_sess.erase(g_sess.begin() + g_active);
  g_active = g_sess.empty() ? -1 : (g_active >= (int)g_sess.size() ? (int)g_sess.size() - 1 : g_active);
}
#define g_terminal    (AS().term)
#define g_sessionData (AS().data)
#define g_password    (AS().password)
#define g_lastParams  (AS().params)

bool g_engineInited = false;
std::function<bool(const engine::TransferProgress &)> g_progressCb;
std::function<bool()> g_opCancel;   // polled from the progress event of a running size calculation
// Set by OnQueryUser / OnShowExtendedException so an operation can report the engine's message.
// Per thread: the engine calls back on the thread running the operation, and parallel transfers
// run several at once (one shared string was both a data race and a way to blame the wrong file).
thread_local std::string g_lastTransferError;
// Confirmation callback (host-key verification). Returns true to accept. If unset, auto-accept.
// Connect runs on the caller (UI) thread, so the GUI can show a modal dialog from here.
std::function<bool(const std::string &)> g_confirmCb;
// True only while connectSftp runs (the UI thread). g_confirmCb shows a modal dialog, so it must
// NEVER be called from a transfer worker thread (Qt GUI off the main thread = crash/deadlock).
bool g_connecting = false;
// Authentication prompts (password, passphrase, keyboard-interactive / 2FA). Same threading rule
// as g_confirmCb: only ever called while g_connecting.
std::function<bool(const engine::PromptRequest &, std::vector<std::string> &)> g_promptCb;

// Serializes ALL engine access. The engine + single TTerminal are not thread-safe; the GUI runs
// transfers on a worker thread, so every public entry point takes this (recursive: some call each
// other, e.g. upload -> remoteConnected). Defensive — the GUI also gates remote UI actions while a
// transfer batch runs, but this guarantees no concurrent engine use even if a gate is missed.
std::recursive_mutex g_engineMutex;
#define ENGINE_LOCK std::lock_guard<std::recursive_mutex> _elk(g_engineMutex)

void EnsureEngineInited()
{
  if (g_engineInited) return;
  ApplicationLog = new TApplicationLog();          // AppLog macro derefs this unguarded
  Configuration = new BridgeConfiguration();
  // Keep the engine's INI (cached host keys, ...) in the per-user config directory. Left to itself
  // the engine prefers "<executable>.ini" whenever the program directory is writable - i.e. inside
  // the .app bundle (breaking its code-signature seal) or next to the AppImage.
  {
    UnicodeString IniName = L"winscp-qt.ini";
    UnicodeString IniPath = IncludeTrailingBackslash(GetShellFolderPath(CSIDL_APPDATA)) + IniName;
    UnicodeString LegacyPath = ChangeFileExt(ParamStr(0), L".ini");   // where earlier builds may have put it
    std::error_code ec;
    // WINSCP_INI=<path>: a private engine INI (test runs: keeps the host keys / certificates of
    // throw-away test servers out of the user's real cache, and makes "first connection" repeatable).
    const char * iniOverride = getenv("WINSCP_INI");
    if (iniOverride != nullptr && *iniOverride != 0) IniPath = FromU8(iniOverride);
    else if (!FileExists(IniPath) && FileExists(LegacyPath))
      std::filesystem::copy_file(ToU8(LegacyPath), ToU8(IniPath), ec);
    Configuration->SetExplicitIniFileStorageName(IniPath);
  }
  Configuration->Default();
  // CoreInitialize normally creates this; TSessionData::CreateTunnelData reads
  // StoredSessions->DefaultSettings, so an SSH tunnel dereferenced NULL without it.
  if (StoredSessions == nullptr) StoredSessions = new TStoredSessionList();
  PuttyInitialize();
  NeonInitialize();    // ne_sock_init; needed for WebDAV (no-op cost otherwise)
  g_engineInited = true;
}

// Flatten an engine exception (incl. ExtException MoreMessages chain) to a UTF-8 string.
std::string ExceptionToU8(Exception & E)
{
  UnicodeString msg = E.Message;
  ExtException * Ext = dynamic_cast<ExtException *>(&E);
  if ((Ext != nullptr) && (Ext->MoreMessages != nullptr))
    for (int i = 0; i < Ext->MoreMessages->Count; i++)
      msg += UnicodeString(L"\n") + Ext->MoreMessages->Strings[i];
  return std::string(UTF8String(msg).c_str());
}

// TDateTime (days, local time) -> whole seconds; both panels' timestamps go through this, so they
// compare directly.
std::int64_t SecondsOf(const TDateTime & dt)
{
  return (std::int64_t)std::llround(dt.Val() * 86400.0);
}

void RemoveTempKeys(std::vector<std::string> & temps)
{
  for (const std::string & t : temps) ::remove(t.c_str());   // never leave a converted key on disk
  temps.clear();
}

// PuTTY's authentication only reads .ppk private keys; OpenSSH / ssh.com keys are converted to a
// temporary .ppk (private to the user, removed after the connect). Returns the file to hand to the
// engine; `passphrase` is cleared when the converted copy needs none. Throws on a bad passphrase.
UnicodeString PrepareKey(const std::string & keyFile, UnicodeString & passphrase, std::vector<std::string> & temps)
{
  UnicodeString kf = FromU8(keyFile);
  TKeyType kt = KeyType(kf);
  if (kt == ktOpenSSHPEM || kt == ktOpenSSHNew || kt == ktSSHCom)
  {
    TPrivateKey * pk = LoadKey(kt, kf, passphrase);
    char tmpl[] = "/tmp/.freescp-key-XXXXXX";
    int fd = mkstemp(tmpl);                    // created 0600
    if (fd >= 0) close(fd);
    temps.push_back(tmpl);
    try { SaveKey(ktSSH2, FromU8(tmpl), UnicodeString(), pk); }
    catch (...) { FreeKey(pk); throw; }
    FreeKey(pk);
    ::chmod(tmpl, 0600);
    passphrase = UnicodeString();
    return FromU8(tmpl);
  }
  return kf;
}

// Session settings shared by the primary connection and the parallel-transfer connections.
void ConfigureSessionData(TSessionData * d, const engine::ConnectOptions & o, std::vector<std::string> & temps)
{
  using engine::Protocol;
  d->Default();
  d->HostName = FromU8(o.host);
  d->PortNumber = o.port;
  d->UserName = FromU8(o.user);
  switch (o.protocol)
  {
    case Protocol::Scp:    d->FSProtocol = fsSCPonly; break;
    case Protocol::WebDav: d->FSProtocol = fsWebDAV;
                           d->Ftps = o.tls ? ftpsImplicit : ftpsNone; break;
    case Protocol::S3:     d->FSProtocol = fsS3;
                           d->Ftps = o.tls ? ftpsImplicit : ftpsNone;
                           d->S3UrlStyle = s3usPath; d->S3DefaultRegion = L"us-east-1"; break;
    case Protocol::Ftp:    d->FSProtocol = fsFTP;
                           d->Ftps = !o.tls ? ftpsNone : (o.ftpExplicitTls ? ftpsExplicitTls : ftpsImplicit);
                           d->FtpPasvMode = o.ftpPassive; break;
    default:               d->FSProtocol = fsSFTPonly; break;
  }
  const bool ssh = (o.protocol == Protocol::Sftp || o.protocol == Protocol::Scp);
  if (ssh && !o.keyFile.empty())   // public-key auth: the password field is the key passphrase
  {
    UnicodeString passphrase = FromU8(o.password);
    d->PublicKeyFile = PrepareKey(o.keyFile, passphrase, temps);
    d->Passphrase = passphrase;
  }
  else
  {
    d->Password = FromU8(o.password);
  }
  if (o.tunnel && !o.tunnelHost.empty())
  {
    d->Tunnel = true;
    d->TunnelHostName = FromU8(o.tunnelHost);
    d->TunnelPortNumber = o.tunnelPort;
    d->TunnelUserName = FromU8(o.tunnelUser);
    if (!o.tunnelKeyFile.empty())
    {
      UnicodeString passphrase = FromU8(o.tunnelPassword);
      d->TunnelPublicKeyFile = PrepareKey(o.tunnelKeyFile, passphrase, temps);
      d->TunnelPassphrase = passphrase;
    }
    else
    {
      d->TunnelPassword = FromU8(o.tunnelPassword);
    }
  }
  if (o.proxyMethod != 0 && !o.proxyHost.empty())
  {
    d->ProxyMethod = (o.proxyMethod == 1) ? pmSocks4 : (o.proxyMethod == 2) ? pmSocks5 : pmHTTP;
    d->ProxyHost = FromU8(o.proxyHost);
    d->ProxyPort = o.proxyPort;
    d->ProxyUsername = FromU8(o.proxyUser);
    d->ProxyPassword = FromU8(o.proxyPassword);
  }
  if (o.serverCrLf) d->EOLType = eolCRLF;
  if (o.utf8Names == 1) d->NotUtf = asOff;        // "UTF-8 encoding for filenames: On"
  else if (o.utf8Names == 2) d->NotUtf = asOn;    // Off
  d->FingerprintScan = false;
  // Always read fresh listings: with the cache on, a panel refresh after an upload/op could show a
  // stale directory (the uploaded file appeared missing). The GUI re-lists on every navigate anyway.
  d->CacheDirectories = false;
  d->CacheDirectoryChanges = false;
}

//--- transfer settings -> TCopyParamType ---
std::mutex g_xferMtx;
engine::TransferSettings g_xfer;

// File names are transferred as they are. The engine's default replaces the characters Windows
// forbids in local names (? * : " < > |) by %XX on download and decodes %XX on upload; none of
// that applies to a Unix file system ("what?.txt" arrived as "what%3F.txt", and a local "a%20b"
// would have been uploaded as "a b").
void UnixFileNames(TCopyParamType & cp)
{
  cp.InvalidCharsReplacement = NoReplacement;
}

void FillCopyParam(TCopyParamType & cp)
{
  engine::TransferSettings x;
  { std::lock_guard<std::mutex> lk(g_xferMtx); x = g_xfer; }
  cp.Default();
  UnixFileNames(cp);
  cp.TransferMode = (x.mode == 1) ? tmAscii : (x.mode == 2) ? tmAutomatic : tmBinary;
  if (!x.textMask.empty()) cp.AsciiFileMask.Masks = FromU8(x.textMask);
  cp.PreserveTime = x.preserveTime;
  cp.PreserveRights = x.setRights;
  if (x.setRights)
  {
    try { cp.Rights.Octal = FromU8(x.rightsOctal); } catch (...) { cp.PreserveRights = false; }
  }
  cp.FileNameCase = (x.fileNameCase == 1) ? ncUpperCase : (x.fileNameCase == 2) ? ncLowerCase : ncNoChange;
  cp.ResumeSupport = (x.resume == 0) ? rsOn : (x.resume == 2) ? rsOff : rsSmart;
  cp.ResumeThreshold = (x.resumeThresholdKB > 0 ? x.resumeThresholdKB : 0) * 1024;
  cp.CPSLimit = (x.speedLimitKB > 0) ? static_cast<unsigned long>(x.speedLimitKB) * 1024UL : 0;
  if (!x.fileMask.empty())
  {
    try { cp.IncludeFileMask.Masks = FromU8(x.fileMask); } catch (...) { cp.IncludeFileMask.Masks = UnicodeString(); }
  }
  cp.NewerOnly = x.newerOnly;
  cp.ExcludeHiddenFiles = x.excludeHidden;
}

// Is this prompt asked by the session's SSH tunnel (TTunnelUI prefixes its instructions)?
bool IsTunnelPrompt(TSessionData * d, const UnicodeString & Instructions)
{
  return d->Tunnel && (Instructions.Pos(FMTLOAD(TUNNEL_INSTRUCTION2, (d->TunnelHostName))) == 1);
}

} // namespace

namespace engine {

std::string banner()
{
  return "WinSCP — native port (engine RTL + platform layer; local FS via ported FindFirst)";
}

void logLine(const std::string & msg) { LogLine(msg); }
std::string logPath() { return kLogPath; }
void installCrashHandler()
{
  for (int sig : { SIGSEGV, SIGABRT, SIGBUS, SIGILL }) signal(sig, CrashHandler);
  LogLine("=== session start (crash handler installed) ===");
}

std::string homeDir()
{
  UnicodeString h = GetEnvironmentVariable(UnicodeString(L"HOME"));
  return ToU8(h.IsEmpty() ? UnicodeString(L"/") : h);
}

std::string parentDir(const std::string & utf8Path)
{
  UnicodeString p = ExcludeTrailingBackslash(FromU8(utf8Path));
  int slash = p.LastDelimiter(UnicodeString(L"/"));
  if (slash <= 1) return "/";
  return ToU8(p.SubString(1, slash - 1));
}

std::string joinPath(const std::string & dir, const std::string & name)
{
  return ToU8(IncludeTrailingBackslash(FromU8(dir)) + FromU8(name));
}

std::string formatSize(std::int64_t bytes)
{
  const char * unit[] = { "B", "KiB", "MiB", "GiB", "TiB" };
  double v = static_cast<double>(bytes); int u = 0;
  while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
  char buf[48];
  std::snprintf(buf, sizeof(buf), (u == 0 ? "%.0f %s" : "%.1f %s"), v, unit[u]);
  return buf;
}

bool copyFile(const std::string & srcUtf8, const std::string & dstUtf8)
{
  std::error_code ec;
  if (std::filesystem::is_directory(srcUtf8, ec))
  {
    // Recursive directory copy (UI selection can include folders).
    std::filesystem::copy(srcUtf8, dstUtf8,
      std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
  }
  std::filesystem::copy_file(srcUtf8, dstUtf8,
    std::filesystem::copy_options::overwrite_existing, ec);
  return !ec;
}

bool localMakeDir(const std::string & dirUtf8, const std::string & nameUtf8, std::string * error)
{
  std::error_code ec;
  bool ok = std::filesystem::create_directory(std::filesystem::path(dirUtf8) / nameUtf8, ec);
  if (!ok && error) *error = ec ? ec.message() : "already exists";
  return ok;
}

bool localDelete(const std::string & dirUtf8, const std::string & nameUtf8, std::string * error)
{
  std::error_code ec;
  std::filesystem::remove_all(std::filesystem::path(dirUtf8) / nameUtf8, ec);
  if (ec && error) *error = ec.message();
  return !ec;
}

bool localRename(const std::string & dirUtf8, const std::string & oldUtf8, const std::string & newUtf8,
                 std::string * error)
{
  std::error_code ec;
  std::filesystem::rename(std::filesystem::path(dirUtf8) / oldUtf8,
                          std::filesystem::path(dirUtf8) / newUtf8, ec);
  if (ec && error) *error = ec.message();
  return !ec;
}

std::vector<DirEntry> listLocalDir(const std::string & utf8Path)
{
  std::vector<DirEntry> result;
  UnicodeString path = IncludeTrailingBackslash(FromU8(utf8Path));

  if (FromU8(utf8Path) != UnicodeString(L"/"))
  {
    DirEntry up; up.name = ".."; up.isDir = true; up.isParent = true;
    result.push_back(up);
  }

  TSearchRec rec;
  if (FindFirst(path + UnicodeString(L"*"), faAnyFile, rec) == 0)
  {
    do
    {
      UnicodeString n = rec.Name;
      if (n == UnicodeString(L".") || n == UnicodeString(L"..")) continue;
      DirEntry e;
      e.name = ToU8(n);
      e.isDir = (rec.Attr & faDirectory) != 0;
      e.size = rec.Size;
      e.modified = ToU8(rec.TimeStamp.DateTimeString());
      e.mtimeSecs = SecondsOf(rec.TimeStamp);
      {
        std::error_code ec;
        const std::filesystem::path full = std::filesystem::path(utf8Path) / e.name;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(full, ec)))
        {
          e.isLink = true;
          const std::filesystem::path target = std::filesystem::read_symlink(full, ec);
          if (!ec) e.linkTarget = target.string();
        }
      }
      result.push_back(e);
    }
    while (FindNext(rec) == 0);
    FindClose(rec);
  }

  std::sort(result.begin() + (result.empty() ? 0 : (result[0].isParent ? 1 : 0)), result.end(),
            [](const DirEntry & a, const DirEntry & b) {
              if (a.isDir != b.isDir) return a.isDir > b.isDir;     // dirs first
              return a.name < b.name;
            });
  return result;
}

//--- remote session -----------------------------------------------------------
ConnectResult connect(const ConnectOptions & o)
{
  LogLine("connect(" + o.user + "@" + o.host + ":" + std::to_string(o.port) + " proto=" + std::to_string((int)o.protocol) +
          " key=" + (o.keyFile.empty() ? "no" : "yes") + (o.tunnel ? " tunnel=" + o.tunnelUser + "@" + o.tunnelHost + ":" + std::to_string(o.tunnelPort) : std::string()) +
          (o.proxyMethod ? " proxy=" + std::to_string(o.proxyMethod) + ":" + o.proxyHost + ":" + std::to_string(o.proxyPort) : std::string()) + ") ...");
  ENGINE_LOCK;
  ConnectResult r;
  if (o.host.empty()) { r.error = "Host name is empty."; LogLine("  connect: empty host"); return r; }   // engine produces a garbage message for an empty host
  if (o.tunnel && o.tunnelHost.empty()) { r.error = "SSH tunnel host name is empty."; return r; }
  if (o.proxyMethod != 0 && o.proxyHost.empty()) { r.error = "Proxy host name is empty."; return r; }
  try
  {
    EnsureEngineInited();
    // Open a NEW session (tab). Each connect adds a tab; closing is via disconnectSftp/closeSession.
    g_sess.push_back(std::unique_ptr<Sess>(new Sess()));
    g_active = (int)g_sess.size() - 1;
    Sess * self = g_sess[g_active].get();   // stable: sessions are held by unique_ptr
    self->label = o.user + "@" + o.host;
    self->params.o = o;
    // Optional full SSH/SFTP protocol log (heavy — every packet). Off by default; set
    // WINSCP_SESSIONLOG=1 to capture /tmp/winscp-session.log for diagnosing protocol issues.
    if (getenv("WINSCP_SESSIONLOG"))
    {
      Configuration->LogProtocol = 2;
      Configuration->LogFileName = FromU8("/tmp/winscp-session.log");
      Configuration->Logging = true;
      LogLine("protocol log -> /tmp/winscp-session.log");
    }
    g_password = FromU8(o.password);

    g_sessionData.reset(new TSessionData(L""));
    ConfigureSessionData(g_sessionData.get(), o, self->tempKeys);

    g_terminal.reset(new TTerminal(g_sessionData.get(), Configuration));
    g_terminal->OnPromptUser =
      [self](TTerminal *, TPromptKind Kind, UnicodeString Name, UnicodeString Instructions, TStrings * Prompts, TStrings * Results, bool & Result, void *)
      {
        if (Results == nullptr) { Result = true; return; }
        const bool forTunnel = IsTunnelPrompt(self->data.get(), Instructions);
        LogLine("  OnPromptUser kind=" + std::to_string((int)Kind) + " prompts=" + std::to_string(Results->Count) + (forTunnel ? " (tunnel)" : ""));
        if (g_promptCb && g_connecting)   // ask the user (connect runs on the UI thread)
        {
          PromptRequest rq;
          rq.kind = (int)Kind; rq.forTunnel = forTunnel;
          rq.name = ToU8(Name); rq.instructions = ToU8(Instructions);
          for (int i = 0; i < Results->Count; i++)
          {
            const bool has = (Prompts != nullptr) && (i < Prompts->Count);
            rq.prompts.push_back(has ? ToU8(Prompts->Strings[i]) : std::string());
            rq.echo.push_back(has && ((reinterpret_cast<intptr_t>(Prompts->Objects[i]) & pupEcho) != 0));
          }
          std::vector<std::string> answers(rq.prompts.size());
          Result = g_promptCb(rq, answers);
          if (!Result) return;
          answers.resize(rq.prompts.size());
          for (int i = 0; i < Results->Count; i++) Results->Strings[i] = FromU8(answers[i]);
          // Remember a typed password / passphrase for the parallel-transfer connections. Anything
          // else (one-time codes, challenge answers) cannot be replayed.
          ConnectOptions & po = self->params.o;
          if ((Kind == pkPassword || Kind == pkPassphrase) && Results->Count == 1)
          {
            if (forTunnel) po.tunnelPassword = answers[0];
            else { po.password = answers[0]; self->password = FromU8(answers[0]); }
          }
          else self->params.interactive = true;
        }
        else   // headless / off the UI thread: the stored password is the only thing we can offer
        {
          // ...and only a few times, and never to a proxy: a server that keeps rejecting it would
          // otherwise be re-answered forever (an HTTP proxy with wrong credentials looped at full speed).
          if (Kind == pkProxyAuth || ++self->autoAnswers > 3) { Result = false; return; }
          UnicodeString pw = forTunnel ? FromU8(self->params.o.tunnelPassword) : self->password;
          for (int i = 0; i < Results->Count; i++) Results->Strings[i] = pw;
          Result = true;
        }
      };
    g_terminal->OnQueryUser =
      [](TObject *, const UnicodeString & Query, TStrings *, unsigned int Answers, const TQueryParams *, unsigned int & Answer, TQueryType Type, void *)
      {
        LogLine("  OnQueryUser type=" + std::to_string((int)Type) + " answers=" + std::to_string(Answers) + " msg='" + ToU8(Query) + "'");
        if (Type == qtError)
        {
          // qaYes here is a recoverable action (e.g. "Delete & recreate" to overwrite a file the
          // server won't truncate) -> take it. Otherwise it's a real failure (permission denied,
          // disk full): record the message and skip so the transfer reports "failed" not "done".
          if (Answers & qaYes) Answer = qaYes;
          else { g_lastTransferError = ToU8(Query); Answer = (Answers & qaSkip) ? qaSkip : ((Answers & qaAbort) ? qaAbort : Answers); }
        }
        else if ((Answers & qaYes) && (Answers & qaNo) && g_confirmCb && g_connecting)   // host key (connect, UI thread only)
          Answer = g_confirmCb(ToU8(Query)) ? qaYes : qaNo;
        else
          Answer = (Answers & qaYes) ? qaYes : ((Answers & qaOK) ? qaOK : Answers);   // transfer-time confirmations: proceed
      };
    g_terminal->OnProgress =
      [](TFileOperationProgressType & P)
      {
        if (g_opCancel && g_opCancel()) P.SetCancel(csCancel);
        if (g_progressCb)
        {
          TransferProgress tp;
          tp.file = ToU8(P.FileName);
          tp.transferred = P.TransferredSize;
          tp.total = P.TransferSize;
          tp.cps = (std::int64_t)P.CPS();
          if (g_progressCb(tp)) P.SetCancel(csCancel);   // sink requested cancel
        }
      };

    // Command failures that are not fatal to the session (cannot change to / read a directory,
    // rename refused, ...) are REPORTED here by the engine, not thrown. With no handler they were
    // lost: the operation "succeeded" and e.g. an unreadable directory showed up as an empty one.
    g_terminal->OnShowExtendedException =
      [](TTerminal *, Exception * E, void *)
      {
        if (E == nullptr) return;
        g_lastTransferError = ExceptionToU8(*E);
        LogLine("  engine error: " + g_lastTransferError);
      };

    g_connecting = true;            // host-key / authentication prompts may fire inside Open() (UI thread)
    try { g_terminal->Open(); } catch (...) { g_connecting = false; throw; }
    g_connecting = false;
    RemoveTempKeys(self->tempKeys);
    r.ok = true;
    r.currentDir = ToU8(g_terminal->CurrentDirectory);
    self->params.valid = true;      // for parallel connections
    LogLine("  connect: OK, cwd='" + r.currentDir + "'");
  }
  catch (Exception & E)
  {
    g_connecting = false;
    r.error = ExceptionToU8(E);
    if (g_active >= 0 && g_active < (int)g_sess.size()) RemoveTempKeys(g_sess[g_active]->tempKeys);
    DropActiveSession();   // remove the failed tab
    LogLine("  connect: FAILED: " + r.error);
  }
  catch (...)
  {
    g_connecting = false; r.error = "unknown error";
    if (g_active >= 0 && g_active < (int)g_sess.size()) RemoveTempKeys(g_sess[g_active]->tempKeys);
    DropActiveSession(); LogLine("  connect: unknown exception");
  }
  return r;
}

ConnectResult connectSftp(const std::string & host, int port,
                          const std::string & user, const std::string & password,
                          Protocol protocol, bool tls, const std::string & keyFile)
{
  ConnectOptions o;
  o.host = host; o.port = port; o.user = user; o.password = password;
  o.protocol = protocol; o.tls = tls; o.ftpExplicitTls = false; o.keyFile = keyFile;
  return connect(o);
}

void setPromptCallback(const std::function<bool(const PromptRequest &, std::vector<std::string> &)> & cb) { g_promptCb = cb; }
void setTransferSettings(const TransferSettings & settings) { std::lock_guard<std::mutex> lk(g_xferMtx); g_xfer = settings; }
TransferSettings transferSettings() { std::lock_guard<std::mutex> lk(g_xferMtx); return g_xfer; }
std::string defaultTextMask()
{
  TCopyParamType cp; cp.Default();
  return ToU8(cp.AsciiFileMask.Masks);
}
bool validateFileMask(const std::string & mask, std::string * error)
{
  try { TFileMasks m; m.Masks = FromU8(mask); return true; }
  catch (Exception & E) { if (error) *error = ExceptionToU8(E); return false; }
  catch (...) { if (error) *error = "invalid mask"; return false; }
}

bool matchFileMask(const std::string & mask, const std::vector<DirEntry> & entries, std::vector<bool> & matches, std::string * error)
{
  matches.assign(entries.size(), true);
  if (mask.empty()) return true;
  try
  {
    TFileMasks m; m.Masks = FromU8(mask);
    for (size_t i = 0; i < entries.size(); ++i)
    {
      const DirEntry & e = entries[i];
      if (e.isParent) continue;
      TFileMasks::TParams params;
      params.Size = e.size;
      params.Modification = TDateTime(static_cast<double>(e.mtimeSecs) / 86400.0);
      matches[i] = m.MatchesFileName(FromU8(e.name), e.isDir, &params);
    }
    return true;
  }
  catch (Exception & E) { if (error) *error = ExceptionToU8(E); matches.assign(entries.size(), true); return false; }
  catch (...) { if (error) *error = "invalid mask"; matches.assign(entries.size(), true); return false; }
}

void setProgressSink(const std::function<bool(const TransferProgress &)> & cb) { g_progressCb = cb; }
void setConfirmCallback(const std::function<bool(const std::string &)> & cb) { g_confirmCb = cb; }

bool remoteConnected() { ENGINE_LOCK; return g_terminal && g_terminal->Active; }

bool sessionLost()
{
  ENGINE_LOCK;
  return g_active >= 0 && g_active < (int)g_sess.size() && AS().params.valid && !(g_terminal && g_terminal->Active);
}

ConnectResult reconnect()
{
  ENGINE_LOCK;
  ConnectResult r;
  const int slot = g_active;
  if (slot < 0 || slot >= (int)g_sess.size()) { r.error = "There is no session to reconnect."; return r; }
  const ConnectOptions o = g_sess[slot]->params.o;   // includes a password typed at the prompt
  const size_t before = g_sess.size();
  LogLine("reconnect: session " + std::to_string(slot));
  r = connect(o);     // adds a session at the end and makes it active (and removes it again on failure)
  if (r.ok && g_sess.size() == before + 1)
  {
    std::unique_ptr<Sess> fresh = std::move(g_sess.back());
    g_sess.pop_back();
    try { g_sess[slot] = std::move(fresh); }     // the lost session's terminal is destroyed here
    catch (...) { LogLine("reconnect: the old session did not close cleanly"); }
  }
  g_active = slot;
  return r;
}

void debugDropConnection()
{
  ENGINE_LOCK;
  try { if (g_terminal && g_terminal->Active) g_terminal->Close(); } catch (...) {}
}
std::string remoteCurrentDir() { ENGINE_LOCK; return remoteConnected() ? ToU8(g_terminal->CurrentDirectory) : std::string(); }

std::vector<DirEntry> listRemoteDir(const std::string & utf8Path, std::string * error)
{
  ENGINE_LOCK;
  std::vector<DirEntry> result;
  if (error) error->clear();
  if (!remoteConnected()) { if (error) *error = "not connected"; return result; }
  g_lastTransferError.clear();
  try
  {
    if (!utf8Path.empty() && (FromU8(utf8Path) != g_terminal->CurrentDirectory))
    {
      g_terminal->ChangeDirectory(FromU8(utf8Path));
      g_terminal->ReadCurrentDirectory();
    }
    g_terminal->ReadDirectory(false);
    TRemoteFileList * Files = g_terminal->Files;
    if (Files != nullptr)
      for (int i = 0; i < Files->Count; i++)
      {
        TRemoteFile * F = Files->Files[i];
        UnicodeString n = F->FileName;
        if (n == UnicodeString(L".")) continue;
        DirEntry e;
        e.name = ToU8(n);
        e.isDir = F->IsDirectory;
        e.isParent = (n == UnicodeString(L".."));
        e.size = F->Size;
        e.modified = e.isParent ? std::string() : ToU8(F->ModificationStr);
        if (!e.isParent && F->ModificationFmt != mfNone)
        {
          e.mtimeSecs = SecondsOf(F->Modification);
          e.mtimeHasSeconds = (F->ModificationFmt == mfFull);
        }
        if (!e.isParent)
        {
          e.rights = ToU8(F->RightsStr);   // e.g. "rwxr-xr-x"
          UnicodeString owner = F->Owner.DisplayText;
          UnicodeString group = F->Group.DisplayText;
          e.owner = ToU8(group.IsEmpty() ? owner : (owner + UnicodeString(L"/") + group));
          e.ownerName = ToU8(owner); e.groupName = ToU8(group);
          e.isLink = F->IsSymLink;
          if (e.isLink) e.linkTarget = ToU8(F->LinkTo);
        }
        result.push_back(e);
      }
  }
  // An unreadable directory used to come back as a silently empty listing.
  catch (Exception & E) { if (error) *error = g_lastTransferError.empty() ? ExceptionToU8(E) : g_lastTransferError; }
  catch (...) { if (error) *error = g_lastTransferError.empty() ? "unknown error" : g_lastTransferError; }
  if (error && error->empty() && !g_lastTransferError.empty()) *error = g_lastTransferError;

  std::sort(result.begin(), result.end(),
            [](const DirEntry & a, const DirEntry & b) {
              if (a.isParent != b.isParent) return a.isParent > b.isParent;  // ".." first
              if (a.isDir != b.isDir) return a.isDir > b.isDir;              // dirs next
              return a.name < b.name;
            });
  return result;
}

namespace {
// Find an entry by name in the current remote listing (the panel the user is browsing).
TRemoteFile * FindInListing(const UnicodeString & name)
{
  TRemoteFileList * Files = g_terminal ? g_terminal->Files : nullptr;
  if (Files != nullptr)
    for (int i = 0; i < Files->Count; i++)
      if (Files->Files[i]->FileName == name) return Files->Files[i];
  return nullptr;
}
} // namespace

//=== parallel-connection pool (experimental; for queue parallelism) ========
namespace {
struct ParConn
{
  std::unique_ptr<TSessionData> data;
  std::unique_ptr<TTerminal> term;
  std::recursive_mutex mtx;            // serializes THIS connection only (parallelism across conns)
  std::function<bool(const engine::TransferProgress &)> sink;
  std::vector<std::string> tempKeys;
};
std::vector<std::unique_ptr<ParConn>> g_pool;
std::mutex g_poolMtx;
}

// FTP's single-connection state machine doesn't run reliably in parallel instances; gate it out.
// A login that needed one-time codes cannot be repeated without asking the user again; stay serial.
bool parallelSupported() { return g_lastParams.valid && g_lastParams.o.protocol != engine::Protocol::Ftp && !g_lastParams.interactive; }

int openParallelConnection(std::string * error)
{
  std::lock_guard<std::mutex> pk(g_poolMtx);
  if (!g_lastParams.valid) { if (error) *error = "no primary connection"; return 0; }
  try
  {
    EnsureEngineInited();
    auto c = std::make_unique<ParConn>();
    c->data.reset(new TSessionData(L""));
    const engine::ConnectOptions po = g_lastParams.o;   // includes a password typed at the login prompt
    ConfigureSessionData(c->data.get(), po, c->tempKeys);
    ParConn * cp = c.get();
    c->term.reset(new TTerminal(c->data.get(), Configuration));
    c->term->OnPromptUser = [po, cp](TTerminal *, TPromptKind, UnicodeString, UnicodeString Instructions, TStrings *, TStrings * Results, bool & Result, void *)
      { UnicodeString pw = FromU8(IsTunnelPrompt(cp->data.get(), Instructions) ? po.tunnelPassword : po.password);
        if (Results) for (int i = 0; i < Results->Count; i++) Results->Strings[i] = pw; Result = true; };
    c->term->OnQueryUser = [](TObject *, const UnicodeString & Query, TStrings *, unsigned int Answers, const TQueryParams *, unsigned int & Answer, TQueryType Type, void *)
      { if (Type == qtError) { if (Answers & qaYes) Answer = qaYes; else { g_lastTransferError = ToU8(Query); Answer = (Answers & qaSkip) ? qaSkip : ((Answers & qaAbort) ? qaAbort : Answers); } }
        else Answer = (Answers & qaYes) ? qaYes : ((Answers & qaOK) ? qaOK : Answers); };
    c->term->OnProgress = [cp](TFileOperationProgressType & P)
      { if (cp->sink) { engine::TransferProgress tp; tp.file = ToU8(P.FileName); tp.transferred = P.TransferredSize; tp.total = P.TransferSize; tp.cps = (std::int64_t)P.CPS(); if (cp->sink(tp)) P.SetCancel(csCancel); } };
    c->term->OnShowExtendedException = [](TTerminal *, Exception * E, void *)
      { if (E != nullptr) { g_lastTransferError = ExceptionToU8(*E); LogLine("  engine error (parallel): " + g_lastTransferError); } };
    try { c->term->Open(); } catch (...) { RemoveTempKeys(c->tempKeys); throw; }
    RemoveTempKeys(c->tempKeys);
    g_pool.push_back(std::move(c));
    return (int)g_pool.size();   // handle = 1-based index
  }
  catch (Exception & E) { if (error) *error = ExceptionToU8(E); return 0; }
  catch (...) { if (error) *error = "unknown error"; return 0; }
}

static ParConn * poolGet(int handle)
{ return (handle >= 1 && handle <= (int)g_pool.size()) ? g_pool[handle - 1].get() : nullptr; }

void setProgressSinkVia(int handle, const std::function<bool(const engine::TransferProgress &)> & cb)
{ ParConn * c = poolGet(handle); if (c) c->sink = cb; }

bool uploadVia(int handle, const std::string & localPathUtf8, const std::string & remoteDirUtf8, std::string * error)
{
  ParConn * c = poolGet(handle);
  if (!c || !c->term || !c->term->Active) { if (error) *error = "parallel connection invalid"; return false; }
  std::lock_guard<std::recursive_mutex> lk(c->mtx);
  try
  {
    std::unique_ptr<TStrings> files(new TStringList());
    files->Add(FromU8(localPathUtf8));
    TCopyParamType cp; FillCopyParam(cp);
    g_lastTransferError.clear();
    bool res = c->term->CopyToRemote(files.get(), UnixIncludeTrailingBackslash(FromU8(remoteDirUtf8)), &cp, cpNoConfirmation, nullptr);
    if (!g_lastTransferError.empty()) { if (error) *error = g_lastTransferError; return false; }
    return res;
  }
  catch (Exception & E) { if (error) *error = g_lastTransferError.empty() ? ExceptionToU8(E) : g_lastTransferError; return false; }
  catch (...) { if (error) *error = "unknown error"; return false; }
}

bool downloadVia(int handle, const std::string & remotePathUtf8, const std::string & localDirUtf8, std::string * error)
{
  ParConn * c = poolGet(handle);
  if (!c || !c->term || !c->term->Active) { if (error) *error = "parallel connection invalid"; return false; }
  std::lock_guard<std::recursive_mutex> lk(c->mtx);
  try
  {
    UnicodeString remote = FromU8(remotePathUtf8);
    UnicodeString dir = UnixExtractFilePath(remote);
    c->term->ChangeDirectory(dir);
    c->term->ReadCurrentDirectory();
    c->term->ReadDirectory(false);
    TRemoteFile * rf = nullptr;
    UnicodeString fn = UnixExtractFileName(remote);
    for (int i = 0; i < c->term->Files->Count; i++)
      if (c->term->Files->Files[i]->FileName == fn) rf = c->term->Files->Files[i];
    if (!rf) { if (error) *error = "file not found on parallel connection"; return false; }
    std::unique_ptr<TStrings> files(new TStringList());
    // A directory's FullFileName ends in '/'; the transfer code derives the local name with
    // UnixExtractFileName(), which is then empty - the folder's contents landed directly in the target.
    files->AddObject(UnixExcludeTrailingBackslash(rf->FullFileName), rf);
    TCopyParamType cp; FillCopyParam(cp);
    g_lastTransferError.clear();
    bool res = c->term->CopyToLocal(files.get(), IncludeTrailingBackslash(FromU8(localDirUtf8)), &cp, cpNoConfirmation, nullptr);
    if (!g_lastTransferError.empty()) { if (error) *error = g_lastTransferError; return false; }
    return res;
  }
  catch (Exception & E) { if (error) *error = g_lastTransferError.empty() ? ExceptionToU8(E) : g_lastTransferError; return false; }
  catch (...) { if (error) *error = "unknown error"; return false; }
}

void closeParallelConnections()
{
  std::lock_guard<std::mutex> pk(g_poolMtx);
  for (auto & c : g_pool) { try { if (c->term && c->term->Active) c->term->Close(); } catch (...) {} }
  g_pool.clear();
}

bool shutdown(int timeoutMs)
{
  // The transfer worker holds g_engineMutex while a file is in flight: wait a bounded time.
  bool locked = false;
  for (int waited = 0; !(locked = g_engineMutex.try_lock()) && waited < timeoutMs; waited += 50)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  if (!locked) { LogLine("shutdown: engine busy, leaving without teardown"); return false; }
  closeParallelConnections();
  while (!g_sess.empty())
  {
    Sess * s = g_sess.back().get();
    if (s->term) { try { if (s->term->Active) s->term->Close(); } catch (...) {} }
    try { g_sess.pop_back(); } catch (...) {}
  }
  g_active = -1;
  g_engineMutex.unlock();
  LogLine("shutdown: sessions closed");
  return true;
}

namespace {
void FillPlainCopyParam(TCopyParamType & cp)
{
  cp.Default();
  UnixFileNames(cp);
  cp.ResumeSupport = rsOff;      // a temporary copy needs no .filepart
}
}

bool uploadToRemote(const std::string & localPathUtf8, const std::string & remoteDirUtf8,
                    std::string * error, bool plain)
{
  LogLine("uploadToRemote('" + localPathUtf8 + "' -> '" + remoteDirUtf8 + "') ...");
  ENGINE_LOCK;
  if (!remoteConnected()) { if (error) *error = "not connected"; LogLine("  upload: not connected"); return false; }
  g_lastTransferError.clear();
  try
  {
    std::unique_ptr<TStrings> files(new TStringList());
    files->Add(FromU8(localPathUtf8));
    TCopyParamType cp; if (plain) FillPlainCopyParam(cp); else FillCopyParam(cp);
    UnicodeString target = UnixIncludeTrailingBackslash(FromU8(remoteDirUtf8));
    LogLine("  upload: CopyToRemote ...");
    bool res = g_terminal->CopyToRemote(files.get(), target, &cp, cpNoConfirmation, nullptr);
    LogLine("  upload: CopyToRemote returned res=" + std::string(res ? "1" : "0"));
    if (!g_lastTransferError.empty()) { if (error) *error = g_lastTransferError; LogLine("  upload: failed: " + g_lastTransferError); return false; }  // skipped-on-error
    LogLine("  upload: OK");
    return res;
  }
  catch (Exception & E) { std::string m = g_lastTransferError.empty() ? ExceptionToU8(E) : g_lastTransferError; if (error) *error = m; LogLine("  upload: EXCEPTION: " + m); return false; }
  catch (...) { if (error) *error = "unknown error"; LogLine("  upload: unknown exception"); return false; }
}

bool downloadFromRemote(const std::string & remotePathUtf8, const std::string & localDirUtf8,
                        std::string * error, bool plain)
{
  LogLine("downloadFromRemote('" + remotePathUtf8 + "' -> '" + localDirUtf8 + "') ...");
  ENGINE_LOCK;
  if (!remoteConnected()) { if (error) *error = "not connected"; LogLine("  download: not connected"); return false; }
  try
  {
    // CopyToLocal requires each entry's Object to be the TRemoteFile* (ProcessFiles reads
    // FileList->Objects[Index]); a bare string list yields a NULL File and crashes. Find the
    // file in the current listing (the panel the user is browsing) and attach it.
    TRemoteFile * rf = FindInListing(UnixExtractFileName(FromU8(remotePathUtf8)));
    if (rf == nullptr) { if (error) *error = "file not in current listing"; LogLine("  download: not in listing"); return false; }

    std::unique_ptr<TStrings> files(new TStringList());
    // A directory's FullFileName ends in '/'; the transfer code derives the local name with
    // UnixExtractFileName(), which is then empty - the folder's contents landed directly in the target.
    files->AddObject(UnixExcludeTrailingBackslash(rf->FullFileName), rf);
    TCopyParamType cp; if (plain) FillPlainCopyParam(cp); else FillCopyParam(cp);
    UnicodeString target = IncludeTrailingBackslash(FromU8(localDirUtf8));
    g_lastTransferError.clear();
    LogLine("  download: CopyToLocal ...");
    bool res = g_terminal->CopyToLocal(files.get(), target, &cp, cpNoConfirmation, nullptr);
    LogLine("  download: CopyToLocal returned res=" + std::string(res ? "1" : "0"));
    if (!g_lastTransferError.empty()) { if (error) *error = g_lastTransferError; LogLine("  download: failed: " + g_lastTransferError); return false; }
    LogLine("  download: OK");
    return res;
  }
  catch (Exception & E) { std::string m = g_lastTransferError.empty() ? ExceptionToU8(E) : g_lastTransferError; if (error) *error = m; LogLine("  download: EXCEPTION: " + m); return false; }
  catch (...) { if (error) *error = "unknown error"; LogLine("  download: unknown exception"); return false; }
}

bool remoteMakeDir(const std::string & nameUtf8, std::string * error)
{
  ENGINE_LOCK;
  if (!remoteConnected()) { if (error) *error = "not connected"; return false; }
  try
  {
    UnicodeString dir = UnixIncludeTrailingBackslash(g_terminal->CurrentDirectory) + FromU8(nameUtf8);
    TRemoteProperties props;   // CreateDirectory asserts non-null; empty Valid = no extra props
    g_lastTransferError.clear();
    g_terminal->CreateDirectory(dir, &props);
    if (!g_lastTransferError.empty()) { if (error) *error = g_lastTransferError; return false; }
    return true;
  }
  // Prefer the message the error-query captured (e.g. "Permission denied") over a bare EAbort.
  catch (Exception & E) { if (error) *error = g_lastTransferError.empty() ? ExceptionToU8(E) : g_lastTransferError; return false; }
  catch (...) { if (error) *error = g_lastTransferError.empty() ? "unknown error" : g_lastTransferError; return false; }
}

bool remoteDelete(const std::string & nameUtf8, std::string * error)
{
  LogLine("remoteDelete('" + nameUtf8 + "') ...");
  ENGINE_LOCK;
  if (!remoteConnected()) { if (error) *error = "not connected"; LogLine("  delete: not connected"); return false; }
  try
  {
    TRemoteFile * rf = FindInListing(FromU8(nameUtf8));
    if (rf == nullptr) { if (error) *error = "file not in current listing"; LogLine("  delete: not in listing"); return false; }
    LogLine("  delete: found '" + ToU8(rf->FullFileName) + "' isDir=" + (rf->IsDirectory ? "1" : "0") + " -> DeleteFile");
    g_lastTransferError.clear();
    // A directory's FullFileName ends in "/" (LEARNINGS 19h). S3 appends its own "/" to a directory
    // key, so "dir//" was sent and no directory could be deleted there.
    g_terminal->DeleteFile(UnixExcludeTrailingBackslash(rf->FullFileName), rf, nullptr);
    LogLine("  delete: DeleteFile returned");
    if (!g_lastTransferError.empty()) { if (error) *error = g_lastTransferError; LogLine("  delete: failed: " + g_lastTransferError); return false; }
    LogLine("  delete: OK");
    return true;
  }
  catch (Exception & E) { if (error) *error = ExceptionToU8(E); return false; }
  catch (...) { if (error) *error = "unknown error"; return false; }
}

bool remoteRename(const std::string & oldNameUtf8, const std::string & newNameUtf8, std::string * error)
{
  ENGINE_LOCK;
  if (!remoteConnected()) { if (error) *error = "not connected"; return false; }
  try
  {
    TRemoteFile * rf = FindInListing(FromU8(oldNameUtf8));
    if (rf == nullptr) { if (error) *error = "file not in current listing"; return false; }
    if (rf->IsDirectory && AS().params.o.protocol == engine::Protocol::S3)     // the engine only says "Error renaming file"
    { if (error) *error = "Moving or renaming a folder is not supported on S3 (the files in it can be moved)."; return false; }
    g_lastTransferError.clear();
    g_terminal->RenameFile(rf, FromU8(newNameUtf8));
    if (!g_lastTransferError.empty()) { if (error) *error = g_lastTransferError; return false; }
    return true;
  }
  catch (Exception & E) { if (error) *error = ExceptionToU8(E); return false; }
  catch (...) { if (error) *error = "unknown error"; return false; }
}

std::string remoteFileOctal(const std::string & nameUtf8)
{
  ENGINE_LOCK;
  if (!remoteConnected()) return std::string();
  TRemoteFile * rf = FindInListing(FromU8(nameUtf8));
  if ((rf == nullptr) || (rf->Rights == nullptr)) return std::string();
  return ToU8(rf->Rights->Octal);
}

bool remoteChmod(const std::string & nameUtf8, const std::string & octalUtf8, bool recursive, std::string * error)
{
  ENGINE_LOCK;
  if (!remoteConnected()) { if (error) *error = "not connected"; return false; }
  try
  {
    TRemoteFile * rf = FindInListing(FromU8(nameUtf8));
    if (rf == nullptr) { if (error) *error = "file not in current listing"; return false; }
    TRemoteProperties props;
    props.Valid = TValidProperties() << vpRights;
    props.Rights.Octal = FromU8(octalUtf8);
    props.Recursive = recursive;   // apply to a directory's contents too
    g_lastTransferError.clear();
    g_terminal->ChangeFileProperties(UnixExcludeTrailingBackslash(rf->FullFileName), rf, &props);
    if (!g_lastTransferError.empty()) { if (error) *error = g_lastTransferError; return false; }
    return true;
  }
  catch (Exception & E) { if (error) *error = ExceptionToU8(E); return false; }
  catch (...) { if (error) *error = "unknown error"; return false; }
}

//=== properties / links / server-side copy and move / information ==========
namespace {
bool AllDigits(const std::string & s)
{
  if (s.empty()) return false;
  for (char c : s) if (c < '0' || c > '9') return false;
  return true;
}
// An owner / group as typed: a numeric id, or a name. SFTP before version 4 can only send ids, so a
// name is looked up among what the session knows (the server's user/group lists when it gives
// them, and the owners/groups of the files of the current directory).
TRemoteToken TokenOf(const std::string & s, bool group)
{
  TRemoteToken t;
  if (AllDigits(s) && s.size() <= 9) { t.ID = static_cast<unsigned int>(std::strtoul(s.c_str(), nullptr, 10)); return t; }
  const UnicodeString name = FromU8(s);
  t.Name = name;
  try
  {
    const TRemoteTokenList * known = group ? g_terminal->Groups : g_terminal->Users;
    const TRemoteToken * found = (known != nullptr) ? known->Find(name) : nullptr;
    if ((found == nullptr) && (g_terminal->Membership != nullptr) && group) found = g_terminal->Membership->Find(name);
    if (found != nullptr && found->IDValid) return *found;
  }
  catch (...) {}
  TRemoteFileList * Files = g_terminal->Files;
  if (Files != nullptr)
    for (int i = 0; i < Files->Count; i++)
    {
      const TRemoteToken & c = group ? Files->Files[i]->Group : Files->Files[i]->Owner;
      if (c.IDValid && c.Name == name) return c;
    }
  return t;
}
// Does this session's protocol need numeric ids for owner/group (SFTP version 3 and older)?
bool NeedsNumericIds()
{
  if (AS().params.o.protocol != engine::Protocol::Sftp) return false;
  try
  {
    const UnicodeString p = g_terminal->GetFileSystemInfo().ProtocolName;   // "SFTP-3"
    const int dash = p.LastDelimiter(UnicodeString(L"-"));
    return (dash > 0) && (StrToIntDef(p.SubString(dash + 1, p.Length() - dash), 3) < 4);
  }
  catch (...) { return true; }
}
// Common tail of the operations below: the engine reports most failures through the error query /
// extended-exception events (captured in g_lastTransferError) instead of throwing.
bool FinishOp(std::string * error)
{
  if (!g_lastTransferError.empty()) { if (error) *error = g_lastTransferError; return false; }
  return true;
}
std::string OpError(Exception & E) { return g_lastTransferError.empty() ? ExceptionToU8(E) : g_lastTransferError; }
} // namespace

bool remoteSetProperties(const std::string & nameUtf8, const RemoteProps & p, std::string * error)
{
  ENGINE_LOCK;
  if (!remoteConnected()) { if (error) *error = "not connected"; return false; }
  if (p.setRights)
  {
    bool valid = (p.octal.size() == 3 || p.octal.size() == 4);
    for (char c : p.octal) if (c < '0' || c > '7') valid = false;
    if (!valid) { if (error) *error = "Invalid permissions \"" + p.octal + "\": use three or four octal digits, e.g. 644."; return false; }
  }
  if ((p.setOwner && p.owner.empty()) || (p.setGroup && p.group.empty())) { if (error) *error = "Owner and group cannot be empty."; return false; }
  if (p.setRights && !g_terminal->IsCapable[fcModeChanging]) { if (error) *error = "The server does not support changing permissions."; return false; }
  if (p.setOwner && !g_terminal->IsCapable[fcOwnerChanging]) { if (error) *error = "The server does not support changing the owner."; return false; }
  if (p.setGroup && !g_terminal->IsCapable[fcGroupChanging]) { if (error) *error = "The server does not support changing the group."; return false; }
  try
  {
    TRemoteFile * rf = FindInListing(FromU8(nameUtf8));
    if (rf == nullptr) { if (error) *error = "file not in current listing"; return false; }
    TRemoteProperties props;
    TValidProperties valid;
    if (p.setRights) { valid << vpRights; props.Rights.Octal = FromU8(p.octal); props.AddXToDirectories = p.addXToDirs; }
    if (p.setOwner) { valid << vpOwner; props.Owner = TokenOf(p.owner, false); }
    if (p.setGroup) { valid << vpGroup; props.Group = TokenOf(p.group, true); }
    if (valid.Empty()) return true;
    // Without this check a name the session cannot translate went out as id 0 (root / wheel).
    if (NeedsNumericIds() && ((p.setOwner && !props.Owner.IDValid) || (p.setGroup && !props.Group.IDValid)))
    {
      const std::string & what = (p.setOwner && !props.Owner.IDValid) ? p.owner : p.group;
      if (error) *error = "This server (SFTP version 3) changes owner and group by numeric id only, and the id of \"" + what +
                          "\" is not known. Enter the numeric id instead.";
      return false;
    }
    props.Valid = valid;
    props.Recursive = p.recursive;
    g_lastTransferError.clear();
    const UnicodeString full = UnixExcludeTrailingBackslash(rf->FullFileName);
    // SCP runs "chmod -R <mode>" and only then gives the top directory its x back: with a mode
    // without x (644, the usual one for files) chmod locks itself out of the directory on the first
    // step and nothing below is changed. Directories first (with x), then everything else.
    if (p.setRights && p.recursive && p.addXToDirs && rf->IsDirectory && AS().params.o.protocol == engine::Protocol::Scp)
    {
      TRights dirRights = props.Rights; dirRights.AddExecute();
      const UnicodeString q = UnicodeString(L"\"") + DelimitStr(full) + UnicodeString(L"\"");   // as the engine quotes paths
      const UnicodeString cmd =
        UnicodeString(L"find ") + q + UnicodeString(L" -type d -exec chmod ") + dirRights.Octal + UnicodeString(L" {} + && find ") + q +
        UnicodeString(L" ! -type d ! -type l -exec chmod ") + props.Rights.Octal + UnicodeString(L" {} +");
      g_terminal->AnyCommand(cmd, nullptr);
      if (!FinishOp(error)) return false;
      props.Valid = valid - (TValidProperties() << vpRights);     // owner / group, if any, the usual way
      if (props.Valid.Empty()) { g_terminal->ReadDirectory(true); return true; }
    }
    g_terminal->ChangeFileProperties(full, rf, &props);
    return FinishOp(error);
  }
  catch (Exception & E) { if (error) *error = OpError(E); return false; }
  catch (...) { if (error) *error = g_lastTransferError.empty() ? "unknown error" : g_lastTransferError; return false; }
}

bool remoteCreateLink(const std::string & nameUtf8, const std::string & targetUtf8, bool symbolic, std::string * error)
{
  ENGINE_LOCK;
  if (!remoteConnected()) { if (error) *error = "not connected"; return false; }
  if (nameUtf8.empty() || targetUtf8.empty()) { if (error) *error = "The link name and its target cannot be empty."; return false; }
  if (!g_terminal->IsCapable[symbolic ? fcSymbolicLink : fcHardLink])
  { if (error) *error = symbolic ? "The server does not support symbolic links." : "The server does not support hard links."; return false; }
  try
  {
    g_lastTransferError.clear();
    g_terminal->CreateLink(FromU8(nameUtf8), FromU8(targetUtf8), symbolic);
    return FinishOp(error);
  }
  catch (Exception & E) { if (error) *error = OpError(E); return false; }
  catch (...) { if (error) *error = g_lastTransferError.empty() ? "unknown error" : g_lastTransferError; return false; }
}

namespace {
bool CopyOrMove(bool move, const std::string & nameUtf8, const std::string & targetDirUtf8, const std::string & newNameUtf8, std::string * error)
{
  ENGINE_LOCK;
  if (!engine::remoteConnected()) { if (error) *error = "not connected"; return false; }
  if (targetDirUtf8.empty()) { if (error) *error = "The target directory cannot be empty."; return false; }
  if (!g_terminal->IsCapable[move ? fcRemoteMove : fcRemoteCopy])
  { if (error) *error = move ? "The server does not support moving files." : "The server does not support duplicating files."; return false; }
  try
  {
    TRemoteFile * rf = FindInListing(FromU8(nameUtf8));
    if (rf == nullptr) { if (error) *error = "file not in current listing"; return false; }
    const UnicodeString full = UnixExcludeTrailingBackslash(rf->FullFileName);
    const UnicodeString target = UnixIncludeTrailingBackslash(FromU8(targetDirUtf8));
    const UnicodeString newName = newNameUtf8.empty() ? UnixExtractFileName(full) : FromU8(newNameUtf8);
    if ((target + newName) == full) { if (error) *error = "The source and the target are the same file."; return false; }
    // Moving/copying a directory into itself would never end (or destroy it).
    if (rf->IsDirectory && (target + newName + UnicodeString(L"/")).SubString(1, full.Length() + 1) == (full + UnicodeString(L"/")))
    { if (error) *error = "A directory cannot be copied or moved into itself."; return false; }
    // SFTP duplicates with the copy-data extension, which works on one file at a time (on a
    // directory it left an empty file under the new name and failed); S3 has no folder copy either.
    const engine::Protocol proto = AS().params.o.protocol;
    if (!move && rf->IsDirectory && (proto == engine::Protocol::Sftp || proto == engine::Protocol::S3))
    { if (error) *error = "Duplicating a directory is not supported with this protocol. Duplicate the files in it instead."; return false; }
    if (move && rf->IsDirectory && proto == engine::Protocol::S3)
    { if (error) *error = "Moving or renaming a folder is not supported on S3 (the files in it can be moved)."; return false; }
    std::unique_ptr<TStrings> files(new TStringList());
    files->AddObject(full, rf);
    g_lastTransferError.clear();
    // the "file mask" is the new name: a mask without wildcards replaces the name as a whole
    bool ok = move ? g_terminal->MoveFiles(files.get(), target, newName, false)
                   : g_terminal->CopyFiles(files.get(), target, newName, false);
    if (!g_lastTransferError.empty()) { if (error) *error = g_lastTransferError; return false; }
    if (!ok && error) *error = move ? "The file could not be moved." : "The file could not be duplicated.";
    return ok;
  }
  catch (Exception & E) { if (error) *error = OpError(E); return false; }
  catch (...) { if (error) *error = g_lastTransferError.empty() ? "unknown error" : g_lastTransferError; return false; }
}
} // namespace

bool remoteDuplicate(const std::string & nameUtf8, const std::string & targetDirUtf8, const std::string & newNameUtf8, std::string * error)
{ return CopyOrMove(false, nameUtf8, targetDirUtf8, newNameUtf8, error); }
bool remoteMoveTo(const std::string & nameUtf8, const std::string & targetDirUtf8, const std::string & newNameUtf8, std::string * error)
{ return CopyOrMove(true, nameUtf8, targetDirUtf8, newNameUtf8, error); }

bool remoteCapable(Capability capability)
{
  ENGINE_LOCK;
  if (!remoteConnected()) return false;
  TFSCapability c = fcModeChanging;
  switch (capability)
  {
    case Capability::ChangeMode: c = fcModeChanging; break;
    case Capability::ChangeOwner: c = fcOwnerChanging; break;
    case Capability::ChangeGroup: c = fcGroupChanging; break;
    case Capability::SymbolicLink: c = fcSymbolicLink; break;
    case Capability::HardLink: c = fcHardLink; break;
    case Capability::RemoteCopy: c = fcRemoteCopy; break;
    case Capability::RemoteMove: c = fcRemoteMove; break;
    case Capability::SpaceAvailable: c = fcCheckingSpaceAvailable; break;
    case Capability::Rename: c = fcRename; break;
    case Capability::AnyCommand: c = fcAnyCommand; break;
  }
  try { return g_terminal->IsCapable[c]; } catch (...) { return false; }
}

bool remoteSpaceAvailable(const std::string & pathUtf8, SpaceInfo & info, std::string * error)
{
  ENGINE_LOCK;
  info = SpaceInfo();
  if (!remoteConnected()) { if (error) *error = "not connected"; return false; }
  if (!g_terminal->IsCapable[fcCheckingSpaceAvailable]) { if (error) *error = "The server does not tell how much space is available."; return false; }
  try
  {
    TSpaceAvailable sa;
    g_lastTransferError.clear();
    g_terminal->SpaceAvailable(FromU8(pathUtf8.empty() ? std::string("/") : pathUtf8), sa);
    if (!FinishOp(error)) return false;
    info.total = sa.BytesOnDevice; info.unused = sa.UnusedBytesOnDevice;
    info.totalForUser = sa.BytesAvailableToUser; info.unusedForUser = sa.UnusedBytesAvailableToUser;
    return true;
  }
  catch (Exception & E) { if (error) *error = OpError(E); return false; }
  catch (...) { if (error) *error = g_lastTransferError.empty() ? "unknown error" : g_lastTransferError; return false; }
}

SessionInformation sessionInformation()
{
  ENGINE_LOCK;
  SessionInformation out;
  if (!remoteConnected()) return out;
  try
  {
    const TSessionInfo & si = g_terminal->GetSessionInfo();
    const TFileSystemInfo & fi = g_terminal->GetFileSystemInfo(true);
    auto row = [](std::vector<InfoRow> & to, const char * label, const UnicodeString & v) {
      if (!v.IsEmpty()) to.push_back(InfoRow{ label, ToU8(v) });
    };
    out.protocol = ToU8(fi.ProtocolName);
    out.encrypted = !si.SecurityProtocolName.IsEmpty();
    out.loginSecs = SecondsOf(si.LoginTime);
    row(out.session, "Session protocol", si.ProtocolName);
    row(out.session, "File transfer protocol", fi.ProtocolName);
    row(out.session, "Cryptographic protocol", si.SecurityProtocolName);
    if (si.CSCipher == si.SCCipher) row(out.session, "Encryption algorithm", si.CSCipher);
    else { row(out.session, "Encryption algorithm (client to server)", si.CSCipher); row(out.session, "Encryption algorithm (server to client)", si.SCCipher); }
    if (si.CSCompression == si.SCCompression) row(out.session, "Compression", si.CSCompression);
    else { row(out.session, "Compression (client to server)", si.CSCompression); row(out.session, "Compression (server to client)", si.SCCompression); }
    row(out.session, "SSH protocol version", si.SshVersionString);
    row(out.session, "SSH implementation", si.SshImplementation);
    row(out.session, "Server host key fingerprint (SHA-256)", si.HostKeyFingerprintSHA256);
    row(out.session, "Server host key fingerprint (MD5)", si.HostKeyFingerprintMD5);
    row(out.session, "Certificate fingerprint (SHA-256)", si.CertificateFingerprintSHA256);
    row(out.session, "Certificate fingerprint (SHA-1)", si.CertificateFingerprintSHA1);
    row(out.session, "Remote system", fi.RemoteSystem);
    struct { const char * label; TFSCapability cap; } caps[] = {
      { "Can change permissions", fcModeChanging },
      { "Can change owner", fcOwnerChanging },
      { "Can change group", fcGroupChanging },
      { "Can execute arbitrary command", fcAnyCommand },
      { "Can create symbolic link", fcSymbolicLink },
      { "Can create hard link", fcHardLink },
      { "Can lookup user groups", fcUserGroupListing },
      { "Can duplicate remote files", fcRemoteCopy },
      { "Can move remote files", fcRemoteMove },
      { "Can check available space", fcCheckingSpaceAvailable },
      { "Can calculate file checksum", fcCalculatingChecksum },
      { "Native text (ASCII) mode transfers", fcNativeTextMode },
      { "Can resume transfers", fcResumeSupport },
      { "Can lock files", fcLocking },
    };
    for (const auto & c : caps) out.capabilities.push_back(InfoRow{ c.label, fi.IsCapable[c.cap] ? "Yes" : "No" });
    out.additional = ToU8(fi.AdditionalInfo);
  }
  catch (...) {}
  return out;
}

//=== directory synchronization =============================================
namespace { TSynchronizeChecklist * g_checklist = nullptr; }

std::vector<SyncItem> synchronizeCollect(const std::string & localDir, const std::string & remoteDir,
                                         int mode, bool del, std::string * error, bool subdirs)
{
  ENGINE_LOCK;
  std::vector<SyncItem> out;
  if (!remoteConnected()) { if (error) *error = "not connected"; return out; }
  synchronizeRelease();
  {
    std::error_code ec;      // the engine compares a missing local directory as an empty one
    if (!std::filesystem::is_directory(localDir, ec)) { if (error) *error = "The local directory \"" + localDir + "\" does not exist."; return out; }
  }
  try
  {
    TTerminal::TSynchronizeMode m = (mode == 1) ? TTerminal::smLocal : (mode == 2) ? TTerminal::smBoth : TTerminal::smRemote;
    int params = TTerminal::spNoConfirmation | (subdirs ? TTerminal::spSubDirs : TTerminal::spNoRecurse);
    if (del && mode != 2) params |= TTerminal::spDelete;
    g_lastTransferError.clear();
    TCopyParamType cp; FillCopyParam(cp);
    g_checklist = g_terminal->SynchronizeCollect(FromU8(localDir), FromU8(remoteDir), m, &cp, params, nullptr, nullptr);
    if (g_checklist != nullptr)
      for (int i = 0; i < g_checklist->Count; i++)
      {
        const TSynchronizeChecklist::TItem * it = g_checklist->Item[i];
        // The checklist also carries what the engine noticed but will NOT do with these options
        // (e.g. files that exist only on the target when "delete" is off): unchecked items.
        // SynchronizeApply skips them, so they are not changes to show.
        if (!it->Checked) continue;
        SyncItem si;
        switch (it->Action)
        {
          case TSynchronizeChecklist::saUploadNew: case TSynchronizeChecklist::saUploadUpdate: si.action = "Upload"; break;
          case TSynchronizeChecklist::saDownloadNew: case TSynchronizeChecklist::saDownloadUpdate: si.action = "Download"; break;
          case TSynchronizeChecklist::saDeleteRemote: si.action = "Delete remote"; break;
          case TSynchronizeChecklist::saDeleteLocal: si.action = "Delete local"; break;
          default: si.action = "?"; break;
        }
        // The name with its directory below the synchronized one ("sub/deep/x.txt"): a checklist of a
        // tree otherwise lists same-named files of different directories alike.
        {
          const bool viaRemote = it->IsRemoteOnly();
          std::string dir = ToU8(viaRemote ? it->Info2.Directory : it->Info1.Directory);   // 1 = local, 2 = remote
          const std::string base = viaRemote ? remoteDir : localDir;
          std::string rel;
          if (dir.size() > base.size() && dir.compare(0, base.size(), base) == 0) rel = dir.substr(base.size());
          while (!rel.empty() && rel.front() == '/') rel.erase(0, 1);
          while (!rel.empty() && rel.back() == '/') rel.pop_back();
          si.name = rel.empty() ? ToU8(it->GetFileName()) : rel + "/" + ToU8(it->GetFileName());
        }
        si.isDir = it->IsDirectory;
        si.size = it->IsDirectory ? 0 : it->GetSize();
        out.push_back(si);
      }
    // a directory that cannot be read is reported, not thrown: without this it compared as empty
    if (error && !g_lastTransferError.empty()) { *error = g_lastTransferError; synchronizeRelease(); out.clear(); }
  }
  catch (Exception & E) { if (error) *error = ExceptionToU8(E); synchronizeRelease(); }
  catch (...) { if (error) *error = "unknown error"; synchronizeRelease(); }
  return out;
}

bool synchronizeApply(std::string * error)
{
  ENGINE_LOCK;
  if (g_checklist == nullptr) { if (error) *error = "nothing to synchronize"; return false; }
  try
  {
    TCopyParamType cp; FillCopyParam(cp);
    g_terminal->SynchronizeApply(g_checklist, &cp, TTerminal::spNoConfirmation | TTerminal::spSubDirs,
                                 nullptr, nullptr, nullptr, nullptr, nullptr);
    return true;
  }
  catch (Exception & E) { if (error) *error = ExceptionToU8(E); return false; }
  catch (...) { if (error) *error = "unknown error"; return false; }
}

void synchronizeRelease() { delete g_checklist; g_checklist = nullptr; }

//=== find files / directory sizes ===========================================
bool remoteFind(const std::string & dir, const std::string & mask,
                const std::function<bool(const FoundFile &)> & onFound,
                const std::function<bool(const std::string &)> & onDirectory, std::string * error)
{
  ENGINE_LOCK;
  if (!remoteConnected()) { if (error) *error = "not connected"; return false; }
  g_lastTransferError.clear();
  try
  {
    TFileMasks masks;
    masks.Masks = FromU8(mask.empty() ? std::string("*") : mask);   // throws on a malformed mask
    UnicodeString start = FromU8(dir.empty() ? std::string("/") : dir);
    g_terminal->FilesFind(start, masks,
      [&](TTerminal *, const UnicodeString FileName, const TRemoteFile * File, bool & Cancel)
      {
        FoundFile f;
        f.path = ToU8(UnixExcludeTrailingBackslash(FileName));
        f.dir = ToU8(UnixExcludeTrailingBackslash(UnixExtractFilePath(FromU8(f.path))));
        if (f.dir.empty()) f.dir = "/";
        f.name = ToU8(UnixExtractFileName(FromU8(f.path)));
        if (File != nullptr)
        {
          f.isDir = File->IsDirectory;
          f.size = File->IsDirectory ? 0 : File->Size;
          f.modified = ToU8(File->ModificationStr);
        }
        if (onFound && onFound(f)) Cancel = true;
      },
      [&](TTerminal *, const UnicodeString Directory, bool & Cancel)
      {
        if (onDirectory && onDirectory(ToU8(Directory))) Cancel = true;
      });
    // a directory that cannot be read is reported through OnShowExtendedException and skipped
    if (!g_lastTransferError.empty() && error) *error = g_lastTransferError;
    return true;
  }
  catch (Exception & E) { if (error) *error = g_lastTransferError.empty() ? ExceptionToU8(E) : g_lastTransferError; return false; }
  catch (...) { if (error) *error = "unknown error"; return false; }
}

bool localFind(const std::string & dir, const std::string & mask,
               const std::function<bool(const FoundFile &)> & onFound,
               const std::function<bool(const std::string &)> & onDirectory, std::string * error)
{
  namespace fs = std::filesystem;
  try
  {
    // No engine session involved, but TFileMasks (the same mask language as the remote search) is engine code.
    { ENGINE_LOCK; EnsureEngineInited(); }
    TFileMasks masks;
    masks.Masks = FromU8(mask.empty() ? std::string("*") : mask);
    masks.NoImplicitMatchWithDirExcludeMask = true;
    masks.AllDirsAreImplicitlyIncluded = true;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) { if (error) *error = "Directory '" + dir + "' does not exist."; return false; }
    std::vector<std::string> pending{ dir };
    while (!pending.empty())
    {
      std::string cur = pending.back(); pending.pop_back();
      if (onDirectory && onDirectory(cur)) return true;
      std::vector<fs::directory_entry> entries;
      for (fs::directory_iterator it(cur, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
        entries.push_back(*it);
      std::sort(entries.begin(), entries.end(), [](const fs::directory_entry & a, const fs::directory_entry & b) { return a.path() < b.path(); });
      for (const fs::directory_entry & e : entries)
      {
        std::error_code e2;
        const bool isLink = e.is_symlink(e2);
        const bool isDir = e.is_directory(e2);
        FoundFile f;
        f.path = e.path().string();
        f.dir = cur;
        f.name = e.path().filename().string();
        f.isDir = isDir;
        f.size = (!isDir && e.is_regular_file(e2)) ? static_cast<std::int64_t>(e.file_size(e2)) : 0;
        TFileMasks::TParams params;
        params.Size = f.size;
        struct stat st;
        if (::stat(f.path.c_str(), &st) == 0)
        {
          struct tm lt; time_t t = st.st_mtime; localtime_r(&t, &lt);
          TDateTime when = EncodeDate(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) + EncodeTime(lt.tm_hour, lt.tm_min, lt.tm_sec, 0);
          params.Modification = when;
          f.modified = ToU8(when.DateTimeString());
        }
        // Same rule as the engine's remote search (TTerminal::FileFind): a directory that is only
        // "implicitly" included (so that its contents get searched) is not itself a result, and a
        // directory the mask excludes is not descended into. Paths here use '/', i.e. the mask
        // code's "remote" flavour.
        bool implicitMatch = false;
        if (masks.Matches(FromU8(f.path), false, isDir, &params, false, implicitMatch))
        {
          if (!implicitMatch && onFound && onFound(f)) return true;
          if (isDir && !isLink) pending.push_back(f.path);    // do not follow directory symlinks (cycles)
        }
      }
    }
    return true;
  }
  catch (Exception & E) { if (error) *error = ExceptionToU8(E); return false; }
  catch (...) { if (error) *error = "unknown error"; return false; }
}

bool remoteCalculateSize(const std::string & name, SizeResult & result,
                         const std::function<bool()> & cancelled, std::string * error)
{
  ENGINE_LOCK;
  result = SizeResult();
  if (!remoteConnected()) { if (error) *error = "not connected"; return false; }
  g_lastTransferError.clear();
  try
  {
    TRemoteFile * rf = FindInListing(FromU8(name));
    if (rf == nullptr) { if (error) *error = "file not in current listing"; return false; }
    std::unique_ptr<TStrings> files(new TStringList());
    files->AddObject(UnixExcludeTrailingBackslash(rf->FullFileName), rf);
    TCalculateSizeStats stats;
    TCalculateSizeParams params;
    params.Stats = &stats;
    __int64 size = 0;
    g_opCancel = cancelled;
    bool ok;
    try { ok = g_terminal->CalculateFilesSize(files.get(), size, params); }
    catch (...) { g_opCancel = nullptr; throw; }
    g_opCancel = nullptr;
    result.bytes = size; result.files = stats.Files; result.dirs = stats.Directories;
    if (cancelled && cancelled()) return false;
    if (!ok && error) *error = g_lastTransferError.empty() ? std::string("Could not read every directory.") : g_lastTransferError;
    return ok;
  }
  catch (Exception & E)
  {
    if (cancelled && cancelled()) return false;
    if (error) *error = g_lastTransferError.empty() ? ExceptionToU8(E) : g_lastTransferError;
    return false;
  }
  catch (...) { if (error && !(cancelled && cancelled())) *error = "unknown error"; return false; }
}

bool localCalculateSize(const std::string & path, SizeResult & result,
                        const std::function<bool()> & cancelled, std::string * error)
{
  namespace fs = std::filesystem;
  result = SizeResult();
  std::error_code ec;
  fs::file_status st = fs::symlink_status(path, ec);
  if (ec) { if (error) *error = ec.message(); return false; }
  if (!fs::is_directory(st))
  {
    result.files = 1;
    if (fs::is_regular_file(st)) result.bytes = static_cast<std::int64_t>(fs::file_size(path, ec));
    return true;
  }
  result.dirs = 1;
  int n = 0;
  for (fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
  {
    if (cancelled && (++n % 256 == 0) && cancelled()) return false;
    std::error_code e2;
    if (it->is_symlink(e2)) { ++result.files; continue; }          // count the link, not its target
    if (it->is_directory(e2)) ++result.dirs;
    else { ++result.files; if (it->is_regular_file(e2)) result.bytes += static_cast<std::int64_t>(it->file_size(e2)); }
  }
  if (ec) { if (error) *error = ec.message(); return false; }
  return true;
}

void disconnectSftp()   // closes + removes the ACTIVE session (tab)
{
  ENGINE_LOCK;
  DropActiveSession();
}

//--- session tabs ---
int sessionCount() { ENGINE_LOCK; return (int)g_sess.size(); }
int activeSession() { ENGINE_LOCK; return g_active; }
std::string sessionLabel(int i) { ENGINE_LOCK; return (i >= 0 && i < (int)g_sess.size()) ? g_sess[i]->label : std::string(); }
void switchSession(int i) { ENGINE_LOCK; if (i >= 0 && i < (int)g_sess.size()) g_active = i; }
void closeSession(int i)
{
  ENGINE_LOCK;
  if (i < 0 || i >= (int)g_sess.size()) return;
  Sess * s = g_sess[i].get();
  if (s->term) { try { if (s->term->Active) s->term->Close(); } catch (...) {} }
  g_sess.erase(g_sess.begin() + i);
  if (g_sess.empty()) g_active = -1;
  else if (g_active > i) g_active--;
  else if (g_active == i) g_active = std::min(i, (int)g_sess.size() - 1);
}

} // namespace engine
