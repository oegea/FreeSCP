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
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>

void __fastcall PuttyInitialize();   // one-time PuTTY init (sk_init + appname)
void __fastcall NeonInitialize();    // ne_sock_init (WebDAV/HTTP)

namespace {

std::string ToU8(const UnicodeString & s) { return std::string(UTF8String(s).c_str()); }
UnicodeString FromU8(const std::string & s) { return UTF8ToString(RawByteString(s.c_str())); }

//--- remote SFTP session state (single session) ---
class BridgeConfiguration : public TConfiguration
{
public:
  __fastcall BridgeConfiguration() : TConfiguration() {}
  virtual UnicodeString TemporaryDir(bool = false) { return UnicodeString(L"/tmp/"); }
};

std::unique_ptr<TTerminal> g_terminal;
std::unique_ptr<TSessionData> g_sessionData;
UnicodeString g_password;
bool g_engineInited = false;
std::function<bool(const engine::TransferProgress &)> g_progressCb;
std::string g_lastTransferError;   // set by OnQueryUser on a qtError so transfers can report it
// Confirmation callback (host-key verification). Returns true to accept. If unset, auto-accept.
// Connect runs on the caller (UI) thread, so the GUI can show a modal dialog from here.
std::function<bool(const std::string &)> g_confirmCb;

// Last successful connect params, so we can open additional (parallel) connections to the same server.
struct ConnParams { std::string host; int port = 0; std::string user, password; engine::Protocol protocol = engine::Protocol::Sftp; bool tls = false; bool valid = false; };
ConnParams g_lastParams;

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
  Configuration->Default();
  PuttyInitialize();
  NeonInitialize();    // ne_sock_init; needed for WebDAV (no-op cost otherwise)
  g_engineInited = true;
}

} // namespace

