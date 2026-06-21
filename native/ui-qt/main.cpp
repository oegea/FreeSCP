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
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMessageBox>
#include <QInputDialog>
#include <QComboBox>
#include <QTabWidget>
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
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QHash>
#include <QIcon>
#include <QFont>
#include <QStyle>
#include <algorithm>
#include <functional>
#include <thread>
#include <atomic>

#include "enginebridge.h"

static QString u8(const std::string & s) { return QString::fromUtf8(s.c_str()); }
static std::string s8(const QString & s) { return s.toUtf8().constData(); }

static bool gShowHidden = true;        // WinSCP "Show hidden files" toggle (dotfiles)
static bool gConfirmDelete = true;     // confirm before deleting
static bool gConfirmOverwrite = true;  // confirm before overwriting (stored; enforcement TBD)
static int  gParallelMax = 2;          // max concurrent connections for queue parallelism (1-4)
static bool gAltColors = true;         // alternating row colors in panels
static std::atomic<bool> gTransferRunning{false};  // a background transfer batch is in flight

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
  bool tls = false;
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
  auto * enc = new QComboBox; enc->addItem("No encryption"); enc->addItem("TLS/SSL");
  auto * encLabel = new QLabel("Encryption:");
  grid->addWidget(encLabel, 3, 0);
  grid->addWidget(enc, 3, 1);
  grid->setRowStretch(4, 1);
  top->addWidget(session, 1);
  // Encryption only applies to WebDAV/S3 (HTTP-based); hide for SFTP/SCP/FTP.
  auto updateEnc = [&](int i) { bool http = (i == 3 || i == 4); encLabel->setVisible(http); enc->setVisible(http); };
  QObject::connect(proto, &QComboBox::currentIndexChanged, updateEnc);
  updateEnc(proto->currentIndex());

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
    case 2: p.protocol = engine::Protocol::Ftp; break;   // FileZilla backend (Phase 8)
    case 3: p.protocol = engine::Protocol::WebDav; break;
    case 4: p.protocol = engine::Protocol::S3; break;
    default: p.protocol = engine::Protocol::Sftp; break;
  }
  p.ok = true;
  p.host = host->text(); p.port = port->value();
  p.user = user->text(); p.pass = pass->text();
  p.tls = (enc->currentIndex() == 1);
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
  std::function<void()> onFileOpen;   // file (not dir) activated
  std::function<void(const QString &)> onEnterDir;  // user entered a subdir (for sync browsing)
  std::function<void()> onLeaveDir;                 // user went up (for sync browsing)
  std::function<void()> onSwitchPanel;              // Tab pressed -> focus the other panel
  std::function<void(FilePanel *, const QStringList &)> onDropFiles;  // files dropped from another panel
  std::function<void(FilePanel *, const QStringList &)> onDropExternal; // local files dropped from Finder
  static FilePanel * s_dragSource;                  // panel a drag originated from

  // Mirror operations (used by synchronized browsing; navigate directly, no callbacks -> no loop).
  void enterSubdir(const QString & name)
  {
    for (const auto & e : FEntries)
      if (e.isDir && !e.isParent && u8(e.name) == name) { navigate(u8(engine::joinPath(s8(FPath), e.name))); return; }
  }
  void upOne() { navigate(u8(engine::parentDir(s8(FPath)))); }

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
    FView->viewport()->installEventFilter(this);     // drag start + drop target
    FView->viewport()->setAcceptDrops(true);

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
    // Columns + header set up ONCE here (not per-render) so they're user-resizable and widths persist.
    FModel->setColumnCount(5);
    FModel->setHorizontalHeaderLabels({ "Name", "Size", "Changed", "Rights", "Owner" });
    auto * hh = FView->horizontalHeader();
    hh->setSectionsClickable(true);
    hh->setSectionsMovable(false);
    hh->setStretchLastSection(true);                 // last column fills leftover space
    for (int c = 0; c < 5; ++c) hh->setSectionResizeMode(c, QHeaderView::Interactive);  // all resizable
    FView->setColumnWidth(0, 240); FView->setColumnWidth(1, 90);
    FView->setColumnWidth(2, 150); FView->setColumnWidth(3, 95);
    QObject::connect(hh, &QHeaderView::sectionClicked, [this](int col) { sortByColumn(col); });
  }

  QString path() const { return FPath; }
  QTableView * view() const { return FView; }
  bool isRemote() const { return FRemote; }
  // Size (bytes) of a listed entry by name, for the transfer queue; 0 if unknown/dir.
  qint64 sizeOf(const QString & name) const {
    std::string n = s8(name);
    for (const auto & e : FEntries) if (!e.isDir && e.name == n) return (qint64)e.size;
    return 0;
  }
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
    if (FRemote && gTransferRunning.load()) return;   // engine busy with a transfer batch
    if (!FNav)   // record history (truncate any forward entries)
    {
      while (FHistory.size() > FHistIdx + 1) FHistory.removeLast();
      if (FHistory.isEmpty() || FHistory.last() != path) { FHistory.append(path); FHistIdx = FHistory.size() - 1; }
    }
    FPath = path;
    FPathEdit->setText(path);
    FEntries = FRemote ? engine::listRemoteDir(s8(path)) : engine::listLocalDir(s8(path));
    if (!gShowHidden)
      FEntries.erase(std::remove_if(FEntries.begin(), FEntries.end(),
        [](const engine::DirEntry & e) { return !e.isParent && !e.name.empty() && e.name[0] == '.'; }),
        FEntries.end());
    FDirs = 0; FFiles = 0;
    for (const auto & e : FEntries) { if (e.isParent) continue; if (e.isDir) ++FDirs; else ++FFiles; }
    sortEntries();
    renderEntries();
    if (onStatusChanged) onStatusChanged();
  }

  // Sort FEntries by the active column ("." parent always first), then dirs-first within the
  // chosen key (WinSCP groups folders), respecting ascending/descending.
  void sortEntries()
  {
    std::stable_sort(FEntries.begin(), FEntries.end(), [this](const engine::DirEntry & a, const engine::DirEntry & b) {
      if (a.isParent != b.isParent) return a.isParent > b.isParent;     // ".." first
      if (a.isDir != b.isDir) return a.isDir > b.isDir;                 // dirs before files
      int c = 0;
      switch (FSortCol)
      {
        case 1: c = (a.size < b.size) ? -1 : (a.size > b.size) ? 1 : 0; break;   // Size
        case 2: c = QString::compare(u8(a.modified), u8(b.modified)); break;     // Changed
        case 3: c = QString::compare(u8(a.rights), u8(b.rights)); break;         // Rights
        case 4: c = QString::compare(u8(a.owner), u8(b.owner)); break;           // Owner
        default: c = QString::compare(u8(a.name), u8(b.name), Qt::CaseInsensitive); break;  // Name
      }
      return FSortAsc ? (c < 0) : (c > 0);
    });
  }

  void renderEntries()
  {
    // Remove only the rows (NOT clear()) so the columns, header sections and the user's column
    // widths survive a navigation. Sort arrow goes on the header text via setHeaderData.
    if (FModel->rowCount() > 0) FModel->removeRows(0, FModel->rowCount());
    static const char * heads[5] = { "Name", "Size", "Changed", "Rights", "Owner" };
    for (int i = 0; i < 5; ++i) {
      QString h = heads[i]; if (i == FSortCol) h += FSortAsc ? "  \xE2\x96\xB2" : "  \xE2\x96\xBC";
      FModel->setHeaderData(i, Qt::Horizontal, h);
    }
    for (const auto & e : FEntries)
    {
      QList<QStandardItem *> row;
      auto * nameItem = new QStandardItem(u8(e.name));
      nameItem->setIcon(iconFor(e));               // native folder / file-type icon
      row << nameItem;
      auto * sz = new QStandardItem(e.isDir ? QString() : u8(engine::formatSize(e.size)));
      sz->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      row << sz;
      row << new QStandardItem(u8(e.modified));
      row << new QStandardItem(u8(e.rights));
      row << new QStandardItem(u8(e.owner));
      FModel->appendRow(row);
    }
    if (FModel->rowCount() > 0) FView->selectRow(0);
  }

  void sortByColumn(int col)
  {
    if (col == FSortCol) FSortAsc = !FSortAsc; else { FSortCol = col; FSortAsc = true; }
    sortEntries();
    renderEntries();
    if (onStatusChanged) onStatusChanged();
  }

  // Per-panel status: selection / totals (WinSCP shows selected of total).
  QString statusText() const
  {
    int sel = (int)selectedItems().size();
    if (sel > 0) return QString("%1 of %2 item(s) selected").arg(sel).arg(FFiles + FDirs);
    return QString("%1 director%2, %3 file(s)").arg(FDirs).arg(FDirs == 1 ? "y" : "ies").arg(FFiles);
  }

  // Selected real files only (excludes dirs + "..").
  QStringList selectedFiles() const { return selected(false); }
  // Selected items including directories (excludes ".."); used by copy/move/delete which the
  // engine handles recursively for folders.
  QStringList selectedItems() const { return selected(true); }

  QStringList selected(bool includeDirs) const
  {
    QStringList out;
    if (!FView->selectionModel()) return out;
    const auto rows = FView->selectionModel()->selectedRows();
    for (const auto & ix : rows)
    {
      int r = ix.row();
      if (r < 0 || r >= (int)FEntries.size() || FEntries[r].isParent) continue;
      if (FEntries[r].isDir && !includeDirs) continue;
      out << u8(FEntries[r].name);
    }
    return out;
  }

  void setStatusLine(const QString & s) { FStatus->setText(s); }
  // Native icon for an entry: folder icon for dirs, "up" for "..", file-type icon (by extension) for
  // files. Cached per extension so listing stays fast.
  QIcon iconFor(const engine::DirEntry & e) const {
    static QFileIconProvider prov;
    static QIcon folder = prov.icon(QFileIconProvider::Folder);
    static QIcon upIcon = qApp->style()->standardIcon(QStyle::SP_FileDialogToParent);
    static QIcon genericFile = prov.icon(QFileIconProvider::File);
    static QHash<QString, QIcon> byExt;
    if (e.isParent) return upIcon;
    if (e.isDir) return folder;
    QString n = u8(e.name); int dot = n.lastIndexOf('.');
    QString ext = (dot > 0) ? n.mid(dot + 1).toLower() : QString();
    if (ext.isEmpty()) return genericFile;
    auto it = byExt.find(ext);
    if (it != byExt.end()) return it.value();
    QIcon ic = prov.icon(QFileInfo("x." + ext));   // type icon by extension
    if (ic.isNull()) ic = genericFile;
    byExt.insert(ext, ic);
    return ic;
  }
  void refresh() { navigate(FPath); }
  // Norton-style: toggle selection of the current row, then move down one.
  void toggleSelectAndAdvance() {
    int row = FView->currentIndex().row();
    if (row < 0) return;
    QItemSelectionModel * sm = FView->selectionModel();
    QModelIndex ix = FModel->index(row, 0);
    sm->select(QItemSelection(FModel->index(row, 0), FModel->index(row, FModel->columnCount() - 1)),
               QItemSelectionModel::Toggle);
    int next = (row + 1 < FModel->rowCount()) ? row + 1 : row;
    FView->setCurrentIndex(FModel->index(next, 0));
  }
  void goUp() { navigate(u8(engine::parentDir(s8(FPath)))); if (onLeaveDir) onLeaveDir(); }

