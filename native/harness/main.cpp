//---------------------------------------------------------------------------
// main.cpp — headless SFTP connect harness. Builds a TConfiguration + TSessionData + TTerminal,
// wires minimal non-interactive callbacks (password from the session, auto-accept host key),
// opens the session and lists the remote directory. Target server: the staged Docker sshd
// (localhost:2222 winscp/winscp123). First end-to-end runtime test of the ported engine.
//---------------------------------------------------------------------------
#include <vcl.h>
#include "CoreMain.h"
#include "Configuration.h"
#include "SessionData.h"
#include "Terminal.h"
#include "CopyParam.h"
#include "Interface.h"
#include <memory>
#include "RemoteFiles.h"
#include "Exceptions.h"
#include <cstdio>

// PuTTY one-time init (sk_init + sets appname); normally called by CoreInitialize.
void __fastcall PuttyInitialize();
void __fastcall PuttyFinalize();

static void out(const UnicodeString & s)
{ std::string u(UTF8String(s).c_str()); std::fprintf(stderr, "%s\n", u.c_str()); }

// Concrete TConfiguration (the base is abstract on TemporaryDir()).
class THarnessConfiguration : public TConfiguration
{
public:
  __fastcall THarnessConfiguration() : TConfiguration() {}
  virtual UnicodeString TemporaryDir(bool = false) { return UnicodeString(L"/tmp/"); }
};