namespace engine {

std::string banner()
{
  return "WinSCP — native port (engine RTL + platform layer; local FS via ported FindFirst)";
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

//--- remote SFTP session ---------------------------------------------------
ConnectResult connectSftp(const std::string & host, int port,
                          const std::string & user, const std::string & password,
                          Protocol protocol, bool tls, const std::string & keyFile)
{
  ENGINE_LOCK;
  ConnectResult r;
  try
  {
    EnsureEngineInited();
    disconnectSftp();
    g_password = FromU8(password);

    g_sessionData.reset(new TSessionData(L""));
    g_sessionData->Default();
    g_sessionData->HostName = FromU8(host);
    g_sessionData->PortNumber = port;
    g_sessionData->UserName = FromU8(user);
    g_sessionData->Password = g_password;
    switch (protocol)
    {
      case Protocol::Scp:    g_sessionData->FSProtocol = fsSCPonly; break;
      case Protocol::WebDav: g_sessionData->FSProtocol = fsWebDAV;
                             g_sessionData->Ftps = tls ? ftpsImplicit : ftpsNone; break;
      case Protocol::S3:     g_sessionData->FSProtocol = fsS3;
                             g_sessionData->Ftps = tls ? ftpsImplicit : ftpsNone;
                             g_sessionData->S3UrlStyle = s3usPath; g_sessionData->S3DefaultRegion = L"us-east-1"; break;
      case Protocol::Ftp:    g_sessionData->FSProtocol = fsFTP;
                             g_sessionData->Ftps = tls ? ftpsImplicit : ftpsNone; break;
      default:               g_sessionData->FSProtocol = fsSFTPonly; break;
    }
    if (!keyFile.empty())   // public-key auth: the password field is the key passphrase
    {
      UnicodeString kf = FromU8(keyFile);
      TKeyType kt = KeyType(kf);
      // PuTTY's auth only uses .ppk private keys; convert OpenSSH/SSHCom keys to a temp .ppk first.
      if (kt == ktOpenSSHPEM || kt == ktOpenSSHNew || kt == ktSSHCom)
      {
        TPrivateKey * pk = LoadKey(kt, kf, g_password);   // throws on bad passphrase
        UnicodeString ppk = FromU8("/tmp/.winscp-tmpkey.ppk");
        SaveKey(ktSSH2, ppk, UnicodeString(), pk);        // unencrypted temp ppk
        FreeKey(pk);
        g_sessionData->PublicKeyFile = ppk;               // no passphrase needed for the temp
      }
      else
      {
        g_sessionData->PublicKeyFile = kf;
        g_sessionData->Passphrase = g_password;           // .ppk: password field = its passphrase
      }
    }
    g_sessionData->FingerprintScan = false;
    // Always read fresh listings: with the cache on, a panel refresh after an upload/op could show a
    // stale directory (the uploaded file appeared missing). The GUI re-lists on every navigate anyway.
    g_sessionData->CacheDirectories = false;
    g_sessionData->CacheDirectoryChanges = false;

    g_terminal.reset(new TTerminal(g_sessionData.get(), Configuration));
    g_terminal->OnPromptUser =
      [](TTerminal *, TPromptKind, UnicodeString, UnicodeString, TStrings *, TStrings * Results, bool & Result, void *)
      { if (Results != nullptr) for (int i = 0; i < Results->Count; i++) Results->Strings[i] = g_password; Result = true; };
    g_terminal->OnQueryUser =
      [](TObject *, const UnicodeString & Query, TStrings *, unsigned int Answers, const TQueryParams *, unsigned int & Answer, TQueryType Type, void *)
      {
        if (Type == qtError)
        {
          // qaYes here is a recoverable action (e.g. "Delete & recreate" to overwrite a file the
          // server won't truncate) -> take it. Otherwise it's a real failure (permission denied,
          // disk full): record the message and skip so the transfer reports "failed" not "done".
          if (Answers & qaYes) Answer = qaYes;
          else { g_lastTransferError = ToU8(Query); Answer = (Answers & qaSkip) ? qaSkip : ((Answers & qaAbort) ? qaAbort : Answers); }
        }
        else if ((Answers & qaYes) && (Answers & qaNo) && g_confirmCb)   // a Yes/No confirmation (host key)
          Answer = g_confirmCb(ToU8(Query)) ? qaYes : qaNo;
        else
          Answer = (Answers & qaYes) ? qaYes : ((Answers & qaOK) ? qaOK : Answers);
      };
    g_terminal->OnProgress =
      [](TFileOperationProgressType & P)
      {
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

    g_terminal->Open();
    if (!g_sessionData->PublicKeyFile.IsEmpty() && g_sessionData->PublicKeyFile == FromU8("/tmp/.winscp-tmpkey.ppk"))
      ::remove("/tmp/.winscp-tmpkey.ppk");   // don't leave the unencrypted converted key on disk
    r.ok = true;
    r.currentDir = ToU8(g_terminal->CurrentDirectory);
    g_lastParams = ConnParams{ host, port, user, password, protocol, tls, true };  // for parallel connections
  }
  catch (Exception & E)
  {
    UnicodeString msg = E.Message;
    ExtException * Ext = dynamic_cast<ExtException *>(&E);
    if ((Ext != nullptr) && (Ext->MoreMessages != nullptr))
      for (int i = 0; i < Ext->MoreMessages->Count; i++) msg += UnicodeString(L"\n") + Ext->MoreMessages->Strings[i];
    r.error = ToU8(msg);
    g_terminal.reset();
  }
  catch (...) { r.error = "unknown error"; g_terminal.reset(); }
  return r;
}

void setProgressSink(const std::function<bool(const TransferProgress &)> & cb) { g_progressCb = cb; }
void setConfirmCallback(const std::function<bool(const std::string &)> & cb) { g_confirmCb = cb; }

bool remoteConnected() { ENGINE_LOCK; return g_terminal && g_terminal->Active; }
std::string remoteCurrentDir() { ENGINE_LOCK; return remoteConnected() ? ToU8(g_terminal->CurrentDirectory) : std::string(); }

std::vector<DirEntry> listRemoteDir(const std::string & utf8Path)
{
  ENGINE_LOCK;
  std::vector<DirEntry> result;
  if (!remoteConnected()) return result;
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
        if (!e.isParent)
        {
          e.rights = ToU8(F->RightsStr);   // e.g. "rwxr-xr-x"
          UnicodeString owner = F->Owner.DisplayText;
          UnicodeString group = F->Group.DisplayText;
          e.owner = ToU8(group.IsEmpty() ? owner : (owner + UnicodeString(L"/") + group));
        }
        result.push_back(e);
      }
  }
  catch (...) { /* leave partial/empty */ }

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
} // namespace

//=== parallel-connection pool (experimental; for queue parallelism) ========
namespace {
struct ParConn
{
  std::unique_ptr<TSessionData> data;
  std::unique_ptr<TTerminal> term;
  std::recursive_mutex mtx;            // serializes THIS connection only (parallelism across conns)
  std::function<bool(const engine::TransferProgress &)> sink;
};
std::vector<std::unique_ptr<ParConn>> g_pool;
std::mutex g_poolMtx;
}

// FTP's single-connection state machine doesn't run reliably in parallel instances; gate it out.
bool parallelSupported() { return g_lastParams.valid && g_lastParams.protocol != engine::Protocol::Ftp; }

int openParallelConnection(std::string * error)
{
  std::lock_guard<std::mutex> pk(g_poolMtx);
  if (!g_lastParams.valid) { if (error) *error = "no primary connection"; return 0; }
  try
  {
    EnsureEngineInited();
    auto c = std::make_unique<ParConn>();
    c->data.reset(new TSessionData(L""));
    c->data->Default();
    c->data->HostName = FromU8(g_lastParams.host);
    c->data->PortNumber = g_lastParams.port;
    c->data->UserName = FromU8(g_lastParams.user);
    c->data->Password = FromU8(g_lastParams.password);
    switch (g_lastParams.protocol)
    {
      case engine::Protocol::Scp:    c->data->FSProtocol = fsSCPonly; break;
      case engine::Protocol::WebDav: c->data->FSProtocol = fsWebDAV; c->data->Ftps = g_lastParams.tls ? ftpsImplicit : ftpsNone; break;
      case engine::Protocol::S3:     c->data->FSProtocol = fsS3; c->data->Ftps = g_lastParams.tls ? ftpsImplicit : ftpsNone;
                                     c->data->S3UrlStyle = s3usPath; c->data->S3DefaultRegion = L"us-east-1"; break;
      case engine::Protocol::Ftp:    c->data->FSProtocol = fsFTP; c->data->Ftps = g_lastParams.tls ? ftpsImplicit : ftpsNone; break;
      default:                       c->data->FSProtocol = fsSFTPonly; break;
    }
    c->data->FingerprintScan = false;
    c->data->CacheDirectories = false; c->data->CacheDirectoryChanges = false;
    UnicodeString pw = FromU8(g_lastParams.password);
    ParConn * cp = c.get();
    c->term.reset(new TTerminal(c->data.get(), Configuration));
    c->term->OnPromptUser = [pw](TTerminal *, TPromptKind, UnicodeString, UnicodeString, TStrings *, TStrings * Results, bool & Result, void *)
      { if (Results) for (int i = 0; i < Results->Count; i++) Results->Strings[i] = pw; Result = true; };
    c->term->OnQueryUser = [](TObject *, const UnicodeString & Query, TStrings *, unsigned int Answers, const TQueryParams *, unsigned int & Answer, TQueryType Type, void *)
      { if (Type == qtError) { if (Answers & qaYes) Answer = qaYes; else { g_lastTransferError = ToU8(Query); Answer = (Answers & qaSkip) ? qaSkip : ((Answers & qaAbort) ? qaAbort : Answers); } }
        else Answer = (Answers & qaYes) ? qaYes : ((Answers & qaOK) ? qaOK : Answers); };
    c->term->OnProgress = [cp](TFileOperationProgressType & P)
      { if (cp->sink) { engine::TransferProgress tp; tp.file = ToU8(P.FileName); tp.transferred = P.TransferredSize; tp.total = P.TransferSize; tp.cps = (std::int64_t)P.CPS(); if (cp->sink(tp)) P.SetCancel(csCancel); } };
    c->term->Open();
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
    TCopyParamType cp; cp.Default();
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
    files->AddObject(rf->FullFileName, rf);
    TCopyParamType cp; cp.Default();
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

bool uploadToRemote(const std::string & localPathUtf8, const std::string & remoteDirUtf8,
                    std::string * error)
{
  ENGINE_LOCK;
  if (!remoteConnected()) { if (error) *error = "not connected"; return false; }
  g_lastTransferError.clear();
  try
  {
    std::unique_ptr<TStrings> files(new TStringList());
    files->Add(FromU8(localPathUtf8));
    TCopyParamType cp; cp.Default();
    UnicodeString target = UnixIncludeTrailingBackslash(FromU8(remoteDirUtf8));
    bool res = g_terminal->CopyToRemote(files.get(), target, &cp, cpNoConfirmation, nullptr);
    if (!g_lastTransferError.empty()) { if (error) *error = g_lastTransferError; return false; }  // skipped-on-error
    return res;
  }
  catch (Exception & E) { if (error) *error = g_lastTransferError.empty() ? ExceptionToU8(E) : g_lastTransferError; return false; }
  catch (...) { if (error) *error = "unknown error"; return false; }
}

bool downloadFromRemote(const std::string & remotePathUtf8, const std::string & localDirUtf8,
                        std::string * error)
{
  ENGINE_LOCK;
  if (!remoteConnected()) { if (error) *error = "not connected"; return false; }
  try
  {
    // CopyToLocal requires each entry's Object to be the TRemoteFile* (ProcessFiles reads
    // FileList->Objects[Index]); a bare string list yields a NULL File and crashes. Find the
    // file in the current listing (the panel the user is browsing) and attach it.
    TRemoteFile * rf = FindInListing(UnixExtractFileName(FromU8(remotePathUtf8)));
    if (rf == nullptr) { if (error) *error = "file not in current listing"; return false; }

    std::unique_ptr<TStrings> files(new TStringList());
    files->AddObject(rf->FullFileName, rf);
    TCopyParamType cp; cp.Default();
    UnicodeString target = IncludeTrailingBackslash(FromU8(localDirUtf8));
    g_lastTransferError.clear();
    bool res = g_terminal->CopyToLocal(files.get(), target, &cp, cpNoConfirmation, nullptr);
    if (!g_lastTransferError.empty()) { if (error) *error = g_lastTransferError; return false; }
    return res;
  }
  catch (Exception & E) { if (error) *error = g_lastTransferError.empty() ? ExceptionToU8(E) : g_lastTransferError; return false; }
  catch (...) { if (error) *error = "unknown error"; return false; }
}

bool remoteMakeDir(const std::string & nameUtf8, std::string * error)
{
  ENGINE_LOCK;
  if (!remoteConnected()) { if (error) *error = "not connected"; return false; }
  try
  {
    UnicodeString dir = UnixIncludeTrailingBackslash(g_terminal->CurrentDirectory) + FromU8(nameUtf8);
    TRemoteProperties props;   // CreateDirectory asserts non-null; empty Valid = no extra props
    g_terminal->CreateDirectory(dir, &props);
    return true;
  }
  catch (Exception & E) { if (error) *error = ExceptionToU8(E); return false; }
  catch (...) { if (error) *error = "unknown error"; return false; }
}

bool remoteDelete(const std::string & nameUtf8, std::string * error)
{
  ENGINE_LOCK;
  if (!remoteConnected()) { if (error) *error = "not connected"; return false; }
  try
  {
    TRemoteFile * rf = FindInListing(FromU8(nameUtf8));
    if (rf == nullptr) { if (error) *error = "file not in current listing"; return false; }
    g_terminal->DeleteFile(rf->FullFileName, rf, nullptr);
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
    g_terminal->RenameFile(rf, FromU8(newNameUtf8));
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

bool remoteChmod(const std::string & nameUtf8, const std::string & octalUtf8, std::string * error)
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
    g_terminal->ChangeFileProperties(rf->FullFileName, rf, &props);
    return true;
  }
  catch (Exception & E) { if (error) *error = ExceptionToU8(E); return false; }
  catch (...) { if (error) *error = "unknown error"; return false; }
}

//=== directory synchronization =============================================
namespace { TSynchronizeChecklist * g_checklist = nullptr; }

std::vector<SyncItem> synchronizeCollect(const std::string & localDir, const std::string & remoteDir,
                                         int mode, bool del, std::string * error)
{
  ENGINE_LOCK;
  std::vector<SyncItem> out;
  if (!remoteConnected()) { if (error) *error = "not connected"; return out; }
  synchronizeRelease();
  try
  {
    TTerminal::TSynchronizeMode m = (mode == 1) ? TTerminal::smLocal : (mode == 2) ? TTerminal::smBoth : TTerminal::smRemote;
    int params = TTerminal::spNoConfirmation | TTerminal::spSubDirs;
    if (del && mode != 2) params |= TTerminal::spDelete;
    TCopyParamType cp; cp.Default();
    g_checklist = g_terminal->SynchronizeCollect(FromU8(localDir), FromU8(remoteDir), m, &cp, params, nullptr, nullptr);
    if (g_checklist != nullptr)
      for (int i = 0; i < g_checklist->Count; i++)
      {
        const TSynchronizeChecklist::TItem * it = g_checklist->Item[i];
        SyncItem si;
        switch (it->Action)
        {
          case TSynchronizeChecklist::saUploadNew: case TSynchronizeChecklist::saUploadUpdate: si.action = "Upload"; break;
          case TSynchronizeChecklist::saDownloadNew: case TSynchronizeChecklist::saDownloadUpdate: si.action = "Download"; break;
          case TSynchronizeChecklist::saDeleteRemote: si.action = "Delete remote"; break;
          case TSynchronizeChecklist::saDeleteLocal: si.action = "Delete local"; break;
          default: si.action = "?"; break;
        }
        si.name = ToU8(it->GetFileName());
        si.isDir = it->IsDirectory;
        si.size = it->IsDirectory ? 0 : it->GetSize();
        out.push_back(si);
      }
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
    TCopyParamType cp; cp.Default();
    g_terminal->SynchronizeApply(g_checklist, &cp, TTerminal::spNoConfirmation | TTerminal::spSubDirs,
                                 nullptr, nullptr, nullptr, nullptr, nullptr);
    return true;
  }
  catch (Exception & E) { if (error) *error = ExceptionToU8(E); return false; }
  catch (...) { if (error) *error = "unknown error"; return false; }
}

void synchronizeRelease() { delete g_checklist; g_checklist = nullptr; }

void disconnectSftp()
{
  ENGINE_LOCK;
  if (g_terminal)
  {
    try { if (g_terminal->Active) g_terminal->Close(); } catch (...) {}
    g_terminal.reset();
  }
  g_sessionData.reset();
}

} // namespace engine
