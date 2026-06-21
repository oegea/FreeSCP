//---------------------------------------------------------------------------
// WinSCP native port — Qt 6 GUI, faithful to WinSCP's Commander interface.
//
// Skeleton mirrors WinSCP: an iconic Login dialog (sites tree + Session form) on startup, then
// the dual-pane Commander (local | remote) with per-panel address bars, the Name/Size/Changed/
// Rights/Owner columns, and the classic bottom function-key bar (F5 Copy / F6 Move / F7 Create
// Directory / F8 Delete / ...). Both panels talk to the ported engine through enginebridge.
//---------------------------------------------------------------------------
#include <QApplication>
#include <QMainWindow>
#include <QSplitter>
#include <QTableView>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QStatusBar>
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QAction>
#include <QKeyEvent>
#include <QMessageBox>
#include <QInputDialog>
#include <QComboBox>
#include <QString>
#include <QDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QPlainTextEdit>
#include <QDockWidget>
#include <QCheckBox>
#include <QMenu>
#include <QTimer>
#include <QSettings>
#include <QProgressDialog>
#include <functional>

#include "enginebridge.h"

static QString u8(const std::string & s) { return QString::fromUtf8(s.c_str()); }
static std::string s8(const QString & s) { return s.toUtf8().constData(); }

//===========================================================================
// Login dialog — faithful to WinSCP: a sites tree on the left, a "Session" form on the right
// (File protocol, Host name + Port, User name, Password), Tools/Manage + Login/Close buttons.
//===========================================================================
struct LoginParams
{
  bool ok = false;
  engine::Protocol protocol = engine::Protocol::Sftp;
  QString host, user, pass;
  int port = 22;
};

