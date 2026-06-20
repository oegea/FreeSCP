//---------------------------------------------------------------------------
// WinSCP native port — Qt 6 GUI. Dual-pane Commander; the local panel is backed by the
// ported engine via enginebridge (real FindFirst/TSearchRec + file copy). The remote panel
// is a placeholder until the SSH/SFTP backend lands — it will use the same bridge pattern.
//---------------------------------------------------------------------------
#include <QApplication>
#include <QMainWindow>
#include <QSplitter>
#include <QTableView>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QStatusBar>
#include <QMenuBar>
#include <QToolBar>
#include <QAction>
#include <QKeyEvent>
#include <QMessageBox>
#include <QString>
#include <functional>

#include "enginebridge.h"

static QString u8(const std::string & s) { return QString::fromUtf8(s.c_str()); }
static std::string s8(const QString & s) { return s.toUtf8().constData(); }

// A local file-system panel backed by the engine.
class FilePanel : public QWidget
{
public:
  std::function<void()> onActivated;   // called when this panel gains focus

  explicit FilePanel(const QString & title, QWidget * parent = nullptr) : QWidget(parent)
  {
    auto * layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    FHeader = new QLabel(title);
    FHeader->setStyleSheet("padding:3px; font-weight:bold; background:palette(midlight);");
    FPathEdit = new QLineEdit;
    FView = new QTableView;
    FModel = new QStandardItemModel(this);
    FView->setModel(FModel);
    FView->setSelectionBehavior(QAbstractItemView::SelectRows);
    FView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    FView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    FView->setSortingEnabled(false);
    FView->verticalHeader()->setVisible(false);
    FView->horizontalHeader()->setStretchLastSection(true);
    FView->installEventFilter(this);

    layout->addWidget(FHeader);
    layout->addWidget(FPathEdit);
    layout->addWidget(FView);

    QObject::connect(FView, &QTableView::doubleClicked,
                     [this](const QModelIndex & ix) { activate(ix.row()); });
    QObject::connect(FPathEdit, &QLineEdit::returnPressed,
                     [this] { navigate(FPathEdit->text()); });
  }

  QString path() const { return FPath; }
  QTableView * view() const { return FView; }

  void setActive(bool a)
  {
    FHeader->setStyleSheet(QString("padding:3px; font-weight:bold; background:%1;")
                             .arg(a ? "palette(highlight)" : "palette(midlight)"));
  }

  void navigate(const QString & path)
  {
    FPath = path;
    FPathEdit->setText(path);
    FModel->clear();
    FModel->setHorizontalHeaderLabels({ "Name", "Size", "Modified" });
    FEntries = engine::listLocalDir(s8(path));
    int dirs = 0, files = 0;
    for (const auto & e : FEntries)
    {
      QList<QStandardItem *> row;
      row << new QStandardItem((e.isDir ? "📁 " : "📄 ") + u8(e.name));
      auto * sz = new QStandardItem(e.isDir ? QString() : u8(engine::formatSize(e.size)));
      sz->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      row << sz;
      row << new QStandardItem(u8(e.modified));
      FModel->appendRow(row);
      if (e.isParent) continue;
      if (e.isDir) ++dirs; else ++files;
    }
    FView->resizeColumnsToContents();
    FView->horizontalHeader()->setStretchLastSection(true);
    FCounts = QString("%1 folders, %2 files").arg(dirs).arg(files);
    if (FView->model()->rowCount() > 0) FView->selectRow(0);
  }

  QString counts() const { return FCounts; }

  // selected real files (not dirs, not "..")
  QStringList selectedFiles() const
  {
    QStringList out;
    const auto rows = FView->selectionModel()->selectedRows();
    for (const auto & ix : rows)
    {
      int r = ix.row();
      if (r >= 0 && r < (int)FEntries.size() && !FEntries[r].isDir)
        out << u8(FEntries[r].name);
    }
    return out;
  }

  void refresh() { navigate(FPath); }
  void goUp() { navigate(u8(engine::parentDir(s8(FPath)))); }

protected:
  bool eventFilter(QObject * obj, QEvent * ev) override
  {
    if (ev->type() == QEvent::FocusIn && onActivated) onActivated();
    if (ev->type() == QEvent::KeyPress)
    {
      auto * ke = static_cast<QKeyEvent *>(ev);
      if (ke->key() == Qt::Key_Backspace) { goUp(); return true; }
      if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
      { activate(FView->currentIndex().row()); return true; }
    }
    return QWidget::eventFilter(obj, ev);
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
  QString FPath;
  QString FCounts;
  std::vector<engine::DirEntry> FEntries;
};

int main(int argc, char ** argv)
{
  QApplication app(argc, argv);
  app.setApplicationName("WinSCP");
  app.setOrganizationName("WinSCP native port");

  QMainWindow window;
  window.setWindowTitle("WinSCP — native port");
  window.resize(1100, 700);

  for (const char * m : { "&Local", "&Mark", "&Files", "&Commands", "&Session", "&Options", "&Remote", "&Help" })
    window.menuBar()->addMenu(m);

  auto * splitter = new QSplitter(Qt::Horizontal);
  auto * left = new FilePanel("Local");
  auto * right = new FilePanel("Local (2)");
  splitter->addWidget(left);
  splitter->addWidget(right);
  splitter->setSizes({ 550, 550 });
  window.setCentralWidget(splitter);

  // active-panel tracking
  FilePanel * active = left;
  auto updateStatus = [&] {
    window.statusBar()->showMessage(active->path() + "   —   " + active->counts());
  };
  left->onActivated = [&] { active = left; left->setActive(true); right->setActive(false); updateStatus(); };
  right->onActivated = [&] { active = right; right->setActive(true); left->setActive(false); updateStatus(); };

  // toolbar
  auto * tb = window.addToolBar("Main");
  auto * actUp = tb->addAction("⬆ Up");
  auto * actHome = tb->addAction("🏠 Home");
  auto * actRefresh = tb->addAction("⟳ Refresh");
  tb->addSeparator();
  auto * actCopy = tb->addAction("F5 Copy →");
  QObject::connect(actUp, &QAction::triggered, [&] { active->goUp(); updateStatus(); });
  QObject::connect(actHome, &QAction::triggered, [&] { active->navigate(u8(engine::homeDir())); updateStatus(); });
  QObject::connect(actRefresh, &QAction::triggered, [&] { active->refresh(); updateStatus(); });

  auto doCopy = [&] {
    FilePanel * dst = (active == left) ? right : left;
    QStringList files = active->selectedFiles();
    if (files.isEmpty()) { window.statusBar()->showMessage("No files selected"); return; }
    int ok = 0;
    for (const QString & f : files)
    {
      std::string src = engine::joinPath(s8(active->path()), s8(f));
      std::string dd = engine::joinPath(s8(dst->path()), s8(f));
      if (engine::copyFile(src, dd)) ++ok;
    }
    dst->refresh();
    window.statusBar()->showMessage(QString("Copied %1/%2 file(s) to %3")
                                      .arg(ok).arg(files.size()).arg(dst->path()));
  };
  QObject::connect(actCopy, &QAction::triggered, doCopy);
  auto * f5 = new QAction(&window); f5->setShortcut(Qt::Key_F5);
  QObject::connect(f5, &QAction::triggered, doCopy); window.addAction(f5);

  left->setActive(true);
  left->navigate(u8(engine::homeDir()));
  right->navigate("/");
  window.statusBar()->showMessage(QString::fromUtf8(engine::banner().c_str()));

  window.show();
  return app.exec();
}
