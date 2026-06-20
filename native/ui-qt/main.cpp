//---------------------------------------------------------------------------
// WinSCP native port — Qt 6 GUI entry point (Phase 0 skeleton).
//
// This is the placeholder shell that proves the Qt toolchain. The real Commander/Explorer
// UI (source/forms rewrite) lands in Phase 6+. For now it shows the dual-pane Commander
// silhouette so the layout target is visible from day one.
//---------------------------------------------------------------------------
#include <QApplication>
#include <QMainWindow>
#include <QSplitter>
#include <QTreeView>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <QStatusBar>
#include <QMenuBar>

static QWidget * makePane(const QString & title)
{
  auto * pane = new QWidget;
  auto * layout = new QVBoxLayout(pane);
  layout->setContentsMargins(0, 0, 0, 0);
  auto * header = new QLabel(title);
  header->setStyleSheet("padding:4px; font-weight:bold; background:palette(midlight);");
  auto * view = new QTreeView;          // -> backed by engine file models in Phase 6
  layout->addWidget(header);
  layout->addWidget(view);
  return pane;
}

int main(int argc, char ** argv)
{
  QApplication app(argc, argv);
  app.setApplicationName("WinSCP");
  app.setOrganizationName("WinSCP native port");

  QMainWindow window;
  window.setWindowTitle("WinSCP — native port (Phase 0 skeleton)");
  window.resize(1000, 640);

  // Dual-pane Commander silhouette: local | remote.
  auto * splitter = new QSplitter(Qt::Horizontal);
  splitter->addWidget(makePane("Local"));
  splitter->addWidget(makePane("Remote (not connected)"));
  splitter->setSizes({500, 500});
  window.setCentralWidget(splitter);

  window.menuBar()->addMenu("&Local");
  window.menuBar()->addMenu("&Mark");
  window.menuBar()->addMenu("&Files");
  window.menuBar()->addMenu("&Commands");
  window.menuBar()->addMenu("&Session");
  window.menuBar()->addMenu("&Options");
  window.menuBar()->addMenu("&Remote");
  window.menuBar()->addMenu("&Help");
  window.statusBar()->showMessage("Native port skeleton — engine wiring pending (see docs/porting/PLAN.md)");

  window.show();
  return app.exec();
}