static LoginParams showLoginDialog(QWidget * parent)
{
  LoginParams p;

  QDialog dlg(parent);
  dlg.setWindowTitle("Login");
  dlg.resize(680, 420);

  auto * outer = new QVBoxLayout(&dlg);
  auto * top = new QHBoxLayout;
  outer->addLayout(top, 1);

  // Left: sites tree — "New Site" plus saved sessions (persisted in QSettings, the WinSCP
  // Site Manager experience: pick a saved server instead of retyping).
  auto * sites = new QTreeWidget;
  sites->setHeaderLabel("");
  sites->setMaximumWidth(220);
  sites->setMinimumWidth(180);
  auto * newSite = new QTreeWidgetItem(sites, QStringList("\xF0\x9F\x96\xA5  New Site"));
  top->addWidget(sites);

  QSettings settings("WinSCP-native-port", "WinSCP");
  auto reloadSites = [&] {
    while (sites->topLevelItemCount() > 1) delete sites->takeTopLevelItem(1);
    settings.beginGroup("sites");
    for (const QString & name : settings.childGroups())
    {
      auto * it = new QTreeWidgetItem(sites, QStringList("\xF0\x9F\x8C\x90  " + name));
      it->setData(0, Qt::UserRole, name);
    }
    settings.endGroup();
  };
  reloadSites();
  sites->setCurrentItem(newSite);

  // Right: Session group.
  auto * session = new QGroupBox("Session");
  auto * grid = new QGridLayout(session);
  auto * proto = new QComboBox;
  proto->addItem("SFTP"); proto->addItem("SCP"); proto->addItem("FTP");
  proto->addItem("WebDAV"); proto->addItem("Amazon S3");
  auto * host = new QLineEdit("127.0.0.1");
  auto * port = new QSpinBox; port->setRange(1, 65535); port->setValue(2222);
  auto * user = new QLineEdit("winscp");
  auto * pass = new QLineEdit("winscp123"); pass->setEchoMode(QLineEdit::Password);

  grid->addWidget(new QLabel("File protocol:"), 0, 0);
  grid->addWidget(proto, 0, 1, 1, 3);
  grid->addWidget(new QLabel("Host name:"), 1, 0);
  grid->addWidget(host, 1, 1);
  grid->addWidget(new QLabel("Port number:"), 1, 2);
  grid->addWidget(port, 1, 3);
  grid->addWidget(new QLabel("User name:"), 2, 0);
  grid->addWidget(user, 2, 1);
  grid->addWidget(new QLabel("Password:"), 2, 2);
  grid->addWidget(pass, 2, 3);
  grid->setRowStretch(3, 1);
  top->addWidget(session, 1);

  // Test-friendly: map protocol -> the local Docker test server's port.
  QObject::connect(proto, &QComboBox::currentIndexChanged, [&](int i) {
    bool ftp = (i == 2);
    int dp = (i == 3) ? 8086 : (i == 4) ? 9100 : ftp ? 21 : 2222;
    port->setValue(dp);
    // FTP not supported yet — disable Login is handled below via the protocol check.
  });

  // Load a saved site into the form.
  auto loadSite = [&](const QString & name) {
    settings.beginGroup("sites/" + name);
    proto->setCurrentIndex(settings.value("protocol", 0).toInt());
    host->setText(settings.value("host").toString());
    port->setValue(settings.value("port", 22).toInt());
    user->setText(settings.value("user").toString());
    pass->setText(settings.value("pass").toString());
    settings.endGroup();
  };
  QObject::connect(sites, &QTreeWidget::currentItemChanged, [&](QTreeWidgetItem * cur, QTreeWidgetItem *) {
    if (cur) { QString n = cur->data(0, Qt::UserRole).toString(); if (!n.isEmpty()) loadSite(n); }
  });
  QObject::connect(sites, &QTreeWidget::itemDoubleClicked, [&](QTreeWidgetItem * it, int) {
    if (it && !it->data(0, Qt::UserRole).toString().isEmpty()) dlg.accept();
  });

  // Bottom buttons: Tools / Save / Delete ... Login / Close (WinSCP layout).
  auto * btnRow = new QHBoxLayout;
  btnRow->addWidget(new QPushButton("Tools"));
  auto * saveBtn = new QPushButton("Save");
  auto * delBtn = new QPushButton("Delete");
  btnRow->addWidget(saveBtn);
  btnRow->addWidget(delBtn);
  QObject::connect(saveBtn, &QPushButton::clicked, [&]{
    bool okIn = false;
    QString def = user->text() + "@" + host->text();
    QString name = QInputDialog::getText(&dlg, "Save session", "Site name:", QLineEdit::Normal, def, &okIn);
    if (!okIn || name.isEmpty()) return;
    settings.beginGroup("sites/" + name);
    settings.setValue("protocol", proto->currentIndex());
    settings.setValue("host", host->text());
    settings.setValue("port", port->value());
    settings.setValue("user", user->text());
    settings.setValue("pass", pass->text());   // local test tool; stores password
    settings.endGroup();
    reloadSites();
  });
  QObject::connect(delBtn, &QPushButton::clicked, [&]{
    auto * it = sites->currentItem();
    if (!it) return;
    QString n = it->data(0, Qt::UserRole).toString();
    if (n.isEmpty()) return;
    settings.remove("sites/" + n);
    reloadSites();
  });
  btnRow->addStretch(1);
  auto * loginBtn = new QPushButton("Login"); loginBtn->setDefault(true);
  auto * closeBtn = new QPushButton("Close");
  btnRow->addWidget(loginBtn);
  btnRow->addWidget(closeBtn);
  outer->addLayout(btnRow);

  QObject::connect(loginBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
  QObject::connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted) return p;

  switch (proto->currentIndex())
  {
    case 1: p.protocol = engine::Protocol::Scp; break;
    case 3: p.protocol = engine::Protocol::WebDav; break;
    case 4: p.protocol = engine::Protocol::S3; break;
    case 2: // FTP — not supported yet
      QMessageBox::information(parent, "WinSCP", "FTP is not supported in this build yet.");
      return p;
    default: p.protocol = engine::Protocol::Sftp; break;
  }
  p.ok = true;
  p.host = host->text(); p.port = port->value();
  p.user = user->text(); p.pass = pass->text();
  return p;
}

