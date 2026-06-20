//---------------------------------------------------------------------------
// WinSCP native port — Qt 6 GUI. Dual-pane Commander; the local panel is backed by the
// ported engine via enginebridge (real FindFirst/TSearchRec). The remote panel is a
// placeholder until the SSH/SFTP backend lands — it will use the same bridge pattern.
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
#include <QString>

#include "enginebridge.h"

static QString u8(const std::string & s) { return QString::fromUtf8(s.c_str()); }
static std::string s8(const QString & s) { return s.toUtf8().constData(); }

// A local file-system panel backed by the engine. Plain QWidget (no new signals -> no moc).
class FilePanel : public QWidget
{
public:
  explicit FilePanel(const QString & title, QWidget * parent = nullptr) : QWidget(parent)
  {
    auto * layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    auto * header = new QLabel(title);
    header->setStyleSheet("padding:3px; font-weight:bold; background:palette(midlight);");
    FPathEdit = new QLineEdit;
    FView = new QTableView;
    FModel = new QStandardItemModel(this);
    FView->setModel(FModel);
    FView->setSelectionBehavior(QAbstractItemView::SelectRows);
    FView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    FView->verticalHeader()->setVisible(false);
    FView->horizontalHeader()->setStretchLastSection(true);

    layout->addWidget(header);
    layout->addWidget(FPathEdit);
    layout->addWidget(FView);

    QObject::connect(FView, &QTableView::doubleClicked, [this](const QModelIndex & ix) {
      onActivate(ix.row());
    });
    QObject::connect(FPathEdit, &QLineEdit::returnPressed, [this] { navigate(FPathEdit->text()); });
  }

  void navigate(const QString & path)
  {
    FPath = path;
    FPathEdit->setText(path);
    FModel->clear();
    FModel->setHorizontalHeaderLabels({ "Name", "Size", "Type" });
    FEntries = engine::listLocalDir(s8(path));
    for (const auto & e : FEntries)
    {
      QList<QStandardItem *> row;
      row << new QStandardItem((e.isDir ? "📁 " : "📄 ") + u8(e.name));
      row << new QStandardItem(e.isDir ? QString() : u8(engine::formatSize(e.size)));
      row << new QStandardItem(e.isDir ? "Folder" : "File");
      FModel->appendRow(row);
    }
    FView->resizeColumnsToContents();
  }

private:
  void onActivate(int row)
  {
    if (row < 0 || row >= static_cast<int>(FEntries.size())) return;
    const auto & e = FEntries[static_cast<size_t>(row)];
    if (!e.isDir) return;
    if (e.isParent) navigate(u8(engine::parentDir(s8(FPath))));
    else navigate(u8(engine::joinPath(s8(FPath), e.name)));
  }

  QLineEdit * FPathEdit;
  QTableView * FView;
  QStandardItemModel * FModel;
  QString FPath;
  std::vector<engine::DirEntry> FEntries;
};

int main(int argc, char ** argv)
{
  QApplication app(argc, argv);
  app.setApplicationName("WinSCP");
  app.setOrganizationName("WinSCP native port");

  QMainWindow window;
  window.setWindowTitle("WinSCP — native port (local browser via ported engine)");
  window.resize(1100, 680);

  for (const char * m : { "&Local", "&Mark", "&Files", "&Commands", "&Session", "&Options", "&Remote", "&Help" })
    window.menuBar()->addMenu(m);

  auto * splitter = new QSplitter(Qt::Horizontal);
  auto * local = new FilePanel("Local");
  auto * remote = new FilePanel("Remote (not connected — SSH/SFTP backend pending)");
  splitter->addWidget(local);
  splitter->addWidget(remote);
  splitter->setSizes({ 550, 550 });
  window.setCentralWidget(splitter);

  window.statusBar()->showMessage(u8(engine::banner()));

  local->navigate(u8(engine::homeDir()));
  remote->navigate("/");   // also local for now (demonstrates the panel)

  window.show();
  return app.exec();
}
