//---------------------------------------------------------------------------
// logindialog.h — the Login dialog (sites + Session form + Advanced: proxy / SSH tunnel / FTP),
// saved sites, and the authentication prompt (password, passphrase, keyboard-interactive / 2FA).
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_LOGINDIALOG_H
#define WINSCP_UIQT_LOGINDIALOG_H

#include <QString>
#include <QStringList>
#include <vector>
#include <string>
#include "enginebridge.h"

class QWidget;

struct LoginParams
{
  bool ok = false;
  engine::ConnectOptions o;
};

// Shows the Login dialog. `preset` (optional) pre-fills the form.
LoginParams showLoginDialog(QWidget * parent, const engine::ConnectOptions * preset = nullptr);

// Saved sites (QSettings group "sites/<name>").
QStringList savedSiteNames();
bool loadSite(const QString & name, engine::ConnectOptions & o);
// False if the site was not saved (a master password is set and was not given: see secrets.h).
bool saveSite(const QString & name, const engine::ConnectOptions & o);
void deleteSite(const QString & name);

// "SFTP", "SCP", "FTP", "WebDAV", "S3".
QString protocolName(engine::Protocol p);
// One line describing the non-default connection route ("SSH tunnel via u@h · SOCKS5 proxy h:1080");
// empty when the connection is direct.
QString routeSummary(const engine::ConnectOptions & o);

// Asks the user the questions of an authentication prompt. Returns false if cancelled.
// Headless runs take the answers from WINSCP_PROMPT_ANSWERS ("a|b|c", consumed in order) and cancel
// when it runs out.
bool showAuthPrompt(QWidget * parent, const engine::PromptRequest & rq, std::vector<std::string> & answers);

#endif