//===========================================================================
// Properties dialog — WinSCP-style permissions grid (Owner/Group/Others x R/W/X) with a synced
// octal field. Returns the chosen octal string, or empty if cancelled.
//===========================================================================
static QString showPropertiesDialog(QWidget * parent, const QString & name, const QString & curOctal)
{
  QDialog dlg(parent);
  dlg.setWindowTitle(name + " — Properties");

  auto * outer = new QVBoxLayout(&dlg);
  auto * grp = new QGroupBox("Permissions");
  auto * g = new QGridLayout(grp);
  g->addWidget(new QLabel("R"), 0, 1, Qt::AlignHCenter);
  g->addWidget(new QLabel("W"), 0, 2, Qt::AlignHCenter);
  g->addWidget(new QLabel("X"), 0, 3, Qt::AlignHCenter);
  const char * rowNames[3] = { "Owner", "Group", "Others" };
  QCheckBox * cb[9];
  for (int row = 0; row < 3; ++row)
  {
    g->addWidget(new QLabel(rowNames[row]), row + 1, 0);
    for (int col = 0; col < 3; ++col)
    {
      cb[row * 3 + col] = new QCheckBox;
      g->addWidget(cb[row * 3 + col], row + 1, col + 1, Qt::AlignHCenter);
    }
  }
  outer->addWidget(grp);

  auto * octRow = new QHBoxLayout;
  octRow->addWidget(new QLabel("Octal:"));
  auto * octEdit = new QLineEdit;
  octRow->addWidget(octEdit);
  outer->addLayout(octRow);

  // seed from curOctal (last 3 digits)
  int initial = curOctal.right(3).toInt(nullptr, 8);
  for (int i = 0; i < 9; ++i) cb[i]->setChecked((initial >> (8 - i)) & 1);

  bool guard = false;
  auto syncOctalFromBoxes = [&] {
    if (guard) return; guard = true;
    int v = 0; for (int i = 0; i < 9; ++i) if (cb[i]->isChecked()) v |= (1 << (8 - i));
    octEdit->setText(QString("%1").arg(v, 3, 8, QChar('0')));
    guard = false;
  };
  auto syncBoxesFromOctal = [&] {
    if (guard) return; guard = true;
    int v = octEdit->text().toInt(nullptr, 8);
    for (int i = 0; i < 9; ++i) cb[i]->setChecked((v >> (8 - i)) & 1);
    guard = false;
  };
  for (int i = 0; i < 9; ++i) QObject::connect(cb[i], &QCheckBox::toggled, syncOctalFromBoxes);
  QObject::connect(octEdit, &QLineEdit::textEdited, syncBoxesFromOctal);
  syncOctalFromBoxes();

  auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  outer->addWidget(bb);
  QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted) return QString();
  return octEdit->text();
}

//===========================================================================
// File panel — a WinSCP-style pane: address bar + file list (Name/Size/Changed/Rights/Owner)
// + a per-panel status line. Local or remote, both via enginebridge.
//===========================================================================
class FilePanel : public QWidget
{
public:
  std::function<void()> onActivated;
  std::function<void()> onStatusChanged;
  std::function<void()> onHome;

  explicit FilePanel(QWidget * parent = nullptr) : QWidget(parent)
  {
    auto * layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Title bar (host/protocol or "Local").
    FHeader = new QLabel("Local");
    FHeader->setStyleSheet("padding:4px 6px; font-weight:bold; background:palette(midlight);");

    // Address bar: path + small Up/Refresh buttons (WinSCP-ish).
    auto * addr = new QWidget;
    auto * addrLay = new QHBoxLayout(addr);
    addrLay->setContentsMargins(2, 2, 2, 2);
    addrLay->setSpacing(2);
    auto * backBtn = new QToolButton; backBtn->setText("\xE2\x97\x80"); backBtn->setToolTip("Back");
    auto * fwdBtn  = new QToolButton; fwdBtn->setText("\xE2\x96\xB6");  fwdBtn->setToolTip("Forward");
    auto * upBtn = new QToolButton; upBtn->setText("\xE2\xAC\x86");  upBtn->setToolTip("Parent directory");
    auto * homeBtn = new QToolButton; homeBtn->setText("\xF0\x9F\x8F\xA0"); homeBtn->setToolTip("Home");
    auto * rfBtn = new QToolButton; rfBtn->setText("\xE2\x9F\xB3"); rfBtn->setToolTip("Refresh");
    FPathEdit = new QLineEdit;
    addrLay->addWidget(backBtn);
    addrLay->addWidget(fwdBtn);
    addrLay->addWidget(upBtn);
    addrLay->addWidget(homeBtn);
    addrLay->addWidget(FPathEdit, 1);
    addrLay->addWidget(rfBtn);

    FView = new QTableView;
    FModel = new QStandardItemModel(this);
    FView->setModel(FModel);
    FView->setSelectionBehavior(QAbstractItemView::SelectRows);
    FView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    FView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    FView->setSortingEnabled(false);
    FView->setShowGrid(false);
    FView->setAlternatingRowColors(true);
    FView->verticalHeader()->setVisible(false);
    FView->verticalHeader()->setDefaultSectionSize(20);
    FView->installEventFilter(this);

    FStatus = new QLabel;
    FStatus->setStyleSheet("padding:2px 6px; background:palette(window); border-top:1px solid palette(mid);");

    layout->addWidget(FHeader);
    layout->addWidget(addr);
    layout->addWidget(FView, 1);
    layout->addWidget(FStatus);

    QObject::connect(FView, &QTableView::doubleClicked,
                     [this](const QModelIndex & ix) { activate(ix.row()); });
    QObject::connect(FPathEdit, &QLineEdit::returnPressed,
                     [this] { navigate(FPathEdit->text()); });
    QObject::connect(upBtn, &QToolButton::clicked, [this] { goUp(); });
    QObject::connect(rfBtn, &QToolButton::clicked, [this] { refresh(); });
    QObject::connect(backBtn, &QToolButton::clicked, [this] { goBack(); });
    QObject::connect(fwdBtn, &QToolButton::clicked, [this] { goForward(); });
    QObject::connect(homeBtn, &QToolButton::clicked, [this] { if (onHome) onHome(); });
    QObject::connect(FView->selectionModel(), &QItemSelectionModel::selectionChanged,
                     [this] { if (onStatusChanged) onStatusChanged(); });
  }

