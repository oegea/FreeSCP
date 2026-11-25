//---------------------------------------------------------------------------
// secrets.cpp — see secrets.h.
//---------------------------------------------------------------------------
#include "secrets.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "enginebridge.h"
#include "uicommon.h"

namespace {

QString gMaster;            // the master password given in this run
bool gHaveMaster = false;
bool gDeclined = false;     // the user cancelled the prompt: do not ask again until askAgain()

const char * const kVerifierKey = "security/masterVerifier";
const QStringList kPasswordKeys = { "pass", "proxyPass", "tunnelPass" };

QString verifier() { return QSettings().value(kVerifierKey).toString(); }

QString encode(const QString & plain, bool encrypt, const QString & master)
{
  if (plain.isEmpty()) return QString();
  if (encrypt) return "enc:" + u8(engine::protectSecret(s8(plain), s8(master)));
  return (plain.startsWith("enc:") || plain.startsWith("plain:")) ? "plain:" + plain : plain;
}

// `ok` false: encrypted, and `master` does not open it.
QString decode(const QString & stored, const QString & master, bool haveMaster, bool * ok)
{
  if (ok) *ok = true;
  if (stored.startsWith("plain:")) return stored.mid(6);
  if (!stored.startsWith("enc:")) return stored;
  std::string plain;
  if (haveMaster && engine::unprotectSecret(s8(stored.mid(4)), s8(master), plain)) return u8(plain);
  if (ok) *ok = false;
  return QString();
}

// Rewrite every stored password from (oldMaster) to (newEncrypt, newMaster). All or nothing.
bool recodeAll(bool haveOld, const QString & oldMaster, bool newEncrypt, const QString & newMaster, QString * error)
{
  QSettings s;
  s.beginGroup("sites");
  const QStringList sites = s.childGroups();
  QList<QPair<QString, QString>> updates;
  for (const QString & site : sites)
    for (const QString & key : kPasswordKeys)
    {
      const QString path = site + "/" + key;
      if (!s.contains(path)) continue;
      bool ok = true;
      const QString plain = decode(s.value(path).toString(), oldMaster, haveOld, &ok);
      if (!ok)
      {
        if (error) *error = QString("The password stored for the site \"%1\" cannot be decrypted with the current master password. Nothing was changed.").arg(site);
        s.endGroup();
        return false;
      }
      updates.append(qMakePair(path, encode(plain, newEncrypt, newMaster)));
    }
  for (const auto & u : updates) s.setValue(u.first, u.second);
  s.endGroup();
  return true;
}

} // namespace