protected:
  bool eventFilter(QObject * obj, QEvent * ev) override
  {
    if (ev->type() == QEvent::FocusIn && onActivated) onActivated();
    if (ev->type() == QEvent::KeyPress)
    {
      auto * ke = static_cast<QKeyEvent *>(ev);
      if (ke->key() == Qt::Key_Backspace) { goUp(); return true; }
      if (ke->key() == Qt::Key_Tab) { if (onSwitchPanel) onSwitchPanel(); return true; }
      if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
      { activate(FView->currentIndex().row()); return true; }
    }
    // --- drag & drop between panels ---
    if (obj == FView->viewport())
    {
      static const char * kFmt = "application/x-winscp-files";
      if (ev->type() == QEvent::MouseButtonPress)
      { auto * me = static_cast<QMouseEvent *>(ev); if (me->button() == Qt::LeftButton) FDragStart = me->pos(); }
      else if (ev->type() == QEvent::MouseMove)
      {
        auto * me = static_cast<QMouseEvent *>(ev);
        if ((me->buttons() & Qt::LeftButton) &&
            (me->pos() - FDragStart).manhattanLength() >= QApplication::startDragDistance() &&
            !selectedItems().isEmpty())
        {
          s_dragSource = this;
          auto * mime = new QMimeData; mime->setData(kFmt, "1");
          auto * drag = new QDrag(this); drag->setMimeData(mime);
          drag->exec(Qt::CopyAction);
          s_dragSource = nullptr;
          return true;
        }
      }
      else if (ev->type() == QEvent::DragEnter || ev->type() == QEvent::DragMove)
      {
        auto * de = static_cast<QDragMoveEvent *>(ev);
        bool internal = de->mimeData()->hasFormat(kFmt) && s_dragSource && s_dragSource != this;
        bool external = de->mimeData()->hasUrls();   // files from Finder/Nautilus
        if (internal || external) { de->acceptProposedAction(); return true; }
      }
      else if (ev->type() == QEvent::Drop)
      {
        auto * de = static_cast<QDropEvent *>(ev);
        if (de->mimeData()->hasFormat(kFmt) && s_dragSource && s_dragSource != this)
        { FilePanel * src = s_dragSource; if (onDropFiles) onDropFiles(src, src->selectedItems()); de->acceptProposedAction(); return true; }
        if (de->mimeData()->hasUrls())   // external (Finder) -> import the local files into this panel
        {
          QStringList paths;
          for (const QUrl & u : de->mimeData()->urls()) if (u.isLocalFile()) paths << u.toLocalFile();
          if (!paths.isEmpty() && onDropExternal) { onDropExternal(this, paths); de->acceptProposedAction(); return true; }
        }
      }
    }
    return false;
  }