  QString path() const { return FPath; }
  QTableView * view() const { return FView; }
  bool isRemote() const { return FRemote; }
  void setLocal() { FRemote = false; FHeader->setText("\xF0\x9F\x92\xBB  Local"); }
  void setRemote(const QString & label) { FRemote = true; FHeader->setText("\xF0\x9F\x8C\x90  " + label); }

  void setActive(bool a)
  {
    FActive = a;
    FHeader->setStyleSheet(QString("padding:4px 6px; font-weight:bold; color:%1; background:%2;")
                             .arg(a ? "palette(highlighted-text)" : "palette(window-text)")
                             .arg(a ? "palette(highlight)" : "palette(midlight)"));
  }

  void goBack()    { if (FHistIdx > 0) { FNav = true; navigate(FHistory[--FHistIdx]); FNav = false; } }
  void goForward() { if (FHistIdx + 1 < FHistory.size()) { FNav = true; navigate(FHistory[++FHistIdx]); FNav = false; } }

  void navigate(const QString & path)
  {
    if (!FNav)   // record history (truncate any forward entries)
    {
      while (FHistory.size() > FHistIdx + 1) FHistory.removeLast();
      if (FHistory.isEmpty() || FHistory.last() != path) { FHistory.append(path); FHistIdx = FHistory.size() - 1; }
    }
    FPath = path;
    FPathEdit->setText(path);
    FModel->clear();
    FModel->setHorizontalHeaderLabels({ "Name", "Size", "Changed", "Rights", "Owner" });
    FEntries = FRemote ? engine::listRemoteDir(s8(path)) : engine::listLocalDir(s8(path));
    FDirs = 0; FFiles = 0;
    for (const auto & e : FEntries)
    {
      QList<QStandardItem *> row;
      QString icon = e.isParent ? "\xE2\xA4\xB4 " : e.isDir ? "\xF0\x9F\x93\x81 " : "\xF0\x9F\x93\x84 ";
      row << new QStandardItem(icon + u8(e.name));
      auto * sz = new QStandardItem(e.isDir ? QString() : u8(engine::formatSize(e.size)));
      sz->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      row << sz;
      row << new QStandardItem(u8(e.modified));
      row << new QStandardItem(u8(e.rights));
      row << new QStandardItem(u8(e.owner));
      FModel->appendRow(row);
      if (e.isParent) continue;
      if (e.isDir) ++FDirs; else ++FFiles;
    }
    auto * hh = FView->horizontalHeader();
    hh->setStretchLastSection(false);
    hh->setSectionResizeMode(0, QHeaderView::Stretch);     // Name fills
    for (int c = 1; c <= 4; ++c) hh->setSectionResizeMode(c, QHeaderView::Interactive);
    FView->setColumnWidth(1, 90);    // Size
    FView->setColumnWidth(2, 150);   // Changed
    FView->setColumnWidth(3, 95);    // Rights
    FView->setColumnWidth(4, 120);   // Owner
    if (FModel->rowCount() > 0) FView->selectRow(0);
    if (onStatusChanged) onStatusChanged();
  }

  // Per-panel status: selection / totals (WinSCP shows selected of total).
  QString statusText() const
  {
    int sel = (int)selectedFiles().size();
    if (sel > 0) return QString("%1 of %2 file(s) selected").arg(sel).arg(FFiles);
    return QString("%1 director%2, %3 file(s)").arg(FDirs).arg(FDirs == 1 ? "y" : "ies").arg(FFiles);
  }

