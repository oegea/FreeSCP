//---------------------------------------------------------------------------
// logindialog.cpp — see logindialog.h.
//
// Login dialog, faithful to WinSCP: a sites tree on the left, a "Session" form on the right (File
// protocol, Encryption, Host name + Port, User name, Password, Private key) and an "Advanced..."
// dialog for what WinSCP keeps under Advanced Site Settings: Connection (FTP passive mode),
// Proxy and SSH Tunnel.
//---------------------------------------------------------------------------
#include "logindialog.h"
#include "uicommon.h"
#include "icons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// Protocol <-> combo index (the index is also what saved sites store as "protocol").
const engine::Protocol kProtocols[] = { engine::Protocol::Sftp, engine::Protocol::Scp, engine::Protocol::Ftp,
                                        engine::Protocol::WebDav, engine::Protocol::S3 };
int protocolIndex(engine::Protocol p)
{
  for (int i = 0; i < 5; ++i) if (kProtocols[i] == p) return i;
  return 0;
}
bool isSsh(engine::Protocol p) { return p == engine::Protocol::Sftp || p == engine::Protocol::Scp; }

int defaultPort(engine::Protocol p, bool tls, bool ftpExplicit)
{
  switch (p)
  {
    case engine::Protocol::Ftp:    return (tls && !ftpExplicit) ? 990 : 21;
    case engine::Protocol::WebDav: return tls ? 443 : 80;
    case engine::Protocol::S3:     return tls ? 443 : 80;
    default:                       return 22;
  }
}
int defaultProxyPort(int method) { return (method == 3) ? 80 : 1080; }

// A small "file path + browse" row.
QWidget * keyRow(QWidget * dlg, QLineEdit * edit)
{
  auto * row = new QWidget;
  auto * lay = new QHBoxLayout(row);
  lay->setContentsMargins(0, 0, 0, 0);
  auto * btn = new QToolButton; btn->setText("\xE2\x80\xA6");
  lay->addWidget(edit, 1); lay->addWidget(btn);
  QObject::connect(btn, &QToolButton::clicked, dlg, [dlg, edit] {
    QString f = QFileDialog::getOpenFileName(dlg, "Select SSH private key", QDir::homePath() + "/.ssh");
    if (!f.isEmpty()) edit->setText(f);
  });
  return row;
}

