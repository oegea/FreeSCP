//---------------------------------------------------------------------------
// secrets.h — the master password: the passwords of saved sites (also tunnel and proxy passwords)
// are stored encrypted with a key derived from it (the engine's AES-256 scheme, as WinSCP).
// Without a master password they are stored as typed, as before.
//
// Stored forms of a password in the settings:
//   ""               no password
//   "enc:<hex>"      encrypted with the master password
//   "plain:<text>"   plain text that itself starts with "enc:" or "plain:"
//   anything else    plain text
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_SECRETS_H
#define WINSCP_UIQT_SECRETS_H

#include <QString>

class QWidget;

namespace secrets {

// Is a master password set (QSettings "security/masterVerifier")?
bool masterEnabled();
// Has the master password been given in this run (or is none needed)?
bool unlocked();
// Ask for the master password if it is needed and was not given yet. False if the user cancelled
// or it was wrong three times. Scripted runs take it from WINSCP_MASTER_PASSWORD and never ask.
bool unlock(QWidget * parent);
// Forget the master password given in this run (the next use asks again).
void lock();

// A password in the form it is stored in. `ok` = false (and an empty result) if the master password
// is needed and was not obtained: the caller must NOT fall back to storing plain text.
QString toStored(const QString & plain, QWidget * parent, bool * ok);
// The password of a stored value. `ok` = false if it is encrypted and could not be decrypted
// (master password not given, or the value does not belong to it); the result is then empty.
QString fromStored(const QString & stored, QWidget * parent, bool * ok);
// Is this stored value encrypted?
bool isEncrypted(const QString & stored);

// Set, change or remove the master password. Every saved site is rewritten in the new form; if any
// existing value cannot be decrypted nothing is changed and `error` says so. `current` is checked
// against the present master password (ignored when none is set).
bool setMaster(const QString & current, const QString & newPassword, QString * error);
bool removeMaster(const QString & current, QString * error);

// The dialog of Preferences > Security: set / change / remove. Returns true if something changed.
bool showMasterPasswordDialog(QWidget * parent);

} // namespace secrets

#endif