  QStringList selectedFiles() const
  {
    QStringList out;
    if (!FView->selectionModel()) return out;
    const auto rows = FView->selectionModel()->selectedRows();
    for (const auto & ix : rows)
    {
      int r = ix.row();
      if (r >= 0 && r < (int)FEntries.size() && !FEntries[r].isDir)
        out << u8(FEntries[r].name);
    }
    return out;
  }

  void setStatusLine(const QString & s) { FStatus->setText(s); }
  void refresh() { navigate(FPath); }
  void goUp() { navigate(u8(engine::parentDir(s8(FPath)))); }

protected:
  bool eventFilter(QObject *, QEvent * ev) override
  {
    if (ev->type() == QEvent::FocusIn && onActivated) onActivated();
    if (ev->type() == QEvent::KeyPress)
    {
      auto * ke = static_cast<QKeyEvent *>(ev);
      if (ke->key() == Qt::Key_Backspace) { goUp(); return true; }
      if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
      { activate(FView->currentIndex().row()); return true; }
    }
    return false;
  }

private:
  void activate(int row)
  {
    if (row < 0 || row >= static_cast<int>(FEntries.size())) return;
    const auto & e = FEntries[static_cast<size_t>(row)];
    if (!e.isDir) return;
    if (e.isParent) goUp();
    else navigate(u8(engine::joinPath(s8(FPath), e.name)));
  }

  QLabel * FHeader;
  QLineEdit * FPathEdit;
  QTableView * FView;
  QStandardItemModel * FModel;
  QLabel * FStatus;
  QString FPath;
  bool FRemote = false;
  bool FActive = false;
  bool FNav = false;
  int FDirs = 0, FFiles = 0;
  QStringList FHistory;
  int FHistIdx = -1;
  std::vector<engine::DirEntry> FEntries;
};

//===========================================================================