// Advanced site settings. Edits the advanced members of `o` in place; returns true on OK.
bool showAdvancedDialog(QWidget * parent, engine::ConnectOptions & o)
{
  QDialog d(parent);
  d.setWindowTitle("Advanced Site Settings");
  d.resize(520, 380);
  auto * outer = new QVBoxLayout(&d);
  auto * tabs = new QTabWidget;
  outer->addWidget(tabs, 1);

  //--- Connection ---
  auto * conTab = new QWidget; auto * conLay = new QVBoxLayout(conTab);
  auto * cbPassive = new QCheckBox("&Passive mode (FTP)");
  cbPassive->setChecked(o.ftpPassive);
  cbPassive->setEnabled(o.protocol == engine::Protocol::Ftp);
  auto * conHint = new QLabel(o.protocol == engine::Protocol::Ftp
    ? "In passive mode the client opens the data connections. Turn it off only for servers that "
      "require active mode (the server then connects back to this computer)."
    : "Passive mode applies to the FTP protocol only.");
  conHint->setWordWrap(true); conHint->setStyleSheet("color: gray;");
  auto * cbCrLf = new QCheckBox("Server uses CR/&LF line endings (Windows server)");
  cbCrLf->setChecked(o.serverCrLf);
  cbCrLf->setEnabled(isSsh(o.protocol));
  auto * crlfHint = new QLabel("Used by text-mode transfers over SFTP and SCP to convert line endings. "
                               "Leave it off for Unix servers.");
  crlfHint->setWordWrap(true); crlfHint->setStyleSheet("color: gray;");
  auto * utfRow = new QHBoxLayout;
  auto * utfCombo = new QComboBox; utfCombo->addItems({ "Auto", "On", "Off" });
  utfCombo->setCurrentIndex(qBound(0, o.utf8Names, 2));
  utfRow->addWidget(new QLabel("&UTF-8 encoding for filenames:")); utfRow->addWidget(utfCombo); utfRow->addStretch(1);
  auto * utfHint = new QLabel("Choose On if file names with accents or non-Latin characters look wrong or cannot "
                              "be transferred (typically SCP, or FTP servers that do not announce UTF-8).");
  utfHint->setWordWrap(true); utfHint->setStyleSheet("color: gray;");
  conLay->addWidget(cbPassive); conLay->addWidget(conHint);
  conLay->addSpacing(8);
  conLay->addWidget(cbCrLf); conLay->addWidget(crlfHint);
  conLay->addSpacing(8);
  conLay->addLayout(utfRow); conLay->addWidget(utfHint); conLay->addStretch(1);
  tabs->addTab(conTab, "Connection");

  //--- Proxy ---
  auto * pxTab = new QWidget; auto * pxForm = new QFormLayout(pxTab);
  auto * pxType = new QComboBox; pxType->addItems({ "None", "SOCKS4", "SOCKS5", "HTTP" });
  pxType->setCurrentIndex(qBound(0, o.proxyMethod, 3));
  auto * pxHost = new QLineEdit(u8(o.proxyHost)); pxHost->setPlaceholderText("proxy host name");
  auto * pxPort = new QSpinBox; pxPort->setRange(1, 65535);
  pxPort->setValue(o.proxyPort > 0 ? o.proxyPort : defaultProxyPort(o.proxyMethod));
  auto * pxUser = new QLineEdit(u8(o.proxyUser));
  auto * pxPass = new QLineEdit(u8(o.proxyPassword)); pxPass->setEchoMode(QLineEdit::Password);
  pxForm->addRow("Proxy &type:", pxType);
  pxForm->addRow("Proxy &host name:", pxHost);
  pxForm->addRow("Po&rt number:", pxPort);
  pxForm->addRow("&User name:", pxUser);
  pxForm->addRow("Pass&word:", pxPass);
  auto * pxHint = new QLabel("SFTP and SCP connections to this computer (localhost) are never sent through the proxy.");
  pxHint->setWordWrap(true); pxHint->setStyleSheet("color: gray;");
  pxForm->addRow(pxHint);
  auto syncProxy = [=] {
    bool on = pxType->currentIndex() != 0;
    for (QWidget * w : { (QWidget *)pxHost, (QWidget *)pxPort, (QWidget *)pxUser, (QWidget *)pxPass }) w->setEnabled(on);
  };
  QObject::connect(pxType, &QComboBox::currentIndexChanged, &d, [=](int i) {
    // follow the type's usual port unless the user chose their own
    if (pxPort->value() == 1080 || pxPort->value() == 80) pxPort->setValue(defaultProxyPort(i));
    syncProxy();
  });
  syncProxy();
  tabs->addTab(pxTab, "Proxy");

  //--- SSH tunnel ---
  auto * tnTab = new QWidget; auto * tnForm = new QFormLayout(tnTab);
  auto * tnOn = new QCheckBox("&Connect through SSH tunnel");
  tnOn->setChecked(o.tunnel);
  auto * tnHost = new QLineEdit(u8(o.tunnelHost)); tnHost->setPlaceholderText("SSH server to tunnel through (jump host)");
  auto * tnPort = new QSpinBox; tnPort->setRange(1, 65535); tnPort->setValue(o.tunnelPort > 0 ? o.tunnelPort : 22);
  auto * tnUser = new QLineEdit(u8(o.tunnelUser));
  auto * tnPass = new QLineEdit(u8(o.tunnelPassword)); tnPass->setEchoMode(QLineEdit::Password);
  tnPass->setPlaceholderText("password (or key passphrase)");
  auto * tnKey = new QLineEdit(u8(o.tunnelKeyFile)); tnKey->setPlaceholderText("(optional) SSH private key");
  auto * tnKeyRow = keyRow(&d, tnKey);
  tnForm->addRow(tnOn);
  tnForm->addRow("&Host name:", tnHost);
  tnForm->addRow("Po&rt number:", tnPort);
  tnForm->addRow("&User name:", tnUser);
  tnForm->addRow("Pass&word:", tnPass);
  tnForm->addRow("Private &key:", tnKeyRow);
  auto * tnHint = new QLabel("The session is opened from the tunnel's SSH server: the host name on the Login "
                             "dialog is resolved and reached from there. FTP data connections cannot be tunnelled.");
  tnHint->setWordWrap(true); tnHint->setStyleSheet("color: gray;");
  tnForm->addRow(tnHint);
  auto syncTunnel = [=] {
    bool on = tnOn->isChecked();
    for (QWidget * w : { (QWidget *)tnHost, (QWidget *)tnPort, (QWidget *)tnUser, (QWidget *)tnPass, tnKeyRow }) w->setEnabled(on);
  };
  QObject::connect(tnOn, &QCheckBox::toggled, &d, [=](bool) { syncTunnel(); });
  syncTunnel();
  tabs->addTab(tnTab, "Tunnel");

  // open on the page that has something set
  tabs->setCurrentIndex(o.tunnel ? 2 : (o.proxyMethod != 0 ? 1 : 0));
  {
    bool okIdx = false; int forced = qEnvironmentVariable("WINSCP_ADVANCED_TAB").toInt(&okIdx);   // dev: render a given page
    if (okIdx) tabs->setCurrentIndex(qBound(0, forced, 2));
  }

  auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  outer->addWidget(bb);
  QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  QObject::connect(bb, &QDialogButtonBox::accepted, &d, [&] {
    if (pxType->currentIndex() != 0 && pxHost->text().trimmed().isEmpty())
    { tabs->setCurrentIndex(1); appMessage(&d, "Proxy", "Please enter the proxy host name."); return; }
    if (tnOn->isChecked() && tnHost->text().trimmed().isEmpty())
    { tabs->setCurrentIndex(2); appMessage(&d, "Tunnel", "Please enter the host name of the SSH server to tunnel through."); return; }
    d.accept();
  });

  if (execDialog(d, "advanced") != QDialog::Accepted) return false;
  o.ftpPassive = cbPassive->isChecked();
  o.serverCrLf = cbCrLf->isChecked();
  o.utf8Names = utfCombo->currentIndex();
  o.proxyMethod = pxType->currentIndex();
  o.proxyHost = s8(pxHost->text().trimmed());
  o.proxyPort = pxPort->value();
  o.proxyUser = s8(pxUser->text());
  o.proxyPassword = s8(pxPass->text());
  o.tunnel = tnOn->isChecked();
  o.tunnelHost = s8(tnHost->text().trimmed());
  o.tunnelPort = tnPort->value();
  o.tunnelUser = s8(tnUser->text());
  o.tunnelPassword = s8(tnPass->text());
  o.tunnelKeyFile = s8(tnKey->text().trimmed());
  return true;
}

} // namespace