private:
  void activate(int row)
  {
    if (row < 0 || row >= static_cast<int>(FEntries.size())) return;
    const auto & e = FEntries[static_cast<size_t>(row)];
    if (!e.isDir) { if (onFileOpen) onFileOpen(); return; }
    if (e.isParent) { goUp(); }
    else { QString nm = u8(e.name); navigate(u8(engine::joinPath(s8(FPath), e.name))); if (onEnterDir) onEnterDir(nm); }
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
  int FSortCol = 0; bool FSortAsc = true;
  QPoint FDragStart;
  QStringList FHistory;
  int FHistIdx = -1;
  std::vector<engine::DirEntry> FEntries;
};
FilePanel * FilePanel::s_dragSource = nullptr;

//===========================================================================

int main(int argc, char ** argv)
{
  QApplication app(argc, argv);
  app.setApplicationName("WinSCP");
  { QSettings s("WinSCP-native-port", "WinSCP");
    gShowHidden = s.value("prefs/showHidden", true).toBool();
    gConfirmDelete = s.value("prefs/confirmDelete", true).toBool();
    gConfirmOverwrite = s.value("prefs/confirmOverwrite", true).toBool();
    gParallelMax = s.value("prefs/parallelMax", 2).toInt();
    gAltColors = s.value("prefs/altColors", true).toBool(); }

  QMainWindow window;
  window.setWindowTitle("WinSCP");
  window.resize(1100, 720);
  { QSettings s("WinSCP-native-port", "WinSCP");
    if (s.contains("win/geometry")) window.restoreGeometry(s.value("win/geometry").toByteArray()); }

  // Main toolbar (native icons, WinSCP-style text-beside-icon).
  auto * tb = window.addToolBar("Main");
  tb->setMovable(false);
  tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  auto ic = [&](QStyle::StandardPixmap sp) { return window.style()->standardIcon(sp); };
  auto * actConnect = tb->addAction(ic(QStyle::SP_DialogOpenButton), "Login");
  tb->addSeparator();
  auto * actHome = tb->addAction(ic(QStyle::SP_DirHomeIcon), "Home");
  auto * actRefresh = tb->addAction(ic(QStyle::SP_BrowserReload), "Refresh");
  auto * actDisconnect = tb->addAction(ic(QStyle::SP_DialogCloseButton), "Disconnect"); actDisconnect->setEnabled(false);
  tb->addSeparator();
  auto * actSync = tb->addAction(ic(QStyle::SP_FileDialogContentsView), "Sync browsing"); actSync->setCheckable(true);

  // Panels.
  auto * splitter = new QSplitter(Qt::Horizontal);
  auto * left = new FilePanel;
  auto * right = new FilePanel;
  left->setLocal();
  right->setLocal();
  splitter->addWidget(left);
  splitter->addWidget(right);
  { QSettings s("WinSCP-native-port", "WinSCP");
    if (s.contains("win/splitter")) splitter->restoreState(s.value("win/splitter").toByteArray());
    else splitter->setSizes({ 550, 550 }); }

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
  // Synchronized browsing: entering/leaving a dir in one panel mirrors to the other.
  left->onEnterDir  = [&](const QString & n) { if (actSync->isChecked()) right->enterSubdir(n); };
  left->onLeaveDir  = [&] { if (actSync->isChecked()) right->upOne(); };
  right->onEnterDir = [&](const QString & n) { if (actSync->isChecked()) left->enterSubdir(n); };
  right->onLeaveDir = [&] { if (actSync->isChecked()) left->upOne(); };
  left->onSwitchPanel  = [&] { right->view()->setFocus(); };
  right->onSwitchPanel = [&] { left->view()->setFocus(); };

  // Session log dock (see what the engine does — invaluable for testing).
  auto * logDock = new QDockWidget("Session log", &window);
  auto * logView = new QPlainTextEdit; logView->setReadOnly(true);
  logView->setMaximumBlockCount(2000);
  logDock->setWidget(logView);
  window.addDockWidget(Qt::BottomDockWidgetArea, logDock);
  logDock->hide();
  auto log = [&](const QString & s) { logView->appendPlainText(s); };

  // Transfer queue dock (WinSCP-style): a list of transfers with live progress. Processed
  // sequentially on the UI thread (processEvents keeps it responsive) — visible like WinSCP's queue.
  auto * queueDock = new QDockWidget("Transfer queue", &window);
  auto * queueWidget = new QWidget;
  auto * queueLay = new QVBoxLayout(queueWidget);
  queueLay->setContentsMargins(0, 0, 0, 0); queueLay->setSpacing(2);
  auto * queueBtns = new QHBoxLayout; queueBtns->setContentsMargins(3, 2, 3, 0);
  auto * btnCancel = new QPushButton("Cancel"); btnCancel->setEnabled(false);
  auto * btnClear = new QPushButton("Clear finished");
  queueBtns->addWidget(btnCancel); queueBtns->addWidget(btnClear); queueBtns->addStretch(1);
  auto * queueView = new QTableView;
  auto * queueModel = new QStandardItemModel(0, 7, &window);
  queueModel->setHorizontalHeaderLabels({ "Operation", "File", "Size", "Progress", "Speed", "Time left", "Status" });
  queueView->setModel(queueModel);
  queueView->verticalHeader()->setVisible(false);
  queueView->verticalHeader()->setDefaultSectionSize(20);   // compact rows (match the panels)
  queueView->setShowGrid(false);
  queueView->setAlternatingRowColors(true);
  queueView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  queueView->horizontalHeader()->setStretchLastSection(true);
  queueView->setColumnWidth(1, 200);
  queueLay->addLayout(queueBtns); queueLay->addWidget(queueView);
  queueDock->setWidget(queueWidget);
  window.addDockWidget(Qt::BottomDockWidgetArea, queueDock);
  queueDock->hide();

  auto fmtSpeed = [](qint64 cps) -> QString { return cps > 0 ? u8(engine::formatSize(cps)) + "/s" : QString(); };
  auto fmtEta = [](qint64 secs) -> QString {
    if (secs <= 0) return QString();
    return QString::asprintf("%lld:%02lld", (long long)(secs / 60), (long long)(secs % 60));
  };
  auto queueAdd = [&](const QString & op, const QString & file, qint64 size) -> int {
    QList<QStandardItem *> row;
    row << new QStandardItem(op) << new QStandardItem(file)
        << new QStandardItem(size > 0 ? u8(engine::formatSize(size)) : QString())
        << new QStandardItem("0%") << new QStandardItem() << new QStandardItem() << new QStandardItem("queued");
    queueModel->appendRow(row);
    return queueModel->rowCount() - 1;
  };
  auto queueCell = [&](int r, int c, const QString & v) { if (r >= 0 && r < queueModel->rowCount()) queueModel->item(r, c)->setText(v); };
  auto queueStatus = [&](int r, const QString & s) { queueCell(r, 6, s); };

  // Cancel flag for the currently-running batch (Cancel button -> sink returns true -> engine aborts).
  auto currentCancel = std::make_shared<std::shared_ptr<std::atomic<bool>>>();
  QObject::connect(btnCancel, &QPushButton::clicked, [&, currentCancel]{
    if (*currentCancel) (*currentCancel)->store(true);
    window.statusBar()->showMessage("Cancelling\xE2\x80\xA6");
  });
  QObject::connect(btnClear, &QPushButton::clicked, [&]{
    for (int r = queueModel->rowCount() - 1; r >= 0; --r) {
      QString st = queueModel->item(r, 6)->text();
      if (st == "done" || st.startsWith("failed") || st.startsWith("cancelled")) queueModel->removeRow(r);
    }
  });

  auto busy = [&]() -> bool {
    if (gTransferRunning.load()) { window.statusBar()->showMessage("Transfer in progress\xE2\x80\xA6 please wait"); return true; }
    return false;
  };

  // Background transfer: runs the batch on a worker thread (UI never freezes); the queue dock updates
  // live with %/speed/time-left (marshaled to the UI thread). Cancel aborts via the engine progress
  // sink. Single-connection/non-thread-safe engine -> serialized by the bridge mutex + gTransferRunning
  // gating remote UI actions for the batch (local browsing stays responsive).
  auto startTransfer = [&](FilePanel * from, FilePanel * to, QStringList files, bool isMove) {
    if (files.isEmpty()) { window.statusBar()->showMessage("No files selected"); return; }
    if (from->isRemote() && to->isRemote())
    { QMessageBox::information(&window, "Transfer", "Remote-to-remote is not supported yet."); return; }
    if (busy()) return;
    const bool srcRemote = from->isRemote(), dstRemote = to->isRemote();
    const std::string srcDir = s8(from->path()), dstDir = s8(to->path());
    const QString op = srcRemote ? "Download" : (dstRemote ? "Upload" : "Copy");
    queueDock->show();
    std::vector<int> rows; std::vector<std::string> names;
    for (const QString & f : files) { rows.push_back(queueAdd(isMove ? "Move" : op, f, from->sizeOf(f))); names.push_back(s8(f)); }
    gTransferRunning = true;
    btnCancel->setEnabled(true);
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    *currentCancel = cancel;

    // Parallelism: a plain remote copy of >1 files uses up to 2 extra connections (each its own
    // worker). Moves, local copies and single files stay serial. Falls back to serial if the pool
    // can't open. Per-worker progress -> that worker's current queue row.
    const bool wantParallel = (srcRemote != dstRemote) && !isMove && names.size() > 1 && engine::parallelSupported();
    std::vector<int> handles;
    if (wantParallel)
    {
      int want = (int)std::min<size_t>((size_t)std::max(1, gParallelMax), names.size());
      for (int k = 0; k < want; ++k) { int h = engine::openParallelConnection(nullptr); if (h > 0) handles.push_back(h); }
      if (handles.size() < 2) { engine::closeParallelConnections(); handles.clear(); }  // need >=2 to be worth it
    }
    const int nWorkers = handles.empty() ? 1 : (int)handles.size();

    auto markActive = [&, rows](int rowVal) { QMetaObject::invokeMethod(&window, [&, rowVal]{ queueStatus(rowVal, "active"); }, Qt::QueuedConnection); };
    auto markDone = [&](int rowVal, const QString & status, bool okRow) {
      QMetaObject::invokeMethod(&window, [&, rowVal, status, okRow]{
        queueStatus(rowVal, status); if (okRow) { queueCell(rowVal, 3, "100%"); queueCell(rowVal, 4, QString()); queueCell(rowVal, 5, QString()); }
      }, Qt::QueuedConnection);
    };
    auto progressFor = [&, cancel](int rowVal, const engine::TransferProgress & tp) -> bool {
      int pct = tp.total > 0 ? (int)(tp.transferred * 100 / tp.total) : 0;
      qint64 cps = tp.cps; qint64 eta = (cps > 0 && tp.total > tp.transferred) ? (tp.total - tp.transferred) / cps : 0;
      QMetaObject::invokeMethod(&window, [&, rowVal, pct, cps, eta]{
        queueCell(rowVal, 3, QString("%1%").arg(pct)); queueCell(rowVal, 4, fmtSpeed(cps)); queueCell(rowVal, 5, fmtEta(eta));
      }, Qt::QueuedConnection);
      return cancel->load();
    };

    auto nextIdx = std::make_shared<std::atomic<size_t>>(0);
    auto okCount = std::make_shared<std::atomic<int>>(0);

    // Coordinator thread: spawn workers, join, then marshal completion (keeps the UI thread free).
    std::thread([&, rows, names, srcDir, dstDir, srcRemote, dstRemote, isMove, from, to, cancel, handles, nWorkers, nextIdx, okCount, markActive, markDone, progressFor]{
      auto worker = [&](int handle) {   // handle 0 = primary (serial), >=1 = pool connection
        auto curRow = std::make_shared<std::atomic<int>>(-1);
        if (handle == 0) engine::setProgressSink([&, curRow](const engine::TransferProgress & tp){ return progressFor(curRow->load(), tp); });
        else engine::setProgressSinkVia(handle, [&, curRow](const engine::TransferProgress & tp){ return progressFor(curRow->load(), tp); });
        for (;;)
        {
          size_t i = nextIdx->fetch_add(1);
          if (i >= names.size()) break;
          int rowVal = rows[i];
          if (cancel->load()) { markDone(rowVal, "cancelled", false); continue; }
          curRow->store(rowVal); markActive(rowVal);
          std::string src = engine::joinPath(srcDir, names[i]);
          std::string err; bool r;
          if (!srcRemote && !dstRemote) r = engine::copyFile(src, engine::joinPath(dstDir, names[i]));
          else if (handle != 0) r = srcRemote ? engine::downloadVia(handle, src, dstDir, &err) : engine::uploadVia(handle, src, dstDir, &err);
          else r = srcRemote ? engine::downloadFromRemote(src, dstDir, &err) : engine::uploadToRemote(src, dstDir, &err);
          if (r && isMove) r = srcRemote ? engine::remoteDelete(names[i], &err) : engine::localDelete(srcDir, names[i], &err);
          bool cancelled = cancel->load();
          markDone(rowVal, cancelled ? "cancelled" : (r ? "done" : ("failed" + (err.empty() ? QString() : (": " + u8(err))))), r && !cancelled);
          if (r && !cancelled) okCount->fetch_add(1);
        }
      };
      std::vector<std::thread> ts;
      if (handles.empty()) worker(0);
      else { for (int h : handles) ts.emplace_back([&, h]{ worker(h); }); for (auto & t : ts) t.join(); }

      int ok = okCount->load(), total = (int)names.size();
      QMetaObject::invokeMethod(&window, [&, ok, total, isMove, from, to]{
        engine::setProgressSink(nullptr);
        engine::closeParallelConnections();
        gTransferRunning = false;
        btnCancel->setEnabled(false);
        to->refresh(); if (isMove) from->refresh();
        window.statusBar()->showMessage(QString("Transferred %1/%2 item(s)").arg(ok).arg(total));
      }, Qt::QueuedConnection);
    }).detach();
  };

  // Drag files from one panel, drop on the other -> transfer (copy).
  left->onDropFiles  = [&](FilePanel * src, const QStringList & f) { startTransfer(src, left, f, false); };
  right->onDropFiles = [&](FilePanel * src, const QStringList & f) { startTransfer(src, right, f, false); };

  // External drop (Finder/Nautilus) of local files -> upload (remote panel) or copy (local panel).
  auto importFiles = [&](FilePanel * dest, const QStringList & absPaths) {
    if (busy()) return;
    const bool toRemote = dest->isRemote();
    const std::string destDir = s8(dest->path());
    queueDock->show();
    std::vector<int> rows; std::vector<std::string> srcs, nm;
    for (const QString & p : absPaths) {
      QFileInfo fi(p); if (fi.isDir()) continue;          // (recursive dir import: future)
      rows.push_back(queueAdd(toRemote ? "Upload" : "Copy", fi.fileName(), fi.size()));
      srcs.push_back(s8(p)); nm.push_back(s8(fi.fileName()));
    }
    if (rows.empty()) { window.statusBar()->showMessage("Nothing to import (folders not yet supported)"); return; }
    gTransferRunning = true; btnCancel->setEnabled(true);
    auto cancel = std::make_shared<std::atomic<bool>>(false); *currentCancel = cancel;
    auto curRow = std::make_shared<std::atomic<int>>(rows[0]);
    engine::setProgressSink([&, cancel, curRow](const engine::TransferProgress & tp) -> bool {
      int rowVal = curRow->load(), pct = tp.total > 0 ? (int)(tp.transferred * 100 / tp.total) : 0;
      qint64 cps = tp.cps, eta = (cps > 0 && tp.total > tp.transferred) ? (tp.total - tp.transferred) / cps : 0;
      QMetaObject::invokeMethod(&window, [&, rowVal, pct, cps, eta]{ queueCell(rowVal, 3, QString("%1%").arg(pct)); queueCell(rowVal, 4, fmtSpeed(cps)); queueCell(rowVal, 5, fmtEta(eta)); }, Qt::QueuedConnection);
      return cancel->load();
    });
    std::thread([&, rows, srcs, nm, destDir, toRemote, dest, cancel, curRow]{
      int ok = 0;
      for (size_t i = 0; i < srcs.size(); ++i) {
        if (cancel->load()) { QMetaObject::invokeMethod(&window, [&, r = rows[i]]{ queueStatus(r, "cancelled"); }, Qt::QueuedConnection); continue; }
        int rowVal = rows[i]; curRow->store(rowVal);
        QMetaObject::invokeMethod(&window, [&, rowVal]{ queueStatus(rowVal, "active"); }, Qt::QueuedConnection);
        std::string err; bool r = toRemote ? engine::uploadToRemote(srcs[i], destDir, &err)
                                            : engine::copyFile(srcs[i], engine::joinPath(destDir, nm[i]));
        QString status = r ? "done" : ("failed" + (err.empty() ? QString() : (": " + u8(err))));
        QMetaObject::invokeMethod(&window, [&, rowVal, status, r]{ queueStatus(rowVal, status); if (r) queueCell(rowVal, 3, "100%"); }, Qt::QueuedConnection);
        if (r) ++ok;
      }
      int total = (int)srcs.size();
      QMetaObject::invokeMethod(&window, [&, ok, total, dest]{
        engine::setProgressSink(nullptr); gTransferRunning = false; btnCancel->setEnabled(false);
        dest->refresh(); window.statusBar()->showMessage(QString("Imported %1/%2 item(s)").arg(ok).arg(total));
      }, Qt::QueuedConnection);
    }).detach();
  };
  left->onDropExternal = importFiles; right->onDropExternal = importFiles;

  //--- operations ---------------------------------------------------------
  auto doConnect = [&] {
    if (busy()) return;
    LoginParams lp = showLoginDialog(&window);
    if (!lp.ok) return;
    window.statusBar()->showMessage("Connecting\xE2\x80\xA6");
    log(QString("Connecting to %1:%2 \xE2\x80\xA6").arg(lp.host).arg(lp.port));
    QApplication::processEvents();
    engine::ConnectResult r = engine::connectSftp(s8(lp.host), lp.port, s8(lp.user), s8(lp.pass), lp.protocol, lp.tls);
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
                    : lp.protocol == engine::Protocol::S3 ? "S3"
                    : lp.protocol == engine::Protocol::Ftp ? "FTP" : "SFTP";
    right->setRemote(QString("%1@%2 \xE2\x80\x94 %3").arg(lp.user).arg(lp.host).arg(pn));
    remoteHome = u8(r.currentDir);
    right->navigate(u8(r.currentDir));
    right->onActivated();
    actDisconnect->setEnabled(true);
    window.setWindowTitle(QString("%1@%2 \xE2\x80\x94 WinSCP").arg(lp.user).arg(lp.host));
    log("Connected. Remote directory: " + u8(r.currentDir));
  };
  auto doDisconnect = [&] {
    if (busy()) return;
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
    startTransfer(active, dst, active->selectedItems(), false);
  };
  auto doMove = [&] {
    FilePanel * dst = (active == left) ? right : left;
    QStringList files = active->selectedItems();
    if (files.isEmpty()) { window.statusBar()->showMessage("No files selected"); return; }
    if (active->isRemote() && dst->isRemote())
    { QMessageBox::information(&window, "Move", "Remote-to-remote move is not supported yet."); return; }
    if (busy()) return;
    if (QMessageBox::question(&window, "Move",
          QString("Move %1 item(s) to %2?").arg(files.size()).arg(dst->path())) != QMessageBox::Yes) return;
    startTransfer(active, dst, files, true);
  };
  auto doMkdir = [&] {
    if (busy()) return;
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
    if (busy()) return;
    QStringList sel = active->selectedItems();
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
    if (busy()) return;
    QStringList sel = active->selectedItems();
    if (sel.isEmpty()) { window.statusBar()->showMessage("No files selected"); return; }
    if (gConfirmDelete && QMessageBox::question(&window, "Delete", QString("Delete %1 item(s)?").arg(sel.size())) != QMessageBox::Yes) return;
    int ok = 0; std::string err;
    for (const QString & f : sel)
      if (active->isRemote() ? engine::remoteDelete(s8(f), &err) : engine::localDelete(s8(active->path()), s8(f), &err)) ++ok;
    active->refresh();
    window.statusBar()->showMessage(QString("Deleted %1/%2 item(s)").arg(ok).arg(sel.size()));
  };
  auto doProps = [&] {
    if (busy()) return;
    QStringList sel = active->selectedItems();
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
  auto doOpen = [&] {
    if (active->isRemote() && busy()) return;
    QStringList sel = active->selectedFiles();
    if (sel.size() != 1) { window.statusBar()->showMessage("Select one file to open"); return; }
    QString f = sel.first();
    QString localPath;
    if (active->isRemote())
    {
      QString tmp = QDir::tempPath() + "/winscp-open";
      QDir().mkpath(tmp);
      std::string err;
      window.statusBar()->showMessage("Opening " + f + "\xE2\x80\xA6");
      QApplication::processEvents();
      if (!engine::downloadFromRemote(engine::joinPath(s8(active->path()), s8(f)), s8(tmp), &err))
      { QMessageBox::warning(&window, "Open", "Could not download: " + u8(err)); return; }
      localPath = tmp + "/" + f;
    }
    else localPath = active->path() + "/" + f;
    QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
    window.statusBar()->showMessage("Opened " + f);
  };
  // Internal editor (F4): download a remote file to temp, edit in a built-in text editor; Save writes
  // it back (re-uploads for remote). Local files are edited in place.
  auto doEdit = [&] {
    if (active->isRemote() && busy()) return;
    QStringList sel = active->selectedFiles();
    if (sel.size() != 1) { window.statusBar()->showMessage("Select one file to edit"); return; }
    QString f = sel.first();
    bool remote = active->isRemote();
    QString remoteDir = active->path();
    QString localPath;
    if (remote)
    {
      QString tmp = QDir::tempPath() + "/winscp-edit"; QDir().mkpath(tmp);
      std::string err;
      if (!engine::downloadFromRemote(engine::joinPath(s8(remoteDir), s8(f)), s8(tmp), &err))
      { QMessageBox::warning(&window, "Edit", "Could not download: " + u8(err)); return; }
      localPath = tmp + "/" + f;
    }
    else localPath = active->path() + "/" + f;

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { QMessageBox::warning(&window, "Edit", "Cannot open file"); return; }
    QString text = QString::fromUtf8(file.readAll()); file.close();

    auto * ed = new QMainWindow(&window); ed->setAttribute(Qt::WA_DeleteOnClose);
    ed->setWindowTitle(f + (remote ? "  \xE2\x80\x94  " + remoteDir + " (remote)" : ""));
    ed->resize(720, 560);
    auto * te = new QPlainTextEdit; te->setPlainText(text);
    te->setLineWrapMode(QPlainTextEdit::NoWrap);
    te->setFont(QFont("Menlo", 12));
    ed->setCentralWidget(te);
    auto * tb = ed->addToolBar("Edit");
    auto saveFn = [te, localPath, remote, remoteDir, f, &window, &left, &right]() {
      QFile out(localPath);
      if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) { QMessageBox::warning(&window, "Save", "Cannot write file"); return; }
      out.write(te->toPlainText().toUtf8()); out.close();
      if (remote)
      {
        std::string err;
        if (!engine::uploadToRemote(s8(localPath), s8(remoteDir), &err))
          QMessageBox::warning(&window, "Save", "Upload failed: " + u8(err));
        else { window.statusBar()->showMessage("Saved + uploaded " + f); if (right->isRemote()) right->refresh(); }
      }
      else { window.statusBar()->showMessage("Saved " + f); left->refresh(); }
    };
    auto * actSave = tb->addAction("\xF0\x9F\x92\xBE Save"); actSave->setShortcut(Qt::CTRL | Qt::Key_S);
    QObject::connect(actSave, &QAction::triggered, ed, saveFn);
    ed->show();
  };
  auto doQuit = [&] { window.close(); };

  // Synchronize directories (local panel <-> remote panel) via the engine's TSynchronizeChecklist.
  auto doSync = [&] {
    FilePanel * lp = left->isRemote() ? right : left;
    FilePanel * rp = left->isRemote() ? left : right;
    if (!rp->isRemote()) { QMessageBox::information(&window, "Synchronize", "Connect a remote session first."); return; }
    if (busy()) return;
    QDialog dlg(&window); dlg.setWindowTitle("Synchronize"); dlg.resize(600, 420);
    auto * v = new QVBoxLayout(&dlg);
    auto * form = new QFormLayout;
    form->addRow("Local:", new QLabel(lp->path()));
    form->addRow("Remote:", new QLabel(rp->path()));
    auto * dir = new QComboBox; dir->addItems({ "Local \xE2\x86\x92 Remote (upload)", "Remote \xE2\x86\x92 Local (download)", "Both directions" });
    form->addRow("Direction:", dir);
    auto * cbDel = new QCheckBox("Delete files missing on the source");
    form->addRow("", cbDel);
    v->addLayout(form);
    auto * list = new QTableView; auto * lm = new QStandardItemModel(0, 3, &dlg);
    lm->setHorizontalHeaderLabels({ "Action", "File", "Size" }); list->setModel(lm);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers); list->verticalHeader()->setVisible(false);
    list->horizontalHeader()->setStretchLastSection(true); list->setColumnWidth(1, 320);
    v->addWidget(list, 1);
    auto * bb = new QDialogButtonBox; auto * bCompare = bb->addButton("Compare", QDialogButtonBox::ActionRole);
    auto * bApply = bb->addButton("Synchronize", QDialogButtonBox::AcceptRole); bApply->setEnabled(false);
    bb->addButton(QDialogButtonBox::Close);
    v->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(bCompare, &QPushButton::clicked, [&]{
      lm->removeRows(0, lm->rowCount());
      std::string err;
      auto items = engine::synchronizeCollect(s8(lp->path()), s8(rp->path()), dir->currentIndex(), cbDel->isChecked(), &err);
      if (!err.empty()) { QMessageBox::warning(&dlg, "Synchronize", u8(err)); return; }
      for (const auto & it : items) {
        QList<QStandardItem *> row;
        row << new QStandardItem(u8(it.action)) << new QStandardItem(u8(it.name))
            << new QStandardItem(it.isDir ? QString() : u8(engine::formatSize(it.size)));
        lm->appendRow(row);
      }
      bApply->setEnabled(lm->rowCount() > 0);
      dlg.setWindowTitle(QString("Synchronize \xE2\x80\x94 %1 change(s)").arg(lm->rowCount()));
    });
    QObject::connect(bApply, &QPushButton::clicked, [&]{
      std::string err;
      bool ok = engine::synchronizeApply(&err);
      engine::synchronizeRelease();
      if (!ok) QMessageBox::warning(&dlg, "Synchronize", u8(err));
      dlg.accept();
    });
    dlg.exec();
    engine::synchronizeRelease();
    lp->refresh(); rp->refresh();
  };

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
    mOptions->addAction("Transfer &queue\tCtrl+Q", [&]{ queueDock->setVisible(!queueDock->isVisible()); });
    auto * actHidden = mOptions->addAction("Show &hidden files"); actHidden->setCheckable(true); actHidden->setChecked(gShowHidden);
    actHidden->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_H));
    QObject::connect(actHidden, &QAction::toggled, [&](bool on){ gShowHidden = on; left->refresh(); right->refresh();
      QSettings("WinSCP-native-port","WinSCP").setValue("prefs/showHidden", on); });
    mOptions->addSeparator();
    mOptions->addAction("&Preferences\xE2\x80\xA6", [&]{
      QDialog d(&window); d.setWindowTitle("Preferences"); d.resize(520, 360);
      auto * outer = new QVBoxLayout(&d);
      auto * tabs = new QTabWidget; outer->addWidget(tabs, 1);

      // --- Environment ---
      auto * envTab = new QWidget; auto * envLay = new QVBoxLayout(envTab);
      auto * cbHidden = new QCheckBox("Show hidden files"); cbHidden->setChecked(gShowHidden);
      auto * cbDel = new QCheckBox("Confirm file deletion"); cbDel->setChecked(gConfirmDelete);
      auto * cbOvr = new QCheckBox("Confirm overwrites"); cbOvr->setChecked(gConfirmOverwrite);
      envLay->addWidget(cbHidden); envLay->addWidget(cbDel); envLay->addWidget(cbOvr); envLay->addStretch(1);
      tabs->addTab(envTab, "Environment");

      // --- Transfer ---
      auto * trTab = new QWidget; auto * trForm = new QFormLayout(trTab);
      auto * spinPar = new QSpinBox; spinPar->setRange(1, 4); spinPar->setValue(gParallelMax);
      trForm->addRow("Maximum parallel transfers:", spinPar);
      auto * cmbMode = new QComboBox; cmbMode->addItems({ "Binary", "Text", "Automatic" }); cmbMode->setCurrentIndex(0);
      trForm->addRow("Default transfer mode:", cmbMode);
      tabs->addTab(trTab, "Transfer");

      // --- Panels ---
      auto * pnTab = new QWidget; auto * pnLay = new QVBoxLayout(pnTab);
      auto * cbAlt = new QCheckBox("Alternating row colors"); cbAlt->setChecked(gAltColors);
      pnLay->addWidget(cbAlt); pnLay->addStretch(1);
      tabs->addTab(pnTab, "Panels");

      auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
      outer->addWidget(bb);
      QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
      QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
      if (d.exec() != QDialog::Accepted) return;
      gShowHidden = cbHidden->isChecked(); gConfirmDelete = cbDel->isChecked();
      gConfirmOverwrite = cbOvr->isChecked(); gParallelMax = spinPar->value(); gAltColors = cbAlt->isChecked();
      actHidden->setChecked(gShowHidden);
      QSettings s("WinSCP-native-port","WinSCP");
      s.setValue("prefs/showHidden", gShowHidden); s.setValue("prefs/confirmDelete", gConfirmDelete);
      s.setValue("prefs/confirmOverwrite", gConfirmOverwrite); s.setValue("prefs/parallelMax", gParallelMax);
      s.setValue("prefs/altColors", gAltColors);
      left->view()->setAlternatingRowColors(gAltColors); right->view()->setAlternatingRowColors(gAltColors);
      left->refresh(); right->refresh();
    });
    auto * mCommands = window.menuBar()->addMenu("&Commands");
    mCommands->addAction("&Synchronize\xE2\x80\xA6\tCtrl+S", doSync);
    { auto * a = new QAction(&window); a->setShortcut(Qt::CTRL | Qt::Key_S);
      QObject::connect(a, &QAction::triggered, doSync); window.addAction(a); }
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
  addFKey("F4 Edit", doEdit);
  addFKey("F5 Copy", doCopy);
  addFKey("F6 Move", doMove);
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
  shortcut(Qt::Key_F3, doOpen);
  shortcut(Qt::Key_F4, doEdit);
  shortcut(Qt::Key_F5, doCopy);
  left->onFileOpen = doOpen; right->onFileOpen = doOpen;
  shortcut(Qt::Key_F6, doMove);
  shortcut(Qt::Key_F7, doMkdir);
  shortcut(Qt::Key_F8, doDelete);
  shortcut(QKeySequence::Delete, doDelete);
  shortcut(Qt::Key_F9, doProps);
  shortcut(Qt::Key_F10, doQuit);
  // toggle the session log
  { auto * a = new QAction(&window); a->setShortcut(Qt::CTRL | Qt::Key_L);
    QObject::connect(a, &QAction::triggered, [&]{ logDock->setVisible(!logDock->isVisible()); });
    window.addAction(a); }
  { auto * a = new QAction(&window); a->setShortcut(Qt::CTRL | Qt::Key_Q);
    QObject::connect(a, &QAction::triggered, [&]{ queueDock->setVisible(!queueDock->isVisible()); });
    window.addAction(a); }
  // WinSCP keyboard set. Qt::CTRL auto-maps to Command on macOS and Ctrl on Linux/Windows, so these
  // are faithful on Linux and native on mac with no per-OS code. F-keys are identical everywhere.
  shortcut(Qt::CTRL | Qt::Key_R, [&]{ active->refresh(); });               // refresh
  shortcut(QKeySequence::Refresh, [&]{ active->refresh(); });
  shortcut(Qt::ALT | Qt::Key_Left,  [&]{ active->goBack(); });             // history back
  shortcut(Qt::ALT | Qt::Key_Right, [&]{ active->goForward(); });          // history forward
  shortcut(Qt::ALT | Qt::Key_Up,    [&]{ active->goUp(); });               // parent dir
  shortcut(Qt::CTRL | Qt::Key_N, [&]{ doConnect(); });                     // new session
  shortcut(Qt::Key_Insert, [&]{ active->toggleSelectAndAdvance(); });      // Norton select + advance
  shortcut(Qt::Key_F1, [&]{ QMessageBox::about(&window, "About WinSCP",
    "WinSCP \xE2\x80\x94 native port (Mac/Linux)\nSFTP / SCP / FTP / WebDAV / S3"); });

  left->setActive(true);
  left->navigate(u8(engine::homeDir()));
  right->navigate(u8(engine::homeDir()));
  updateStatuses();

  // Persist window geometry + splitter on quit.
  QObject::connect(&app, &QApplication::aboutToQuit, [&]{
    QSettings s("WinSCP-native-port", "WinSCP");
    s.setValue("win/geometry", window.saveGeometry());
    s.setValue("win/splitter", splitter->saveState());
  });

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
                            : p[4]=="s3"?engine::Protocol::S3 : p[4]=="ftp"?engine::Protocol::Ftp : engine::Protocol::Sftp;
        auto r = engine::connectSftp(s8(p[0]), p[1].toInt(), s8(p[2]), s8(p[3]), pr);
        if (r.ok) { right->setRemote(QString("%1@%2").arg(p[2]).arg(p[0])); remoteHome = u8(r.currentDir); right->navigate(u8(r.currentDir));
          // exercise the BACKGROUND transfer queue end-to-end (multi-file, on the worker thread)
          QString xf = qEnvironmentVariable("WINSCP_AUTOXFER");
          if (!xf.isEmpty()) {
            QDir().mkpath("/tmp/dlq"); left->navigate("/tmp/dlq");
            startTransfer(right, left, xf.split(','), false);
          }
        }
      }
      // Grab after a delay so background transfers (detached worker) complete first.
      QTimer::singleShot(qEnvironmentVariableIsEmpty("WINSCP_AUTOXFER") ? 100 : 3000, [&]{
        window.grab().save(qEnvironmentVariable("WINSCP_SHOT"));
        app.quit();
      });
      return;
    }, Qt::QueuedConnection);

  // Offer the Login dialog on startup (like WinSCP), unless launched headless for a smoke test.
  if (qEnvironmentVariableIsEmpty("WINSCP_NO_AUTOLOGIN"))
    QMetaObject::invokeMethod(&window, [&]{ doConnect(); }, Qt::QueuedConnection);

  return app.exec();
}