namespace secrets {

bool masterEnabled() { return !verifier().isEmpty(); }
bool unlocked() { return !masterEnabled() || gHaveMaster; }
void lock() { gMaster.clear(); gHaveMaster = false; gDeclined = false; }
void askAgain() { gDeclined = false; }
bool isEncrypted(const QString & stored) { return stored.startsWith("enc:"); }

bool unlock(QWidget * parent)
{
  if (unlocked()) return true;
  const QString v = verifier();
  if (!qEnvironmentVariableIsEmpty("WINSCP_MASTER_PASSWORD"))      // scripted runs
  {
    const QString given = qEnvironmentVariable("WINSCP_MASTER_PASSWORD");
    if (engine::masterVerify(s8(given), s8(v))) { gMaster = given; gHaveMaster = true; return true; }
    fprintf(stderr, "[master] the master password given is wrong\n");
    return false;
  }
  if (uiScripted()) { fprintf(stderr, "[master] the master password is needed and was not given\n"); return false; }
  if (gDeclined) return false;
  for (int attempt = 0; attempt < 3; ++attempt)
  {
    bool okIn = false;
    const QString given = QInputDialog::getText(parent, "Master password",
      attempt == 0 ? QString("The passwords of the saved sites are protected.\n\n&Master password:")
                   : QString("That is not the master password.\n\n&Master password:"),
      QLineEdit::Password, QString(), &okIn);
    if (!okIn) { gDeclined = true; return false; }
    if (engine::masterVerify(s8(given), s8(v))) { gMaster = given; gHaveMaster = true; return true; }
  }
  appMessage(parent, "Master password", "The master password was wrong three times.");
  gDeclined = true;
  return false;
}

QString toStored(const QString & plain, QWidget * parent, bool * ok)
{
  if (ok) *ok = true;
  if (plain.isEmpty()) return QString();
  if (!masterEnabled()) return encode(plain, false, QString());
  if (!unlock(parent)) { if (ok) *ok = false; return QString(); }
  return encode(plain, true, gMaster);
}

QString fromStored(const QString & stored, QWidget * parent, bool * ok)
{
  if (ok) *ok = true;
  if (!isEncrypted(stored)) return decode(stored, QString(), false, nullptr);
  if (!unlock(parent)) { if (ok) *ok = false; return QString(); }
  bool good = true;
  const QString plain = decode(stored, gMaster, true, &good);
  if (!good && ok) *ok = false;
  return plain;
}

bool setMaster(const QString & current, const QString & newPassword, QString * error)
{
  if (newPassword.isEmpty()) { if (error) *error = "The master password cannot be empty."; return false; }
  const bool had = masterEnabled();
  if (had && !engine::masterVerify(s8(current), s8(verifier()))) { if (error) *error = "The current master password is wrong."; return false; }
  if (!recodeAll(had, current, true, newPassword, error)) return false;
  QSettings s; s.setValue(kVerifierKey, u8(engine::masterVerifier(s8(newPassword)))); s.sync();
  gMaster = newPassword; gHaveMaster = true;
  return true;
}

bool removeMaster(const QString & current, QString * error)
{
  if (!masterEnabled()) return true;
  if (!engine::masterVerify(s8(current), s8(verifier()))) { if (error) *error = "The current master password is wrong."; return false; }
  if (!recodeAll(true, current, false, QString(), error)) return false;
  QSettings s; s.remove(kVerifierKey); s.sync();
  lock();
  return true;
}

bool showMasterPasswordDialog(QWidget * parent)
{
  const bool had = masterEnabled();
  QDialog d(parent);
  d.setWindowTitle(had ? "Change master password" : "Set master password");
  auto * lay = new QVBoxLayout(&d);
  auto * intro = new QLabel(had
    ? "Enter the current master password, and a new one - or tick \"Remove\" to store the passwords unprotected again."
    : "The passwords of your saved sites (also tunnel and proxy passwords) will be stored encrypted with AES-256, "
      "with a key made from this master password. You are asked for it once each time FreeSCP starts and a saved "
      "password is needed.\n\nThere is no way to recover the saved passwords if you forget it.");
  intro->setWordWrap(true); intro->setMinimumWidth(420);
  lay->addWidget(intro);
  auto * form = new QFormLayout;
  auto * cur = new QLineEdit; cur->setEchoMode(QLineEdit::Password); cur->setObjectName("current");
  auto * pw1 = new QLineEdit; pw1->setEchoMode(QLineEdit::Password); pw1->setObjectName("new");
  auto * pw2 = new QLineEdit; pw2->setEchoMode(QLineEdit::Password); pw2->setObjectName("confirm");
  if (had) form->addRow("&Current master password:", cur); else cur->hide();
  form->addRow("&New master password:", pw1);
  form->addRow("&Repeat it:", pw2);
  lay->addLayout(form);
  auto * remove = new QCheckBox("Re&move the master password (store passwords unprotected)"); remove->setObjectName("remove");
  remove->setVisible(had);
  lay->addWidget(remove);
  QObject::connect(remove, &QCheckBox::toggled, &d, [pw1, pw2](bool on) { pw1->setEnabled(!on); pw2->setEnabled(!on); });
  auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  bb->button(QDialogButtonBox::Ok)->setObjectName("dialogOk");
  lay->addWidget(bb);
  bool changed = false;
  QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  QObject::connect(bb, &QDialogButtonBox::accepted, &d, [&] {
    QString error;
    if (had && remove->isChecked())
    {
      if (!removeMaster(cur->text(), &error)) { appMessage(&d, d.windowTitle(), error); return; }
    }
    else
    {
      if (pw1->text().isEmpty()) { appMessage(&d, d.windowTitle(), "Enter the new master password."); pw1->setFocus(); return; }
      if (pw1->text() != pw2->text()) { appMessage(&d, d.windowTitle(), "The two passwords are not the same."); pw2->setFocus(); return; }
      if (!setMaster(cur->text(), pw1->text(), &error)) { appMessage(&d, d.windowTitle(), error); return; }
    }
    changed = true;
    d.accept();
  });
  (had ? cur : pw1)->setFocus();
  execDialog(d, "masterpassword");
  return changed;
}

} // namespace secrets