QString protocolName(engine::Protocol p)
{
  switch (p)
  {
    case engine::Protocol::Scp:    return "SCP";
    case engine::Protocol::Ftp:    return "FTP";
    case engine::Protocol::WebDav: return "WebDAV";
    case engine::Protocol::S3:     return "S3";
    default:                       return "SFTP";
  }
}

QString routeSummary(const engine::ConnectOptions & o)
{
  QStringList parts;
  if (o.tunnel && !o.tunnelHost.empty())
    parts << QString("SSH tunnel via %1%2").arg(o.tunnelUser.empty() ? QString() : u8(o.tunnelUser) + "@", u8(o.tunnelHost));
  if (o.proxyMethod != 0 && !o.proxyHost.empty())
  {
    static const char * names[] = { "", "SOCKS4", "SOCKS5", "HTTP" };
    parts << QString("%1 proxy %2:%3").arg(names[qBound(0, o.proxyMethod, 3)], u8(o.proxyHost)).arg(o.proxyPort);
  }
  if (o.protocol == engine::Protocol::Ftp && !o.ftpPassive) parts << "active mode";
  if (o.utf8Names == 1) parts << "UTF-8 names";
  else if (o.utf8Names == 2) parts << "non-UTF-8 names";
  if (o.serverCrLf && (o.protocol == engine::Protocol::Sftp || o.protocol == engine::Protocol::Scp)) parts << "CR/LF server";
  return parts.join(" \xC2\xB7 ");
}

//--- saved sites -------------------------------------------------------------
QStringList savedSiteNames()
{
  QSettings s;
  s.beginGroup("sites");
  QStringList names = s.childGroups();
  s.endGroup();
  return names;
}

