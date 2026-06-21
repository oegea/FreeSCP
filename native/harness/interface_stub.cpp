//---------------------------------------------------------------------------
// interface_stub.cpp — headless implementations of the GUI/Interface boundary the engine calls
// but which upstream live in source/windows (the VCL app). Enough to LINK + run a non-interactive
// SFTP session: no UI, no clipboard, prompts answered by the harness's Seat. Replace piecemeal as
// the Qt UI grows.
//---------------------------------------------------------------------------
#include <vcl.h>
#include "CoreMain.h"
#include "Interface.h"
#include "Security.h"
#include "NeonIntf.h"
#include "S3FileSystem.h"

//--- engine-wide globals (CoreMain owns these in the VCL app; the harness sets Configuration) ---
TConfiguration * Configuration = nullptr;
TStoredSessionList * StoredSessions = nullptr;
bool AnySession = false;
TApplicationLog * ApplicationLog = nullptr;
const wchar_t * ToggleNames[] = { L"off", L"on" };

//--- Interface.h: app identity / registry ---
UnicodeString __fastcall AppNameString() { return L"WinSCP"; }
UnicodeString __fastcall SshVersionString() { return L"WinSCP-native"; }
UnicodeString __fastcall GetRegistryKey() { return L"Software\\Martin Prikryl\\WinSCP 2"; }
UnicodeString __fastcall GetCompanyRegistryKey() { return L"Software\\Martin Prikryl"; }
TOptions * __fastcall GetGlobalOptions() { return nullptr; }

//--- Interface.h: GUI hooks — no-ops headless ---
void * __fastcall BusyStart() { return nullptr; }
void __fastcall BusyEnd(void *) {}
bool __fastcall ProcessGUI(bool) { return false; }
void SystemRequired() {}
void __fastcall CopyToClipboard(UnicodeString) {}
bool __fastcall TextFromClipboard(UnicodeString &, bool) { return false; }

//--- Interface.h: diagnostics ---
bool __fastcall AppendExceptionStackTraceAndForget(TStrings *&) { return false; }
UnicodeString __fastcall GetExceptionDebugInfo() { return UnicodeString(); }

//--- Interface.h: prompt classification (drives Seat answering) ---
bool __fastcall IsAuthenticationPrompt(TPromptKind Kind)
{
  return (Kind == pkUserName) || (Kind == pkPassword) || (Kind == pkPassphrase) ||
         (Kind == pkNewPassword) || (Kind == pkTIS) || (Kind == pkCryptoCard) ||
         (Kind == pkKeybInteractive);
}
bool __fastcall IsPasswordOrPassphrasePrompt(TPromptKind Kind, TStrings *)
{
  return (Kind == pkPassword) || (Kind == pkPassphrase) || (Kind == pkNewPassword) ||
         (Kind == pkTIS) || (Kind == pkCryptoCard) || (Kind == pkKeybInteractive);
}
void __fastcall AnswerNameAndCaption(unsigned int, UnicodeString & Name, UnicodeString & Caption)
{ Name = UnicodeString(); Caption = UnicodeString(); }

//--- Interface.h: TQueryButtonAlias ---
TQueryButtonAlias::TQueryButtonAlias() :
  Button(0), OnSubmit(nullptr), GroupWith(-1), ElevationRequired(false), MenuButton(false) {}
TQueryButtonAlias TQueryButtonAlias::CreateYesToAllGrouppedWithYes() { return TQueryButtonAlias(); }
TQueryButtonAlias TQueryButtonAlias::CreateNoToAllGrouppedWithNo() { return TQueryButtonAlias(); }
TQueryButtonAlias TQueryButtonAlias::CreateAllAsYesToNewerGrouppedWithYes() { return TQueryButtonAlias(); }
TQueryButtonAlias TQueryButtonAlias::CreateIgnoreAsRenameGrouppedWithNo() { return TQueryButtonAlias(); }

//--- Interface.h: TQueryParams ---
TQueryParams::TQueryParams(unsigned int AParams, UnicodeString AHelpKeyword) :
  Aliases(nullptr), AliasesCount(0), Params(AParams), Timer(0), TimerEvent(nullptr),
  TimerAnswers(0), TimerQueryType(qtConfirmation), Timeout(0), TimeoutAnswer(0),
  TimeoutResponse(0), NoBatchAnswers(0), HelpKeyword(AHelpKeyword) {}
void TQueryParams::Assign(const TQueryParams & Source) { *this = Source; }

//--- Interface.h: operation visualizers (busy-cursor RAII) — no-ops headless ---
__fastcall TOperationVisualizer::TOperationVisualizer(bool UseBusyCursor) :
  FUseBusyCursor(UseBusyCursor), FToken(nullptr) {}
__fastcall TOperationVisualizer::~TOperationVisualizer() {}
__fastcall TInstantOperationVisualizer::TInstantOperationVisualizer() : TOperationVisualizer(true) {}
__fastcall TInstantOperationVisualizer::~TInstantOperationVisualizer() {}

//--- Security.h: stored-password obfuscation. Passthrough for now (no WinSCP.ini interop yet) ---
RawByteString EncryptPassword(UnicodeString Password, UnicodeString /*Key*/, Integer /*Algorithm*/)
{ return RawByteString(UTF8String(Password)); }
UnicodeString DecryptPassword(RawByteString Password, UnicodeString /*Key*/, Integer /*Algorithm*/)
{ return UTF8ToString(Password); }

//--- NeonIntf.h: CertificateSummary/CertificateVerificationMessage/NeonWindowsValidateCertificate-
//    WithMessage are now provided by the real NeonIntf.cpp (Phase 4 winscpcore_neon group).
// Security.h: Windows cert-store validation (last-resort path); n/a on macOS — OpenSSL verifies.
bool WindowsValidateCertificate(const unsigned char *, size_t, UnicodeString &)
{ return false; }

//--- S3 env helpers (S3LibDefaultHostName/S3EnvUserName/...) are now provided by the real
//    S3FileSystem.cpp (Phase 4 winscpcore_neon group). ---