int main(int argc, char ** argv)
{
  QApplication app(argc, argv);
  app.setApplicationName("WinSCP");

  QMainWindow window;
  window.setWindowTitle("WinSCP");
  window.resize(1100, 720);

  // Main toolbar.
  auto * tb = window.addToolBar("Main");
  tb->setMovable(false);
  auto * actConnect = tb->addAction("\xF0\x9F\x94\x8C Login");
  tb->addSeparator();
  auto * actHome = tb->addAction("\xF0\x9F\x8F\xA0 Home");
  auto * actRefresh = tb->addAction("\xE2\x9F\xB3 Refresh");
  auto * actDisconnect = tb->addAction("\xE2\x9C\x96 Disconnect"); actDisconnect->setEnabled(false);

  // Panels.
  auto * splitter = new QSplitter(Qt::Horizontal);
  auto * left = new FilePanel;
  auto * right = new FilePanel;
  left->setLocal();
  right->setLocal();
  splitter->addWidget(left);
  splitter->addWidget(right);
  splitter->setSizes({ 550, 550 });

  auto * central = new QWidget;
  auto * centralLay = new QVBoxLayout(central);
  centralLay->setContentsMargins(0, 0, 0, 0);
  centralLay->setSpacing(0);
  centralLay->addWidget(splitter, 1);

  // Bottom function-key bar (the iconic WinSCP/Norton-Commander row).
  auto * fkeys = new QWidget;
  auto * fkLay = new QHBoxLayout(fkeys);
  fkLay->setContentsMargins(3, 3, 3, 3);
  fkLay->setSpacing(3);
  central->setLayout(centralLay);
  centralLay->addWidget(fkeys);
  window.setCentralWidget(central);

  FilePanel * active = left;

  auto updateStatuses = [&] {
    left->setStatusLine(left->statusText());
    right->setStatusLine(right->statusText());
    window.statusBar()->showMessage(active->path());
  };
  left->onStatusChanged = updateStatuses;
  right->onStatusChanged = updateStatuses;
  left->onActivated = [&] { active = left; left->setActive(true); right->setActive(false); updateStatuses(); };
  right->onActivated = [&] { active = right; right->setActive(true); left->setActive(false); updateStatuses(); };
  static QString remoteHome = "/";
  left->onHome  = [&] { left->navigate(u8(engine::homeDir())); };
  right->onHome = [&] { right->navigate(right->isRemote() ? remoteHome : u8(engine::homeDir())); };

  // Session log dock (see what the engine does — invaluable for testing).
  auto * logDock = new QDockWidget("Session log", &window);
  auto * logView = new QPlainTextEdit; logView->setReadOnly(true);
  logView->setMaximumBlockCount(2000);
  logDock->setWidget(logView);
  window.addDockWidget(Qt::BottomDockWidgetArea, logDock);
  logDock->hide();
  auto log = [&](const QString & s) { logView->appendPlainText(s); };

  //--- operations ---------------------------------------------------------
  auto doConnect = [&] {
    LoginParams lp = showLoginDialog(&window);
    if (!lp.ok) return;
    window.statusBar()->showMessage("Connecting\xE2\x80\xA6");
    log(QString("Connecting to %1:%2 \xE2\x80\xA6").arg(lp.host).arg(lp.port));
    QApplication::processEvents();
    engine::ConnectResult r = engine::connectSftp(s8(lp.host), lp.port, s8(lp.user), s8(lp.pass), lp.protocol);
    if (!r.ok)
    {
      log("FAILED: " + u8(r.error));
      logDock->show();
      QMessageBox::critical(&window, "Login", u8(r.error));
      window.statusBar()->showMessage("Connection failed");
      return;
    }
    const char * pn = lp.protocol == engine::Protocol::Scp ? "SCP"
                    : lp.protocol == engine::Protocol::WebDav ? "WebDAV"
                    : lp.protocol == engine::Protocol::S3 ? "S3" : "SFTP";
    right->setRemote(QString("%1@%2 \xE2\x80\x94 %3").arg(lp.user).arg(lp.host).arg(pn));
    remoteHome = u8(r.currentDir);
    right->navigate(u8(r.currentDir));
    right->onActivated();
    actDisconnect->setEnabled(true);
    window.setWindowTitle(QString("%1@%2 \xE2\x80\x94 WinSCP").arg(lp.user).arg(lp.host));
    log("Connected. Remote directory: " + u8(r.currentDir));
  };
  auto doDisconnect = [&] {
    engine::disconnectSftp();
    right->setLocal();
    right->navigate(u8(engine::homeDir()));
    actDisconnect->setEnabled(false);
    window.setWindowTitle("WinSCP");
    window.statusBar()->showMessage("Disconnected");
    log("Disconnected.");
  };

  auto doCopy = [&] {
    FilePanel * dst = (active == left) ? right : left;
    QStringList files = active->selectedFiles();
    if (files.isEmpty()) { window.statusBar()->showMessage("No files selected"); return; }
    if (active->isRemote() && dst->isRemote())
    { QMessageBox::information(&window, "Copy", "Remote-to-remote copy is not supported yet."); return; }
    // WinSCP-style transfer progress (per-file bar fed by the engine OnProgress sink).
    QProgressDialog prog(&window);
    prog.setWindowTitle(active->isRemote() ? "Download" : (dst->isRemote() ? "Upload" : "Copy"));
    prog.setMinimumDuration(0);
    prog.setRange(0, 100);
    prog.setMinimumWidth(420);
    engine::setProgressSink([&](const std::string & file, int pct) {
      prog.setLabelText(u8(file));
      prog.setValue(pct);
      QApplication::processEvents();
    });
    int ok = 0; std::string lastErr;
    for (const QString & f : files)
    {
      prog.setLabelText(f);
      QApplication::processEvents();
      std::string src = engine::joinPath(s8(active->path()), s8(f));
      bool r;
      if (!active->isRemote() && !dst->isRemote())
        r = engine::copyFile(src, engine::joinPath(s8(dst->path()), s8(f)));
      else if (!active->isRemote() && dst->isRemote())
        r = engine::uploadToRemote(src, s8(dst->path()), &lastErr);
      else
        r = engine::downloadFromRemote(src, s8(dst->path()), &lastErr);
      if (r) ++ok; else log("Copy failed: " + f + " — " + u8(lastErr));
    }
    engine::setProgressSink(nullptr);
    prog.close();
    dst->refresh();
    window.statusBar()->showMessage(QString("Copied %1/%2 file(s) to %3").arg(ok).arg(files.size()).arg(dst->path()));
  };
  auto doMkdir = [&] {
    bool okIn = false;
    QString name = QInputDialog::getText(&window, "Create folder", "New folder name:", QLineEdit::Normal, "", &okIn);
    if (!okIn || name.isEmpty()) return;
    std::string err;
    bool ok = active->isRemote() ? engine::remoteMakeDir(s8(name), &err)
                                 : engine::localMakeDir(s8(active->path()), s8(name), &err);
    active->refresh();
    window.statusBar()->showMessage(ok ? "Created " + name : "Create folder failed — " + u8(err));
  };
  auto doRename = [&] {
    QStringList sel = active->selectedFiles();
    if (sel.size() != 1) { window.statusBar()->showMessage("Select exactly one item to rename"); return; }
    bool okIn = false;
    QString nn = QInputDialog::getText(&window, "Rename", "New name:", QLineEdit::Normal, sel.first(), &okIn);
    if (!okIn || nn.isEmpty() || nn == sel.first()) return;
    std::string err;
    bool ok = active->isRemote() ? engine::remoteRename(s8(sel.first()), s8(nn), &err)
                                 : engine::localRename(s8(active->path()), s8(sel.first()), s8(nn), &err);
    active->refresh();
    window.statusBar()->showMessage(ok ? "Renamed to " + nn : "Rename failed — " + u8(err));
  };
  auto doDelete = [&] {
    QStringList sel = active->selectedFiles();
    if (sel.isEmpty()) { window.statusBar()->showMessage("No files selected"); return; }
    if (QMessageBox::question(&window, "Delete", QString("Delete %1 item(s)?").arg(sel.size())) != QMessageBox::Yes) return;
    int ok = 0; std::string err;
    for (const QString & f : sel)
      if (active->isRemote() ? engine::remoteDelete(s8(f), &err) : engine::localDelete(s8(active->path()), s8(f), &err)) ++ok;
    active->refresh();
    window.statusBar()->showMessage(QString("Deleted %1/%2 item(s)").arg(ok).arg(sel.size()));
  };
  auto doProps = [&] {
    QStringList sel = active->selectedFiles();
    if (sel.size() != 1) { window.statusBar()->showMessage("Select exactly one item"); return; }
    if (!active->isRemote()) { QMessageBox::information(&window, "Properties", "Properties (permissions) apply to remote files."); return; }
    std::string cur = engine::remoteFileOctal(s8(sel.first()));
    QString oct = showPropertiesDialog(&window, sel.first(), cur.empty() ? "644" : u8(cur));
    if (oct.isEmpty()) return;
    std::string err;
    bool ok = engine::remoteChmod(s8(sel.first()), s8(oct), &err);
    active->refresh();
    window.statusBar()->showMessage(ok ? "Set " + oct : "chmod failed — " + u8(err));
  };
  auto doQuit = [&] { window.close(); };

  // Right-click context menu on each panel (WinSCP-style).
  auto installContextMenu = [&](FilePanel * panel) {
    panel->view()->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(panel->view(), &QWidget::customContextMenuRequested, [&, panel](const QPoint & pos) {
      active = panel; panel->onActivated();
      QMenu menu;
      menu.addAction(active->isRemote() ? "Download (F5)" : "Upload / Copy (F5)", doCopy);
      menu.addAction("Rename (F2)", doRename);
      menu.addAction("Delete (F8)", doDelete);
      menu.addSeparator();
      menu.addAction("Create Directory (F7)", doMkdir);
      if (active->isRemote()) menu.addAction("Properties (F9)", doProps);
      menu.exec(panel->view()->viewport()->mapToGlobal(pos));
    });
  };
  installContextMenu(left);
  installContextMenu(right);

  // Menu bar (WinSCP layout; actions wired to the same ops as the toolbar/F-keys).
  {
    auto * mLocal = window.menuBar()->addMenu("&Local");
    mLocal->addAction("&Refresh", [&]{ left->refresh(); });
    mLocal->addAction("&Home", [&]{ left->navigate(u8(engine::homeDir())); });
    auto * mFiles = window.menuBar()->addMenu("&Files");
    mFiles->addAction("&Copy / Upload\tF5", doCopy);
    mFiles->addAction("&Rename\tF2", doRename);
    mFiles->addAction("&Delete\tF8", doDelete);
    mFiles->addAction("&Properties\tF9", doProps);
    mFiles->addSeparator();
    mFiles->addAction("Create &Directory\tF7", doMkdir);
    auto * mSession = window.menuBar()->addMenu("&Session");
    mSession->addAction("&Login\xE2\x80\xA6", doConnect);
    mSession->addAction("&Disconnect", doDisconnect);
    mSession->addSeparator();
    mSession->addAction("E&xit\tF10", doQuit);
    auto * mOptions = window.menuBar()->addMenu("&Options");
    mOptions->addAction("Session &log\tCtrl+L", [&]{ logDock->setVisible(!logDock->isVisible()); });
    auto * mRemote = window.menuBar()->addMenu("&Remote");
    mRemote->addAction("&Refresh", [&]{ if (right->isRemote()) right->refresh(); });
    auto * mHelp = window.menuBar()->addMenu("&Help");
    mHelp->addAction("&About WinSCP (native port)", [&]{
      QMessageBox::about(&window, "About",
        "WinSCP — native macOS port\n\nPorted engine (RTL compat + platform layer) + Qt 6 GUI.\n"
        "Protocols: SFTP, SCP, WebDAV, S3."); });
  }

  //--- function-key bar buttons ------------------------------------------
  auto addFKey = [&](const QString & text, const std::function<void()> & fn) {
    auto * b = new QPushButton(text);
    b->setFocusPolicy(Qt::NoFocus);
    QObject::connect(b, &QPushButton::clicked, fn);
    fkLay->addWidget(b, 1);
    return b;
  };
  addFKey("F2 Rename", doRename);
  addFKey("F5 Copy", doCopy);
  addFKey("F6 Move", [&]{ QMessageBox::information(&window, "Move", "Move (F6) not implemented yet."); });
  addFKey("F7 Create Directory", doMkdir);
  addFKey("F8 Delete", doDelete);
  addFKey("F9 Properties", doProps);
  addFKey("F10 Quit", doQuit);

  //--- toolbar / shortcuts ------------------------------------------------
  QObject::connect(actConnect, &QAction::triggered, doConnect);
  QObject::connect(actDisconnect, &QAction::triggered, doDisconnect);
  QObject::connect(actHome, &QAction::triggered, [&] { if (!active->isRemote()) active->navigate(u8(engine::homeDir())); });
  QObject::connect(actRefresh, &QAction::triggered, [&] { active->refresh(); });

  auto shortcut = [&](QKeySequence k, const std::function<void()> & fn) {
    auto * a = new QAction(&window); a->setShortcut(k);
    QObject::connect(a, &QAction::triggered, fn); window.addAction(a);
  };
  shortcut(Qt::Key_F2, doRename);
  shortcut(Qt::Key_F5, doCopy);
  shortcut(Qt::Key_F7, doMkdir);
  shortcut(Qt::Key_F8, doDelete);
  shortcut(QKeySequence::Delete, doDelete);
  shortcut(Qt::Key_F9, doProps);
  shortcut(Qt::Key_F10, doQuit);
  // toggle the session log
  { auto * a = new QAction(&window); a->setShortcut(Qt::CTRL | Qt::Key_L);
    QObject::connect(a, &QAction::triggered, [&]{ logDock->setVisible(!logDock->isVisible()); });
    window.addAction(a); }

  left->setActive(true);
  left->navigate(u8(engine::homeDir()));
  right->navigate(u8(engine::homeDir()));
  updateStatuses();

  window.statusBar()->showMessage(QString::fromUtf8(engine::banner().c_str()));
  window.show();

  // Dev affordance: WINSCP_SHOT=path renders the main window to a PNG and quits (UI verification).
  // WINSCP_AUTOCONNECT=host:port:user:pass:proto connects first (proto: sftp/scp/dav/s3).
  if (!qEnvironmentVariableIsEmpty("WINSCP_SHOT"))
    QMetaObject::invokeMethod(&window, [&]{
      QString ac = qEnvironmentVariable("WINSCP_AUTOCONNECT");
      if (!ac.isEmpty()) {
        QStringList p = ac.split(':');
        engine::Protocol pr = p[4]=="scp"?engine::Protocol::Scp : p[4]=="dav"?engine::Protocol::WebDav
                            : p[4]=="s3"?engine::Protocol::S3 : engine::Protocol::Sftp;
        auto r = engine::connectSftp(s8(p[0]), p[1].toInt(), s8(p[2]), s8(p[3]), pr);
        if (r.ok) { right->setRemote(QString("%1@%2").arg(p[2]).arg(p[0])); right->navigate(u8(r.currentDir)); }
      }
      window.grab().save(qEnvironmentVariable("WINSCP_SHOT"));
      app.quit();
    }, Qt::QueuedConnection);

  // Offer the Login dialog on startup (like WinSCP), unless launched headless for a smoke test.
  if (qEnvironmentVariableIsEmpty("WINSCP_NO_AUTOLOGIN"))
    QMetaObject::invokeMethod(&window, [&]{ doConnect(); }, Qt::QueuedConnection);

  return app.exec();
}