bool loadSite(const QString & name, engine::ConnectOptions & o)
{
  QSettings s;
  if (!s.childGroups().contains("sites")) return false;
  s.beginGroup("sites/" + name);
  if (s.childKeys().isEmpty()) { s.endGroup(); return false; }
  o = engine::ConnectOptions();
  o.protocol = kProtocols[qBound(0, s.value("protocol", 0).toInt(), 4)];
  o.host = s8(s.value("host").toString());
  o.port = s.value("port", 22).toInt();
  o.user = s8(s.value("user").toString());
  o.password = s8(s.value("pass").toString());
  o.keyFile = s8(s.value("keyFile").toString());
  // Sites saved before 0.3 have no "tls" key (the Encryption choice was not stored): an HTTP-based
  // site on port 443 was necessarily meant as HTTPS.
  o.tls = s.contains("tls") ? s.value("tls").toBool()
                            : ((o.protocol == engine::Protocol::WebDav || o.protocol == engine::Protocol::S3) && o.port == 443);
  o.ftpExplicitTls = s.value("ftpExplicitTls", true).toBool();
  o.ftpPassive = s.value("ftpPassive", true).toBool();
  o.serverCrLf = s.value("serverCrLf", false).toBool();
  o.utf8Names = qBound(0, s.value("utf8Names", 0).toInt(), 2);
  o.proxyMethod = qBound(0, s.value("proxyMethod", 0).toInt(), 3);
  o.proxyHost = s8(s.value("proxyHost").toString());
  o.proxyPort = s.value("proxyPort", 0).toInt();
  o.proxyUser = s8(s.value("proxyUser").toString());
  o.proxyPassword = s8(s.value("proxyPass").toString());
  o.tunnel = s.value("tunnel", false).toBool();
  o.tunnelHost = s8(s.value("tunnelHost").toString());
  o.tunnelPort = s.value("tunnelPort", 22).toInt();
  o.tunnelUser = s8(s.value("tunnelUser").toString());
  o.tunnelPassword = s8(s.value("tunnelPass").toString());
  o.tunnelKeyFile = s8(s.value("tunnelKeyFile").toString());
  s.endGroup();
  return true;
}

void saveSite(const QString & name, const engine::ConnectOptions & o)
{
  QSettings s;
  s.remove("sites/" + name);                 // no stale keys from an earlier version of the site
  s.beginGroup("sites/" + name);
  s.setValue("protocol", protocolIndex(o.protocol));
  s.setValue("host", u8(o.host));
  s.setValue("port", o.port);
  s.setValue("user", u8(o.user));
  s.setValue("pass", u8(o.password));        // stored as typed, like the rest of the site (no master password yet)
  s.setValue("keyFile", u8(o.keyFile));
  s.setValue("tls", o.tls);
  s.setValue("ftpExplicitTls", o.ftpExplicitTls);
  s.setValue("ftpPassive", o.ftpPassive);
  s.setValue("serverCrLf", o.serverCrLf);
  s.setValue("utf8Names", o.utf8Names);
  s.setValue("proxyMethod", o.proxyMethod);
  s.setValue("proxyHost", u8(o.proxyHost));
  s.setValue("proxyPort", o.proxyPort);
  s.setValue("proxyUser", u8(o.proxyUser));
  s.setValue("proxyPass", u8(o.proxyPassword));
  s.setValue("tunnel", o.tunnel);
  s.setValue("tunnelHost", u8(o.tunnelHost));
  s.setValue("tunnelPort", o.tunnelPort);
  s.setValue("tunnelUser", u8(o.tunnelUser));
  s.setValue("tunnelPass", u8(o.tunnelPassword));
  s.setValue("tunnelKeyFile", u8(o.tunnelKeyFile));
  s.endGroup();
  s.sync();                                  // persist immediately (survives an abrupt close)
}

void deleteSite(const QString & name)
{
  QSettings s;
  s.remove("sites/" + name);
  s.sync();
}

