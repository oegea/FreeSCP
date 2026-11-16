//---------------------------------------------------------------------------
// finddialog.cpp — see finddialog.h.
//
// The search runs on a worker thread (the engine call blocks); matches are handed to the dialog
// through a mutex-protected queue that a timer drains, so a search with very many results neither
// floods the event loop nor blocks the worker.
//---------------------------------------------------------------------------
#include "finddialog.h"
#include "uicommon.h"
#include "enginebridge.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

struct SearchState
{
  std::mutex mtx;
  std::vector<engine::FoundFile> pending;   // found, not yet shown
  std::string currentDir;
  std::string error;
  bool done = false;
  std::atomic<bool> cancel{false};
};

const int kMaxShown = 50000;   // rows the table will hold; the search itself continues counting

} // namespace

void showFindDialog(QWidget * parent, const FindContext & ctx, const QString & autoMask)
{
  QDialog d(parent);
  d.setWindowTitle(ctx.remote ? "Find Files \xE2\x80\x94 remote" : "Find Files \xE2\x80\x94 local");
  d.resize(760, 480);
  auto * lay = new QVBoxLayout(&d);

  auto * form = new QFormLayout;
  auto * mask = new QLineEdit(autoMask.isEmpty() ? QStringLiteral("*") : autoMask);
  mask->setToolTip("File mask, for example:  *.txt    *.log; *.tmp    *.jpg | thumb*    *>1M");
  auto * dirEdit = new QLineEdit(ctx.startDir);
  form->addRow("File &mask:", mask);
  form->addRow("&Search in:", dirEdit);
  lay->addLayout(form);

  auto * table = new QTableView;
  auto * model = new QStandardItemModel(0, 4, &d);
  model->setHorizontalHeaderLabels({ "Name", "Directory", "Size", "Changed" });
  table->setModel(model);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->verticalHeader()->setVisible(false);
  table->verticalHeader()->setDefaultSectionSize(20);
  table->setShowGrid(false);
  table->horizontalHeader()->setStretchLastSection(true);
  table->setColumnWidth(0, 200); table->setColumnWidth(1, 300); table->setColumnWidth(2, 80);
  lay->addWidget(table, 1);

  auto * status = new QLabel("Enter a file mask and press Start.");
  status->setTextFormat(Qt::PlainText);
  lay->addWidget(status);

  auto * buttons = new QHBoxLayout;
  auto * startBtn = new QPushButton("&Start"); startBtn->setDefault(true);
  auto * focusBtn = new QPushButton("&Focus"); focusBtn->setEnabled(false);
  focusBtn->setToolTip("Show the selected file in the panel");
  auto * copyBtn = new QPushButton("&Copy paths"); copyBtn->setEnabled(false);
  auto * closeBtn = new QPushButton("Close");
  buttons->addWidget(startBtn); buttons->addWidget(focusBtn); buttons->addWidget(copyBtn);
  buttons->addStretch(1); buttons->addWidget(closeBtn);
  lay->addLayout(buttons);

  std::shared_ptr<SearchState> state;
  std::thread worker;
  bool running = false, holding = false;
  qint64 total = 0;
  std::vector<engine::FoundFile> shown;   // row -> file
  QTimer poll(&d);
  poll.setInterval(80);

  auto finishWorker = [&] {
    if (worker.joinable()) worker.join();
    if (holding) { holding = false; if (ctx.release) ctx.release(); }
    running = false;
    startBtn->setText("&Start");
    mask->setEnabled(true); dirEdit->setEnabled(true);
  };

  auto drain = [&] {
    if (!state) return;
    std::vector<engine::FoundFile> batch; std::string cur, err; bool done;
    {
      std::lock_guard<std::mutex> lk(state->mtx);
      batch.swap(state->pending); cur = state->currentDir; done = state->done; err = state->error;
    }
    for (const engine::FoundFile & f : batch)
    {
      ++total;
      if ((int)shown.size() >= kMaxShown) continue;
      QList<QStandardItem *> row;
      auto * nameItem = new QStandardItem(u8(f.name) + (f.isDir ? "/" : ""));
      row << nameItem << new QStandardItem(u8(f.dir));
      auto * sz = new QStandardItem(f.isDir ? QString() : u8(engine::formatSize(f.size)));
      sz->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      row << sz << new QStandardItem(u8(f.modified));
      model->appendRow(row);
      shown.push_back(f);
    }
    copyBtn->setEnabled(!shown.empty());
    if (!done)
    {
      status->setText(QString("%1 found \xE2\x80\x94 searching %2").arg(total).arg(u8(cur)));
      return;
    }
    // finished
    poll.stop();
    const bool stopped = state->cancel.load();
    finishWorker();
    QString msg = QString("%1 file(s) found%2").arg(total).arg(stopped ? " (search stopped)" : "");
    if ((int)shown.size() < total) msg += QString(" \xE2\x80\x94 showing the first %1").arg(shown.size());
    if (!err.empty()) msg += " \xE2\x80\x94 " + u8(err).section('\n', 0, 0);
    status->setText(msg);
    if (model->rowCount() > 0 && !table->currentIndex().isValid()) table->selectRow(0);
    if (!autoMask.isEmpty())
    {
      // scripted: report and leave
      fprintf(stdout, "[find] %lld result(s)%s\n", (long long)total, err.empty() ? "" : " (with an error)");
      if (!err.empty()) fprintf(stdout, "[find] error: %s\n", err.c_str());
      for (const engine::FoundFile & f : shown) fprintf(stdout, "  %s\n", f.path.c_str());
      fflush(stdout);
      const QString dir = qEnvironmentVariable("WINSCP_DIALOG_SHOTS");
      if (!dir.isEmpty()) d.grab().save(dir + "/find.png");
      if (!qEnvironmentVariableIsEmpty("WINSCP_FIND_FOCUS") && !shown.empty() && ctx.focus)
        ctx.focus(u8(shown[0].dir), u8(shown[0].name));
      d.accept();
    }
  };
  QObject::connect(&poll, &QTimer::timeout, &d, drain);

  auto start = [&] {
    if (running) return;
    const std::string m = s8(mask->text().trimmed().isEmpty() ? QStringLiteral("*") : mask->text().trimmed());
    std::string why;
    if (!engine::validateFileMask(m, &why)) { status->setText("The file mask is not valid: " + u8(why).section('\n', 0, 0)); if (!autoMask.isEmpty()) { fprintf(stdout, "[find] invalid mask\n"); fflush(stdout); QTimer::singleShot(0, &d, &QDialog::reject); } return; }
    if (ctx.remote)
    {
      if (ctx.acquire && !ctx.acquire()) { status->setText("The session is busy with a transfer \xE2\x80\x94 try again when it has finished."); return; }
      holding = true;
    }
    model->removeRows(0, model->rowCount());
    shown.clear(); total = 0;
    focusBtn->setEnabled(false); copyBtn->setEnabled(false);
    state = std::make_shared<SearchState>();
    running = true;
    startBtn->setText("&Stop");
    mask->setEnabled(false); dirEdit->setEnabled(false);
    status->setText("Searching\xE2\x80\xA6");
    const std::string dir = s8(dirEdit->text().trimmed());
    const bool remote = ctx.remote;
    std::shared_ptr<SearchState> st = state;
    worker = std::thread([st, dir, m, remote] {
      auto onFound = [st](const engine::FoundFile & f) -> bool {
        std::lock_guard<std::mutex> lk(st->mtx); st->pending.push_back(f); return st->cancel.load();
      };
      auto onDir = [st](const std::string & cur) -> bool {
        std::lock_guard<std::mutex> lk(st->mtx); st->currentDir = cur; return st->cancel.load();
      };
      std::string err;
      if (remote) engine::remoteFind(dir, m, onFound, onDir, &err);
      else engine::localFind(dir, m, onFound, onDir, &err);
      std::lock_guard<std::mutex> lk(st->mtx);
      st->error = err; st->done = true;
    });
    poll.start();
  };
  auto stop = [&] { if (state) state->cancel.store(true); status->setText("Stopping\xE2\x80\xA6"); };

  QObject::connect(startBtn, &QPushButton::clicked, &d, [&] { if (running) stop(); else start(); });
  QObject::connect(mask, &QLineEdit::returnPressed, &d, [&] { if (!running) start(); });
  QObject::connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, &d,
                   [&] { focusBtn->setEnabled(table->selectionModel()->selectedRows().size() == 1 && bool(ctx.focus)); });
  auto doFocus = [&] {
    const auto rows = table->selectionModel()->selectedRows();
    if (rows.size() != 1 || !ctx.focus) return;
    const engine::FoundFile f = shown[(size_t)rows.first().row()];
    if (running) { stop(); while (running) { QApplication::processEvents(QEventLoop::AllEvents, 50); drain(); } }
    ctx.focus(u8(f.dir), u8(f.name));
    d.accept();
  };
  QObject::connect(focusBtn, &QPushButton::clicked, &d, doFocus);
  QObject::connect(table, &QTableView::doubleClicked, &d, [&](const QModelIndex &) { doFocus(); });
  QObject::connect(copyBtn, &QPushButton::clicked, &d, [&] {
    QStringList paths;
    const auto rows = table->selectionModel()->selectedRows();
    if (rows.size() > 1) for (const QModelIndex & ix : rows) paths << u8(shown[(size_t)ix.row()].path);
    else for (const engine::FoundFile & f : shown) paths << u8(f.path);
    QApplication::clipboard()->setText(paths.join("\n"));
    status->setText(QString("Copied %1 path(s) to the clipboard").arg(paths.size()));
  });
  QObject::connect(closeBtn, &QPushButton::clicked, &d, &QDialog::reject);

  if (!autoMask.isEmpty()) QTimer::singleShot(0, &d, start);
  d.exec();

  // leaving with a search still running: stop it and wait (the engine returns at its next callback)
  if (state) state->cancel.store(true);
  poll.stop();
  finishWorker();
}