int main(int argc, char ** argv)
{
  UnicodeString Host = (argc > 1) ? UnicodeString(argv[1]) : UnicodeString(L"127.0.0.1");
  int Port = (argc > 2) ? atoi(argv[2]) : 2222;
  UnicodeString User = (argc > 3) ? UnicodeString(argv[3]) : UnicodeString(L"winscp");
  UnicodeString Pass = (argc > 4) ? UnicodeString(argv[4]) : UnicodeString(L"winscp123");

  try
  {
    // The engine's AppLog macro dereferences ApplicationLog with no null guard (CoreInitialize
    // normally creates it). Provide one (Logging defaults off).
    ApplicationLog = new TApplicationLog();
    Configuration = new THarnessConfiguration();
    Configuration->Default();
    PuttyInitialize();   // sk_init + appname; the engine's CoreInitialize would normally do this    // logging disabled for now
    out(L"[harness] Configuration created; PuTTY initialized.");

    std::unique_ptr<TSessionData> Data(new TSessionData(L""));
    Data->Default();
    Data->HostName = Host;
    Data->PortNumber = Port;
    Data->UserName = User;
    Data->Password = Pass;
    Data->FSProtocol = (::getenv("WINSCP_SCP") != nullptr) ? fsSCPonly : fsSFTPonly;  // TEMP SCP test
    Data->FingerprintScan = false;   // ensure normal connect (not fingerprint-scan mode)
    out(FORMAT(L"[harness] Session: %s@%s:%d (SFTP)", (User, Host, Port)));

    std::unique_ptr<TTerminal> Terminal(new TTerminal(Data.get(), Configuration));

    Terminal->OnInformation =
      [](TTerminal *, const UnicodeString & Str, int, const UnicodeString &) { out(UnicodeString(L"[info] ") + Str); };
    Terminal->OnPromptUser =
      [&](TTerminal *, TPromptKind Kind, UnicodeString Name, UnicodeString, TStrings * Prompts, TStrings * Results, bool & Result, void *)
      {
        out(FORMAT(L"[prompt] kind=%d name='%s' prompts=%d results=%d",
          ((int)Kind, Name, (Prompts ? Prompts->Count : 0), (Results ? Results->Count : 0))));
        // Fill every requested field with the password (covers password + keyboard-interactive).
        if (Results != nullptr)
          for (int i = 0; i < Results->Count; i++) Results->Strings[i] = Pass;
        Result = true;
      };
    Terminal->OnQueryUser =
      [](TObject *, const UnicodeString & Query, TStrings * More, unsigned int Answers, const TQueryParams *, unsigned int & Answer, TQueryType, void *)
      { out(UnicodeString(L"[query] ") + Query);
        if (More != nullptr) for (int i = 0; i < More->Count; i++) out(UnicodeString(L"   | ") + More->Strings[i]);
        Answer = (Answers & qaYes) ? qaYes : ((Answers & qaOK) ? qaOK : Answers); };
    Terminal->OnShowExtendedException =
      [](TTerminal *, Exception * E, void *) { out(UnicodeString(L"[exception] ") + (E ? UnicodeString(E->Message) : UnicodeString())); };

    out(L"[harness] Opening session...");
    Terminal->Open();
    out(L"[harness] CONNECTED.");

    // The session startup already resolved + read the home directory; just list it.
    TRemoteFileList * Files = Terminal->Files;
    out(FORMAT(L"[harness] %s : %d entries", (Terminal->CurrentDirectory, (Files ? Files->Count : 0))));
    if (Files != nullptr)
      for (int i = 0; i < Files->Count; i++)
      { TRemoteFile * F = Files->Files[i]; out(FORMAT(L"  %s\t%s", (F->FileName, UnicodeString((__int64)F->Size)))); }

    // Optional transfer self-test (set WINSCP_XFER=1) — exercises CopyToRemote/CopyToLocal on
    // the current protocol (SFTP, or SCP via WINSCP_SCP). Round-trips /tmp/xfer-up.txt.
    if (::getenv("WINSCP_XFER") != nullptr)
    {
      FILE * f = fopen("/tmp/xfer-up.txt", "w"); fputs("scp xfer test\n", f); fclose(f);
      std::unique_ptr<TStrings> up(new TStringList()); up->Add(L"/tmp/xfer-up.txt");
      TCopyParamType cp; cp.Default();
      bool uok = Terminal->CopyToRemote(up.get(), L"/config/", &cp, cpNoConfirmation, NULL);
      out(FORMAT(L"[harness] SCP/SFTP UPLOAD ok=%d", ((int)uok)));

      Terminal->ReadCurrentDirectory(); Terminal->ReadDirectory(false);
      TRemoteFile * rf = NULL;
      for (int i = 0; i < Terminal->Files->Count; i++)
        if (Terminal->Files->Files[i]->FileName == UnicodeString(L"xfer-up.txt")) { rf = Terminal->Files->Files[i]; break; }
      std::unique_ptr<TStrings> dn(new TStringList());
      if (rf) dn->AddObject(rf->FullFileName, rf);
      bool dok = rf && Terminal->CopyToLocal(dn.get(), L"/tmp/dl/", &cp, cpNoConfirmation, NULL);
      out(FORMAT(L"[harness] SCP/SFTP DOWNLOAD ok=%d (rf=%d)", ((int)dok, (int)(rf!=NULL))));
    }

    // Optional file-ops self-test (set WINSCP_OPS=1) — mkdir / rename / delete on the current
    // protocol (SFTP, or SCP via WINSCP_SCP; SCP runs shell mkdir/mv/rm).
    if (::getenv("WINSCP_OPS") != nullptr)
    {
      TRemoteProperties props;
      Terminal->CreateDirectory(L"/config/optest", &props);
      Terminal->ReadCurrentDirectory(); Terminal->ReadDirectory(false);
      TRemoteFile * d = NULL;
      for (int i = 0; i < Terminal->Files->Count; i++)
        if (Terminal->Files->Files[i]->FileName == UnicodeString(L"optest")) d = Terminal->Files->Files[i];
      out(FORMAT(L"[harness] OPS mkdir ok=%d", ((int)(d!=NULL))));
      if (d) Terminal->RenameFile(d, L"optest2");
      Terminal->ReadCurrentDirectory(); Terminal->ReadDirectory(false);
      TRemoteFile * d2 = NULL;
      for (int i = 0; i < Terminal->Files->Count; i++)
        if (Terminal->Files->Files[i]->FileName == UnicodeString(L"optest2")) d2 = Terminal->Files->Files[i];
      out(FORMAT(L"[harness] OPS rename ok=%d", ((int)(d2!=NULL))));
      if (d2) Terminal->DeleteFile(d2->FullFileName, d2, NULL);
      Terminal->ReadCurrentDirectory(); Terminal->ReadDirectory(false);
      bool gone = true;
      for (int i = 0; i < Terminal->Files->Count; i++)
        if (Terminal->Files->Files[i]->FileName == UnicodeString(L"optest2")) gone = false;
      out(FORMAT(L"[harness] OPS delete ok=%d", ((int)gone)));
    }

    Terminal->Close();
    out(L"[harness] Done.");
    return 0;
  }
  catch (Exception & E)
  {
    out(UnicodeString(L"[FATAL] ") + E.Message);
    ExtException * Ext = dynamic_cast<ExtException *>(&E);
    if ((Ext != nullptr) && (Ext->MoreMessages != nullptr))
      for (int i = 0; i < Ext->MoreMessages->Count; i++)
        out(UnicodeString(L"   | ") + Ext->MoreMessages->Strings[i]);
    return 1;
  }
  catch (...)
  {
    out(L"[FATAL] unknown exception");
    return 2;
  }
}