//--- the dialog ---------------------------------------------------------------
LoginParams showLoginDialog(QWidget * parent, const engine::ConnectOptions * preset)
{
  LoginParams p;
  engine::ConnectOptions adv;   // advanced settings of the site being edited (proxy / tunnel / FTP mode)
  if (preset != nullptr) adv = *preset;

  QDialog dlg(parent);
  dlg.setWindowTitle("Login");
  dlg.resize(720, 430);

  auto * outer = new QVBoxLayout(&dlg);
  auto * top = new QHBoxLayout;
  outer->addLayout(top, 1);

  // Left: sites tree — "New Site" plus saved sessions (the WinSCP Site Manager experience: pick a
  // saved server instead of retyping).
  auto * sites = new QTreeWidget;
  sites->setHeaderHidden(true);
  sites->setRootIsDecorated(false);
  sites->setIconSize(QSize(16, 16));
  sites->setMaximumWidth(230);
  sites->setMinimumWidth(190);
  auto * newSite = new QTreeWidgetItem(sites, QStringList("New Site"));
  newSite->setIcon(0, appIcon("newtab"));
  top->addWidget(sites);

  auto reloadSites = [&] {
    while (sites->topLevelItemCount() > 1) delete sites->takeTopLevelItem(1);
    for (const QString & name : savedSiteNames())
    {
      auto * it = new QTreeWidgetItem(sites, QStringList(name));
      it->setIcon(0, appIcon("login"));
      it->setData(0, Qt::UserRole, name);
    }
  };
  reloadSites();

  // Right: Session group.
  auto * session = new QGroupBox("Session");
  auto * grid = new QGridLayout(session);
  auto * proto = new QComboBox;
  proto->addItem("SFTP"); proto->addItem("SCP"); proto->addItem("FTP");
  proto->addItem("WebDAV"); proto->addItem("Amazon S3");
  auto * enc = new QComboBox;
  auto * encLabel = new QLabel("Encryption:");
  auto * host = new QLineEdit; host->setPlaceholderText("hostname or IP");
  auto * port = new QSpinBox; port->setRange(1, 65535); port->setValue(22);
  auto * user = new QLineEdit; user->setPlaceholderText("username");
  auto * pass = new QLineEdit; pass->setEchoMode(QLineEdit::Password); pass->setPlaceholderText("password (or key passphrase)");
  auto * keyEdit = new QLineEdit; keyEdit->setPlaceholderText("(optional) SSH private key");
  auto * keyLabel = new QLabel("Private key:");
  auto * keyWidget = keyRow(&dlg, keyEdit);

  // WinSCP's Session box: captions above the fields - protocol (and encryption), host + port,
  // user + password - then Save on the left and Advanced on the right.
  auto caption = [](const QString & text, QWidget * buddy) { auto * l = new QLabel(text); l->setBuddy(buddy); return l; };
  grid->setVerticalSpacing(3); grid->setHorizontalSpacing(10);
  grid->addWidget(caption("&File protocol:", proto), 0, 0);
  encLabel->setText("&Encryption:"); encLabel->setBuddy(enc);
  grid->addWidget(encLabel, 0, 1, 1, 2);
  grid->addWidget(proto, 1, 0);
  grid->addWidget(enc, 1, 1, 1, 2);
  grid->setRowMinimumHeight(2, 8);
  grid->addWidget(caption("&Host name:", host), 3, 0, 1, 2);
  grid->addWidget(caption("Po&rt number:", port), 3, 2);
  grid->addWidget(host, 4, 0, 1, 2);
  grid->addWidget(port, 4, 2);
  grid->setRowMinimumHeight(5, 8);
  grid->addWidget(caption("&User name:", user), 6, 0);
  grid->addWidget(caption("&Password:", pass), 6, 1, 1, 2);
  grid->addWidget(user, 7, 0);
  grid->addWidget(pass, 7, 1, 1, 2);
  grid->setRowMinimumHeight(8, 8);
  keyLabel->setText("Private &key:"); keyLabel->setBuddy(keyEdit);
  grid->addWidget(keyLabel, 9, 0, 1, 3);
  grid->addWidget(keyWidget, 10, 0, 1, 3);
  grid->setRowMinimumHeight(11, 10);
  // Advanced settings: a button + a line saying what is configured, so a tunnel or proxy is never
  // an invisible property of the site.
  auto * saveBtn = new QPushButton("&Save\xE2\x80\xA6");
  auto * advBtn = new QPushButton("&Advanced\xE2\x80\xA6");
  auto * sessionBtns = new QHBoxLayout;
  sessionBtns->addWidget(saveBtn); sessionBtns->addStretch(1); sessionBtns->addWidget(advBtn);
  grid->addLayout(sessionBtns, 12, 0, 1, 3);
  auto * advSummary = new QLabel; advSummary->setStyleSheet("color: gray;"); advSummary->setWordWrap(true);
  grid->addWidget(advSummary, 13, 0, 1, 3);
  grid->setRowStretch(14, 1);
  grid->setColumnStretch(0, 3); grid->setColumnStretch(1, 2); grid->setColumnStretch(2, 1);
  top->addWidget(session, 1);

  auto currentProtocol = [&]() { return kProtocols[qBound(0, proto->currentIndex(), 4)]; };
  // The form's encryption choice as (tls, ftpExplicit).
  auto currentTls = [&]() { return enc->currentIndex() > 0; };
  auto currentFtpExplicit = [&]() { return enc->currentIndex() != 2; };
  auto refreshSummary = [&] {
    engine::ConnectOptions tmp = adv; tmp.protocol = currentProtocol();
    QString sum = routeSummary(tmp);
    advSummary->setText(sum.isEmpty() ? "Direct connection" : sum);
  };

  bool loading = false;   // true while the form is being filled programmatically: no default-port side effects
  // Rebuild the Encryption choices for a protocol; key file applies to SSH protocols only.
  auto applyProtocol = [&](engine::Protocol pr, bool tls, bool ftpExplicit) {
    enc->blockSignals(true);
    enc->clear();
    enc->addItem("No encryption");
    if (pr == engine::Protocol::Ftp) { enc->addItem("TLS/SSL Explicit encryption"); enc->addItem("TLS/SSL Implicit encryption"); }
    else if (!isSsh(pr)) enc->addItem("TLS/SSL");
    enc->setCurrentIndex(!tls ? 0 : (pr == engine::Protocol::Ftp && !ftpExplicit) ? 2 : 1);
    enc->blockSignals(false);
    encLabel->setVisible(!isSsh(pr)); enc->setVisible(!isSsh(pr));
    keyLabel->setVisible(isSsh(pr)); keyWidget->setVisible(isSsh(pr));
  };
  QObject::connect(proto, &QComboBox::currentIndexChanged, &dlg, [&](int) {
    engine::Protocol pr = currentProtocol();
    const bool tls = (pr == engine::Protocol::S3);        // S3 is HTTPS unless told otherwise (as in WinSCP)
    applyProtocol(pr, tls, true);
    if (!loading) port->setValue(defaultPort(pr, tls, true));
    refreshSummary();
  });
  QObject::connect(enc, &QComboBox::currentIndexChanged, &dlg, [&](int) {
    if (loading) return;
    // move between the protocol's standard ports, but leave a custom port alone
    engine::Protocol pr = currentProtocol();
    const int ports[] = { defaultPort(pr, false, true), defaultPort(pr, true, true), defaultPort(pr, true, false) };
    if (port->value() == ports[0] || port->value() == ports[1] || port->value() == ports[2])
      port->setValue(defaultPort(pr, currentTls(), currentFtpExplicit()));
  });

  // form <-> options
  auto setForm = [&](const engine::ConnectOptions & o) {
    loading = true;
    proto->setCurrentIndex(protocolIndex(o.protocol));
    applyProtocol(o.protocol, o.tls, o.ftpExplicitTls);
    host->setText(u8(o.host));
    port->setValue(o.port > 0 ? o.port : defaultPort(o.protocol, o.tls, o.ftpExplicitTls));
    user->setText(u8(o.user));
    pass->setText(u8(o.password));
    keyEdit->setText(u8(o.keyFile));
    adv = o;
    loading = false;
    refreshSummary();
  };
  auto getForm = [&]() {
    engine::ConnectOptions o = adv;          // advanced members
    o.protocol = currentProtocol();
    o.host = s8(host->text().trimmed());
    o.port = port->value();
    o.user = s8(user->text());
    o.password = s8(pass->text());
    o.keyFile = isSsh(o.protocol) ? s8(keyEdit->text().trimmed()) : std::string();
    o.tls = !isSsh(o.protocol) && currentTls();
    o.ftpExplicitTls = currentFtpExplicit();
    return o;
  };

  QObject::connect(advBtn, &QPushButton::clicked, &dlg, [&] {
    engine::ConnectOptions tmp = getForm();
    if (showAdvancedDialog(&dlg, tmp)) { adv = tmp; refreshSummary(); }
  });

  {
    engine::ConnectOptions initial;
    if (preset != nullptr) initial = *preset;
    setForm(initial);
  }
  sites->setCurrentItem(newSite);

  QObject::connect(sites, &QTreeWidget::currentItemChanged, &dlg, [&](QTreeWidgetItem * cur, QTreeWidgetItem *) {
    if (cur == nullptr) return;
    QString n = cur->data(0, Qt::UserRole).toString();
    engine::ConnectOptions o;
    if (n.isEmpty()) setForm(engine::ConnectOptions());          // "New Site": a clean form
    else if (loadSite(n, o)) setForm(o);
  });
  QObject::connect(sites, &QTreeWidget::itemDoubleClicked, &dlg, [&](QTreeWidgetItem * it, int) {
    if (it && !it->data(0, Qt::UserRole).toString().isEmpty()) dlg.accept();
  });

  // Bottom buttons, as in WinSCP: Manage (the selected site) on the left, Login / Close on the right.
  auto * btnRow = new QHBoxLayout;
  auto * manageBtn = new QPushButton("&Manage");
  auto * manageMenu = new QMenu(manageBtn);
  auto * actRename = manageMenu->addAction("&Rename\xE2\x80\xA6");
  auto * actDelete = manageMenu->addAction("&Delete");
  manageBtn->setMenu(manageMenu);
  btnRow->addWidget(manageBtn);
  auto currentSiteName = [&]() { return sites->currentItem() ? sites->currentItem()->data(0, Qt::UserRole).toString() : QString(); };
  auto selectSite = [&](const QString & name) {
    for (int i = 1; i < sites->topLevelItemCount(); ++i)
      if (sites->topLevelItem(i)->data(0, Qt::UserRole).toString() == name) sites->setCurrentItem(sites->topLevelItem(i));
  };
  QObject::connect(manageMenu, &QMenu::aboutToShow, &dlg, [&] {
    const bool saved = !currentSiteName().isEmpty();
    actRename->setEnabled(saved); actDelete->setEnabled(saved);
  });
  QObject::connect(actRename, &QAction::triggered, &dlg, [&] {
    const QString old = currentSiteName();
    if (old.isEmpty()) return;
    bool okIn = false;
    QString name = QInputDialog::getText(&dlg, "Rename site", "New site name:", QLineEdit::Normal, old, &okIn).trimmed();
    if (!okIn || name.isEmpty() || name == old) return;
    name.replace('/', '_');
    if (savedSiteNames().contains(name)) { appMessage(&dlg, "Rename site", QString("A site named \"%1\" already exists.").arg(name)); return; }
    engine::ConnectOptions o;
    if (!loadSite(old, o)) return;
    saveSite(name, o); deleteSite(old);
    reloadSites(); selectSite(name);
  });
  QObject::connect(saveBtn, &QPushButton::clicked, &dlg, [&] {
    engine::ConnectOptions o = getForm();
    QString def = sites->currentItem() ? sites->currentItem()->data(0, Qt::UserRole).toString() : QString();
    if (def.isEmpty()) def = u8(o.user) + "@" + u8(o.host);
    bool okIn = false;
    QString name = QInputDialog::getText(&dlg, "Save session", "Site name:", QLineEdit::Normal, def, &okIn).trimmed();
    if (!okIn || name.isEmpty()) return;
    name.replace('/', '_');                  // '/' would nest the QSettings group
    saveSite(name, o);
    reloadSites();
    selectSite(name);
  });
  QObject::connect(actDelete, &QAction::triggered, &dlg, [&] {
    auto * it = sites->currentItem();
    if (!it) return;
    QString n = it->data(0, Qt::UserRole).toString();
    if (n.isEmpty()) return;
    deleteSite(n);
    reloadSites();
    sites->setCurrentItem(newSite);
  });
  btnRow->addStretch(1);
  auto * loginBtn = new QPushButton("Login"); loginBtn->setDefault(true);
  auto * closeBtn = new QPushButton("Close");
  btnRow->addWidget(loginBtn);
  btnRow->addWidget(closeBtn);
  outer->addLayout(btnRow);

  QObject::connect(loginBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
  QObject::connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

  // Dev/test affordances (scripted runs only): select a saved site, or save the form as one.
  if (!qEnvironmentVariableIsEmpty("WINSCP_LOGIN_SELECT_SITE"))
    for (int i = 1; i < sites->topLevelItemCount(); ++i)
      if (sites->topLevelItem(i)->data(0, Qt::UserRole).toString() == qEnvironmentVariable("WINSCP_LOGIN_SELECT_SITE"))
        sites->setCurrentItem(sites->topLevelItem(i));
  if (!qEnvironmentVariableIsEmpty("WINSCP_LOGIN_OPEN_ADVANCED"))
    QTimer::singleShot(50, &dlg, [&] { advBtn->click(); });

  const int rc = execDialog(dlg, "login");
  if (!qEnvironmentVariableIsEmpty("WINSCP_LOGIN_SAVE_AS"))
  { saveSite(qEnvironmentVariable("WINSCP_LOGIN_SAVE_AS"), getForm()); }
  if (rc != QDialog::Accepted) return p;

  p.ok = true;
  p.o = getForm();
  return p;
}

//--- authentication prompt ---------------------------------------------------
bool showAuthPrompt(QWidget * parent, const engine::PromptRequest & rq, std::vector<std::string> & answers)
{
  answers.assign(rq.prompts.size(), std::string());

  // '&' marks a keyboard accelerator in the engine's texts ("&Password:"); show plain labels.
  auto plain = [](const std::string & s) { QString t = u8(s); t.replace("&&", "\x01"); t.remove('&'); t.replace("\x01", "&"); return t.trimmed(); };

  if (uiScripted() && qEnvironmentVariableIsEmpty("WINSCP_DIALOG_SHOTS"))
  {
    // Headless: answers come from WINSCP_PROMPT_ANSWERS, consumed across prompts.
    static QStringList queue = qEnvironmentVariable("WINSCP_PROMPT_ANSWERS").split('|', Qt::SkipEmptyParts);
    for (size_t i = 0; i < rq.prompts.size(); ++i)
    {
      fprintf(stderr, "[prompt] %s%s / %s\n", rq.forTunnel ? "(tunnel) " : "", plain(rq.name).toUtf8().constData(),
              plain(rq.prompts[i]).toUtf8().constData());
      if (queue.isEmpty()) return false;
      answers[i] = s8(queue.takeFirst());
    }
    return true;
  }

  QDialog d(parent);
  QString title = plain(rq.name);
  d.setWindowTitle(title.isEmpty() ? QStringLiteral("Authentication") : title);
  d.setMinimumWidth(420);
  auto * lay = new QVBoxLayout(&d);
  if (rq.forTunnel)
  {
    auto * tl = new QLabel("<b>SSH tunnel</b>"); lay->addWidget(tl);
  }
  if (!rq.instructions.empty())
  {
    auto * il = new QLabel(u8(rq.instructions));
    il->setWordWrap(true); il->setTextFormat(Qt::PlainText);
    lay->addWidget(il);
  }
  auto * form = new QFormLayout;
  std::vector<QLineEdit *> edits;
  for (size_t i = 0; i < rq.prompts.size(); ++i)
  {
    auto * e = new QLineEdit;
    if (!rq.echo[i]) e->setEchoMode(QLineEdit::Password);
    QString label = plain(rq.prompts[i]);
    auto * l = new QLabel(label.isEmpty() ? QStringLiteral("Response:") : label);
    l->setTextFormat(Qt::PlainText);       // server-supplied text is never interpreted as markup
    form->addRow(l, e);
    edits.push_back(e);
  }
  lay->addLayout(form);
  auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  lay->addWidget(bb);
  QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
  QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  if (!edits.empty()) edits[0]->setFocus();

  if (execDialog(d, "authprompt") != QDialog::Accepted) return false;
  for (size_t i = 0; i < edits.size(); ++i) answers[i] = s8(edits[i]->text());
  return true;
}
