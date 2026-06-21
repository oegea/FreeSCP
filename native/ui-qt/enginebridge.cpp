//---------------------------------------------------------------------------
// enginebridge.cpp — the ONLY UI TU compiled with engine flags (-fshort-wchar -fms-extensions).
// Calls the ported engine/rtlcompat and converts UnicodeString <-> UTF-8 std::string.
//---------------------------------------------------------------------------
#include "enginebridge.h"

#include <vcl.h>              // ported RTL + platform layer (rtlcompat): UnicodeString,
                              // FindFirst/TSearchRec, path helpers, GetEnvironmentVariable.
#include "CoreMain.h"         // engine globals (Configuration, ApplicationLog, ...)
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

void __fastcall PuttyInitialize();   // one-time PuTTY init (sk_init + appname)

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

void EnsureEngineInited()
{
  if (g_engineInited) return;
  ApplicationLog = new TApplicationLog();          // AppLog macro derefs this unguarded
  Configuration = new BridgeConfiguration();
  Configuration->Default();
  PuttyInitialize();
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
                          Protocol protocol)
{
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
    g_sessionData->FSProtocol = (protocol == Protocol::Scp) ? fsSCPonly : fsSFTPonly;
    g_sessionData->FingerprintScan = false;

    g_terminal.reset(new TTerminal(g_sessionData.get(), Configuration));
    g_terminal->OnPromptUser =
      [](TTerminal *, TPromptKind, UnicodeString, UnicodeString, TStrings *, TStrings * Results, bool & Result, void *)
      { if (Results != nullptr) for (int i = 0; i < Results->Count; i++) Results->Strings[i] = g_password; Result = true; };
    g_terminal->OnQueryUser =
      [](TObject *, const UnicodeString &, TStrings *, unsigned int Answers, const TQueryParams *, unsigned int & Answer, TQueryType, void *)
      { Answer = (Answers & qaYes) ? qaYes : ((Answers & qaOK) ? qaOK : Answers); };   // auto-accept host key

    g_terminal->Open();
    r.ok = true;
    r.currentDir = ToU8(g_terminal->CurrentDirectory);
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

bool remoteConnected() { return g_terminal && g_terminal->Active; }
std::string remoteCurrentDir() { return remoteConnected() ? ToU8(g_terminal->CurrentDirectory) : std::string(); }

std::vector<DirEntry> listRemoteDir(const std::string & utf8Path)
{
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

bool uploadToRemote(const std::string & localPathUtf8, const std::string & remoteDirUtf8,
                    std::string * error)
{
  if (!remoteConnected()) { if (error) *error = "not connected"; return false; }
  try
  {
    std::unique_ptr<TStrings> files(new TStringList());
    files->Add(FromU8(localPathUtf8));
    TCopyParamType cp; cp.Default();
    UnicodeString target = UnixIncludeTrailingBackslash(FromU8(remoteDirUtf8));
    return g_terminal->CopyToRemote(files.get(), target, &cp, cpNoConfirmation, nullptr);
  }
  catch (Exception & E) { if (error) *error = ExceptionToU8(E); return false; }
  catch (...) { if (error) *error = "unknown error"; return false; }
}

bool downloadFromRemote(const std::string & remotePathUtf8, const std::string & localDirUtf8,
                        std::string * error)
{
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
    return g_terminal->CopyToLocal(files.get(), target, &cp, cpNoConfirmation, nullptr);
  }
  catch (Exception & E) { if (error) *error = ExceptionToU8(E); return false; }
  catch (...) { if (error) *error = "unknown error"; return false; }
}

bool remoteMakeDir(const std::string & nameUtf8, std::string * error)
{
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
  if (!remoteConnected()) return std::string();
  TRemoteFile * rf = FindInListing(FromU8(nameUtf8));
  if ((rf == nullptr) || (rf->Rights == nullptr)) return std::string();
  return ToU8(rf->Rights->Octal);
}

bool remoteChmod(const std::string & nameUtf8, const std::string & octalUtf8, std::string * error)
{
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

void disconnectSftp()
{
  if (g_terminal)
  {
    try { if (g_terminal->Active) g_terminal->Close(); } catch (...) {}
    g_terminal.reset();
  }
  g_sessionData.reset();
}

} // namespace engine
